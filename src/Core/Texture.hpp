#pragma once

#include "Buffer.hpp"

namespace stm {

// TODO: TextureView object for Swapchain images, to allow for the swapchain to be used directly in a renderpass

class Texture : public DeviceResource {
public:
	using PixelData = tuple<byte_blob, vk::Extent3D, vk::Format>;
	STRATUM_API static PixelData LoadPixels(const fs::path& filename, bool srgb = true);

	// If mipLevels = 0, will auto-determine according to extent 
	STRATUM_API Texture(Device& device, const string& name,
		const vk::Extent3D& extent, vk::Format format, uint32_t arrayLayers = 1, uint32_t mipLevels = 0,
		vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1, 
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, 
		vk::ImageCreateFlags createFlags = {}, vk::MemoryPropertyFlags memoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageTiling tiling = vk::ImageTiling::eOptimal);
	
	inline Texture(Device& device, const string& name, const vk::Extent3D& extent, const vk::AttachmentDescription& description, vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, 
		vk::ImageCreateFlags createFlags = {}, vk::MemoryPropertyFlags memoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageTiling tiling = vk::ImageTiling::eOptimal)
		: Texture(device, name, extent, description.format, 1, 1, description.samples, usage, createFlags, memoryProperties, tiling) {}
	inline ~Texture() {
		for (auto&[k,v] : mViews) mDevice->destroyImageView(v);
		mDevice->destroyImage(mImage);
	}

	inline const vk::Image& operator*() const { return mImage; }
	inline const vk::Image* operator->() const { return &mImage; }
	inline operator bool() const { return mImage; }

	inline vk::Extent3D Extent() const { return mExtent; }
	inline vk::Format Format() const { return mFormat; }
	inline vk::ImageUsageFlags Usage() const { return mUsage; }
	inline vk::SampleCountFlags SampleCount() const { return mSampleCount; }
	inline uint32_t MipLevels() const { return mMipLevels; }
	inline uint32_t ArrayLayers() const { return mArrayLayers; }
	inline vk::ImageAspectFlags AspectFlags() const { return mAspectFlags; }
	inline vk::MemoryPropertyFlags MemoryProperties() const { return mMemoryProperties; }
	inline const Device::Memory::Block& Memory() const { return *mMemoryBlock; }
	inline vk::ImageCreateFlags CreateFlags() const { return mCreateFlags; }

	// Texture must support vk::ImageAspect::eColor and vk::ImageLayout::eTransferDstOptimal
	STRATUM_API void GenerateMipMaps(CommandBuffer& commandBuffer);
	
	inline void TransitionBarrier(CommandBuffer& commandBuffer, vk::ImageLayout newLayout) {
		TransitionBarrier(commandBuffer, mTrackedStageFlags, GuessStage(newLayout), mTrackedLayout, newLayout);
	}
	inline void TransitionBarrier(CommandBuffer& commandBuffer, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
		TransitionBarrier(commandBuffer, GuessStage(oldLayout), GuessStage(newLayout), oldLayout, newLayout);
	}
	inline void TransitionBarrier(CommandBuffer& commandBuffer, vk::PipelineStageFlags dstStage, vk::ImageLayout newLayout) {
		TransitionBarrier(commandBuffer, mTrackedStageFlags, dstStage, mTrackedLayout, newLayout);
	}
	STRATUM_API void TransitionBarrier(CommandBuffer& commandBuffer, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);

private:
	friend class CommandBuffer;
	friend class TextureView;
	
	vk::Image mImage;
	shared_ptr<Device::Memory::Block> mMemoryBlock;
	
	vk::Extent3D mExtent;
	uint32_t mArrayLayers = 0;
	vk::Format mFormat;
	uint32_t mMipLevels = 0;
	vk::SampleCountFlagBits mSampleCount;
	vk::ImageUsageFlags mUsage;
	vk::MemoryPropertyFlags mMemoryProperties;
	vk::ImageCreateFlags mCreateFlags;
	vk::ImageAspectFlags mAspectFlags;
	vk::ImageTiling mTiling = vk::ImageTiling::eOptimal;
	
	unordered_map<size_t, vk::ImageView> mViews;
	
	vk::ImageLayout mTrackedLayout;
	vk::PipelineStageFlags mTrackedStageFlags;
	vk::AccessFlags mTrackedAccessFlags;

	STRATUM_API void Create();
};

class TextureView {
	vk::ImageView mView;
	shared_ptr<Texture> mTexture;
	vk::ImageAspectFlags mAspectMask;
	uint32_t mBaseMip;
	uint32_t mMipCount;
	uint32_t mBaseLayer;
	uint32_t mLayerCount;

public:
	TextureView() = default;
	TextureView(const TextureView&) = default;
	TextureView(TextureView&&) = default;
	inline TextureView(shared_ptr<Texture> texture, uint32_t baseMip=0, uint32_t mipCount=0, uint32_t baseLayer=0, uint32_t layerCount=0, vk::ImageAspectFlags aspectMask=(vk::ImageAspectFlags)0, vk::ComponentMapping components={})
		: mTexture(texture), mBaseMip(baseMip), mMipCount(mipCount ? mipCount : mMipCount = texture->MipLevels()), mBaseLayer(baseLayer), mLayerCount(layerCount ? layerCount : texture->ArrayLayers()), mAspectMask(aspectMask) {
		if (mAspectMask == (vk::ImageAspectFlags)0) mAspectMask = texture->AspectFlags();

		size_t key = hash_combine(mBaseMip, mMipCount, mBaseLayer, mLayerCount, mAspectMask, components);
		if (texture->mViews.count(key))
			mView = texture->mViews.at(key);
		else {
			vk::ImageViewCreateInfo info = {};
			if (mTexture->CreateFlags() & vk::ImageCreateFlagBits::eCubeCompatible)
				info.viewType = vk::ImageViewType::eCube;
			else if (mTexture->Extent().depth > 1)
				info.viewType = vk::ImageViewType::e3D;
			else if (mTexture->Extent().height > 1)
				info.viewType = vk::ImageViewType::e2D;
			else
				info.viewType = vk::ImageViewType::e1D;
			
			info.image = **mTexture;
			info.format = mTexture->Format();
			info.subresourceRange.aspectMask = mAspectMask;
			info.subresourceRange.baseArrayLayer = mBaseLayer;
			info.subresourceRange.layerCount = mLayerCount;
			info.subresourceRange.baseMipLevel = mBaseMip;
			info.subresourceRange.levelCount = mMipCount;
			info.components = components;
			mView = mTexture->mDevice->createImageView(info);
			mTexture->mDevice.SetObjectName(mView, mTexture->Name() + "/TextureView");

			texture->mViews.emplace(key, mView);
		}
	}

	TextureView& operator=(const TextureView&) = default;
	TextureView& operator=(TextureView&& v) = default;
	inline bool operator==(const TextureView& rhs) const = default;
	inline operator bool() const { return mTexture && mView; }

	inline const vk::ImageView& operator*() const { return mView; }
	inline const vk::ImageView* operator->() const { return &mView; }
	inline shared_ptr<Texture> get() const { return mTexture; }
	inline Texture& texture() const { return *mTexture; }
};

}