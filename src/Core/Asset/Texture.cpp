#include "Texture.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include "../Buffer.hpp"
#include "../CommandBuffer.hpp"
#include "../Pipeline.hpp"


using namespace stm;

uint8_t* LoadPixels(const fs::path& filename, TextureLoadFlags loadFlags, uint32_t& pixelSize, int32_t& x, int32_t& y, int32_t& channels, vk::Format& format) {
	uint8_t* pixels = nullptr;
	pixelSize = 0;
	x, y, channels;
	stbi_info(filename.string().c_str(), &x, &y, &channels);

	int desiredChannels = 4;
	if (stbi_is_16_bit(filename.string().c_str())) {
		pixels = (uint8_t*)stbi_load_16(filename.string().c_str(), &x, &y, &channels, desiredChannels);
		pixelSize = sizeof(uint16_t);
	} else if (stbi_is_hdr(filename.string().c_str())) {
		pixels = (uint8_t*)stbi_loadf(filename.string().c_str(), &x, &y, &channels, desiredChannels);
		pixelSize = sizeof(float);
	} else {
		pixels = (uint8_t*)stbi_load(filename.string().c_str(), &x, &y, &channels, desiredChannels);
		pixelSize = sizeof(uint8_t);
	}
	if (!pixels) throw invalid_argument("could not LoadPixels " + filename.string());
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

Texture::Texture(stm::Device& device, const vector<fs::path>& layers, TextureLoadFlags loadFlags, vk::ImageCreateFlags createFlags)
	: Asset(device, layers[0]), mName(), mDevice(device), mCreateFlags(createFlags), mArrayLayers((uint32_t)layers.size()), mTiling(vk::ImageTiling::eOptimal) {
	int32_t x, y, channels;
	vk::Format format;
	uint32_t size;
	
	vector<uint8_t*> pixels(layers.size());
	for (uint32_t i = 0; i < layers.size(); i++) {
		pixels[i] = LoadPixels(layers[i], loadFlags, size, x, y, channels, format);
		// TODO: warn or fail about inconsistent format/channels/resolution
	}

	mFormat = format;
	mExtent = vk::Extent3D((uint32_t)x, (uint32_t)y, 1);
	mArrayLayers = (uint32_t)pixels.size();
	mMipLevels = (uint32_t)floor(log2(max(mExtent.width, mExtent.height))) + 1;
	mSampleCount = vk::SampleCountFlagBits::e1;
	mUsage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
	mMemoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;

	Create();

	vk::DeviceSize dataSize = mExtent.width * mExtent.height * size * channels;

	CommandBuffer* commandBuffer = mDevice.GetCommandBuffer(mName + "/BufferImageCopy", vk::QueueFlagBits::eGraphics);
	auto stagingBuffer = commandBuffer->GetBuffer(mName + "/Staging", dataSize * mArrayLayers, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	for (uint32_t j = 0; j < pixels.size(); j++)
		memcpy((uint8_t*)stagingBuffer->Mapped() + j * dataSize, pixels[j], dataSize);
	commandBuffer->TransitionBarrier(*this, vk::ImageLayout::eTransferDstOptimal);
	vk::BufferImageCopy copyRegion(0, 0, 0, { mAspectFlags, 0, 0, mArrayLayers }, { 0,0,0 }, { mExtent.width, mExtent.height, 1 });
	(*commandBuffer)->copyBufferToImage(**stagingBuffer, mImage, vk::ImageLayout::eTransferDstOptimal, { copyRegion });
	GenerateMipMaps(*commandBuffer);
	mDevice.Execute(commandBuffer, true);

	for (uint32_t i = 0; i < pixels.size(); i++)
		stbi_image_free(pixels[i]);
}
Texture::Texture(const string& name, stm::Device& device, const vk::Extent3D& extent, vk::Format format, const byte_blob& data, vk::ImageUsageFlags usage, uint32_t mipLevels, vk::SampleCountFlagBits numSamples, vk::MemoryPropertyFlags properties)
		: Asset(device, {}), mName(name), mDevice(device), mExtent(extent), mFormat(format), mMipLevels(mipLevels), mUsage(usage), mSampleCount(numSamples), mMemoryProperties(properties), mArrayLayers(1), mTiling(vk::ImageTiling::eOptimal) {
	
	if (mipLevels == 0) mMipLevels = (uint32_t)floor(log2(max(mExtent.width, mExtent.height))) + 1;

	if (data.data()) {
		Create();

		auto stagingBuffer = make_shared<Buffer>(mName + "/Staging", mDevice, data, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		CommandBuffer* commandBuffer = mDevice.GetCommandBuffer(mName + "BufferImageCopy", vk::QueueFlagBits::eTransfer);
		commandBuffer->TrackResource(stagingBuffer);
		commandBuffer->TransitionBarrier(*this, vk::ImageLayout::eTransferDstOptimal);
		vk::BufferImageCopy copyRegion(0, 0, 0, { mAspectFlags, 0, 0, mArrayLayers }, { 0,0,0 }, { mExtent.width, mExtent.height, 1 });
		(*commandBuffer)->copyBufferToImage(**stagingBuffer, mImage, vk::ImageLayout::eTransferDstOptimal, { copyRegion });
		
		mDevice.Execute(commandBuffer, true);
	} else
		Create();
}

Texture::~Texture() {
	for (auto& kp : mViews)
		if (kp.second) mDevice->destroyImageView(kp.second);
	mDevice->destroyImage(mImage);
	mDevice.FreeMemory(mMemoryBlock);
}

vk::ImageView Texture::View(uint32_t mipLevel, uint32_t mipCount, uint32_t arrayLayer, uint32_t layerCount) {
	if (mipCount == 0) mipCount = mMipLevels;
	if (layerCount == 0) layerCount = mArrayLayers;

	uint64_t h = basic_hash(mipLevel, mipCount, arrayLayer, layerCount);

	if (mViews.count(h)) return mViews.at(h);

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
	vk::ImageView view = mDevice->createImageView(viewInfo);
	mDevice.SetObjectName(view, mName + " View");
	mViews.emplace(h, view);
	return view;
}

void Texture::Create() {
	vk::ImageCreateInfo imageInfo = {};
	imageInfo.imageType = mExtent.depth > 1 ? vk::ImageType::e3D
		: (mExtent.height > 1 || (mExtent.width == 1 && mExtent.height == 1 && mExtent.depth == 1)) ? vk::ImageType::e2D // special 1x1 case: force 2D
		: vk::ImageType::e1D;
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
	mImage = mDevice->createImage(imageInfo);
	mDevice.SetObjectName(mImage, mName);

	vk::MemoryRequirements memRequirements = mDevice->getImageMemoryRequirements(mImage);
	mMemoryBlock = mDevice.AllocateMemory(memRequirements, mMemoryProperties, mName);
	mDevice->bindImageMemory(mImage, **mMemoryBlock.mMemory, mMemoryBlock.mOffset);
	
	mAspectFlags = vk::ImageAspectFlagBits::eColor;
	switch (mFormat) {
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

	mTrackedLayout = vk::ImageLayout::eUndefined;
	mTrackedStageFlags = vk::PipelineStageFlagBits::eTopOfPipe;
	mTrackedAccessFlags = {};
}

void Texture::GenerateMipMaps(CommandBuffer& commandBuffer) {
	// Transition mip 0 to vk::ImageLayout::eTransferSrcOptimal
	commandBuffer.TransitionBarrier(mImage, { mAspectFlags, 0, 1, 0, mArrayLayers }, mTrackedStageFlags, vk::PipelineStageFlagBits::eTransfer, mTrackedLayout, vk::ImageLayout::eTransferSrcOptimal);
	// Transition all other mips to vk::ImageLayout::eTransferDstOptimal
	commandBuffer.TransitionBarrier(mImage, { mAspectFlags, 1, mMipLevels - 1, 0, mArrayLayers }, mTrackedStageFlags, vk::PipelineStageFlagBits::eTransfer, mTrackedLayout, vk::ImageLayout::eTransferDstOptimal);

	vk::ImageBlit blit = {};
	blit.srcOffsets[0] = vk::Offset3D(0, 0, 0);
	blit.srcSubresource.baseArrayLayer = 0;
	blit.srcSubresource.layerCount = mArrayLayers;
	blit.srcSubresource.aspectMask = mAspectFlags;
	blit.dstOffsets[0] = vk::Offset3D(0, 0, 0);
	blit.dstSubresource.baseArrayLayer = 0;
	blit.dstSubresource.layerCount = mArrayLayers;
	blit.dstSubresource.aspectMask = mAspectFlags;

	blit.srcOffsets[1] = vk::Offset3D((int32_t)mExtent.width, (int32_t)mExtent.height, (int32_t)mExtent.depth);

	for (uint32_t i = 1; i < mMipLevels; i++) {
		// Blit the previous mip level into this one
		blit.srcSubresource.mipLevel = i - 1;
		blit.dstSubresource.mipLevel = i;
		blit.dstOffsets[1].x = max(1, blit.srcOffsets[1].x / 2);
		blit.dstOffsets[1].y = max(1, blit.srcOffsets[1].y / 2);
		blit.dstOffsets[1].z = max(1, blit.srcOffsets[1].z / 2);
		commandBuffer->blitImage(
			mImage, vk::ImageLayout::eTransferSrcOptimal,
			mImage, vk::ImageLayout::eTransferDstOptimal,
			{ blit }, vk::Filter::eLinear);

		blit.srcOffsets[1] = blit.dstOffsets[1];
		
		// Transition this mip level to vk::ImageLayout::eTransferSrcOptimal
		commandBuffer.TransitionBarrier(mImage, { mAspectFlags, i, 1, 0, mArrayLayers }, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal);
	}

	mTrackedLayout = vk::ImageLayout::eTransferSrcOptimal;
	mTrackedStageFlags = vk::PipelineStageFlagBits::eTransfer;
	mTrackedAccessFlags = vk::AccessFlagBits::eTransferRead;
}