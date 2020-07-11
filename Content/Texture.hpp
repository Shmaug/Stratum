#pragma once

#include <Content/Asset.hpp>
#include <Core/Device.hpp>
#include <Util/Util.hpp>

class Sampler {
public:
	const std::string mName;

	ENGINE_EXPORT Sampler(const std::string& name, Device* device, const VkSamplerCreateInfo& samplerInfo);
	ENGINE_EXPORT Sampler(const std::string& name, Device* device, float maxLod, VkFilter filter = VK_FILTER_LINEAR, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT, float maxAnisotropy = 16);
	ENGINE_EXPORT ~Sampler();

	inline const ::VkSampler& VkSampler() const { return mSampler; }
	inline operator ::VkSampler() const { return mSampler; }

private:
	Device* mDevice;
	::VkSampler mSampler;
};

class Texture : public Asset {
public:
	const std::string mName;

	// Texture will be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL or (VK_IMAGE_LAYOUT_GENERAL if VK_IMAGE_USAGE_STORAGE_BIT is set)
	ENGINE_EXPORT Texture(const std::string& name, Device* device, const void* data, VkDeviceSize dataSize, 
		const VkExtent3D& extent, VkFormat format, uint32_t mipLevels, VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT,
		VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// Texture will be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL or (VK_IMAGE_LAYOUT_GENERAL if VK_IMAGE_USAGE_STORAGE_BIT is set)
	ENGINE_EXPORT Texture(const std::string& name, Device* device, 
		const VkExtent3D& extent, VkFormat format, uint32_t mipLevels, VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT,
		VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	
	// Texture will be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL or (VK_IMAGE_LAYOUT_GENERAL if VK_IMAGE_USAGE_STORAGE_BIT is set)
	ENGINE_EXPORT Texture(const std::string& name, Device* device, const void* data, VkDeviceSize dataSize,
		const VkExtent2D& extent, VkFormat format, uint32_t mipLevels, VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT,
		VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	
	// Texture will be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL or (VK_IMAGE_LAYOUT_GENERAL if VK_IMAGE_USAGE_STORAGE_BIT is set)
	ENGINE_EXPORT Texture(const std::string& name, Device* device, 
		const VkExtent2D& extent, VkFormat format, uint32_t mipLevels, VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT,
		VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	
	ENGINE_EXPORT ~Texture() override;

	inline operator VkImage() const { return mImage; }

	inline VkExtent3D Extent() const { return mExtent; }
	inline uint32_t ArrayLayers() const { return mArrayLayers; }
	inline VkFormat Format() const { return mFormat; }
	inline uint32_t MipLevels() const { return mMipLevels; }
	inline VkSampleCountFlagBits SampleCount() const { return mSampleCount; }
	inline VkImageAspectFlags AspectFlags() const { return mAspectFlags; }
	inline VkImageUsageFlags Usage() const { return mUsage; }
	inline VkMemoryPropertyFlags MemoryProperties() const { return mMemoryProperties; }
	inline VkImageView View() const { return mView; }
	inline VkImageView View(uint32_t mipLevel);

	inline VkImageLayout LastKnownLayout() const { return mLastKnownLayout; }
	
	// Texture must have been created with the appropriate mipmap levels
	// Texture must support VK_IMAGE_ASPECT_COLOR
	ENGINE_EXPORT void GenerateMipMaps(CommandBuffer* commandBuffer);

private:
	friend class CommandBuffer;
	friend class AssetManager;
	ENGINE_EXPORT Texture(const std::string& name, Device* device, const std::string& filename, bool srgb = true);
	ENGINE_EXPORT Texture(const std::string& name, Device* device, const std::string& posx, const std::string& negx, const std::string& posy, const std::string& negy, const std::string& posz, const std::string& negz, bool srgb = true);

	Device* mDevice;
	DeviceMemoryAllocation mMemory;
	
	VkExtent3D mExtent;
	uint32_t mArrayLayers;
	VkFormat mFormat;
	uint32_t mMipLevels;
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
	VkImageView mView;
	std::unordered_map<uint32_t, VkImageView> mMipViews;

	ENGINE_EXPORT void Create();
};