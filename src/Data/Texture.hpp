#pragma once

#include "Asset.hpp"

namespace stm {

class Texture : public Asset {
public:
	// Construct image or image array from (optional) pixel data and metadata. If mipLevels = 0, will auto-determine according to extent 
	STRATUM_API Texture(const std::string& name, stm::Device* device, const vk::Extent3D& extent, vk::Format format, const void* data = nullptr, vk::DeviceSize dataSize = 0,
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, uint32_t mipLevels = 1, vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);
	
	// Construct image or image array from file
	STRATUM_API Texture(const std::vector<fs::path>& files, stm::Device* device, const std::string& name, TextureLoadFlags loadFlags = TextureLoadFlags::eSrgb, vk::ImageCreateFlags createFlags = vk::ImageCreateFlags());
	inline Texture(const fs::path& file, stm::Device* device, const std::string& name, TextureLoadFlags loadFlags = TextureLoadFlags::eSrgb, vk::ImageCreateFlags createFlags = vk::ImageCreateFlags())
		: Texture(std::vector<fs::path> { file }, device, name, loadFlags, createFlags) {}

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
	STRATUM_API vk::ImageView View(uint32_t mipLevel = 0, uint32_t levelCount = 0, uint32_t arrayLayer = 0, uint32_t layerCount = 0);

	// Texture must support vk::ImageAspect::eColor and vk::ImageLayout::eTransferDstOptimal
	STRATUM_API void GenerateMipMaps(CommandBuffer& commandBuffer);

private:
	friend class CommandBuffer;
	friend class Device;
	friend class RenderPass;

	Device::Memory::Block mMemoryBlock;
	
	vk::Extent3D mExtent = 0;
	uint32_t mArrayLayers = 0;
	vk::Format mFormat;
	uint32_t mMipLevels = 0;
	vk::ImageCreateFlags mCreateFlags;
	vk::ImageAspectFlags mAspectFlags;
	vk::SampleCountFlagBits mSampleCount;
	vk::ImageUsageFlags mUsage;
	vk::MemoryPropertyFlags mMemoryProperties;
	vk::ImageTiling mTiling = vk::ImageTiling::eOptimal;
	
	vk::ImageLayout mLastKnownLayout;
	vk::PipelineStageFlags mLastKnownStageFlags;
	vk::AccessFlags mLastKnownAccessFlags;

	vk::MemoryAllocateInfo mAllocationInfo;

	vk::Image mImage;
	std::unordered_map<uint64_t, vk::ImageView> mViews;

	STRATUM_API void Create();
};

}