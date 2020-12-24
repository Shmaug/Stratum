#pragma once

#include "Instance.hpp"

namespace stm {

struct QueueFamily {
	uint32_t mFamilyIndex = 0;
	vector<vk::Queue> mQueues;
	string mName;
	vk::QueueFamilyProperties mProperties;
	bool mSurfaceSupport;
	// CommandBuffers may be in-flight or idle and are managed by stm::Device
	unordered_map<thread::id, pair<vk::CommandPool, list<CommandBuffer*>>> mCommandBuffers;
};

class Device {
public:
	const vk::DeviceSize mMemoryBlockSize = 256_mB;

	class Memory {
	private:
		friend class Device;
		vk::DeviceMemory mDeviceMemory;
		map<vk::DeviceSize /*begin*/, vk::DeviceSize/*end*/> mBlocks;

	public:
		struct Block {
			Memory* mMemory = nullptr;
			vk::DeviceSize mOffset = 0;
			Block() = default;
			Block(const Block& b) = default;
			inline Block(Memory& memory, vk::DeviceSize offset) : mMemory(&memory), mOffset(offset) {}
			inline byte* Mapped() const { return mMemory->mMapped + mOffset; }
		};

		Device& mDevice;
		uint32_t mMemoryTypeIndex = 0;
		vk::DeviceSize mSize = 0;
		byte* mMapped = nullptr;

		inline Memory(Device& device, uint32_t memoryTypeIndex, vk::DeviceSize size) : mDevice(device), mMemoryTypeIndex(memoryTypeIndex), mSize(size) {
			mDeviceMemory = mDevice->allocateMemory(vk::MemoryAllocateInfo(size, memoryTypeIndex));
			if (mDevice.MemoryProperties().memoryTypes[mMemoryTypeIndex].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)
				mMapped = (byte*)mDevice->mapMemory(mDeviceMemory, 0, mSize);
		}
		inline ~Memory() {
			if (mDeviceMemory) {
				if (mBlocks.size()) fprintf_color(ConsoleColorBits::eYellow, stderr, "freeing device memory with unfreed blocks");
				if (mMapped) mDevice->unmapMemory(mDeviceMemory);
				mDevice->freeMemory(mDeviceMemory);
			}
		}

		inline vk::DeviceMemory operator*() const { return mDeviceMemory; }
		inline const vk::DeviceMemory* operator->() const { return &mDeviceMemory; }

		STRATUM_API Block GetBlock(const vk::MemoryRequirements& requirements);
		STRATUM_API void ReturnBlock(const Block& block);

		inline bool empty() const { return mBlocks.empty(); }
	};

	Device() = delete;
	Device(const Device&) = delete;
	Device(Device&&) = delete;
	Device& operator=(const Device&) = delete;
	Device& operator=(Device&&) = delete;
	STRATUM_API ~Device();
	inline vk::Device operator*() const { return mDevice; }
	inline const vk::Device* operator->() const { return &mDevice; }
	
	inline vk::PhysicalDevice PhysicalDevice() const { return mPhysicalDevice; }
	inline uint32_t PhysicalDeviceIndex() const { return mPhysicalDeviceIndex; }
	inline const vk::PhysicalDeviceMemoryProperties& MemoryProperties() const { return mMemoryProperties; }
	inline const vk::PhysicalDeviceLimits& Limits() const { return mLimits; }
	inline vk::PipelineCache PipelineCache() const { return mPipelineCache; }
	inline const vector<uint32_t> QueueFamilies(uint32_t index) const { return mQueueFamilyIndices; }
	STRATUM_API QueueFamily* FindQueueFamily(vk::SurfaceKHR surface);
	STRATUM_API vk::SampleCountFlagBits GetMaxUsableSampleCount();

	inline Instance& Instance() const { return mInstance; }

	// Allocate device memory. Will attempt to sub-allocate from larger allocations. Host-visible memory is automatically mapped.
	STRATUM_API Memory::Block AllocateMemory(const vk::MemoryRequirements& requirements, vk::MemoryPropertyFlags properties, const string& tag = "");
	STRATUM_API void FreeMemory(const Memory::Block& allocation);

	template<typename T> inline void SetObjectName(const T& object, const string& name) {
		if (mSetDebugUtilsObjectNameEXT) {
			vk::DebugUtilsObjectNameInfoEXT info = {};
			info.objectHandle = *reinterpret_cast<const uint64_t*>(&object);
			info.objectType = object.objectType;
			info.pObjectName = name.c_str();
			mSetDebugUtilsObjectNameEXT(mDevice, reinterpret_cast<VkDebugUtilsObjectNameInfoEXT*>(&info));
		}
	}
	
	template<typename T> inline shared_ptr<T> LoadAsset(const fs::path& filename) {
		static_assert(is_base_of<Asset, T>::value);
		uint64_t key = basic_hash(filename.string());
		lock_guard lock(mAssetMutex);
		if (mLoadedAssets.count(key) == 0)
			mLoadedAssets.emplace(key, shared_ptr<T>(new T(*this, filename)));
		return dynamic_pointer_cast<T>(mLoadedAssets.at(key));
	}
	template<typename T> inline shared_ptr<T> FindLoadedAsset(const fs::path& filename) const {
		static_assert(is_base_of<Asset, T>::value);
		uint64_t key = basic_hash(filename.string());
		if (mLoadedAssets.count(key) == 0) return nullptr;
		return dynamic_pointer_cast<T>(mLoadedAssets.at(key));
	}
	inline void UnloadAssets() { lock_guard lock(mAssetMutex); mLoadedAssets.clear(); }

	STRATUM_API shared_ptr<Buffer> GetPooledBuffer(const string& name, vk::DeviceSize size, vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal);
	STRATUM_API shared_ptr<DescriptorSet> GetPooledDescriptorSet(const string& name, vk::DescriptorSetLayout layout);
	STRATUM_API shared_ptr<Texture> GetPooledTexture(const string& name, const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels = 1, vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1, vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);
	// Place a resource back in the resource pool
	STRATUM_API void PoolResource(shared_ptr<Buffer> resource);
	// Place a resource back in the resource pool
	STRATUM_API void PoolResource(shared_ptr<DescriptorSet> resource);
	// Place a resource back in the resource pool
	STRATUM_API void PoolResource(shared_ptr<Texture> resource);

	STRATUM_API void PurgeResourcePools(uint32_t maxAge);

	STRATUM_API CommandBuffer* GetCommandBuffer(const string& name, vk::QueueFlags queueFlags = vk::QueueFlagBits::eGraphics, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
	// The CommandBuffer will be managed by the device after being passed in
	STRATUM_API void Execute(CommandBuffer* commandBuffer, bool wait = false);
	// Finish all work being done on this device
	STRATUM_API void Flush();

	inline uint64_t FrameCount() const { return mFrameCount; }

private:
	template<typename T, typename Tk> class ResourcePool {
	public:
		struct PooledResource {
		public:
			shared_ptr<T> mResource;
			uint64_t mLastFrameUsed;
		};

		mutable mutex mMutex;
		unordered_map<Tk, list<PooledResource>> mResources;

		inline void EraseOld(uint64_t currentFrame, uint64_t maxAge) {
			lock_guard lock(mMutex);
			for (auto& [key,pool] : mResources)
				ranges:::remove_if(pool, [=](const auto& i){ i.mLastFrameUsed + maxAge > currentFrame });
		}
	};

	friend class Instance;
	friend class DescriptorSet;
	STRATUM_API Device(vk::PhysicalDevice physicalDevice, uint32_t physicalDeviceIndex, stm::Instance& instance, const set<string>& deviceExtensions, vector<const char*> validationLayers);
	STRATUM_API void PrintAllocations();

	vk::Device mDevice;
 	vk::PhysicalDevice mPhysicalDevice;
	vk::PipelineCache mPipelineCache;
	stm::Instance& mInstance;
	uint32_t mPhysicalDeviceIndex;
	size_t mFrameCount = 0;
	vk::PhysicalDeviceMemoryProperties mMemoryProperties;
	vk::PhysicalDeviceLimits mLimits;
	vk::SampleCountFlagBits mMaxMSAASamples;
	PFN_vkSetDebugUtilsObjectNameEXT mSetDebugUtilsObjectNameEXT = nullptr;
	vector<uint32_t> mQueueFamilyIndices;


	ResourcePool<DescriptorSet, vk::DescriptorSetLayout> mDescriptorSetPool;
	ResourcePool<Buffer, size_t> mBufferPool;
	ResourcePool<Texture, size_t> mTexturePool;
	
	
	mutable mutex mQueueMutex;
	unordered_map<uint32_t, stm::QueueFamily> mQueueFamilies;

	mutable mutex mMemoryMutex;
	unordered_map<uint32_t /*memoryTypeIndex*/, list<unique_ptr<Memory>>> mMemoryPool;

	mutable mutex mDescriptorPoolMutex;
	vk::DescriptorPool mDescriptorPool;

	mutable mutex mAssetMutex;
	unordered_map<size_t, shared_ptr<Asset>> mLoadedAssets;
};

class Fence {
private:
	vk::Fence mFence;
	string mName;
	Device& mDevice;
public:
	inline Fence(const string& name, Device& device) : mName(name), mDevice(device) {
		mFence = mDevice->createFence({});
		mDevice.SetObjectName(mFence, mName);
	}
	inline ~Fence() { mDevice->destroyFence(mFence); }
	inline vk::Fence& operator*() { return mFence; }
	inline const vk::Fence* operator->() { return &mFence; }
};
class Semaphore {
private:
	vk::Semaphore mSemaphore;
	string mName;
	Device& mDevice;
public:
	inline Semaphore(const string& name, Device& device) : mName(name), mDevice(device) {
		mSemaphore = mDevice->createSemaphore({});
		mDevice.SetObjectName(mSemaphore, mName);
	}
	inline ~Semaphore() { mDevice->destroySemaphore(mSemaphore); }
	inline vk::Semaphore& operator*() { return mSemaphore; }
	inline const vk::Semaphore* operator->() { return &mSemaphore; }
};
class Sampler {
private:
	vk::Sampler mSampler;
	string mName;
	Device& mDevice;
public:
	inline Sampler(const string& name, Device& device, const vk::SamplerCreateInfo& samplerInfo) : mName(name), mDevice(device) {
		mSampler = mDevice->createSampler(samplerInfo);
		mDevice.SetObjectName(mSampler, mName);
	}
	inline Sampler(const string& name, Device& device, float maxLod, vk::Filter filter, vk::SamplerAddressMode addressMode, float maxAnisotropy) : mName(name), mDevice(device) {
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

		mSampler = mDevice->createSampler(samplerInfo);
		mDevice.SetObjectName(mSampler, mName);
	}
	inline ~Sampler() {
		mDevice->destroySampler(mSampler);
	}
	inline vk::Sampler operator*() const { return mSampler; }
	inline const vk::Sampler* operator->() const { return &mSampler; }
};

}