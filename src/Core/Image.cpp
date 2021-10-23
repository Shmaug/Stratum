#include "Image.hpp"

#include "Buffer.hpp"
#include "CommandBuffer.hpp"
#include "Pipeline.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>

using namespace stm;

Image::PixelData Image::load(Device& device, const fs::path& filename, bool srgb) {
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
	if (!pixels) throw invalid_argument("Could not load " + filename.string());
	cout << "Loaded " << filename << " (" << x << "x" << y << ")" << endl;
	if (desiredChannels) channels = desiredChannels;

	Buffer::View<byte> buf(make_shared<Buffer>(device, filename.stem().string() + "/Staging", x*y*texel_size(format), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY));
	memcpy(buf.data(), pixels, buf.size());
	stbi_image_free(pixels);
	return make_pair(Buffer::TexelView(buf, format), vk::Extent3D(x,y,1));
}

void Image::init_state() {
	vector<vk::ImageAspectFlags> aspects;
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
		mAspect = vk::ImageAspectFlagBits::eDepth|vk::ImageAspectFlagBits::eStencil;
		break;
	}

	uint32_t aspectMask = (uint32_t)mAspect;
	while (aspectMask) {
		uint32_t aspect = 1 << countr_zero(aspectMask);
		aspectMask &= ~aspect;
		for (uint32_t layer = 0; layer < mLayerCount; layer++)	
			for (uint32_t level = 0; level < mLevelCount; level++)	
				tracked_state((vk::ImageAspectFlags)aspect, layer, level) = make_tuple(vk::ImageLayout::eUndefined, vk::PipelineStageFlagBits::eTopOfPipe, vk::AccessFlags{});
	}
}
void Image::create() {
	vk::ImageCreateInfo imageInfo = {};
	imageInfo.imageType = mType;
	imageInfo.extent = mExtent;
	imageInfo.mipLevels = mLevelCount;
	imageInfo.arrayLayers = mLayerCount;
	imageInfo.format = mFormat;
	imageInfo.tiling = mTiling;
	imageInfo.initialLayout = vk::ImageLayout::eUndefined;
	imageInfo.usage = mUsage;
	imageInfo.samples = mSampleCount;
	imageInfo.sharingMode = vk::SharingMode::eExclusive;
	imageInfo.flags = mCreateFlags;
	mImage = mDevice->createImage(imageInfo);
	mDevice.set_debug_name(mImage, name());
}

void Image::transition_barrier(CommandBuffer& commandBuffer, vk::PipelineStageFlags dstStage, vk::ImageLayout newLayout, vk::AccessFlags accessFlags, vk::ImageSubresourceRange subresourceRange) {
	if (subresourceRange.levelCount == 0) subresourceRange.levelCount = mLevelCount - subresourceRange.baseMipLevel;
	if (subresourceRange.layerCount == 0) subresourceRange.layerCount = mLayerCount - subresourceRange.baseArrayLayer;
	uint32_t aspectMask = (subresourceRange.aspectMask == vk::ImageAspectFlags{0}) ? (uint32_t)mAspect : (uint32_t)subresourceRange.aspectMask;
	while (aspectMask) {
		uint32_t aspect = 1 << countr_zero(aspectMask);
		aspectMask &= ~aspect;
		for (uint32_t layer = subresourceRange.baseArrayLayer; layer < subresourceRange.baseArrayLayer+subresourceRange.layerCount; layer++) {
			for (uint32_t level = subresourceRange.baseMipLevel; level < subresourceRange.baseMipLevel+subresourceRange.levelCount; level++) {
				auto& state = tracked_state((vk::ImageAspectFlags)aspect, layer, level);
				//if (get<vk::ImageLayout>(state) != newLayout || get<vk::AccessFlags>(state) != accessFlags) {
					vk::ImageMemoryBarrier b;
					b.image = mImage;
					b.newLayout = newLayout;
					b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					b.dstAccessMask = accessFlags;
					b.oldLayout = get<vk::ImageLayout>(state);
					b.subresourceRange.aspectMask = (vk::ImageAspectFlags)aspect;
					b.subresourceRange.baseArrayLayer = layer;
					b.subresourceRange.layerCount = 1;
					b.subresourceRange.baseMipLevel = level;
					b.subresourceRange.levelCount = 1;
					b.srcAccessMask = get<vk::AccessFlags>(state);
					commandBuffer.barrier(b, get<vk::PipelineStageFlags>(state), dstStage);
				//}
				state = make_tuple(newLayout, dstStage, accessFlags);
			}
		}
	}
}

void Image::generate_mip_maps(CommandBuffer& commandBuffer) {
	transition_barrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
	vk::ImageBlit blit = {};
	blit.srcOffsets[0] = blit.dstOffsets[0] = vk::Offset3D(0, 0, 0);
	blit.srcOffsets[1] = vk::Offset3D((int32_t)mExtent.width, (int32_t)mExtent.height, (int32_t)mExtent.depth);
	blit.srcSubresource.baseArrayLayer = 0;
	blit.srcSubresource.layerCount = mLayerCount;
	blit.srcSubresource.aspectMask = mAspect;
	blit.dstSubresource = blit.srcSubresource;
	for (uint32_t i = 1; i < mLevelCount; i++) {
		transition_barrier(commandBuffer, vk::ImageLayout::eTransferSrcOptimal, vk::ImageSubresourceRange({}, i-1, 1));
		blit.srcSubresource.mipLevel = i - 1;
		blit.dstSubresource.mipLevel = i;
		blit.dstOffsets[1].x = max(1, blit.srcOffsets[1].x / 2);
		blit.dstOffsets[1].y = max(1, blit.srcOffsets[1].y / 2);
		blit.dstOffsets[1].z = max(1, blit.srcOffsets[1].z / 2);
		commandBuffer->blitImage(
			mImage, vk::ImageLayout::eTransferSrcOptimal,
			mImage, vk::ImageLayout::eTransferDstOptimal,
			blit, vk::Filter::eLinear);
		blit.srcOffsets[1] = blit.dstOffsets[1];
	}
}

Image::View::View(const shared_ptr<Image>& image, const vk::ImageSubresourceRange& subresource, const vk::ComponentMapping& components, vk::ImageViewType type)
	: mImage(image), mSubresource(subresource), mComponents(components) {
	if (mSubresource.aspectMask == (vk::ImageAspectFlags)0) mSubresource.aspectMask = image->aspect();
	if (mSubresource.levelCount == 0) mSubresource.levelCount = image->level_count();
	if (mSubresource.layerCount == 0) mSubresource.layerCount = image->layer_count();
	auto key = make_pair(mSubresource, mComponents);
	if (auto it = image->mViews.find(key); it != image->mViews.end())
		mView = it->second;
	else {
		vk::ImageViewCreateInfo info = {};
		info.image = **mImage;
		info.format = mImage->format();
		info.subresourceRange = mSubresource;
		info.components = components;
		if (type == (vk::ImageViewType)VK_IMAGE_VIEW_TYPE_MAX_ENUM) {
			if (mImage->create_flags() & vk::ImageCreateFlagBits::eCubeCompatible)
				info.viewType = (mImage->layer_count() > 1) ? vk::ImageViewType::eCubeArray : vk::ImageViewType::eCube;
			else if (mImage->type() == vk::ImageType::e3D)
				info.viewType = vk::ImageViewType::e3D;
			else if (mImage->type() == vk::ImageType::e2D)
				info.viewType = (mImage->layer_count() > 1) ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
			else
				info.viewType = (mImage->layer_count() > 1) ? vk::ImageViewType::e1DArray : vk::ImageViewType::e1D;
		} else
			info.viewType = type;
		mView = image->mViews.emplace(key, mImage->mDevice->createImageView(info)).first->second;
		mImage->mDevice.set_debug_name(mView, mImage->name() + "/View");
	}
}