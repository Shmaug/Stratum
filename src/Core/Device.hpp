#pragma once

#include "Instance.hpp"

namespace stm {

class CommandBuffer;

class DeviceResource {
private:
	string mName;
public:
	Device& mDevice;
	inline DeviceResource(Device& device, string name) : mDevice(device), mName(name) {}
	inline virtual ~DeviceResource() {}
	inline const string& Name() const { return mName; }
};

template<typename T>
concept DeviceResourceView = requires(T t) { { t.get() } -> convertible_to<shared_ptr<DeviceResource>>; };

class Device {
public:
	stm::Instance& mInstance;
	static const vk::DeviceSize mDeviceMemoryAllocSize = 256_mB;

	class Memory : public DeviceResource {
	private:
		friend class Device;
		friend class View;
		vk::DeviceMemory mDeviceMemory;
		uint32_t mTypeIndex;
		vk::DeviceSize mSize = 0;
		byte* mMapped = nullptr;
		std::list<std::pair<VkDeviceSize, VkDeviceSize>> mUnallocated;

	public:
		class View {
		private:
			friend class Memory;
			inline View(Memory& memory, vk::DeviceSize offset, vk::DeviceSize size) : mMemory(memory), mOffset(offset), mSize(size) {}
		public:
			Memory& mMemory;
			const vk::DeviceSize mOffset = 0;
			const vk::DeviceSize mSize = 0;
			View() = default;
			View(const View& b) = default;
			STRATUM_API ~View();
			inline View& operator=(const View& b) { return *new (this) View(b); }
			inline vk::DeviceSize offset() const { return mOffset; }
			inline vk::DeviceSize size() const { return mSize; }
			inline byte* data() const { return mMemory.mMapped + mOffset; }
		};

		inline Memory(Device& device, uint32_t typeIndex, vk::DeviceSize size) : DeviceResource(device, "DeviceMemory"), mTypeIndex(typeIndex), mSize(size) {
			mDeviceMemory = mDevice->allocateMemory(vk::MemoryAllocateInfo(size, typeIndex));
			if (mDevice.mMemoryTypes[typeIndex].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)
				mMapped = (byte*)mDevice->mapMemory(mDeviceMemory, 0, mSize);
			mUnallocated = { { 0, mSize } };
		}
		inline ~Memory() {
			if (mDeviceMemory) {
				if (!mUnallocated.empty() && mUnallocated.front() != make_pair(vk::DeviceSize(0),mSize))
					fprintf_color(ConsoleColorBits::eYellow, stderr, "freeing device memory with blocks in use");
				if (mMapped) mDevice->unmapMemory(mDeviceMemory);
				mDevice->freeMemory(mDeviceMemory);
			}
		}
		inline const vk::DeviceMemory& operator*() const { return mDeviceMemory; }
		inline const vk::DeviceMemory* operator->() const { return &mDeviceMemory; }
		inline byte* data() const { return mMapped; }
		inline vk::DeviceSize size() const { return mSize; }
		inline const vk::MemoryType& Type() const { return mDevice.mMemoryTypes[mTypeIndex]; }
		STRATUM_API shared_ptr<View> Allocate(vk::DeviceSize size, vk::DeviceSize alignment);
	};

	struct QueueFamily {
		uint32_t mFamilyIndex = 0;
		vector<vk::Queue> mQueues;
		vk::QueueFamilyProperties mProperties;
		bool mSurfaceSupport;
		// CommandBuffers may be in-flight or idle
		unordered_map<thread::id, pair<vk::CommandPool, list<shared_ptr<CommandBuffer>>>> mCommandBuffers;
	};
	
	STRATUM_API Device(stm::Instance& instance, vk::PhysicalDevice physicalDevice, const unordered_set<string>& deviceExtensions, const vector<const char*>& validationLayers);
	STRATUM_API ~Device();
	inline const vk::Device& operator*() const { return mDevice; }
	inline const vk::Device* operator->() const { return &mDevice; }
	
	inline vk::PhysicalDevice PhysicalDevice() const { return mPhysicalDevice; }
	inline const vk::PhysicalDeviceLimits& Limits() const { return mLimits; }
	inline vk::PipelineCache PipelineCache() const { return mPipelineCache; }
	inline const vector<uint32_t>& QueueFamilies(uint32_t index) const { return mQueueFamilyIndices; }
	
	STRATUM_API QueueFamily* FindQueueFamily(vk::SurfaceKHR surface);
	STRATUM_API vk::SampleCountFlagBits GetMaxUsableSampleCount();

	inline uint32_t MemoryTypeIndex(vk::MemoryPropertyFlags properties, uint32_t typeBits = 0) {
		if (typeBits == 0) typeBits = ~uint32_t(0) >> (32 - mMemoryTypes.size());
		uint32_t best = -1;
		while (typeBits) {
    	int i = countr_zero(typeBits);
			if (mMemoryTypes[i].propertyFlags & properties) {
				best = i;
				break;
			}
			typeBits &= ~(1 << i);
		}
		return best;
	}

	// Will attempt to sub-allocate from larger allocations, will create a new allocation if unsuccessful. Host-Visible memory is automatically mapped.
	inline shared_ptr<Memory::View> AllocateMemory(vk::DeviceSize size, vk::DeviceSize alignment, uint32_t memoryTypeIndex) {
		const auto& memoryType = mMemoryTypes[memoryTypeIndex];
		auto memoryPool = mMemoryPool.lock();
		for (auto& memory : (*memoryPool)[memoryType.heapIndex])
			if (auto block = memory->Allocate(size, alignment))
				return block;
		// Failed to sub-allocate, allocate new memory
		auto& m = (*memoryPool)[memoryType.heapIndex].emplace_back(make_unique<Memory>(*this, memoryTypeIndex, (memoryType.propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal) ? AlignUp(size, 256_mB) : size));
		return m->Allocate(size, alignment);
	}
	inline shared_ptr<Memory::View> AllocateMemory(vk::DeviceSize size, vk::DeviceSize alignment, vk::MemoryPropertyFlags properties) {
		uint32_t memoryIndex = MemoryTypeIndex(properties);
		if (memoryIndex > mMemoryTypes.size()) return nullptr;
		return AllocateMemory(size, alignment, memoryIndex);
	}
	inline shared_ptr<Memory::View> AllocateMemory(const vk::MemoryRequirements& requirements, vk::MemoryPropertyFlags properties) {
		uint32_t memoryIndex = MemoryTypeIndex(properties, requirements.memoryTypeBits);
		if (memoryIndex > mMemoryTypes.size()) return nullptr;
		return AllocateMemory(requirements.size, requirements.alignment, memoryIndex);
	}
	

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
	STRATUM_API void Execute(shared_ptr<CommandBuffer> commandBuffer);
	STRATUM_API void Flush();

private:
	friend class Instance;
	friend class DescriptorSet;
	STRATUM_API void PrintAllocations();

	vk::Device mDevice;
 	vk::PhysicalDevice mPhysicalDevice;
	vk::PipelineCache mPipelineCache;
	vector<vk::MemoryType> mMemoryTypes;
	
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
		mDevice.SetObjectName(mFence, Name());
	}
	inline ~Fence() { mDevice->destroyFence(mFence); }
	inline const vk::Fence& operator*() const { return mFence; }
	inline const vk::Fence* operator->() const { return &mFence; }
	inline vk::Result status() { return mDevice->getFenceStatus(mFence); }
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
		mDevice.SetObjectName(mSemaphore, Name());
	}
	inline ~Semaphore() { mDevice->destroySemaphore(mSemaphore); }
	inline const vk::Semaphore& operator*() { return mSemaphore; }
	inline const vk::Semaphore* operator->() { return &mSemaphore; }
};

template<class T>
struct device_allocator : std::allocator<T> {
	using value_type = T;
	Device& mDevice;
	uint32_t mMemoryTypeBits;
	vk::DeviceSize mAlignment;
	unordered_map<T*, shared_ptr<Device::Memory::View>> mAllocations;
	
  device_allocator() = delete;
  device_allocator(Device& device, uint32_t memoryType, size_t alignment = 0) : mDevice(device), mMemoryTypeBits(memoryType), mAlignment(alignment) {}
  template<class U> device_allocator(const device_allocator<U>& v) noexcept : mDevice(v.mDevice), mMemoryTypeBits(v.mMemoryTypeBits), mAlignment(v.mAlignment) {}

  template <class U> 
  struct rebind { typedef device_allocator<U> other; };

	inline T* allocate(size_t n) {
		auto view = mDevice.AllocateMemory(n*sizeof(T), mAlignment, mMemoryTypeBits);
		auto it = mAllocations.emplace(reinterpret_cast<T*>(view->data()), view).first;
		return it->first;
	}
  inline void deallocate(T* p, size_t n) {
		mAllocations.erase(p);
	}
	
	inline shared_ptr<Device::Memory::View> device_memory(T* ptr) const {
		auto it = mAllocations.find(ptr);
		if (it == mAllocations.end()) return nullptr;
		else return it->second;
	}
};

template<class T, class U>
constexpr bool operator==(const device_allocator<T>& lhs, const device_allocator<U>& rhs) noexcept { return lhs.mDevice == rhs.mDevice && lhs.mMemoryType == rhs.mMemoryType; }
template<class T, class U>
constexpr bool operator!=(const device_allocator<T>& lhs, const device_allocator<U>& rhs) noexcept { return !operator==(lhs, rhs); }

template<typename R>
concept device_allocator_range = requires { is_specialization_v<typename R::allocator_type, device_allocator>; };

template<typename T>
using device_vector = vector<T, device_allocator<T>>;

}