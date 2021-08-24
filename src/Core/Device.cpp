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

	mSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)mInstance->getProcAddr("vkSetDebugUtilsObjectNameEXT");

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
	vk::PhysicalDeviceFeatures deviceFeatures = {};	
	deviceFeatures.fillModeNonSolid = VK_TRUE;
	deviceFeatures.sparseBinding = VK_TRUE;
	deviceFeatures.samplerAnisotropy = VK_TRUE;
	deviceFeatures.shaderImageGatherExtended = VK_TRUE;
	deviceFeatures.shaderStorageImageExtendedFormats = VK_TRUE;
	deviceFeatures.wideLines = VK_TRUE;
	deviceFeatures.largePoints = VK_TRUE;
	deviceFeatures.sampleRateShading = VK_TRUE;
	deviceFeatures.shaderUniformBufferArrayDynamicIndexing = VK_TRUE;
	deviceFeatures.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
	deviceFeatures.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
	deviceFeatures.shaderStorageImageArrayDynamicIndexing = VK_TRUE;
	mDevice = mPhysicalDevice.createDevice(vk::DeviceCreateInfo({}, queueCreateInfos, validationLayers, deviceExts, &deviceFeatures));
	string name = "[" + to_string(properties.deviceID) + "]: " + properties.deviceName.data();
	set_debug_name(mDevice, name);
	#pragma endregion

	#pragma region Create PipelineCache and DesriptorPool
	vk::PipelineCacheCreateInfo cacheInfo = {};
	string tmp;
	if (!mInstance.find_argument("noPipelineCache")) {
		try {
			ifstream cacheFile(fs::temp_directory_path() / "stm_pipeline_cache", ios::binary | ios::ate);
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
		vk::DescriptorPoolSize(vk::DescriptorType::eSampler, 								min(1024u, mLimits.maxDescriptorSetSamplers)),
		vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 	min(1024u, mLimits.maxDescriptorSetSampledImages)),
    vk::DescriptorPoolSize(vk::DescriptorType::eInputAttachment, 				min(1024u, mLimits.maxDescriptorSetInputAttachments)),
		vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, 					min(1024u, mLimits.maxDescriptorSetSampledImages)),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 					min(1024u, mLimits.maxDescriptorSetStorageImages)),
		vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 					min(1024u, mLimits.maxDescriptorSetUniformBuffers)),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, 	min(1024u, mLimits.maxDescriptorSetUniformBuffersDynamic)),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 					min(1024u, mLimits.maxDescriptorSetStorageBuffers)),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageBufferDynamic, 	min(1024u, mLimits.maxDescriptorSetStorageBuffersDynamic))
	};
	{
		auto descriptorPool = mDescriptorPool.lock();
		*descriptorPool = mDevice.createDescriptorPool(vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 8192, poolSizes));
		set_debug_name(*descriptorPool, name);
	}
	#pragma endregion

	for (const vk::DeviceQueueCreateInfo& info : queueCreateInfos) {
		QueueFamily q = {};
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
		auto ret = ranges::remove_if(commandBuffers, &CommandBuffer::clear_if_done);
		if (!ret.empty()) {
				auto u = ranges::find(ret, 1, &shared_ptr<CommandBuffer>::use_count);
				if (u != ret.end()) commandBuffer = *u;
				commandBuffers.erase(ret.begin(), ret.end());
		}
	}
	if (commandBuffer) {
		commandBuffer->reset(name);
		return commandBuffer;
	} else
		return make_shared<CommandBuffer>(*this, name, queueFamily, level);
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
	commandBuffer->mQueueFamily->mQueues[0].submit(vk::SubmitInfo(waits, waitStages, **commandBuffer, signals), **commandBuffer->mCompletionFence);
	commandBuffer->mState = CommandBuffer::CommandBufferState::eInFlight;
	
	scoped_lock l(mQueueFamilies.m());
	commandBuffer->mQueueFamily->mCommandBuffers.at(this_thread::get_id()).second.emplace_back(commandBuffer);
}
void Device::flush() {
	mDevice.waitIdle();
	auto queueFamilies = mQueueFamilies.lock();
	for (auto& [idx,queueFamily] : *queueFamilies)
		for (auto& [tid,p] : queueFamily.mCommandBuffers)
			for (auto& commandBuffer : p.second)
				commandBuffer->clear_if_done();
}