#pragma once

#include <Core/Instance.hpp>

class Device {
public:
	ENGINE_EXPORT ~Device();
	
	// Allocate device memory. Will attempt to sub-allocate from larger allocations. If the 'properties' contains VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, the memory will be mapped.
	ENGINE_EXPORT DeviceMemoryAllocation AllocateMemory(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags properties, const std::string& tag);
	ENGINE_EXPORT void FreeMemory(const DeviceMemoryAllocation& allocation);
	
	ENGINE_EXPORT Buffer* GetPooledBuffer(const std::string& name, VkDeviceSize size, VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ENGINE_EXPORT DescriptorSet* GetPooledDescriptorSet(const std::string& name, VkDescriptorSetLayout layout);
	ENGINE_EXPORT Texture* GetPooledTexture(const std::string& name, const VkExtent3D& extent, VkFormat format, uint32_t mipLevels = 1, VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT, VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ENGINE_EXPORT void ReturnToPool(Buffer* buffer);
	ENGINE_EXPORT void ReturnToPool(DescriptorSet* descriptorSet);
	ENGINE_EXPORT void ReturnToPool(Texture* texture);
	
	ENGINE_EXPORT void PurgePooledResources(uint32_t maxAge);

	ENGINE_EXPORT CommandBuffer* GetCommandBuffer(const std::string& name = "Command Buffer");
	ENGINE_EXPORT void Execute(CommandBuffer* commandBuffer);
	// Finish all work being done on this device
	ENGINE_EXPORT void Flush();

	ENGINE_EXPORT void SetObjectName(void* object, const std::string& name, VkObjectType type) const;

	ENGINE_EXPORT VkSampleCountFlagBits GetMaxUsableSampleCount();

	inline VkPhysicalDevice PhysicalDevice() const { return mPhysicalDevice; }
	inline uint32_t PhysicalDeviceIndex() const { return mPhysicalDeviceIndex; }
	
	inline VkQueue GraphicsQueue() const { return mGraphicsQueue; };
	inline VkQueue PresentQueue() const { return mPresentQueue; };
	inline uint32_t GraphicsQueueIndex() const { return mGraphicsQueueIndex; };
	inline uint32_t PresentQueueIndex() const { return mPresentQueueIndex; };
	inline uint32_t GraphicsQueueFamilyIndex() const { return mGraphicsQueueFamilyIndex; };
	inline uint32_t PresentQueueFamilyIndex() const { return mPresentQueueFamilyIndex; };

	inline uint32_t CommandBufferCount() const { return mCommandBufferCount; };
	inline uint32_t DescriptorSetCount() const { return mDescriptorSetCount; };
	inline uint32_t MemoryAllocationCount() const { return mMemoryAllocationCount; };
	inline VkDeviceSize MemoryUsage() const { return mMemoryUsage; };
	inline const VkPhysicalDeviceMemoryProperties& MemoryProperties() const { return mMemoryProperties; }
	inline uint64_t FrameCount() const { return mFrameCount; }

	inline const VkPhysicalDeviceLimits& Limits() const { return mLimits; }
	inline VkPipelineCache PipelineCache() const { return mPipelineCache; }

	inline ::AssetManager* AssetManager() const { return mAssetManager; }
	inline ::Instance* Instance() const { return mInstance; }

	inline operator VkDevice() const { return mDevice; }

private:
	struct Allocation {
		void* mMapped;
		VkDeviceMemory mMemory;
		VkDeviceSize mSize;
		// <offset, size>
		std::list<std::pair<VkDeviceSize, VkDeviceSize>> mAvailable;
		std::list<DeviceMemoryAllocation> mAllocations;

		ENGINE_EXPORT bool SubAllocate(const VkMemoryRequirements& requirements, DeviceMemoryAllocation& allocation, const std::string& tag);
		ENGINE_EXPORT void Deallocate(const DeviceMemoryAllocation& allocation);
	};
	
	template<typename T>
	struct PooledResource {
		uint64_t mLastFrameUsed;
		T* mResource;
	};

	friend class DescriptorSet;
	friend class CommandBuffer;
	friend class ::Instance;
	friend class Window;
	ENGINE_EXPORT Device(::Instance* instance, VkPhysicalDevice physicalDevice, uint32_t physicalDeviceIndex, uint32_t graphicsQueue, uint32_t presentQueue, const std::set<std::string>& deviceExtensions, std::vector<const char*> validationLayers);
	
	ENGINE_EXPORT void PrintAllocations();

	uint64_t mFrameCount;
	uint32_t mCommandBufferCount;
	uint32_t mDescriptorSetCount;
	uint32_t mMemoryAllocationCount;
	VkDeviceSize mMemoryUsage;

	::Instance* mInstance;
	::AssetManager* mAssetManager;

	VkDevice mDevice;
	VkPhysicalDevice mPhysicalDevice;
	VkPipelineCache mPipelineCache;
	uint32_t mPhysicalDeviceIndex;
	VkPhysicalDeviceMemoryProperties mMemoryProperties;
	VkPhysicalDeviceLimits mLimits;
	uint32_t mMaxMSAASamples;

	VkQueue mGraphicsQueue;
	uint32_t mGraphicsQueueIndex;
	uint32_t mGraphicsQueueFamilyIndex;
	
	VkQueue mPresentQueue;
	uint32_t mPresentQueueIndex;
	uint32_t mPresentQueueFamilyIndex;

	mutable std::mutex mCommandPoolMutex;
	std::unordered_map<std::thread::id, VkCommandPool> mCommandPools;
	mutable std::mutex mCommandBufferPoolMutex;
	std::unordered_map<VkCommandPool, std::list<PooledResource<CommandBuffer>>> mCommandBufferPool;

	mutable std::mutex mDescriptorPoolMutex;
	VkDescriptorPool mDescriptorPool;

	mutable std::mutex mBufferPoolMutex;
	std::list<PooledResource<Buffer>> mBufferPool;
	mutable std::mutex mDescriptorSetPoolMutex;
	std::unordered_map<VkDescriptorSetLayout, std::list<PooledResource<DescriptorSet>>> mDescriptorSetPool;
	mutable std::mutex mTexturePoolMutex;
	std::unordered_map<size_t, std::list<PooledResource<Texture>>> mTexturePool;

	mutable std::mutex mMemoryMutex;
	std::unordered_map<uint32_t, std::vector<Allocation>> mMemoryAllocations;

	#ifdef ENABLE_DEBUG_LAYERS
	PFN_vkSetDebugUtilsObjectNameEXT SetDebugUtilsObjectNameEXT;
	PFN_vkCmdBeginDebugUtilsLabelEXT CmdBeginDebugUtilsLabelEXT;
	PFN_vkCmdEndDebugUtilsLabelEXT CmdEndDebugUtilsLabelEXT;
	#endif
};