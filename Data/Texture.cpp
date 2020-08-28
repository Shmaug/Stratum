#include <cmath>

#include <Data/AssetManager.hpp>
#include <Core/Pipeline.hpp>
#include <Data/Texture.hpp>

#include <Core/Buffer.hpp>
#include <Core/CommandBuffer.hpp>
#include <Util/Util.hpp>
#include <ThirdParty/stb_image.h>

using namespace std;

Sampler::Sampler(const string& name, Device* device, const vk::SamplerCreateInfo& samplerInfo) : mName(name), mDevice(device) {
	mSampler = ((vk::Device)*mDevice).createSampler(samplerInfo);
	mDevice->SetObjectName(mSampler, mName);
}
Sampler::Sampler(const string& name, Device* device, float maxLod, vk::Filter filter, vk::SamplerAddressMode addressMode, float maxAnisotropy) : mName(name), mDevice(device) {
	vk::SamplerCreateInfo samplerInfo = {};
	samplerInfo.magFilter = filter;
	samplerInfo.minFilter = filter;
	samplerInfo.addressModeU = addressMode;
	samplerInfo.addressModeV = addressMode;
	samplerInfo.addressModeW = addressMode;
	samplerInfo.anisotropyEnable = maxAnisotropy > 0 ? VK_TRUE : VK_FALSE;
	samplerInfo.maxAnisotropy = maxAnisotropy;
	samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = vk::CompareOp::eAlways;
	samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
	samplerInfo.minLod = 0;
	samplerInfo.maxLod = (float)maxLod;
	samplerInfo.mipLodBias = 0;

	mSampler = ((vk::Device)*mDevice).createSampler(samplerInfo);
	mDevice->SetObjectName(mSampler, mName);
}
Sampler::~Sampler() {
	mDevice->Destroy(mSampler);
}


uint8_t* load(const string& filename, TextureLoadFlags loadFlags, uint32_t& pixelSize, int32_t& x, int32_t& y, int32_t& channels, vk::Format& format) {
	uint8_t* pixels = nullptr;
	pixelSize = 0;
	x, y, channels;
	stbi_info(filename.c_str(), &x, &y, &channels);

	int desiredChannels = 4;
	if (stbi_is_16_bit(filename.c_str())) {
		pixels = (uint8_t*)stbi_load_16(filename.c_str(), &x, &y, &channels, desiredChannels);
		pixelSize = sizeof(uint16_t);
	} else if (stbi_is_hdr(filename.c_str())) {
		pixels = (uint8_t*)stbi_loadf(filename.c_str(), &x, &y, &channels, desiredChannels);
		pixelSize = sizeof(float);
	} else {
		pixels = (uint8_t*)stbi_load(filename.c_str(), &x, &y, &channels, desiredChannels);
		pixelSize = sizeof(uint8_t);
	}
	if (!pixels) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "Failed to load image: %s\n", filename.c_str());
		throw;
	}
	if (desiredChannels > 0) channels = desiredChannels;

	vk::Format formatMap[4][4] {
		{ vk::Format::eR8Unorm , vk::Format::eR8G8Unorm , vk::Format::eR8G8B8Unorm , vk::Format::eR8G8B8A8Unorm },
		{ vk::Format::eR16Unorm , vk::Format::eR16G16Unorm , vk::Format::eR16G16B16Unorm , vk::Format::eR16G16B16A16Unorm  },
		{ vk::Format::eR32Sfloat, vk::Format::eR32G32Sfloat, vk::Format::eR32G32B32Sfloat, vk::Format::eR32G32B32A32Sfloat },
		{ vk::Format::eR32Sfloat, vk::Format::eR32G32Sfloat, vk::Format::eR32G32B32Sfloat, vk::Format::eR32G32B32A32Sfloat },
	};

	if (loadFlags == TextureLoadFlags::eSrgb) {
		formatMap[0][0] = vk::Format::eR8Srgb;
		formatMap[0][1] = vk::Format::eR8G8Srgb;
		formatMap[0][2] = vk::Format::eR8G8B8Srgb;
		formatMap[0][3] = vk::Format::eR8G8B8A8Srgb;
	} else if (loadFlags == TextureLoadFlags::eSigned) {
		formatMap[0][0] = vk::Format::eR8Snorm;
		formatMap[0][1] = vk::Format::eR8G8Snorm;
		formatMap[0][2] = vk::Format::eR8G8B8Snorm;
		formatMap[0][3] = vk::Format::eR8G8B8A8Snorm;
		formatMap[1][0] = vk::Format::eR16Snorm;
		formatMap[1][1] = vk::Format::eR16G16Snorm;
		formatMap[1][2] = vk::Format::eR16G16B16Snorm;
		formatMap[1][3] = vk::Format::eR16G16B16A16Snorm;
	}

	format = formatMap[pixelSize - 1][channels - 1];

	return pixels;
}

Texture::Texture(const string& name, ::Device* device, const vector<string>& files, TextureLoadFlags loadFlags, vk::ImageCreateFlags createFlags)
	: mName(name), mDevice(device), mTiling(vk::ImageTiling::eOptimal), mCreateFlags(createFlags), mArrayLayers((uint32_t)files.size()) {
	int32_t x, y, channels;
	vk::Format format;
	uint32_t size;
	
	vector<uint8_t*> pixels(files.size());
	for (uint32_t i = 0; i < files.size(); i++) {
		pixels[i] = load(files[i], loadFlags, size, x, y, channels, format);
		// TODO: warn or fail about inconsistent format/channels/resolution
	}

	mFormat = format;
	mExtent = { (uint32_t)x, (uint32_t)y, 1 };
	mArrayLayers = (uint32_t)pixels.size();
	mMipLevels = (uint32_t)std::floor(std::log2(std::max(mExtent.width, mExtent.height))) + 1;
	mSampleCount = vk::SampleCountFlagBits::e1;
	mUsage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
	mMemoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;

	Create();

	vk::DeviceSize dataSize = mExtent.width * mExtent.height * size * channels;

	CommandBuffer* commandBuffer = mDevice->GetCommandBuffer();
	Buffer* uploadBuffer = commandBuffer->GetBuffer(name + " Copy", dataSize * mArrayLayers, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	for (uint32_t j = 0; j < pixels.size(); j++)
		memcpy((uint8_t*)uploadBuffer->MappedData() + j * dataSize, pixels[j], dataSize);

	commandBuffer->TransitionBarrier(this, vk::ImageLayout::eTransferDstOptimal);
	vk::BufferImageCopy copyRegion = {};
	copyRegion.bufferOffset = 0;
	copyRegion.bufferRowLength = 0;
	copyRegion.bufferImageHeight = 0;
	copyRegion.imageSubresource.aspectMask = mAspectFlags;
	copyRegion.imageSubresource.mipLevel = 0;
	copyRegion.imageSubresource.baseArrayLayer = 0;
	copyRegion.imageSubresource.layerCount = mArrayLayers;
	copyRegion.imageOffset = { 0, 0, 0 };
	copyRegion.imageExtent = { mExtent.width, mExtent.height, 1 };
	((vk::CommandBuffer)*commandBuffer).copyBufferToImage(*uploadBuffer, mImage, vk::ImageLayout::eTransferDstOptimal, { copyRegion });
	GenerateMipMaps(commandBuffer);
	mDevice->Execute(commandBuffer);

	for (uint32_t i = 0; i < pixels.size(); i++)
		stbi_image_free(pixels[i]);
}
Texture::Texture(const string& name, ::Device* device, const void* data, vk::DeviceSize dataSize, const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels, vk::SampleCountFlagBits numSamples, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties)
		: mName(name), mDevice(device), mExtent(extent), mArrayLayers(1), mMipLevels(mipLevels), mFormat(format), mSampleCount(numSamples), mUsage(usage), mMemoryProperties(properties), mTiling(vk::ImageTiling::eOptimal) {
	
	if (mipLevels == 0) mMipLevels = (uint32_t)std::floor(std::log2(std::max(mExtent.width, mExtent.height))) + 1;

	if (data && dataSize) {
		mUsage |= vk::ImageUsageFlagBits::eTransferDst;
		if (mMipLevels > 1) mUsage |= vk::ImageUsageFlagBits::eTransferSrc;
		Create();

		Buffer uploadBuffer(name + " Copy", mDevice, data, dataSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		CommandBuffer* commandBuffer = mDevice->GetCommandBuffer();
		commandBuffer->TransitionBarrier(this, vk::ImageLayout::eTransferDstOptimal);

		vk::BufferImageCopy copyRegion = {};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;
		copyRegion.imageSubresource.aspectMask = mAspectFlags;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = mArrayLayers;
		copyRegion.imageOffset = { 0, 0, 0 };
		copyRegion.imageExtent = mExtent;
		((vk::CommandBuffer)*commandBuffer).copyBufferToImage(uploadBuffer, mImage, vk::ImageLayout::eTransferDstOptimal, 1, &copyRegion);
		if (mMipLevels > 1) GenerateMipMaps(commandBuffer);
		mDevice->Execute(commandBuffer);
		commandBuffer->Wait();
	} else
		Create();
}

Texture::~Texture() {
	for (auto& kp : mViews)
		if (kp.second) mDevice->Destroy(kp.second);
	mDevice->Destroy(mImage);
	mDevice->FreeMemory(mMemory);
}

vk::ImageView Texture::View(uint32_t mipLevel, uint32_t mipCount, uint32_t arrayLayer, uint32_t layerCount) {
	if (mipCount == 0) mipCount = mMipLevels;
	if (layerCount == 0) layerCount = mArrayLayers;

	uint64_t hash = mipLevel;
	hash_combine(hash, mipCount);
	hash_combine(hash, arrayLayer);
	hash_combine(hash, layerCount);

	if (mViews.count(hash)) return mViews.at(hash);

	vk::ImageViewCreateInfo viewInfo = {};
	viewInfo.viewType = (mCreateFlags & vk::ImageCreateFlagBits::eCubeCompatible) ? vk::ImageViewType::eCube : (mExtent.depth > 1 ? vk::ImageViewType::e3D : (mExtent.height > 1 ? vk::ImageViewType::e2D : vk::ImageViewType::e1D));
	if (mExtent.width == 1 && mExtent.height == 1 && mExtent.depth == 1) viewInfo.viewType = vk::ImageViewType::e2D; // special 1x1 case
	viewInfo.image = mImage;
	viewInfo.format = mFormat;
	viewInfo.subresourceRange.aspectMask = mAspectFlags;
	viewInfo.subresourceRange.baseArrayLayer = arrayLayer;
	viewInfo.subresourceRange.layerCount = layerCount;
	viewInfo.subresourceRange.baseMipLevel = mipLevel;
	viewInfo.subresourceRange.levelCount = mipCount;
	vk::ImageView view = ((vk::Device)*mDevice).createImageView(viewInfo);
	mDevice->SetObjectName(view, mName + " View");
	mViews.emplace(hash, view);
	return view;
}

void Texture::Create() {
	vk::ImageCreateInfo imageInfo = {};
	imageInfo.imageType = mExtent.depth > 1 ? vk::ImageType::e3D : (mExtent.height > 1 ? vk::ImageType::e2D : vk::ImageType::e1D);
	if (mExtent.width == 1 && mExtent.height == 1 && mExtent.depth == 1) imageInfo.imageType = vk::ImageType::e2D; // special 1x1 case
	imageInfo.extent.width = mExtent.width;
	imageInfo.extent.height = mExtent.height;
	imageInfo.extent.depth = mExtent.depth;
	imageInfo.mipLevels = mMipLevels;
	imageInfo.arrayLayers = mArrayLayers;
	imageInfo.format = mFormat;
	imageInfo.tiling = mTiling;
	imageInfo.initialLayout = vk::ImageLayout::eUndefined;
	imageInfo.usage = mUsage;
	imageInfo.samples = mSampleCount;
	imageInfo.sharingMode = vk::SharingMode::eExclusive;
	imageInfo.flags = mCreateFlags;
	mImage = ((vk::Device)*mDevice).createImage(imageInfo);
	mDevice->SetObjectName(mImage, mName);

	vk::MemoryRequirements memRequirements = ((vk::Device)*mDevice).getImageMemoryRequirements(mImage);
	mMemory = mDevice->AllocateMemory(memRequirements, mMemoryProperties, mName);
	((vk::Device)*mDevice).bindImageMemory(mImage, mMemory.mDeviceMemory, mMemory.mOffset);
	
	mAspectFlags = {};
	switch (mFormat) {
	default:
		mAspectFlags = vk::ImageAspectFlagBits::eColor;
		break;
	case vk::Format::eD16Unorm:
	case vk::Format::eD32Sfloat:
		mAspectFlags = vk::ImageAspectFlagBits::eDepth;
		break;
	case vk::Format::eD16UnormS8Uint:
	case vk::Format::eD24UnormS8Uint:
	case vk::Format::eD32SfloatS8Uint:
		mAspectFlags = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
		break;
	}

	mLastKnownLayout = vk::ImageLayout::eUndefined;
	mLastKnownStageFlags = vk::PipelineStageFlagBits::eTopOfPipe;
	mLastKnownAccessFlags = {};
}

void Texture::GenerateMipMaps(CommandBuffer* commandBuffer) {
	// Transition mip 0 to vk::ImageLayout::eTransferSrcOptimal
	commandBuffer->TransitionBarrier(*this, { mAspectFlags, 0, 1, 0, mArrayLayers }, mLastKnownStageFlags, vk::PipelineStageFlagBits::eTransfer, mLastKnownLayout, vk::ImageLayout::eTransferSrcOptimal);
	// Transition all other mips to vk::ImageLayout::eTransferDstOptimal
	commandBuffer->TransitionBarrier(*this, { mAspectFlags, 1, mMipLevels - 1, 0, mArrayLayers }, mLastKnownStageFlags, vk::PipelineStageFlagBits::eTransfer, mLastKnownLayout, vk::ImageLayout::eTransferDstOptimal);

	vk::ImageBlit blit = {};
	blit.srcOffsets[0] = { 0, 0, 0 };
	blit.srcSubresource.baseArrayLayer = 0;
	blit.srcSubresource.layerCount = mArrayLayers;
	blit.srcSubresource.aspectMask = mAspectFlags;
	blit.dstOffsets[0] = { 0, 0, 0 };
	blit.dstSubresource.baseArrayLayer = 0;
	blit.dstSubresource.layerCount = mArrayLayers;
	blit.dstSubresource.aspectMask = mAspectFlags;

	blit.srcOffsets[1] = { (int32_t)mExtent.width, (int32_t)mExtent.height, (int32_t)mExtent.depth };

	for (uint32_t i = 1; i < mMipLevels; i++) {
		// Blit the previous mip level into this one
		blit.srcSubresource.mipLevel = i - 1;
		blit.dstSubresource.mipLevel = i;
		blit.dstOffsets[1].x = max(1, blit.srcOffsets[1].x / 2);
		blit.dstOffsets[1].y = max(1, blit.srcOffsets[1].y / 2);
		blit.dstOffsets[1].z = max(1, blit.srcOffsets[1].z / 2);
		((vk::CommandBuffer)*commandBuffer).blitImage(
			mImage, vk::ImageLayout::eTransferSrcOptimal,
			mImage, vk::ImageLayout::eTransferDstOptimal,
			{ blit }, vk::Filter::eLinear);

		blit.srcOffsets[1] = blit.dstOffsets[1];
		
		// Transition this mip level to vk::ImageLayout::eTransferSrcOptimal
		commandBuffer->TransitionBarrier(*this, { mAspectFlags, i, 1, 0, mArrayLayers }, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal);
	}

	mLastKnownLayout = vk::ImageLayout::eTransferSrcOptimal;
	mLastKnownStageFlags = vk::PipelineStageFlagBits::eTransfer;
	mLastKnownAccessFlags = vk::AccessFlagBits::eTransferRead;
}