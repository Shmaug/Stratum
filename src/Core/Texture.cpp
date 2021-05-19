#include "Texture.hpp"

#include "Buffer.hpp"
#include "CommandBuffer.hpp"
#include "Pipeline.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <stb_image.h>

using namespace stm;

Texture::PixelData Texture::load(Device& device, const fs::path& filename, bool srgb) {
	int x,y,channels;
	stbi_info(filename.string().c_str(), &x, &y, &channels);

	int desiredChannels = 0;
	if (channels == 3) desiredChannels = 4;

	byte* pixels = nullptr;
	vk::Format format = vk::Format::eUndefined;
	if (stbi_is_hdr(filename.string().c_str())) {
		pixels = (byte*)stbi_loadf(filename.string().c_str(), &x, &y, &channels, desiredChannels);
		switch(desiredChannels ? desiredChannels : channels) {
			case 1: format = vk::Format::eR32Sfloat; break;
			case 2: format = vk::Format::eR32G32Sfloat; break;
			case 3: format = vk::Format::eR32G32B32Sfloat; break;
			case 4: format = vk::Format::eR32G32B32A32Sfloat; break;
		}
	} else if (stbi_is_16_bit(filename.string().c_str())) {
		pixels = (byte*)stbi_load_16(filename.string().c_str(), &x, &y, &channels, desiredChannels);
		switch(desiredChannels ? desiredChannels : channels) {
			case 1: format = vk::Format::eR16Unorm; break;
			case 2: format = vk::Format::eR16G16Unorm; break;
			case 3: format = vk::Format::eR16G16B16Unorm; break;
			case 4: format = vk::Format::eR16G16B16A16Unorm; break;
		}
	} else {
		pixels = (byte*)stbi_load(filename.string().c_str(), &x, &y, &channels, desiredChannels);
		switch (desiredChannels ? desiredChannels : channels) {
			case 1: format = vk::Format::eR8Unorm; break;
			case 2: format = vk::Format::eR8G8Unorm; break;
			case 3: format = vk::Format::eR8G8B8Unorm; break;
			case 4: format = vk::Format::eR8G8B8A8Unorm; break;
		}
	}
	if (!pixels) throw invalid_argument("could not load " + filename.string());
	if (desiredChannels) channels = desiredChannels;

	Buffer::View<byte> buf(make_shared<Buffer>(device, filename.stem().string() + "/Staging", x*y*texel_size(format), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eUniformTexelBuffer|vk::BufferUsageFlagBits::eStorageTexelBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU));
	memcpy(buf.data(), pixels, buf.size());
	stbi_image_free(pixels);
	return make_pair(Buffer::TexelView(buf, format), vk::Extent3D(x,y,1));
}

void Texture::create() {
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
	mDevice.set_debug_name(mImage, name());

	switch (mFormat) {
	default:
		mAspect = vk::ImageAspectFlagBits::eColor;
		break;
	case vk::Format::eD16Unorm:
	case vk::Format::eD32Sfloat:
		mAspect = vk::ImageAspectFlagBits::eDepth;
		break;
	case vk::Format::eS8Uint:
		mAspect = vk::ImageAspectFlagBits::eStencil;
		break;
	case vk::Format::eD16UnormS8Uint:
	case vk::Format::eD24UnormS8Uint:
	case vk::Format::eD32SfloatS8Uint:
	case vk::Format::eX8D24UnormPack32:
		mAspect = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
		break;
	}
}

void Texture::transition_barrier(CommandBuffer& commandBuffer, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
	if (oldLayout == newLayout) return;
	if (newLayout == vk::ImageLayout::eUndefined) {
		mTrackedLayout = newLayout;
		mTrackedStageFlags = dstStage;
		mTrackedAccessFlags = {};
		return;
	}
	vk::ImageMemoryBarrier barrier = {};
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = mImage;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = mip_levels();
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = array_layers();
	barrier.srcAccessMask = mTrackedAccessFlags;
	barrier.dstAccessMask = guess_access_flags(newLayout);
	barrier.subresourceRange.aspectMask = mAspect;
	commandBuffer.barrier(srcStage, dstStage, barrier);
	mTrackedLayout = newLayout;
	mTrackedStageFlags = dstStage;
	mTrackedAccessFlags = barrier.dstAccessMask;
}

void Texture::generate_mip_maps(CommandBuffer& commandBuffer) {
	// Transition mip 0 to vk::ImageLayout::eTransferSrcOptimal
	commandBuffer.transition_barrier(mImage, { mAspect, 0, 1, 0, mArrayLayers }, mTrackedStageFlags, vk::PipelineStageFlagBits::eTransfer, mTrackedLayout, vk::ImageLayout::eTransferSrcOptimal);
	// Transition all other mips to vk::ImageLayout::eTransferDstOptimal
	commandBuffer.transition_barrier(mImage, { mAspect, 1, mMipLevels - 1, 0, mArrayLayers }, mTrackedStageFlags, vk::PipelineStageFlagBits::eTransfer, mTrackedLayout, vk::ImageLayout::eTransferDstOptimal);

	vk::ImageBlit blit = {};
	blit.srcOffsets[0] = blit.dstOffsets[0] = vk::Offset3D(0, 0, 0);
	blit.srcSubresource.baseArrayLayer = 0;
	blit.srcSubresource.layerCount = mArrayLayers;
	blit.srcSubresource.aspectMask = mAspect;
	blit.dstSubresource = blit.srcSubresource;

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
		commandBuffer.transition_barrier(mImage, { mAspect, i, 1, 0, mArrayLayers }, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal);
	}

	mTrackedLayout = vk::ImageLayout::eTransferSrcOptimal;
	mTrackedStageFlags = vk::PipelineStageFlagBits::eTransfer;
	mTrackedAccessFlags = vk::AccessFlagBits::eTransferRead;
}

Texture::View::View(const shared_ptr<Texture>& texture, const vk::ImageSubresourceRange& subresource, const vk::ComponentMapping& components)
	: mTexture(texture), mSubresource(subresource), mComponents(components) {
	if (mSubresource.aspectMask == (vk::ImageAspectFlags)0) mSubresource.aspectMask = texture->aspect();
	if (mSubresource.levelCount == 0) mSubresource.levelCount = texture->mip_levels();
	if (mSubresource.layerCount == 0) mSubresource.layerCount = texture->array_layers();
	auto key = make_pair(mSubresource, mComponents);
	if (auto it = texture->mViews.find(key); it != texture->mViews.end())
		mView = it->second;
	else {
		vk::ImageViewCreateInfo info = {};
		info.image = **mTexture;
		info.format = mTexture->format();
		info.subresourceRange = mSubresource;
		info.components = components;
		if (mTexture->create_flags() & vk::ImageCreateFlagBits::eCubeCompatible)
			info.viewType = vk::ImageViewType::eCube;
		else if (mTexture->extent().depth > 1)
			info.viewType = vk::ImageViewType::e3D;
		else if (mTexture->extent().height > 1)
			info.viewType = vk::ImageViewType::e2D;
		else
			info.viewType = vk::ImageViewType::e1D;
		mView = texture->mViews.emplace(key, mTexture->mDevice->createImageView(info)).first->second;
		mTexture->mDevice.set_debug_name(mView, mTexture->name() + "/View");
	}
}