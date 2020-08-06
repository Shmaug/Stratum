#pragma once

#include <Core/Device.hpp>

class Sampler {
public:
	const std::string mName;

	STRATUM_API Sampler(const std::string& name, Device* device, const VkSamplerCreateInfo& samplerInfo);
	STRATUM_API Sampler(const std::string& name, Device* device, float maxLod, VkFilter filter = VK_FILTER_LINEAR, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT, float maxAnisotropy = 16);
	STRATUM_API ~Sampler();

	inline operator VkSampler() const { return mSampler; }

private:
	Device* mDevice;
	VkSampler mSampler;
};

class Texture {
public:
	const std::string mName;

	// Construt from pixel data and metadata. Will generate mip maps if mipLevels = 0
	STRATUM_API Texture(const std::string& name, Device* device, const void* data, VkDeviceSize dataSize, 
		const VkExtent3D& extent, VkFormat format, uint32_t mipLevels, VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT,
		VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// Construt from metadata, leave empty. Will create approprioate number of (empty) subresources if mipLevels = 0
	STRATUM_API Texture(const std::string& name, Device* device, 
		const VkExtent3D& extent, VkFormat format, uint32_t mipLevels, VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT,
		VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	
	// Construt from pixel data and metadata. Will generate mip maps if mipLevels = 0
	STRATUM_API Texture(const std::string& name, Device* device, const void* data, VkDeviceSize dataSize,
		const VkExtent2D& extent, VkFormat format, uint32_t mipLevels = 0, VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT,
		VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	
	// Construt from metadata, leave empty. Will create approprioate number of (empty) subresources if mipLevels = 0
	STRATUM_API Texture(const std::string& name, Device* device, 
		const VkExtent2D& extent, VkFormat format, uint32_t mipLevels, VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT,
		VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	
	STRATUM_API ~Texture();

	inline ::Device* Device() const { return mDevice; };
	inline operator VkImage() const { return mImage; }

	inline VkExtent3D Extent() const { return mExtent; }
	inline uint32_t ArrayLayers() const { return mArrayLayers; }
	inline VkFormat Format() const { return mFormat; }
	inline uint32_t MipLevels() const { return mMipLevels; }
	inline VkSampleCountFlagBits SampleCount() const { return mSampleCount; }
	inline VkImageAspectFlags AspectFlags() const { return mAspectFlags; }
	inline VkImageUsageFlags Usage() const { return mUsage; }
	inline VkMemoryPropertyFlags MemoryProperties() const { return mMemoryProperties; }
	STRATUM_API VkImageView View(uint32_t mipLevel = 0, uint32_t levelCount = 0, uint32_t arrayLayer = 0, uint32_t layerCount = 0);

	inline VkImageLayout LastKnownLayout() const { return mLastKnownLayout; }
	
	// Texture must have been created with the appropriate mipmap levels
	// Texture must support VK_IMAGE_ASPECT_COLOR
	STRATUM_API void GenerateMipMaps(CommandBuffer* commandBuffer);

private:
	friend class CommandBuffer;
	friend class AssetManager;
	friend class RenderPass;
	STRATUM_API Texture(const std::string& name, ::Device* device, const std::string& filename, bool srgb = true);
	STRATUM_API Texture(const std::string& name, ::Device* device, const std::string& posx, const std::string& negx, const std::string& posy, const std::string& negy, const std::string& posz, const std::string& negz, bool srgb = true);

	::Device* mDevice;
	DeviceMemoryAllocation mMemory;
	
	VkExtent3D mExtent;
	uint32_t mArrayLayers;
	VkFormat mFormat;
	uint32_t mMipLevels;
	VkImageCreateFlags mCreateFlags;
	VkSampleCountFlagBits mSampleCount;
	VkImageAspectFlags mAspectFlags;
	VkImageUsageFlags mUsage;
	VkMemoryPropertyFlags mMemoryProperties;
	VkImageTiling mTiling;
	
	VkImageLayout mLastKnownLayout;
	VkPipelineStageFlags mLastKnownStageFlags;
	VkAccessFlags mLastKnownAccessFlags;

	VkMemoryAllocateInfo mAllocationInfo;

	VkImage mImage;
	std::unordered_map<uint64_t, VkImageView> mViews;

	STRATUM_API void Create();
};