#pragma once

#include "Instance.hpp"

namespace stm {

class DeviceResource {
public:
	Device& mDevice;
	string mName;
	inline DeviceResource(Device& device, string name) : mDevice(device), mName(name) {}
	inline virtual ~DeviceResource() {}
};

class Fence;
class CommandBuffer;

class Device {
public:
	stm::Instance& mInstance;
	const vk::DeviceSize mMemoryBlockSize = 256_kB;

	struct QueueFamily {
		uint32_t mFamilyIndex = 0;
		vector<vk::Queue> mQueues;
		vk::QueueFamilyProperties mProperties;
		bool mSurfaceSupport;
		// CommandBuffers may be in-flight or idle
		unordered_map<thread::id, pair<vk::CommandPool, list<shared_ptr<CommandBuffer>>>> mCommandBuffers;
	};
	
	class Memory : public DeviceResource {
	private:
		vk::DeviceMemory mDeviceMemory;
		unordered_map<vk::DeviceSize/*begin*/, vk::DeviceSize/*end*/> mBlocks;
		
		friend class Device;

	public:
		class Block {
		public:
			Memory* const mMemory = nullptr;
			const vk::DeviceSize mOffset = 0;
			Block() = default;
			Block(const Block& b) = default;
			inline operator bool() const { return mMemory; }
			inline Block& operator=(const Block& b) { return *new (this) Block(b); }
			inline byte* data() const { return mMemory->mMapped + mOffset; }
		private:
			friend class Memory;
			inline Block(Memory& memory, vk::DeviceSize offset) : mMemory(&memory), mOffset(offset) {}
		};

		uint32_t mMemoryTypeIndex = 0;
		vk::DeviceSize mSize = 0;
		byte* mMapped = nullptr;

		inline Memory(Device& device, uint32_t memoryTypeIndex, vk::DeviceSize size) : DeviceResource(device, "/mem"+to_string(memoryTypeIndex)+"_"+to_string(size)), mMemoryTypeIndex(memoryTypeIndex), mSize(size) {
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

		inline bool empty() const { return mBlocks.empty(); }

		STRATUM_API Block AllocateBlock(const vk::MemoryRequirements& requirements);
		STRATUM_API void FreeBlock(Block& block);
	};

	STRATUM_API Device(stm::Instance& instance, vk::PhysicalDevice physicalDevice, const unordered_set<string>& deviceExtensions, const vector<const char*>& validationLayers);
	STRATUM_API ~Device();
	inline vk::Device operator*() const { return mDevice; }
	inline const vk::Device* operator->() const { return &mDevice; }
	
	inline vk::PhysicalDevice PhysicalDevice() const { return mPhysicalDevice; }
	inline const vk::PhysicalDeviceMemoryProperties& MemoryProperties() const { return mMemoryProperties; }
	inline const vk::PhysicalDeviceLimits& Limits() const { return mLimits; }
	inline vk::PipelineCache PipelineCache() const { return mPipelineCache; }
	inline const vector<uint32_t>& QueueFamilies(uint32_t index) const { return mQueueFamilyIndices; }
	
	STRATUM_API QueueFamily* FindQueueFamily(vk::SurfaceKHR surface);
	STRATUM_API vk::SampleCountFlagBits GetMaxUsableSampleCount();

	// Will attempt to sub-allocate from larger allocations, will create a new allocation if unsuccessful. HostVisible memory is automatically mapped.
	STRATUM_API Memory::Block AllocateMemory(const vk::MemoryRequirements& requirements, vk::MemoryPropertyFlags properties, const string& tag = "");
	STRATUM_API void FreeMemory(Memory::Block& allocation);

	template<typename T> requires(convertible_to<decltype(T::objectType), vk::ObjectType>)
	inline void SetObjectName(const T& object, const string& name) {
		if (mSetDebugUtilsObjectNameEXT) {
			vk::DebugUtilsObjectNameInfoEXT info = {};
			info.objectHandle = *reinterpret_cast<const uint64_t*>(&object);
			info.objectType = T::objectType;
			info.pObjectName = name.c_str();
			mSetDebugUtilsObjectNameEXT(mDevice, reinterpret_cast<VkDebugUtilsObjectNameInfoEXT*>(&info));
		}
	}
	
	STRATUM_API shared_ptr<CommandBuffer> GetCommandBuffer(const string& name, vk::QueueFlags queueFlags = vk::QueueFlagBits::eGraphics, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
	STRATUM_API void Execute(shared_ptr<CommandBuffer> commandBuffer, bool pool = true);
	STRATUM_API void Flush();

	inline uint64_t FrameCount() const { return mFrameCount; }
	
private:
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
	locked_object<unordered_map<uint32_t, QueueFamily>> mQueueFamilies;
	locked_object<unordered_map<uint32_t, list<unique_ptr<Memory>>>> mMemoryPool;
	locked_object<vk::DescriptorPool> mDescriptorPool;
};

class Fence : public DeviceResource {
private:
	vk::Fence mFence;

public:
	inline Fence(Device& device, const string& name) : DeviceResource(device,name) {
		mFence = mDevice->createFence({});
		mDevice.SetObjectName(mFence, mName);
	}
	inline ~Fence() { mDevice->destroyFence(mFence); }
	inline vk::Fence& operator*() { return mFence; }
	inline const vk::Fence* operator->() { return &mFence; }

	inline vk::Result wait(uint64_t timeout = numeric_limits<uint64_t>::max()) {
		return mDevice->waitForFences({ mFence }, true, timeout);
	}
};
class Semaphore : public DeviceResource {
private:
	vk::Semaphore mSemaphore;

public:
	inline Semaphore(Device& device, const string& name) : DeviceResource(device, name) {
		mSemaphore = mDevice->createSemaphore({});
		mDevice.SetObjectName(mSemaphore, mName);
	}
	inline ~Semaphore() { mDevice->destroySemaphore(mSemaphore); }
	inline vk::Semaphore& operator*() { return mSemaphore; }
	inline const vk::Semaphore* operator->() { return &mSemaphore; }
};

class Sampler : public DeviceResource {
private:
	vk::Sampler mSampler;
	vk::SamplerCreateInfo mInfo;

public:
	inline Sampler(Device& device, const string& name, const vk::SamplerCreateInfo& samplerInfo) : DeviceResource(device, name), mInfo(samplerInfo) {
		mSampler = mDevice->createSampler(mInfo);
		mDevice.SetObjectName(mSampler, mName);
	}
	inline Sampler(Device& device, const string& name, vk::Filter filter, vk::SamplerAddressMode addressMode, float maxAnisotropy) : DeviceResource(device, name) {
		mInfo.magFilter = filter;
		mInfo.minFilter = filter;
		mInfo.addressModeU = addressMode;
		mInfo.addressModeV = addressMode;
		mInfo.addressModeW = addressMode;
		mInfo.anisotropyEnable = maxAnisotropy > 0 ? VK_TRUE : VK_FALSE;
		mInfo.maxAnisotropy = maxAnisotropy;
		mInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
		mInfo.unnormalizedCoordinates = VK_FALSE;
		mInfo.compareEnable = VK_FALSE;
		mInfo.compareOp = vk::CompareOp::eAlways;
		mInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
		mInfo.minLod = 0;
		mInfo.maxLod = VK_LOD_CLAMP_NONE;
		mInfo.mipLodBias = 0;
		mSampler = mDevice->createSampler(mInfo);
		mDevice.SetObjectName(mSampler, mName);
	}
	inline ~Sampler() {
		mDevice->destroySampler(mSampler);
	}
	inline vk::Sampler operator*() const { return mSampler; }
	inline const vk::Sampler* operator->() const { return &mSampler; }
};

}