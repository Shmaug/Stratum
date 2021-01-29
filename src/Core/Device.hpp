#pragma once

#include "Instance.hpp"

namespace stm {

class Buffer;
class CommandBuffer;
class DescriptorSet;
class Fence;
class Texture;

class Asset {
public:
  inline virtual ~Asset() {};
};

class Device {
public:
	stm::Instance& mInstance;
	const vk::DeviceSize mMemoryBlockSize = 256_kB;

	struct QueueFamily {
		uint32_t mFamilyIndex = 0;
		vector<vk::Queue> mQueues;
		string mName;
		vk::QueueFamilyProperties mProperties;
		bool mSurfaceSupport;
		// CommandBuffers may be in-flight or idle and are managed by Device
		unordered_map<thread::id, pair<vk::CommandPool, list<unique_ptr<CommandBuffer>>>> mCommandBuffers;
	};
	class Memory {
	private:
		friend class Device;
		vk::DeviceMemory mDeviceMemory;
		unordered_map<vk::DeviceSize/*begin*/, vk::DeviceSize/*end*/> mBlocks;

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

	STRATUM_API Device(stm::Instance& instance, vk::PhysicalDevice physicalDevice, const unordered_set<string>& deviceExtensions, const vector<const char*>& validationLayers);
	STRATUM_API ~Device();
	inline vk::Device operator*() const { return mDevice; }
	inline const vk::Device* operator->() const { return &mDevice; }
	
	inline vk::PhysicalDevice PhysicalDevice() const { return mPhysicalDevice; }
	inline const vk::PhysicalDeviceMemoryProperties& MemoryProperties() const { return mMemoryProperties; }
	inline const vk::PhysicalDeviceLimits& Limits() const { return mLimits; }
	inline vk::PipelineCache PipelineCache() const { return mPipelineCache; }
	inline const vector<uint32_t> QueueFamilies(uint32_t index) const { return mQueueFamilyIndices; }
	STRATUM_API QueueFamily* FindQueueFamily(vk::SurfaceKHR surface);
	STRATUM_API vk::SampleCountFlagBits GetMaxUsableSampleCount();

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
	
	template<class T> requires(derived_from<T,Asset>)
	inline shared_ptr<T> FindOrLoadAsset(const fs::path& filename) const {
		uint64_t key = hash_combine(filename.string());
		if (mLoadedAssets.count(key) == 0) return nullptr;
		return dynamic_pointer_cast<T>(mLoadedAssets.at(key));
	}
	inline void UnloadAssets() { lock_guard lock(mAssetMutex); mLoadedAssets.clear(); }

	STRATUM_API shared_ptr<Buffer> GetPooledBuffer(const string& name, vk::DeviceSize size, vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal);
	STRATUM_API shared_ptr<DescriptorSet> GetPooledDescriptorSet(const string& name, vk::DescriptorSetLayout layout);
	STRATUM_API shared_ptr<Texture> GetPooledTexture(const string& name, const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels = 1, vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1, vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);
	STRATUM_API void PoolResource(shared_ptr<Buffer> resource);
	STRATUM_API void PoolResource(shared_ptr<DescriptorSet> resource);
	STRATUM_API void PoolResource(shared_ptr<Texture> resource);

	STRATUM_API void PurgeResourcePools(uint32_t maxAge);

	STRATUM_API unique_ptr<CommandBuffer> GetCommandBuffer(const string& name, vk::QueueFlags queueFlags = vk::QueueFlagBits::eGraphics, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
	// The CommandBuffer will be managed by the device after being passed in
	STRATUM_API Fence& Execute(unique_ptr<CommandBuffer>&& commandBuffer);
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
				erase_if(pool, [=](const auto& i){ return i.mLastFrameUsed + maxAge > currentFrame; });
		}
	};

	friend class Instance;
	friend class DescriptorSet;
	STRATUM_API void PrintAllocations();

	vk::Device mDevice;
 	vk::PhysicalDevice mPhysicalDevice;
	vk::PipelineCache mPipelineCache;
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
	unordered_map<uint32_t, QueueFamily> mQueueFamilies;

	mutable mutex mMemoryMutex;
	unordered_map<uint32_t /*memoryTypeIndex*/, list<unique_ptr<Memory>>> mMemoryPool;

	mutable mutex mDescriptorPoolMutex;
	vk::DescriptorPool mDescriptorPool;

	mutable mutex mAssetMutex;
	unordered_map<size_t, shared_ptr<Asset>> mLoadedAssets;
};

class Fence {
public:
	Device& mDevice;

	inline Fence(Device& device, const string& name) : mName(name), mDevice(device) {
		mFence = mDevice->createFence({});
		mDevice.SetObjectName(mFence, mName);
	}
	inline ~Fence() { mDevice->destroyFence(mFence); }
	inline vk::Fence& operator*() { return mFence; }
	inline const vk::Fence* operator->() { return &mFence; }

	inline vk::Result wait(uint64_t timeout = numeric_limits<uint64_t>::max()) {
		return mDevice->waitForFences({ mFence }, true, timeout);
	}

private:
	vk::Fence mFence;
	string mName;
};

class Semaphore {
public:
	Device& mDevice;

	inline Semaphore(Device& device, const string& name) : mName(name), mDevice(device) {
		mSemaphore = mDevice->createSemaphore({});
		mDevice.SetObjectName(mSemaphore, mName);
	}
	inline ~Semaphore() { mDevice->destroySemaphore(mSemaphore); }
	inline vk::Semaphore& operator*() { return mSemaphore; }
	inline const vk::Semaphore* operator->() { return &mSemaphore; }

private:
	vk::Semaphore mSemaphore;
	string mName;
};

class Sampler {
public:
	Device& mDevice;

	inline Sampler(Device& device, const string& name, const vk::SamplerCreateInfo& samplerInfo) : mName(name), mDevice(device) {
		mSampler = mDevice->createSampler(samplerInfo);
		mDevice.SetObjectName(mSampler, mName);
	}
	inline Sampler(Device& device, const string& name, float maxLod, vk::Filter filter, vk::SamplerAddressMode addressMode, float maxAnisotropy) : mName(name), mDevice(device) {
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

private:
	vk::Sampler mSampler;
	string mName;
};

}