#include "Device.hpp"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#undef VMA_IMPLEMENTATION

#include "Window.hpp"

using namespace stm;

Device::Device(stm::Instance& instance, vk::PhysicalDevice physicalDevice, const unordered_set<string>& deviceExtensions, const vector<const char*>& validationLayers)
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
	mFeatures.fillModeNonSolid = true;
	mFeatures.sparseBinding = true;
	mFeatures.samplerAnisotropy = true;
	mFeatures.shaderImageGatherExtended = true;
	mFeatures.shaderStorageImageExtendedFormats = true;
	mFeatures.wideLines = true;
	mFeatures.largePoints = true;
	mFeatures.sampleRateShading = true;
	mFeatures.shaderFloat64 = true; // needed by slang?
	mFeatures.shaderUniformBufferArrayDynamicIndexing = true;
	mFeatures.shaderStorageBufferArrayDynamicIndexing = true;
	mFeatures.shaderSampledImageArrayDynamicIndexing = true;
	mFeatures.shaderStorageImageArrayDynamicIndexing = true;

	auto& difeatures = get<vk::PhysicalDeviceDescriptorIndexingFeatures>(mFeatureChain);
	difeatures.shaderUniformBufferArrayNonUniformIndexing = true;
	difeatures.shaderStorageBufferArrayNonUniformIndexing = true;
	difeatures.shaderSampledImageArrayNonUniformIndexing = true;
	difeatures.shaderStorageImageArrayNonUniformIndexing = true;
	difeatures.shaderUniformTexelBufferArrayNonUniformIndexing = true;
	difeatures.shaderStorageTexelBufferArrayNonUniformIndexing = true;
	difeatures.descriptorBindingPartiallyBound = true;
	get<vk::PhysicalDeviceBufferDeviceAddressFeatures>(mFeatureChain).bufferDeviceAddress = deviceExtensions.contains(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>(mFeatureChain).accelerationStructure = deviceExtensions.contains(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	auto& rtfeatures = get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>(mFeatureChain);
	rtfeatures.rayTracingPipeline = deviceExtensions.contains(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
	rtfeatures.rayTraversalPrimitiveCulling = rtfeatures.rayTracingPipeline;
	get<vk::PhysicalDeviceRayQueryFeaturesKHR>(mFeatureChain).rayQuery = deviceExtensions.contains(VK_KHR_RAY_QUERY_EXTENSION_NAME);
	//get<vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>(mFeatureChain).shaderBufferFloat32AtomicAdd = deviceExtensions.contains(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);

	auto& createInfo = get<vk::DeviceCreateInfo>(mFeatureChain);
	createInfo.setQueueCreateInfos(queueCreateInfos);
	createInfo.setPEnabledLayerNames(validationLayers);
	createInfo.setPEnabledExtensionNames(deviceExts);
	createInfo.setPEnabledFeatures(&mFeatures);
	createInfo.setPNext(&difeatures);
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
	allocatorInfo.instance = *mInstance;
	allocatorInfo.vulkanApiVersion = mInstance.vulkan_version();
	allocatorInfo.preferredLargeHeapBlockSize = 1024 * 1024;
	#if VK_KHR_buffer_device_address
	if (buffer_device_address_features().bufferDeviceAddress)
		allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	#endif
	vmaCreateAllocator(&allocatorInfo, &mAllocator);
}
Device::~Device() {
	flush();

	for (auto[qp,count,labels] : mTimestamps)
		mDevice.destroyQueryPool(qp);

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

void Device::create_query_pools(uint32_t queryCount) {
	for (auto[qp,count,labels] : mTimestamps)
		mDevice.destroyQueryPool(qp);

	mTimestamps.resize(mInstance.window().back_buffer_count());
	for (auto&[qp,count,labels] : mTimestamps) {
		qp = mDevice.createQueryPool(vk::QueryPoolCreateInfo({}, vk::QueryType::eTimestamp, queryCount));
		labels.clear();
		count = queryCount;
	}
}
tuple<vk::QueryPool,uint32_t,vector<string>>& Device::query_pool() { return mTimestamps[mInstance.window().back_buffer_index()]; }

shared_ptr<CommandBuffer> Device::get_command_buffer(const string& name, vk::QueueFlags queueFlags, vk::CommandBufferLevel level) {
	ProfilerRegion ps("CommandBuffer::get_command_buffer");
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
	if (commandBuffer)
		commandBuffer->reset(name);
	else
		commandBuffer = make_shared<CommandBuffer>(*queueFamily, name, level);
	return commandBuffer;
}
void Device::submit(const shared_ptr<CommandBuffer>& commandBuffer) {
	ProfilerRegion ps("CommandBuffer::submit");

	(*commandBuffer)->end();

	vector<vk::Semaphore> waitSemaphores;
	vector<vk::PipelineStageFlags> waitStages;
	waitSemaphores.reserve(commandBuffer->mWaitSemaphores.size());
	waitStages.reserve(commandBuffer->mWaitSemaphores.size());
	for (auto&[semaphore, stage] : commandBuffer->mWaitSemaphores) {
		waitSemaphores.push_back(**semaphore);
		waitStages.push_back(stage);
	}

	vector<vk::Semaphore> signalSemaphores(commandBuffer->mSignalSemaphores.size());
	ranges::transform(commandBuffer->mSignalSemaphores, signalSemaphores.begin(), [](const shared_ptr<Semaphore>& s) { return **s; });

	commandBuffer->mQueueFamily.mQueues[0].submit(vk::SubmitInfo(waitSemaphores, waitStages, **commandBuffer, signalSemaphores), **commandBuffer->mFence);
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