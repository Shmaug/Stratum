#pragma once

#include "Device.hpp"

namespace stm {

enum class TextureLoadFlagBits {
	eSrgb,
	eSigned
};
using TextureLoadFlags = vk::Flags<TextureLoadFlagBits>;

class Texture : public DeviceResource {
public:
	// Construct image or image array from (optional) pixel/metadata. If mipLevels = 0, will auto-determine according to extent 
	STRATUM_API Texture(Device& device, const string& name, const vk::Extent3D& extent, vk::Format format, const byte_blob& data = {},
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, uint32_t mipLevels = 1, vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1, vk::MemoryPropertyFlags memoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal);

	// Construct image from image file(s)
	STRATUM_API Texture(Device& device, const string& name, const vector<fs::path>& files, TextureLoadFlags loadFlags = TextureLoadFlagBits::eSrgb, vk::ImageCreateFlags createFlags = {});
	inline Texture(Device& device, const string& name, const fs::path& filename, TextureLoadFlags loadFlags = TextureLoadFlagBits::eSrgb, vk::ImageCreateFlags createFlags = {})
		: Texture(device, name, vector<fs::path> { filename }, loadFlags, createFlags) {}

	STRATUM_API ~Texture();

	inline vk::Image operator*() const { return mImage; }
	inline const vk::Image* operator->() const { return &mImage; }

	inline vk::Extent3D Extent() const { return mExtent; }
	inline vk::Format Format() const { return mFormat; }
	inline vk::ImageUsageFlags Usage() const { return mUsage; }
	inline vk::SampleCountFlags SampleCount() const { return mSampleCount; }
	inline uint32_t MipLevels() const { return mMipLevels; }
	inline uint32_t ArrayLayers() const { return mArrayLayers; }
	inline vk::ImageAspectFlags AspectFlags() const { return mAspectFlags; }
	inline vk::MemoryPropertyFlags MemoryProperties() const { return mMemoryProperties; }
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
	Device::Memory::Block mMemoryBlock;
	
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
	inline TextureView(shared_ptr<Texture> texture, uint32_t baseMip=0, uint32_t mipCount=0, uint32_t baseLayer=0, uint32_t layerCount=0, vk::ImageAspectFlags aspectMask=(vk::ImageAspectFlags)0, vk::ComponentMapping components={})
		: mTexture(texture), mBaseMip(baseMip), mMipCount(mipCount), mBaseLayer(baseLayer), mLayerCount(layerCount), mAspectMask(aspectMask) {
		if (mMipCount == 0) mMipCount = texture->MipLevels();
		if (mLayerCount == 0) mLayerCount = texture->ArrayLayers();
		if (mAspectMask == (vk::ImageAspectFlags)0) mAspectMask = texture->AspectFlags();

		size_t key = hash_combine(mBaseMip, mMipCount, mBaseLayer, mLayerCount, mAspectMask, components);
		if (texture->mViews.count(key) == 0)
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
			mTexture->mDevice.SetObjectName(mView, mTexture->mName+"/TextureView");

			texture->mViews.emplace(key, mView);
		}
	}
	TextureView& operator=(const TextureView&) = default;
	TextureView& operator=(TextureView&& v) = default;
	inline bool operator==(const TextureView& rhs) const = default;
		
	inline vk::ImageView operator*() const { return mView; }
	inline const vk::ImageView* operator->() const { return &mView; }
	inline shared_ptr<Texture> get() const { return mTexture; }
};

}