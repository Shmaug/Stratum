#pragma once

#include "Instance.hpp"

namespace stm {

template<typename T, typename Tk>
class ResourcePool {
public:
	struct PooledResource {
	public:
		std::shared_ptr<T> mResource;
		uint64_t mLastFrameUsed;
	};

	mutable std::mutex mMutex;
	std::unordered_map<Tk, std::list<PooledResource>> mResources;

	inline void EraseOld(uint64_t currentFrame, uint64_t maxAge) {
		std::lock_guard lock(mMutex);
		for (auto& [key,pool] : mResources)
			for (auto& it = pool.begin(); it != pool.end();)
				if (it->mLastFrameUsed + maxAge > currentFrame) it++;
				else it = pool.erase(it);
	}
};

class Device {
private:
	vk::Device mDevice;
	const vk::DeviceSize mMemoryBlockSize = 256_mB;
	
	struct QueueFamily {
		// TODO: use more queues for parallelization (if necessary?)
		vk::Queue mQueue;
		std::string mName;
		uint32_t mFamilyIndex = 0;
		vk::QueueFamilyProperties mProperties;
		// CommandBuffers may be in-flight or idle
		std::unordered_map<std::thread::id, std::pair<vk::CommandPool, std::list<CommandBuffer*>>> mCommandBuffers;
	};
	
public:
	class Memory {
	private:
		friend class Device;
		vk::DeviceMemory mDeviceMemory;
		std::map<vk::DeviceSize /*begin*/, vk::DeviceSize/*end*/> mBlocks;

	public:
		class Block {
		public:
			Memory* mMemory = nullptr;
			vk::DeviceSize mOffset = 0;
			Block() = default;
			inline Block(Memory* memory, vk::DeviceSize offset) : mMemory(memory), mOffset(offset) {}
			inline void* Mapped() const { return (uint8_t*)mMemory->mMapped + mOffset; }
			inline operator bool() const { return mMemory; }
		};

		Device* mDevice = nullptr;
		uint32_t mMemoryTypeIndex = 0;
		vk::DeviceSize mSize = 0;
		void* mMapped = nullptr;

		inline Memory(Device* device, uint32_t memoryTypeIndex, vk::DeviceSize size) : mDevice(device), mMemoryTypeIndex(memoryTypeIndex), mSize(size) {
			mDeviceMemory = (*mDevice)->allocateMemory(vk::MemoryAllocateInfo(size, memoryTypeIndex));
			if (mDevice->MemoryProperties().memoryTypes[mMemoryTypeIndex].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)
				mMapped = (*mDevice)->mapMemory(mDeviceMemory, 0, mSize);
		}
		inline ~Memory() {
			if (mDeviceMemory) {
				if (mBlocks.size()) fprintf_color(ConsoleColorBits::eYellow, stderr, "freeing device memory with unfreed blocks");
				if (mMapped) (*mDevice)->unmapMemory(mDeviceMemory);
				(*mDevice)->freeMemory(mDeviceMemory);
			}
		}

		inline vk::DeviceMemory operator*() const { return mDeviceMemory; }
		inline const vk::DeviceMemory* operator->() const { return &mDeviceMemory; }

		STRATUM_API Block GetBlock(const vk::MemoryRequirements& requirements);
		STRATUM_API void ReturnBlock(const Memory::Block& block);

		inline bool empty() const { return mBlocks.empty(); }
	};

	Instance* const mInstance;
	const vk::PhysicalDevice mPhysicalDevice;

	Device() = delete;
	Device(const Device&) = delete;
	Device(Device&&) = delete;
	Device& operator=(const Device&) = delete;
	Device& operator=(Device&&) = delete;
	STRATUM_API ~Device();
	inline vk::Device operator*() const { return mDevice; }
	inline const vk::Device* operator->() const { return &mDevice; }
	
	// Allocate device memory. Will attempt to sub-allocate from larger allocations. Host-visible memory is automatically mapped.
	STRATUM_API Memory::Block AllocateMemory(const vk::MemoryRequirements& requirements, vk::MemoryPropertyFlags properties, const std::string& tag = "");
	STRATUM_API void FreeMemory(const Memory::Block& allocation);

	template<typename T>
	inline void SetObjectName(const T& object, const std::string& name) {
		if (mSetDebugUtilsObjectNameEXT) {
			vk::DebugUtilsObjectNameInfoEXT info = {};
			info.objectHandle = *reinterpret_cast<const uint64_t*>(&object);
			info.objectType = object.objectType;
			info.pObjectName = name.c_str();
			mSetDebugUtilsObjectNameEXT(mDevice, reinterpret_cast<VkDebugUtilsObjectNameInfoEXT*>(&info));
		}
	}
	
	template<typename T, typename... Targs>
	inline std::shared_ptr<T> FindLoadedAsset(const fs::path& filename) {
		uint64_t key = hash<string>()(filename.string());
		if (mLoadedAssets.count(key) == 0) return nullptr;
		return dynamic_pointer_cast<T>(mLoadedAssets.at(key));
	}
	template<typename T, typename... Targs>
	inline std::shared_ptr<T> LoadAsset(const fs::path& filename, Targs&&... args) {
		uint64_t key = hash<string>()(filename.string());
		std::lock_guard lock(mAssetMutex);
		if (mLoadedAssets.count(key) == 0)
			mLoadedAssets.emplace(key, shared_ptr<T>(new T(filename, this, args...)));
		return dynamic_pointer_cast<T>(mLoadedAssets.at(key));
	}
	inline void UnloadAssets() { std::lock_guard lock(mAssetMutex); mLoadedAssets.clear(); }

	STRATUM_API std::shared_ptr<Buffer> GetPooledBuffer(const std::string& name, vk::DeviceSize size, vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal);
	STRATUM_API std::shared_ptr<DescriptorSet> GetPooledDescriptorSet(const std::string& name, vk::DescriptorSetLayout layout);
	STRATUM_API std::shared_ptr<Texture> GetPooledTexture(const std::string& name, const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels = 1, vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1, vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);
	// Place a resource back in the resource pool
	STRATUM_API void PoolResource(std::shared_ptr<Buffer> resource);
	// Place a resource back in the resource pool
	STRATUM_API void PoolResource(std::shared_ptr<DescriptorSet> resource);
	// Place a resource back in the resource pool
	STRATUM_API void PoolResource(std::shared_ptr<Texture> resource);

	STRATUM_API void PurgeResourcePools(uint32_t maxAge);

	STRATUM_API CommandBuffer* GetCommandBuffer(const std::string& name, vk::QueueFlags queueFlags = vk::QueueFlagBits::eGraphics, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
	// The CommandBuffer will be managed by the device after being passed in
	STRATUM_API void Execute(CommandBuffer* commandBuffer, bool wait = false);
	// Finish all work being done on this device
	STRATUM_API void Flush();

	STRATUM_API vk::SampleCountFlagBits GetMaxUsableSampleCount();

	inline vk::PhysicalDevice PhysicalDevice() const { return mPhysicalDevice; }
	inline uint32_t PhysicalDeviceIndex() const { return mPhysicalDeviceIndex; }
	
	inline const vk::PhysicalDeviceMemoryProperties& MemoryProperties() const { return mMemoryProperties; }
	inline uint64_t FrameCount() const { return mFrameCount; }

	inline vk::DescriptorSetLayout DefaultDescriptorSetLayout(uint32_t index) const { return index < mDefaultDescriptorSetLayouts.size() ? mDefaultDescriptorSetLayouts[index] : nullptr; }
	inline uint32_t DefaultDescriptorSetCount() const { return (uint32_t)mDefaultDescriptorSetLayouts.size(); }

	inline const vk::PhysicalDeviceLimits& Limits() const { return mLimits; }
	inline vk::PipelineCache PipelineCache() const { return mPipelineCache; }

private:
	friend class DescriptorSet;
	friend class CommandBuffer;
	friend class Instance;
	friend class Window;
	STRATUM_API Device(Instance* instance, vk::PhysicalDevice physicalDevice, uint32_t physicalDeviceIndex, const std::set<std::string>& deviceExtensions, std::vector<const char*> validationLayers);
	STRATUM_API void PrintAllocations();

	vk::PipelineCache mPipelineCache;
	
	uint64_t mFrameCount = 0;
	PFN_vkSetDebugUtilsObjectNameEXT mSetDebugUtilsObjectNameEXT = nullptr;

	uint32_t mPhysicalDeviceIndex;
	vk::PhysicalDeviceMemoryProperties mMemoryProperties;
	vk::PhysicalDeviceLimits mLimits;
	vk::SampleCountFlagBits mMaxMSAASamples;

	std::vector<Sampler*> mDefaultImmutableSamplers;
	std::vector<vk::DescriptorSetLayout> mDefaultDescriptorSetLayouts;
	
	ResourcePool<DescriptorSet, vk::DescriptorSetLayout> mDescriptorSetPool;
	ResourcePool<Buffer, size_t> mBufferPool;
	ResourcePool<Texture, size_t> mTexturePool;
	
	mutable std::mutex mMemoryMutex;
	std::map<uint32_t /*memoryTypeIndex*/, std::list<Memory*>> mMemoryPool;

	mutable std::mutex mQueueMutex;
	std::unordered_map<uint32_t, QueueFamily> mQueueFamilies;

	mutable std::mutex mDescriptorPoolMutex;
	vk::DescriptorPool mDescriptorPool;

	mutable std::mutex mAssetMutex;
	std::unordered_map<uint64_t, std::shared_ptr<Asset>> mLoadedAssets;
};

class Fence {
private:
	vk::Fence mFence;
	Device* mDevice;
public:
	const std::string mName;
	inline Fence::Fence(const std::string& name, Device* device) : mName(name), mDevice(device) {
		mFence = (*mDevice)->createFence({});
		mDevice->SetObjectName(mFence, mName);
	}
	inline Fence::~Fence() { (*mDevice)->destroyFence(mFence); }
	inline vk::Fence& operator*() { return mFence; }
	inline const vk::Fence* operator->() { return &mFence; }
};
class Semaphore {
private:
	vk::Semaphore mSemaphore;
	Device* mDevice;
public:
	const std::string mName;
	inline Semaphore::Semaphore(const std::string& name, Device* device) : mName(name), mDevice(device) {
		mSemaphore = (*mDevice)->createSemaphore({});
		mDevice->SetObjectName(mSemaphore, mName);
	}
	inline Semaphore::~Semaphore() { (*mDevice)->destroySemaphore(mSemaphore); }
	inline vk::Semaphore& operator*() { return mSemaphore; }
	inline const vk::Semaphore* operator->() { return &mSemaphore; }
};
class Sampler {
private:
	vk::Sampler mSampler;
	Device* mDevice;
public:
	const std::string mName;
	inline Sampler::Sampler(const std::string& name, Device* device, const vk::SamplerCreateInfo& samplerInfo) : mName(name), mDevice(device) {
		mSampler = (*mDevice)->createSampler(samplerInfo);
		mDevice->SetObjectName(mSampler, mName);
	}
	inline Sampler::Sampler(const std::string& name, Device* device, float maxLod, vk::Filter filter, vk::SamplerAddressMode addressMode, float maxAnisotropy) : mName(name), mDevice(device) {
		vk::SamplerCreateInfo samplerInfo = {};
		samplerInfo.magFilter = filter;
		samplerInfo.minFilter = filter;
		samplerInfo.addressModeU = addressMode;
		samplerInfo.addressModeV = addressMode;
		samplerInfo.addressModeW = addressMode;
		samplerInfo.anisotropyEnable = maxAnisotropy > 0 ? VK_TRUE : VK_FALSE;
		samplerInfo.maxAnisotropy = maxAnisotropy;
		samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = vk::CompareOp::eAlways;
		samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
		samplerInfo.minLod = 0;
		samplerInfo.maxLod = (float)maxLod;
		samplerInfo.mipLodBias = 0;

		mSampler = (*mDevice)->createSampler(samplerInfo);
		mDevice->SetObjectName(mSampler, mName);
	}
	inline Sampler::~Sampler() {
		(*mDevice)->destroySampler(mSampler);
	}
	inline vk::Sampler operator*() const { return mSampler; }
	inline const vk::Sampler* operator->() const { return &mSampler; }
};

}