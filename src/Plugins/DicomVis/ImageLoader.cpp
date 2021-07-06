#include "ImageLoader.hpp"

#include <execution>

#include <stb_image.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmimgle/dcmimage.h>

using namespace dcmvs;

Texture::View load_dicom(const fs::path& folder, CommandBuffer& commandBuffer, Array3f* voxelSize) {
	Array2d bounds = Array2d::Zero();
	Array2d maxSpacing = Array2d::Zero();

	vector<pair<double, unique_ptr<DicomImage>>> slices;
	for (const auto& p : fs::directory_iterator(folder))
		if (p.path().extension() == ".dcm") {
			DcmFileFormat fileFormat;
			fileFormat.loadFile(p.path().string().c_str());
			DcmDataset* dataset = fileFormat.getDataset();

			Array2d spacing;
			double location;
			dataset->findAndGetFloat64(DCM_PixelSpacing, spacing[0], 0);
			dataset->findAndGetFloat64(DCM_PixelSpacing, spacing[1], 1);
			dataset->findAndGetFloat64(DCM_SliceLocation, location, 0);

			maxSpacing = maxSpacing.max(spacing);
			bounds[0] = min(location - spacing.z()/2, bounds[0]);
			bounds[1] = max(location + spacing.z()/2, bounds[1]);

			slices.emplace_back(location, make_unique<DicomImage>(p.path().string().c_str()));
		}
	if (slices.empty()) return {};

	ranges::sort(slices, {}, [](const auto& p) { return p.first; });

	vk::Extent3D extent(slices.front().second->getWidth(), slices.front().second->getHeight(), (uint32_t)slices.size());
	if (extent.width == 0 || extent.height == 0) return {};

	// voxel size in meters
	if (voxelSize) {
		*voxelSize = (.001 * Vector3d(maxSpacing.x(), maxSpacing.y(), (bounds[1] - bounds[0])/extent.depth)).cast<float>();
		printf("%fm x %fm x %fm\n", voxelSize->x()*extent.width, voxelSize->y()*extent.height, voxelSize->z()*extent.depth);
	}

	size_t sliceSize = extent.width * extent.height * sizeof(uint16_t);
	auto pixels = make_shared<Buffer>(commandBuffer.mDevice, folder.string()+"/Staging", extent.depth*sliceSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);

	auto inds = ranges::iota_view(size_t(0), slices.size());
	for (uint32_t i = 0; i < slices.size(); i++) {
		slices[i].second->setMinMaxWindow();
		memcpy(pixels->data() + i * extent.width * extent.height, slices[i].second->getOutputData(16), sliceSize);
	}

	auto volume = make_shared<Texture>(folder.string(), commandBuffer.mDevice, extent, vk::Format::eR16Unorm, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eTransferDst);
	return commandBuffer.upload_image<byte>(pixels, volume);
}

Texture::View load_stbi(const fs::path& folder, CommandBuffer& commandBuffer, bool reverse, bool isInteger, bool isSigned) {
	vector<pair<fs::path, Array3i>> images;
	Array3i c;
	for (const auto& p : fs::directory_iterator(folder))
		if (stbi_info(p.path().string().c_str(), &c[0], &c[1], &c[2]) == 0) 
			if (images.empty() || (c == images.front().second).all())
				images.emplace_back(p.path(), c);
	if (images.empty()) return {};
	
	ranges::sort(images, [reverse](const auto& lhs, const auto& rhs) {
		string astr = lhs.first.stem().string();
		string bstr = rhs.first.stem().string();
		if (astr.find_first_not_of( "0123456789" ) == string::npos && bstr.find_first_not_of( "0123456789" ) == string::npos)
			return reverse ? (stoi(astr) > stoi(bstr)) : (stoi(astr) < stoi(bstr));
		return reverse ? (astr > bstr) : (astr < bstr);
	});
	
	vk::Extent3D extent((uint32_t)images.front().second[0], (uint32_t)images.front().second[1], (uint32_t)images.size());

	vk::Format format;
	switch (images.front().second[2]) {
	case 4:
		format = isInteger ? (isSigned ? vk::Format::eR8G8B8A8Sint : vk::Format::eR8G8B8A8Uint) : (isSigned ? vk::Format::eR8G8B8A8Snorm : vk::Format::eR8G8B8A8Unorm);
		break;
	case 3:
		format = isInteger ? (isSigned ? vk::Format::eR8G8B8Sint : vk::Format::eR8G8B8Uint) : (isSigned ? vk::Format::eR8G8B8Snorm : vk::Format::eR8G8B8Unorm);
		break;
	case 2:
		format = isInteger ? (isSigned ? vk::Format::eR8G8Sint : vk::Format::eR8G8Uint) : (isSigned ? vk::Format::eR8G8Snorm : vk::Format::eR8G8Unorm);
		break;
	case 1:
		format = isInteger ? (isSigned ? vk::Format::eR8Sint : vk::Format::eR8Uint) : (isSigned ? vk::Format::eR8Snorm : vk::Format::eR8Unorm);
		break;
	default:
		return {}; // ??
	}

	size_t sliceSize = extent.width * extent.height * images.front().second[2];
	auto pixels = make_shared<Buffer>(commandBuffer.mDevice, folder.string()+"/Staging", sliceSize * extent.depth, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);

	int xt, yt, ct;
	for (uint32_t i = 0; i < images.size(); i++) {
		stbi_uc* img = stbi_load(images[i].first.string().c_str(), &xt, &yt, &ct, 0);
		memcpy(pixels->data() + sliceSize * i, img, sliceSize);
		stbi_image_free(img);
	}

	auto volume = make_shared<Texture>(folder.string(), commandBuffer.mDevice, extent, format, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eTransferDst);
	return commandBuffer.upload_image<byte>(pixels, volume);
}

Texture::View load_raw(const fs::path& folder, CommandBuffer& commandBuffer, vk::Extent2D sliceExtent, vk::Format format) {
	if (!fs::exists(folder)) return {};

	vector<pair<fs::path, size_t>> slices;
	for (const auto& p : fs::directory_iterator(folder)) {
		ifstream s(p.path(), ios::ate|ios::binary);
		if (s.is_open() && (slices.empty() || s.tellg() == slices.front().second))
			slices.emplace_back(p.path(), s.tellg());
	}
	if (slices.empty()) return {};
	
	ranges::sort(slices, [](const auto& a, const auto& b) { return a.first.string() < b.first.string(); });

	vk::Extent3D extent(sliceExtent.width, sliceExtent.height, (uint32_t)slices.size());
	
	size_t pixelCount = extent.width * extent.height;
	auto pixels = make_shared<Buffer>(commandBuffer.mDevice, folder.string()+"/Staging", slices.front().second * extent.depth, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);

	for (uint32_t i = 0; i < slices.size(); i++) {
		auto slice = read_file<vector<byte>>(slices[i].first);
		memcpy(pixels->data() + slices.front().second * i, slice.data(), slices.front().second);
	}
	
	auto volume = make_shared<Texture>(folder.string(), commandBuffer.mDevice, extent, format, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eTransferDst);
	commandBuffer.upload_image<byte>(pixels, volume);

	return volume;
}
