#pragma once

#include "Instance.hpp"
#include <Common/locked_object.hpp>
#include <vk_mem_alloc.h>

namespace stm {

class CommandBuffer;
class Semaphore;

class DeviceResource {
private:
	friend class CommandBuffer;
	string mName;
	unordered_set<CommandBuffer*> mTracking;
public:
	Device& mDevice;
	inline DeviceResource(Device& device, const string& name) : mDevice(device), mName(name) {}
	inline virtual ~DeviceResource() {}
	inline const string& name() const { return mName; }
	bool in_use(); // defined in CommandBuffer.hpp
};

class Device {
public:
	class MemoryAllocation : public DeviceResource {
	private:
		friend class Device;
		VmaAllocation mAllocation;
		VmaAllocationInfo mInfo;
		VmaMemoryUsage mUsage;
		vk::MemoryRequirements mRequirements;

	public:
		inline MemoryAllocation() = delete;
		inline MemoryAllocation(MemoryAllocation&& a) : DeviceResource(a.mDevice, a.name()) {
			mAllocation = a.mAllocation;
			mInfo = a.mInfo;
			mUsage = a.mUsage;
			mRequirements = a.mRequirements;
			a.mAllocation = nullptr;
			a.mInfo = {};
		}
		inline MemoryAllocation(Device& device, const vk::MemoryRequirements& requirements, VmaMemoryUsage usage) : DeviceResource(device, "DeviceMemory"), mUsage(usage), mRequirements(requirements) {
			VmaAllocationCreateInfo allocInfo = {};
			allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
			allocInfo.usage = usage;
			vmaAllocateMemory(mDevice.mAllocator, &((const VkMemoryRequirements&)requirements), &allocInfo, &mAllocation, &mInfo);
		}
		inline ~MemoryAllocation() {
			vmaFreeMemory(mDevice.mAllocator, mAllocation);
		}
		inline const vk::DeviceMemory& operator*() const { return *reinterpret_cast<const vk::DeviceMemory*>(&mInfo.deviceMemory); }
		inline const vk::DeviceMemory* operator->() const { return reinterpret_cast<const vk::DeviceMemory*>(&mInfo.deviceMemory); }
		inline operator bool() const { return mAllocation; }
		inline const VmaAllocation& allocation() const { return mAllocation; }
		inline byte* data() { return reinterpret_cast<byte*>(mInfo.pMappedData); }
		inline vk::DeviceSize size() const { return mInfo.size; }
		inline vk::DeviceSize offset() const { return mInfo.offset; }
		inline VmaMemoryUsage usage() const { return mUsage; }
		inline vk::MemoryRequirements requirements() const { return mRequirements; }
	};

	struct QueueFamily {
		Device& mDevice;
		uint32_t mFamilyIndex = 0;
		vector<vk::Queue> mQueues;
		vk::QueueFamilyProperties mProperties;
		bool mSurfaceSupport;
		// CommandBuffers may be in-flight or idle
		unordered_map<thread::id, pair<vk::CommandPool, list<shared_ptr<CommandBuffer>>>> mCommandBuffers;
	};

	stm::Instance& mInstance;
	static const vk::DeviceSize mMinAllocSize = 256_mB;

	STRATUM_API Device(stm::Instance& instance, vk::PhysicalDevice physicalDevice, const unordered_set<string>& deviceExtensions, const vector<const char*>& validationLayers);
	STRATUM_API ~Device();

	inline vk::Device& operator*() { return mDevice; }
	inline vk::Device* operator->() { return &mDevice; }
	inline const vk::Device& operator*() const { return mDevice; }
	inline const vk::Device* operator->() const { return &mDevice; }

	inline vk::PhysicalDevice physical() const { return mPhysicalDevice; }
	inline const vk::PhysicalDeviceLimits& limits() const { return mLimits; }
	inline vk::PipelineCache pipeline_cache() const { return mPipelineCache; }
	inline VmaAllocator allocator() const { return mAllocator; }
	inline uint32_t descriptor_set_count() const { return mDescriptorSetCount; }

	inline const vk::PhysicalDeviceFeatures& features() const  { return mFeatures; }
	inline const vk::PhysicalDeviceDescriptorIndexingFeatures& descriptor_indexing_features() const  { return get<vk::PhysicalDeviceDescriptorIndexingFeatures>(mFeatureChain); }
	inline const vk::PhysicalDeviceBufferDeviceAddressFeatures& buffer_device_address_features() const  { return get<vk::PhysicalDeviceBufferDeviceAddressFeatures>(mFeatureChain); }
	inline const vk::PhysicalDeviceAccelerationStructureFeaturesKHR& acceleration_structure_features() const  { return get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>(mFeatureChain); }
	inline const vk::PhysicalDeviceRayTracingPipelineFeaturesKHR& ray_tracing_pipeline_features() const  { return get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>(mFeatureChain); }
	inline const vk::PhysicalDeviceRayQueryFeaturesKHR& ray_query_features() const  { return get<vk::PhysicalDeviceRayQueryFeaturesKHR>(mFeatureChain); }
	inline const vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT& shader_atomic_float_features() const  { return get<vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>(mFeatureChain); }

	template<typename T> inline vk::DeviceSize min_uniform_buffer_offset_alignment() {
		return (sizeof(T) + mLimits.minUniformBufferOffsetAlignment - 1) & ~(mLimits.minUniformBufferOffsetAlignment - 1);
	}

	inline auto queue_families() { return mQueueFamilies.lock(); }
	inline QueueFamily* find_queue_family(vk::SurfaceKHR surface) {
		auto queueFamilies = queue_families();
		for (auto& [queueFamilyIndex, queueFamily] : *queueFamilies)
			if (mPhysicalDevice.getSurfaceSupportKHR(queueFamilyIndex, surface))
				return &queueFamily;
		return nullptr;
	}

	template<typename T> requires(convertible_to<decltype(T::objectType), vk::ObjectType>)
	inline void set_debug_name(const T& object, const string& name) {
		vk::DebugUtilsObjectNameInfoEXT info = {};
		info.objectHandle = *reinterpret_cast<const uint64_t*>(&object);
		info.objectType = T::objectType;
		info.pObjectName = name.c_str();
		mDevice.setDebugUtilsObjectNameEXT(info);
	}

	inline bool use_timestamps() const { return mEnableTimestamps; }
	inline void use_timestamps(bool v) { mEnableTimestamps = v; }
	STRATUM_API void create_query_pools(uint32_t queryCount);
	STRATUM_API tuple<vk::QueryPool,uint32_t,vector<string>>& query_pool();

	STRATUM_API shared_ptr<CommandBuffer> get_command_buffer(const string& name, vk::QueueFlags queueFlags = vk::QueueFlagBits::eGraphics, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
	STRATUM_API void submit(const shared_ptr<CommandBuffer>& commandBuffer);
	STRATUM_API void flush();

private:
	friend class Instance;
	friend class DescriptorSet;
	friend class CommandBuffer;
	friend class DescriptorSetLayout;
	friend class Pipeline;
	vk::Device mDevice;
 	vk::PhysicalDevice mPhysicalDevice;
	VmaAllocator mAllocator;
	vk::PipelineCache mPipelineCache;

	vk::PhysicalDeviceFeatures mFeatures;
	vk::StructureChain<
		vk::DeviceCreateInfo,
		vk::PhysicalDeviceDescriptorIndexingFeatures,
		vk::PhysicalDeviceBufferDeviceAddressFeatures,
		vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
		vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
		vk::PhysicalDeviceRayQueryFeaturesKHR,
		vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT
	> mFeatureChain;
	vk::PhysicalDeviceLimits mLimits;

	locked_object<unordered_map<uint32_t, QueueFamily>> mQueueFamilies;
	locked_object<vk::DescriptorPool> mDescriptorPool;
	uint32_t mDescriptorSetCount = 0;

	vector<tuple<vk::QueryPool,uint32_t,vector<string>>> mTimestamps;
	bool mEnableTimestamps = false;
};

}