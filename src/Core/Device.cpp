#include "Device.hpp"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#undef VMA_IMPLEMENTATION

#include "Window.hpp"

using namespace stm;

Device::Device(stm::Instance& instance, vk::PhysicalDevice physicalDevice, const unordered_set<string>& deviceExtensions, const vector<const char*>& validationLayers, uint32_t frameInUseCount)
	: mPhysicalDevice(physicalDevice), mInstance(instance) {

	vk::PhysicalDeviceProperties properties = mPhysicalDevice.getProperties();
	mLimits = properties.limits;

	vector<const char*> deviceExts;
	for (const string& s : deviceExtensions)
		deviceExts.emplace_back(s.c_str());
		

	#pragma region get queue infos
	vector<vk::QueueFamilyProperties> queueFamilyProperties = mPhysicalDevice.getQueueFamilyProperties();
	vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
	float queuePriority = 1.0f;
	for (uint32_t i = 0; i < queueFamilyProperties.size(); i++) {
		if (queueFamilyProperties[i].queueFlags & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer)) {
			vk::DeviceQueueCreateInfo queueCreateInfo = {};
			queueCreateInfo.queueFamilyIndex = i;
			queueCreateInfo.queueCount = 1;
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.emplace_back(queueCreateInfo);
		}
	}
	#pragma endregion

	#pragma region Create logical device
	vk::DeviceCreateInfo createInfo;
	mFeatures.fillModeNonSolid = true;
	mFeatures.sparseBinding = true;
	mFeatures.samplerAnisotropy = true;
	mFeatures.shaderImageGatherExtended = true;
	mFeatures.shaderStorageImageExtendedFormats = true;
	mFeatures.wideLines = true;
	mFeatures.largePoints = true;
	mFeatures.sampleRateShading = true;
	mFeatures.shaderUniformBufferArrayDynamicIndexing = true;
	mFeatures.shaderStorageBufferArrayDynamicIndexing = true;
	mFeatures.shaderSampledImageArrayDynamicIndexing = true;
	mFeatures.shaderStorageImageArrayDynamicIndexing = true;
	mDescriptorIndexingFeatures.shaderUniformBufferArrayNonUniformIndexing = true;
	mDescriptorIndexingFeatures.shaderStorageBufferArrayNonUniformIndexing = true;
	mDescriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = true;
	mDescriptorIndexingFeatures.shaderStorageImageArrayNonUniformIndexing = true;
	mDescriptorIndexingFeatures.shaderUniformTexelBufferArrayNonUniformIndexing = true;
	mDescriptorIndexingFeatures.shaderStorageTexelBufferArrayNonUniformIndexing = true;
	mDescriptorIndexingFeatures.descriptorBindingPartiallyBound = true;
	mBufferDeviceAddressFeatures.bufferDeviceAddress = deviceExtensions.contains(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	mAccelerationStructureFeatures.accelerationStructure = deviceExtensions.contains(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	mRayTracingPipelineFeatures.rayTracingPipeline = deviceExtensions.contains(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
	mRayTracingPipelineFeatures.rayTraversalPrimitiveCulling = mRayTracingPipelineFeatures.rayTracingPipeline;
	mRayQueryFeatures.rayQuery = deviceExtensions.contains(VK_KHR_RAY_QUERY_EXTENSION_NAME);

	createInfo.setQueueCreateInfos(queueCreateInfos);
	createInfo.setPEnabledLayerNames(validationLayers);
	createInfo.setPEnabledExtensionNames(deviceExts);
	createInfo.setPEnabledFeatures(&mFeatures);
	createInfo.pNext = &mDescriptorIndexingFeatures;
	mDescriptorIndexingFeatures.pNext = &mBufferDeviceAddressFeatures;
	mBufferDeviceAddressFeatures.pNext = &mAccelerationStructureFeatures;
	mAccelerationStructureFeatures.pNext = &mRayTracingPipelineFeatures;
	mRayTracingPipelineFeatures.pNext = &mRayQueryFeatures;

	mDevice = mPhysicalDevice.createDevice(createInfo);
	#if VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
  VULKAN_HPP_DEFAULT_DISPATCHER.init(mDevice);
	#endif

	string name = "[" + to_string(properties.deviceID) + "]: " + properties.deviceName.data();
	set_debug_name(mDevice, name);
	#pragma endregion

	#pragma region Create PipelineCache and DescriptorPool
	vk::PipelineCacheCreateInfo cacheInfo = {};
	string tmp;
	if (!mInstance.find_argument("noPipelineCache")) {
		try {
			ifstream cacheFile(fs::temp_directory_path() / "pcache", ios::binary | ios::ate);
			if (cacheFile.is_open()) {
				cacheInfo.initialDataSize = cacheFile.tellg();
				cacheInfo.pInitialData = new char[cacheInfo.initialDataSize];
				cacheFile.seekg(0, ios::beg);
				cacheFile.read((char*)cacheInfo.pInitialData, cacheInfo.initialDataSize);
				printf("Read pipeline cache (%.2f kb)\n", cacheInfo.initialDataSize/(float)1_mB);
			}
		} catch (exception& e) {
			fprintf_color(ConsoleColor::eYellow, stderr, "Warning: Failed to read pipeline cache: %s\n", e.what());
		}
	}
	mPipelineCache = mDevice.createPipelineCache(cacheInfo);
	if (cacheInfo.pInitialData) delete reinterpret_cast<const byte*>(cacheInfo.pInitialData);
	
	vector<vk::DescriptorPoolSize> poolSizes {
		vk::DescriptorPoolSize(vk::DescriptorType::eSampler, 								min(16384u, mLimits.maxDescriptorSetSamplers)),
		vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 	min(16384u, mLimits.maxDescriptorSetSampledImages)),
    vk::DescriptorPoolSize(vk::DescriptorType::eInputAttachment, 				min(16384u, mLimits.maxDescriptorSetInputAttachments)),
		vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, 					min(16384u, mLimits.maxDescriptorSetSampledImages)),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 					min(16384u, mLimits.maxDescriptorSetStorageImages)),
		vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 					min(16384u, mLimits.maxDescriptorSetUniformBuffers)),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, 	min(16384u, mLimits.maxDescriptorSetUniformBuffersDynamic)),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 					min(16384u, mLimits.maxDescriptorSetStorageBuffers)),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageBufferDynamic, 	min(16384u, mLimits.maxDescriptorSetStorageBuffersDynamic))
	};
	{
		auto descriptorPool = mDescriptorPool.lock();
		*descriptorPool = mDevice.createDescriptorPool(vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 8192, poolSizes));
		set_debug_name(*descriptorPool, name);
	}
	#pragma endregion

	for (const vk::DeviceQueueCreateInfo& info : queueCreateInfos) {
		QueueFamily q = { *this };
		q.mFamilyIndex = info.queueFamilyIndex;
		q.mProperties = queueFamilyProperties[info.queueFamilyIndex];
		q.mSurfaceSupport = mPhysicalDevice.getSurfaceSupportKHR(info.queueFamilyIndex, mInstance.window().surface());
		// TODO: create more queues for parallelization (if necessary?)
		for (uint32_t i = 0; i < 1; i++) {
			q.mQueues.emplace_back(mDevice.getQueue(info.queueFamilyIndex, i));
			set_debug_name(q.mQueues[i], "DeviceQueue"+to_string(i));
		}
		mQueueFamilies.lock()->emplace(q.mFamilyIndex, q);
	}

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = mPhysicalDevice;
	allocatorInfo.device = mDevice;
	allocatorInfo.frameInUseCount = frameInUseCount;
	allocatorInfo.instance = *mInstance;
	allocatorInfo.vulkanApiVersion = mInstance.vulkan_version();
	#if VK_KHR_buffer_device_address
	if (mBufferDeviceAddressFeatures.bufferDeviceAddress)
		allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	#endif
	vmaCreateAllocator(&allocatorInfo, &mAllocator);
}
Device::~Device() {
	flush();

	auto queueFamilies = mQueueFamilies.lock();
	for (auto& [idx, queueFamily] : *queueFamilies) {
		for (auto&[tid, p] : queueFamily.mCommandBuffers) {
			p.second.clear();
			mDevice.destroyCommandPool(p.first);
		}
	}
	queueFamilies->clear();
	
	mDevice.destroyDescriptorPool(*mDescriptorPool.lock());

	vmaDestroyAllocator(mAllocator);

	if (!mInstance.find_argument("noPipelineCache")) {
		try {
			auto cacheData = mDevice.getPipelineCacheData(mPipelineCache);
			ofstream output(fs::temp_directory_path()/"stm_pipeline_cache", ios::binary);
			output.write((const char*)cacheData.data(), cacheData.size());
		} catch (exception& e) {
			fprintf_color(ConsoleColor::eYellow, stderr, "Warning: Failed to write pipeline cache: %s\n", e.what());
		}
	}
	mDevice.destroyPipelineCache(mPipelineCache);
	mDevice.destroy();
}

shared_ptr<CommandBuffer> Device::get_command_buffer(const string& name, vk::QueueFlags queueFlags, vk::CommandBufferLevel level) {
	// find most specific queue family
	QueueFamily* queueFamily = nullptr;
	auto queueFamilies = mQueueFamilies.lock();
	for (auto& [queueFamilyIndex, family] : *queueFamilies)
		if (family.mProperties.queueFlags & queueFlags)
			queueFamily = &family;

	if (queueFamily == nullptr) throw invalid_argument("invalid queueFlags " + to_string(queueFlags));

	auto& [commandPool,commandBuffers] = queueFamily->mCommandBuffers[this_thread::get_id()];
	if (!commandPool) {
		commandPool = mDevice.createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamily->mFamilyIndex));
		set_debug_name(commandPool, "CommandPool");
	}

	shared_ptr<CommandBuffer> commandBuffer;
	if (level == vk::CommandBufferLevel::ePrimary) {
		for (auto it = commandBuffers.begin(); it != commandBuffers.end();) {
			if ((*it)->clear_if_done() && it->use_count() == 1) {
				commandBuffer = *it;
				commandBuffers.erase(it);
				break;
			} else
				it++;
		}
	}
	if (commandBuffer) {
		commandBuffer->reset(name);
		return commandBuffer;
	} else
		return make_shared<CommandBuffer>(*queueFamily, name, level);
}
void Device::submit(shared_ptr<CommandBuffer> commandBuffer, const vk::ArrayProxy<pair<shared_ptr<Semaphore>, vk::PipelineStageFlags>>& waitSemaphores, const vk::ArrayProxy<shared_ptr<Semaphore>>& signalSemaphores) {
	ProfilerRegion ps("CommandBuffer::submit");

	vector<vk::Semaphore> waits(waitSemaphores.size());
	vector<vk::PipelineStageFlags> waitStages(waitSemaphores.size());
	vector<vk::Semaphore> signals(signalSemaphores.size());

	ranges::transform(views::elements<0>(waitSemaphores), waits.begin(), [](const shared_ptr<Semaphore>& s) { return **s; });
	ranges::copy(views::elements<1>(waitSemaphores), waitStages.begin());
	ranges::transform(signalSemaphores, signals.begin(), [](const shared_ptr<Semaphore>& s) { return **s; });

	for(const auto&[s, stage] : waitSemaphores) commandBuffer->hold_resource(s);
	for(const auto& s : signalSemaphores) commandBuffer->hold_resource(s);

	(*commandBuffer)->end();
	commandBuffer->mQueueFamily.mQueues[0].submit(vk::SubmitInfo(waits, waitStages, **commandBuffer, signals), **commandBuffer->mCompletionFence);
	commandBuffer->mState = CommandBuffer::CommandBufferState::eInFlight;
	
	scoped_lock l(mQueueFamilies.m());
	commandBuffer->mQueueFamily.mCommandBuffers.at(this_thread::get_id()).second.emplace_back(commandBuffer);
}
void Device::flush() {
	mDevice.waitIdle();
	auto queueFamilies = mQueueFamilies.lock();
	for (auto& [idx,queueFamily] : *queueFamilies)
		for (auto& [tid,p] : queueFamily.mCommandBuffers)
			for (auto& commandBuffer : p.second)
				commandBuffer->clear_if_done();
}