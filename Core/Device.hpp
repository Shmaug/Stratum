#pragma once

#include <Core/Instance.hpp>


class Semaphore {
public:
	inline Semaphore::Semaphore(Device* device) : mDevice(device) { mSemaphore = ((vk::Device)*mDevice).createSemaphore({}); }
	inline Semaphore::~Semaphore() { mDevice->Destroy(mSemaphore); }
	inline operator vk::Semaphore() const { return mSemaphore; }
private:
	vk::Semaphore mSemaphore;
	Device* mDevice;
};



class Device {
private:
	vk::Device mDevice;
	
public:
	STRATUM_API ~Device();
	
	// Allocate device memory. Will attempt to sub-allocate from larger allocations. If the 'properties' contains vk::MemoryPropertyFlagBits::eHostVisible, the memory will be mapped.
	STRATUM_API DeviceMemoryAllocation AllocateMemory(const vk::MemoryRequirements& requirements, vk::MemoryPropertyFlags properties, const std::string& tag);
	STRATUM_API void FreeMemory(const DeviceMemoryAllocation& allocation);

	template<typename T>
	inline void Destroy(const T& obj) { mDevice.destroy(obj); }
	
	STRATUM_API Buffer* GetPooledBuffer(const std::string& name, vk::DeviceSize size, vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);
	STRATUM_API DescriptorSet* GetPooledDescriptorSet(const std::string& name, vk::DescriptorSetLayout layout);
	STRATUM_API Texture* GetPooledTexture(const std::string& name, const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels = 1, vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1, vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);
	// Place a resource back in the resource pool
	STRATUM_API void PoolResource(Buffer* resource);
	// Place a resource back in the resource pool
	STRATUM_API void PoolResource(DescriptorSet* resource);
	// Place a resource back in the resource pool
	STRATUM_API void PoolResource(Texture* resource);

	STRATUM_API void PurgePooledResources(uint32_t maxAge);

	STRATUM_API CommandBuffer* GetCommandBuffer(const std::string& name = "Command Buffer", vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
	STRATUM_API void Execute(CommandBuffer* commandBuffer);
	// Finish all work being done on this device
	STRATUM_API void Flush();

	template<typename T>
	inline void SetObjectName(const T& object, const std::string& name) const {
		#ifdef ENABLE_DEBUG_LAYERS
	
		auto f = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr((vk::Instance)*mInstance, "vkSetDebugUtilsObjectNameEXT");
		
		vk::DebugUtilsObjectNameInfoEXT info = {};
		info.objectHandle = *reinterpret_cast<const uint64_t*>(&object);
		info.objectType = object.objectType;
		info.pObjectName = name.c_str();
		f(mDevice, reinterpret_cast<VkDebugUtilsObjectNameInfoEXT*>(&info));
		#endif
	}

	STRATUM_API vk::SampleCountFlagBits GetMaxUsableSampleCount();

	inline vk::PhysicalDevice PhysicalDevice() const { return mPhysicalDevice; }
	inline uint32_t PhysicalDeviceIndex() const { return mPhysicalDeviceIndex; }
	
	inline vk::Queue GraphicsQueue() const { return mGraphicsQueue; };
	inline vk::Queue PresentQueue() const { return mPresentQueue; };
	inline uint32_t GraphicsQueueIndex() const { return mGraphicsQueueIndex; };
	inline uint32_t PresentQueueIndex() const { return mPresentQueueIndex; };
	inline uint32_t GraphicsQueueFamilyIndex() const { return mGraphicsQueueFamilyIndex; };
	inline uint32_t PresentQueueFamilyIndex() const { return mPresentQueueFamilyIndex; };

	inline uint32_t CommandBufferCount() const { return mCommandBufferCount; };
	inline uint32_t DescriptorSetCount() const { return mDescriptorSetCount; };
	inline uint32_t MemoryAllocationCount() const { return mMemoryAllocationCount; };
	inline vk::DeviceSize MemoryUsage() const { return mMemoryUsage; };
	inline const vk::PhysicalDeviceMemoryProperties& MemoryProperties() const { return mMemoryProperties; }
	inline uint64_t FrameCount() const { return mFrameCount; }

	inline const vk::PhysicalDeviceLimits& Limits() const { return mLimits; }
	inline vk::PipelineCache PipelineCache() const { return mPipelineCache; }

	inline ::AssetManager* AssetManager() const { return mAssetManager; }
	inline ::Instance* Instance() const { return mInstance; }

	inline vk::DescriptorSetLayout PerCameraSetLayout() const { return mCameraSetLayout; }
	inline vk::DescriptorSetLayout PerObjectSetLayout() const { return mObjectSetLayout; }

	inline operator vk::Device() const { return mDevice; }

private:
	struct Allocation {
		void* mMapped;
		vk::DeviceMemory mMemory;
		vk::DeviceSize mSize;
		// <offset, size>
		std::list<std::pair<vk::DeviceSize, vk::DeviceSize>> mAvailable;
		std::list<DeviceMemoryAllocation> mAllocations;

		STRATUM_API bool SubAllocate(const vk::MemoryRequirements& requirements, DeviceMemoryAllocation& allocation, const std::string& tag);
		STRATUM_API void Deallocate(const DeviceMemoryAllocation& allocation);
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
	STRATUM_API Device(::Instance* instance, vk::PhysicalDevice physicalDevice, uint32_t physicalDeviceIndex, uint32_t graphicsQueue, uint32_t presentQueue, const std::set<std::string>& deviceExtensions, std::vector<const char*> validationLayers);
	STRATUM_API void PrintAllocations();

	vk::PhysicalDevice mPhysicalDevice;
	vk::PipelineCache mPipelineCache;
	::Instance* mInstance;
	::AssetManager* mAssetManager;
	
	uint32_t mPhysicalDeviceIndex;
	vk::PhysicalDeviceMemoryProperties mMemoryProperties;
	vk::PhysicalDeviceLimits mLimits;
	vk::SampleCountFlagBits mMaxMSAASamples;

	vk::Queue mGraphicsQueue;
	uint32_t mGraphicsQueueIndex;
	uint32_t mGraphicsQueueFamilyIndex;
	
	vk::Queue mPresentQueue;
	uint32_t mPresentQueueIndex;
	uint32_t mPresentQueueFamilyIndex;

	uint64_t mFrameCount;
	uint32_t mCommandBufferCount;
	uint32_t mDescriptorSetCount;
	uint32_t mMemoryAllocationCount;
	vk::DeviceSize mMemoryUsage;

	mutable std::mutex mCommandPoolMutex;
	std::unordered_map<std::thread::id, vk::CommandPool> mCommandPools;
	mutable std::mutex mCommandBufferPoolMutex;
	std::unordered_map<vk::CommandPool, std::list<PooledResource<CommandBuffer>>> mCommandBufferPool;

	mutable std::mutex mDescriptorPoolMutex;
	vk::DescriptorPool mDescriptorPool;

	mutable std::mutex mBufferPoolMutex;
	std::list<PooledResource<Buffer>> mBufferPool;
	mutable std::mutex mDescriptorSetPoolMutex;
	std::unordered_map<vk::DescriptorSetLayout, std::list<PooledResource<DescriptorSet>>> mDescriptorSetPool;
	mutable std::mutex mTexturePoolMutex;
	std::unordered_map<size_t, std::list<PooledResource<Texture>>> mTexturePool;

	mutable std::mutex mMemoryMutex;
	std::unordered_map<uint32_t, std::vector<Allocation>> mMemoryAllocations;

	vk::DescriptorSetLayout mCameraSetLayout;
	vk::DescriptorSetLayout mObjectSetLayout;
};