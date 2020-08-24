#pragma once

#include <Core/Device.hpp>

class Sampler {
public:
	const std::string mName;

	STRATUM_API Sampler(const std::string& name, Device* device, const vk::SamplerCreateInfo& samplerInfo);
	STRATUM_API Sampler(const std::string& name, Device* device, float maxLod, vk::Filter filter = vk::Filter::eLinear, vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat, float maxAnisotropy = 16);
	STRATUM_API ~Sampler();

	inline operator vk::Sampler() const { return mSampler; }

private:
	Device* mDevice;
	vk::Sampler mSampler;
};

class Texture {
public:
	const std::string mName;

	// Construct from file
	STRATUM_API Texture(const std::string& name, ::Device* device, const std::string& filename, TextureLoadFlags flags = TextureLoadFlags::eSrgb);

	// Construct cubemap from file
	STRATUM_API Texture(const std::string& name, ::Device* device, const std::string& posx, const std::string& negx, const std::string& posy, const std::string& negy, const std::string& posz, const std::string& negz, TextureLoadFlags flags = TextureLoadFlags::eSrgb);

	// Construct from pixel data and metadata. Will generate mip maps if mipLevels = 0
	STRATUM_API Texture(const std::string& name, Device* device, const void* data, vk::DeviceSize dataSize, 
		const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels, vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1,
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);

	// Construct from metadata, leave empty. Will create approprioate number of (empty) subresources if mipLevels = 0
	STRATUM_API Texture(const std::string& name, Device* device, 
		const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels, vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1,
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);
	
	// Construct from pixel data and metadata. Will generate mip maps if mipLevels = 0
	STRATUM_API Texture(const std::string& name, Device* device, const void* data, vk::DeviceSize dataSize,
		const vk::Extent2D& extent, vk::Format format, uint32_t mipLevels = 0, vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1,
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);
	
	// Construct from metadata, leave empty. Will create approprioate number of (empty) subresources if mipLevels = 0
	STRATUM_API Texture(const std::string& name, Device* device, 
		const vk::Extent2D& extent, vk::Format format, uint32_t mipLevels, vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1,
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);

	STRATUM_API ~Texture();

	inline ::Device* Device() const { return mDevice; };
	inline operator vk::Image() const { return mImage; }

	inline vk::Extent3D Extent() const { return mExtent; }
	inline uint32_t ArrayLayers() const { return mArrayLayers; }
	inline vk::Format Format() const { return mFormat; }
	inline uint32_t MipLevels() const { return mMipLevels; }
	inline vk::SampleCountFlags SampleCount() const { return mSampleCount; }
	inline vk::ImageAspectFlags AspectFlags() const { return mAspectFlags; }
	inline vk::ImageUsageFlags Usage() const { return mUsage; }
	inline vk::MemoryPropertyFlags MemoryProperties() const { return mMemoryProperties; }
	STRATUM_API vk::ImageView View(uint32_t mipLevel = 0, uint32_t levelCount = 0, uint32_t arrayLayer = 0, uint32_t layerCount = 0);

	inline vk::ImageLayout LastKnownLayout() const { return mLastKnownLayout; }
	
	// Texture must have been created with the appropriate mipmap levels
	// Texture must support vk::ImageAspect::eColor
	STRATUM_API void GenerateMipMaps(CommandBuffer* commandBuffer);

private:
	friend class CommandBuffer;
	friend class AssetManager;
	friend class RenderPass;
	::Device* mDevice = nullptr;
	DeviceMemoryAllocation mMemory;
	
	vk::Extent3D mExtent = 0;
	uint32_t mArrayLayers = 0;
	vk::Format mFormat;
	uint32_t mMipLevels = 0;
	vk::ImageCreateFlags mCreateFlags;
	vk::ImageAspectFlags mAspectFlags;
	vk::SampleCountFlagBits mSampleCount;
	vk::ImageUsageFlags mUsage;
	vk::MemoryPropertyFlags mMemoryProperties;
	vk::ImageTiling mTiling;
	
	vk::ImageLayout mLastKnownLayout;
	vk::PipelineStageFlags mLastKnownStageFlags;
	vk::AccessFlags mLastKnownAccessFlags;

	vk::MemoryAllocateInfo mAllocationInfo;

	vk::Image mImage;
	std::unordered_map<uint64_t, vk::ImageView> mViews;

	STRATUM_API void Create();
};