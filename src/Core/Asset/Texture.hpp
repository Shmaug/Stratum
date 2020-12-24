#pragma once

#include "Asset.hpp"

namespace stm {

enum class TextureLoadFlags {
	eSrgb,
	eSigned
};

class Texture : public Asset {
public:
	// Construct image or image array from (optional) pixel data and metadata. If mipLevels = 0, will auto-determine according to extent 
	STRATUM_API Texture(const string& name, stm::Device& device, const vk::Extent3D& extent, vk::Format format, const byte_blob& data,
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, uint32_t mipLevels = 1, vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1, vk::MemoryPropertyFlags memoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal);

	// Construct image from image file(s)
	STRATUM_API Texture(stm::Device& device, const vector<fs::path>& files, TextureLoadFlags loadFlags = TextureLoadFlags::eSrgb, vk::ImageCreateFlags createFlags = vk::ImageCreateFlags());
	inline Texture(stm::Device& device, const fs::path& filename, TextureLoadFlags loadFlags = TextureLoadFlags::eSrgb, vk::ImageCreateFlags createFlags = vk::ImageCreateFlags())
		: Texture(device, vector<fs::path> { filename }, loadFlags, createFlags) {}

	STRATUM_API ~Texture();

	inline vk::Image operator*() const { return mImage; }
	inline const vk::Image* operator->() const { return &mImage; }

	inline stm::Device& Device() const { return mDevice; }

	inline vk::Extent3D Extent() const { return mExtent; }
	inline vk::Format Format() const { return mFormat; }
	inline vk::ImageUsageFlags Usage() const { return mUsage; }
	inline vk::SampleCountFlags SampleCount() const { return mSampleCount; }
	inline uint32_t MipLevels() const { return mMipLevels; }
	inline uint32_t ArrayLayers() const { return mArrayLayers; }
	inline vk::ImageAspectFlags AspectFlags() const { return mAspectFlags; }
	inline vk::MemoryPropertyFlags MemoryProperties() const { return mMemoryProperties; }
	STRATUM_API vk::ImageView View(uint32_t mipLevel = 0, uint32_t levelCount = 0, uint32_t arrayLayer = 0, uint32_t layerCount = 0);

	// Texture must support vk::ImageAspect::eColor and vk::ImageLayout::eTransferDstOptimal
	STRATUM_API void GenerateMipMaps(CommandBuffer& commandBuffer);

private:
	friend class CommandBuffer;
	friend class Device;
	friend class RenderPass;

	vk::Image mImage;
	Device::Memory::Block mMemoryBlock;
	unordered_map<uint64_t, vk::ImageView> mViews;
	stm::Device& mDevice;
	string mName;
	
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
	
	vk::ImageLayout mTrackedLayout;
	vk::PipelineStageFlags mTrackedStageFlags;
	vk::AccessFlags mTrackedAccessFlags;

	STRATUM_API void Create();
};

}