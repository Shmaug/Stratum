#include "ImageLoader.hpp"

#include <ThirdParty/stb_image.h>

#include <dcmtk/dcmimgle/dcmimage.h>
#include <dcmtk/dcmdata/dctk.h>

using namespace std;

unordered_multimap<string, ImageStackType> ExtensionMap {
	{ ".dcm", eDicom },
	{ ".raw", eRaw },
	{ ".png", eStandard },
	{ ".jpg", eStandard }
};

ImageStackType ImageLoader::FolderStackType(const fs::path& folder) {
	try {
		if (!fs::exists(folder)) return ImageStackType::eNone;

		ImageStackType type = ImageStackType::eNone;
		for (const auto& p : fs::directory_iterator(folder))
			if (ExtensionMap.count(p.path().extension().string())) {
				ImageStackType t = ExtensionMap.find(p.path().extension().string())->second;
				if (type != ImageStackType::eNone && type != t) return ImageStackType::eNone; // inconsistent image type
				type = t;
			}

		if (type == ImageStackType::eNone) return type;

		if (type == ImageStackType::eStandard) {
			vector<fs::path> images;
			for (const auto& p : fs::directory_iterator(folder))
				if (ExtensionMap.count(p.path().extension().string()))
					images.push_back(p.path());
			std::sort(images.begin(), images.end(), [](const fs::path& a, const fs::path& b) {
				return a.stem().string() < b.stem().string();
			});

			int x, y, c;
			if (stbi_info(images[0].string().c_str(), &x, &y, &c) == 0) return ImageStackType::eNone;

			uint32_t width = x;
			uint32_t height = y;
			uint32_t channels = c;
			uint32_t depth = (uint32_t)images.size();

			for (uint32_t i = 1; i < images.size(); i++) {
				stbi_info(images[i].string().c_str(), &x, &y, &c);
				if (x != width) { return ImageStackType::eNone; }
				if (y != height) { return ImageStackType::eNone; }
				if (c != channels) { return ImageStackType::eNone; }
			}
		}
		return type;
	} catch (exception e) {
		return ImageStackType::eNone;
	}
}

Texture* ImageLoader::LoadStandardStack(const fs::path& folder, Device* device, float3* scale, bool reverse, uint32_t channelCount, bool unorm) {
	if (!fs::exists(folder)) return nullptr;

	vector<fs::path> images;
	for (const auto& p : fs::directory_iterator(folder))
		if (ExtensionMap.count(p.path().extension().string()) && ExtensionMap.find(p.path().extension().string())->second == ImageStackType::eStandard)
			images.push_back(p.path());
	if (images.empty()) return nullptr;
	std::sort(images.begin(), images.end(), [reverse](const fs::path& a, const fs::path& b) {
		string astr = a.stem().string();
		string bstr = b.stem().string();
		if (astr.find_first_not_of( "0123456789" ) == string::npos && bstr.find_first_not_of( "0123456789" ) == string::npos)
			return reverse ? atoi(astr.c_str()) > atoi(bstr.c_str()) : atoi(astr.c_str()) < atoi(bstr.c_str());
		return reverse ? astr > bstr : astr < bstr;
	});

	int x, y, c;
	stbi_info(images[0].string().c_str(), &x, &y, &c);
	
	vk::Extent3D extent = { (uint32_t)x, (uint32_t)y, (uint32_t)images.size() };
	uint32_t channels = channelCount == 0 ? c : channelCount;

	vk::Format format;
	switch (channels) {
	case 4:
		format = unorm ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8G8B8A8Uint;
		break;
	case 3:
		format = unorm ? vk::Format::eR8G8B8Unorm : vk::Format::eR8G8B8Uint;
		break;
	case 2:
		format = unorm ? vk::Format::eR8G8Unorm : vk::Format::eR8G8Uint;
		break;
	case 1:
		format = unorm ? vk::Format::eR8Unorm : vk::Format::eR8Uint;
		break;
	default:
		return nullptr; // ??
	}

	size_t sliceSize = extent.width * extent.height * channels;
	uint8_t* pixels = new uint8_t[sliceSize * extent.depth];
	memset(pixels, 0, sliceSize * extent.depth);

	uint32_t done = 0;
	vector<thread> threads;
	uint32_t threadCount = thread::hardware_concurrency();
	for (uint32_t j = 0; j < threadCount; j++) {
		threads.push_back(thread([=, &done]() {
			int xt, yt, ct;
			for (uint32_t i = j; i < images.size(); i += threadCount) {
				stbi_uc* img = stbi_load(images[i].string().c_str(), &xt, &yt, &ct, 0);
				if (xt == extent.width || yt == extent.height) {
					if (ct == channels)
						memcpy(pixels + sliceSize * i, img, sliceSize);
					else {
						uint8_t* slice = pixels + sliceSize * i;
						for (uint32_t y = 0; y < extent.height; y++)
							for (uint32_t x = 0; x < extent.width; x++)
								for (uint32_t k = 0; k < min((uint32_t)ct, channels); k++)
									slice[channels * (y * extent.width + x) + k] = img[ct * (y * extent.width + x) + k];
					}
				}
				stbi_image_free(img);

				done++;
			}
		}));
	}
	printf("Loading stack");
	while (done < images.size()) {
		printf("\rLoading stack: %u/%u    ", done, (uint32_t)images.size());
		this_thread::sleep_for(10ms);
	}
	for (thread& t : threads) t.join();
	printf("\rLoading stack: Done           \n");

	Texture* volume = new Texture(folder.string(), device, pixels, sliceSize*extent.depth, extent, format, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);
	delete[] pixels;

	if (scale) *scale = float3(.05f, .05f, .05f);

	return volume;
}

struct DcmSlice {
	DicomImage* image;
	double3 spacing;
	double location;
};
DcmSlice ReadDicomSlice(const string& file) {
	DcmFileFormat fileFormat;
	fileFormat.loadFile(file.c_str());
	DcmDataset* dataset = fileFormat.getDataset();

	double3 s = 0;
	dataset->findAndGetFloat64(DCM_PixelSpacing, s.x, 0);
	dataset->findAndGetFloat64(DCM_PixelSpacing, s.y, 1);
	dataset->findAndGetFloat64(DCM_SliceThickness, s.z, 0);

	double x = 0;
	dataset->findAndGetFloat64(DCM_SliceLocation, x, 0);

	return { new DicomImage(file.c_str()), s, x };
}
Texture* ImageLoader::LoadDicomStack(const fs::path& folder, Device* device, float3* size) {
	if (!fs::exists(folder)) return nullptr;

	double3 maxSpacing = 0;
	vector<DcmSlice> images = {};
	for (const auto& p : fs::directory_iterator(folder))
		if (ExtensionMap.count(p.path().extension().string()) && ExtensionMap.find(p.path().extension().string())->second == ImageStackType::eDicom) {
			images.push_back(ReadDicomSlice(p.path().string()));
			maxSpacing = max(maxSpacing, images[images.size() - 1].spacing);
		}

	if (images.empty()) return nullptr;

	std::sort(images.begin(), images.end(), [](const DcmSlice& a, const DcmSlice& b) {
		return a.location < b.location;
	});

	vk::Extent3D extent = { images[0].image->getWidth(), images[0].image->getHeight(), (uint32_t)images.size() };

	if (extent.width == 0 || extent.height == 0) return nullptr;

	// volume size in meters
	if (size) {
		float2 b = (float)images[0].location;
		for (auto i : images) {
			b.x = (float)fmin(i.location - i.spacing.z * .5, b.x);
			b.y = (float)fmax(i.location + i.spacing.z * .5, b.y);
		}

		*size = float3(.001 * double3(maxSpacing.xy * double2(extent.width, extent.height), b.y - b.x));
		printf("%fm x %fm x %fm\n", size->x, size->y, size->z);
	}

	size_t sliceSize = extent.width * extent.height * sizeof(uint16_t);

	uint16_t* data = new uint16_t[extent.width * extent.height * extent.depth];
	memset(data, 0, extent.depth * sliceSize);
	for (uint32_t i = 0; i < images.size(); i++) {
		images[i].image->setMinMaxWindow();
		uint16_t* pixels = (uint16_t*)images[i].image->getOutputData(16);
		memcpy(data + i * extent.width * extent.height, pixels, sliceSize);
	}

	Texture* tex = new Texture(folder.string(), device, data, extent.depth*sliceSize, extent, vk::Format::eR16Unorm, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage);
	delete[] data;
	for (auto& i : images) delete i.image;
	return tex;
}

Texture* ImageLoader::LoadRawStack(const fs::path& folder, Device* device, float3* scale) {
	if (!fs::exists(folder)) return nullptr;

	vector<fs::path> images;
	for (const auto& p : fs::directory_iterator(folder))
		if (ExtensionMap.count(p.path().extension().string()) && ExtensionMap.find(p.path().extension().string())->second == ImageStackType::eRaw)
			images.push_back(p.path());
	if (images.empty()) return nullptr;
	std::sort(images.begin(), images.end(), [](const fs::path& a, const fs::path& b) {
		return a.string() < b.string();
	});

	vk::Extent3D extent = { 2048, 1216, (uint32_t)images.size() };
	size_t pixelCount = extent.width * extent.height;

	size_t sliceSize = pixelCount * 4;
	uint8_t* pixels = new uint8_t[sliceSize * extent.depth];
	memset(pixels, 0, sliceSize * extent.depth);

	uint32_t done = 0;
	vector<thread> threads;
	uint32_t threadCount = thread::hardware_concurrency();
	for (uint32_t j = 0; j < threadCount; j++) {
		threads.push_back(thread([=, &done]() {
			for (uint32_t i = j; i < images.size(); i += threadCount) {
				vector<uint8_t> slice;
				if (!ReadFile(images[i].string(), slice)) {
					fprintf_color(ConsoleColorBits::eRed, stderr, "Failed to read file %s\n", images[i].string().c_str());
					throw;
				}

				uint8_t* sliceStart = pixels + sliceSize * i;
				for (uint32_t y = 0; y < extent.height; y++)
					for (uint32_t x = 0; x < extent.width; x++) {
						sliceStart[4 * (x + y * extent.width) + 0] = slice[x + y * extent.width];
						sliceStart[4 * (x + y * extent.width) + 1] = slice[x + y * extent.width + pixelCount];
						sliceStart[4 * (x + y * extent.width) + 2] = slice[x + y * extent.width + 2*pixelCount];
						sliceStart[4 * (x + y * extent.width) + 3] = 0xFF;
					}

				done++;
			}
		}));
	}
	printf("Loading stack");
	while (done < images.size()) {
		printf("\rLoading stack: %u/%u    ", done, (uint32_t)images.size());
		this_thread::sleep_for(10ms);
	}
	for (thread& t : threads) t.join();
	printf("\rLoading stack: Done           \n");

	Texture* volume = new Texture(folder.string(), device, pixels, sliceSize * extent.depth, extent, vk::Format::eR8G8B8A8Unorm, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);
	delete[] pixels;

	if (scale) *scale = float3(.00033f * extent.width, .00033f * extent.height, .001f * extent.depth);

	return volume;
}
