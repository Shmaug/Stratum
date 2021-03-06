#include "Device.hpp"

#include "Buffer.hpp"
#include "CommandBuffer.hpp"
#include "DescriptorSet.hpp"
#include "Texture.hpp"
#include "Window.hpp"

using namespace stm;

Device::Device(stm::Instance& instance, vk::PhysicalDevice physicalDevice, const unordered_set<string>& deviceExtensions, const vector<const char*>& validationLayers)
	: mPhysicalDevice(physicalDevice), mInstance(instance) {

	mSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)mInstance->getProcAddr("vkSetDebugUtilsObjectNameEXT");

	mMaxMSAASamples = GetMaxUsableSampleCount();

	vector<const char*> deviceExts;
	for (const string& s : deviceExtensions)
		deviceExts.push_back(s.c_str());

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
			queueCreateInfos.push_back(queueCreateInfo);
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
	vk::PhysicalDeviceDescriptorIndexingFeatures indexingFeatures;
	indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
	indexingFeatures.runtimeDescriptorArray = VK_TRUE;
	vk::DeviceCreateInfo createInfo({}, queueCreateInfos, validationLayers, deviceExts, &deviceFeatures);
	createInfo.pNext = &indexingFeatures;
	mDevice = mPhysicalDevice.createDevice(createInfo);
	vk::PhysicalDeviceProperties properties = mPhysicalDevice.getProperties();
	string name = "[" + to_string(properties.deviceID) + "]: " + properties.deviceName.data();
	SetObjectName(mDevice, name);
	mLimits = properties.limits;
	mMemoryProperties = mPhysicalDevice.getMemoryProperties();
	#pragma endregion
	
	#pragma region Create PipelineCache and DesriptorPool
	vk::PipelineCacheCreateInfo cacheInfo = {};
	string tmp;
	if (!mInstance.TryGetOption("noPipelineCache", tmp)) {
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
			fprintf_color(ConsoleColorBits::eYellow, stderr, "Warning: Failed to read pipeline cache: %s\n", e.what());
		}
	}
	mPipelineCache = mDevice.createPipelineCache(cacheInfo);
	if (cacheInfo.pInitialData) delete cacheInfo.pInitialData;
	
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
		SetObjectName(*descriptorPool, name);
	}
	#pragma endregion

	for (const vk::DeviceQueueCreateInfo& info : queueCreateInfos) {
		QueueFamily q = {};
		q.mFamilyIndex = info.queueFamilyIndex;
		q.mProperties = queueFamilyProperties[info.queueFamilyIndex];
		q.mSurfaceSupport = mPhysicalDevice.getSurfaceSupportKHR(info.queueFamilyIndex, mInstance.Window().Surface());
		// TODO: create more queues to utilize more parallelization (if necessary?)
		for (uint32_t i = 0; i < 1; i++) {
			q.mQueues.push_back(mDevice.getQueue(info.queueFamilyIndex, i));
			SetObjectName(q.mQueues[i], "DeviceQueue"+to_string(i));
		}
		mQueueFamilies.lock()->emplace(q.mFamilyIndex, q);
		mQueueFamilyIndices.push_back(q.mFamilyIndex);
	}
}
Device::~Device() {
	Flush();

	auto queueFamilies = mQueueFamilies.lock();
	for (auto& [idx, queueFamily] : *queueFamilies) {
		for (auto&[tid, p] : queueFamily.mCommandBuffers) {
			p.second.clear();
			mDevice.destroyCommandPool(p.first);
		}
	}
	queueFamilies->clear();
	
	mDevice.destroyDescriptorPool(*mDescriptorPool.lock());
	mMemoryPool.lock()->clear();

	string tmp;
	if (!mInstance.TryGetOption("noPipelineCache", tmp)) {
		try {
			auto cacheData = mDevice.getPipelineCacheData(mPipelineCache);
			ofstream output(fs::temp_directory_path() / "stm_pipeline_cache", ios::binary);
			output.write((const char*)cacheData.data(), cacheData.size());
		} catch (exception& e) {
			fprintf_color(ConsoleColorBits::eYellow, stderr, "Warning: Failed to write pipeline cache: %s\n", e.what());
		}
	}
	mDevice.destroyPipelineCache(mPipelineCache);
	mDevice.destroy();
}

Device::Memory::Block Device::Memory::GetBlock(const vk::MemoryRequirements& requirements) {
	// find first chunk of free space that can fit the required size
	vk::DeviceSize allocBegin = 0;
	for (auto[blockStart, blockEnd] : mBlocks) {
		if (allocBegin + requirements.size <= blockStart) break;
		allocBegin = AlignUp(blockEnd, requirements.alignment);
	}
	if (allocBegin + requirements.size > mSize) return Block();
	mBlocks.emplace(allocBegin, allocBegin + requirements.size);
	return Block(*this, allocBegin);
}
void Device::Memory::ReleaseBlock(const Device::Memory::Block& block) { mBlocks.erase(block.mOffset); }

Device::Memory::Block Device::AllocateMemory(const vk::MemoryRequirements& requirements, vk::MemoryPropertyFlags properties, const string& tag) {
	uint32_t memoryTypeIndex = mMemoryProperties.memoryTypeCount;
	uint32_t best = numeric_limits<uint32_t>::max();
	for (uint32_t i = 0; i < mMemoryProperties.memoryTypeCount; i++) {
		// skip invalid memory types
		if ((requirements.memoryTypeBits & (1 << i)) == 0 || (mMemoryProperties.memoryTypes[i].propertyFlags & properties) != properties) continue;

		bitset<32> mask((uint32_t)(mMemoryProperties.memoryTypes[i].propertyFlags ^ properties));
		if (mask.count() < best) {
			best = (uint32_t)mask.count();
			memoryTypeIndex = i;
			if (best == 0) break;
		}
	}
	if (memoryTypeIndex == mMemoryProperties.memoryTypeCount)
		throw invalid_argument("could not find compatible memory type");
	
	auto memoryPool = mMemoryPool.lock();
	for (auto& memory : (*memoryPool)[memoryTypeIndex]) {
		Memory::Block block = memory->GetBlock(requirements);
		if (block.mMemory) return block;
	}
	// Failed to sub-allocate a block, allocate new memory
	return (*memoryPool)[memoryTypeIndex].emplace_back(make_unique<Memory>(*this, memoryTypeIndex, AlignUp(requirements.size, mMemoryBlockSize)))->GetBlock(requirements);
}
void Device::FreeMemory(const Memory::Block& block) {
	auto memoryPool = mMemoryPool.lock();
	block.mMemory->ReleaseBlock(block);
	auto& allocs = (*memoryPool)[block.mMemory->mMemoryTypeIndex];
	// free allocations without any blocks in use
	erase_if(allocs, [](const auto& x){ return x->empty(); });
}

inline Device::QueueFamily* Device::FindQueueFamily(vk::SurfaceKHR surface) {
	auto queueFamilies = mQueueFamilies.lock();
	for (auto& [queueFamilyIndex, queueFamily] : *queueFamilies)
		if (mPhysicalDevice.getSurfaceSupportKHR(queueFamilyIndex, surface))
			return &queueFamily;
	return nullptr;
}

shared_ptr<CommandBuffer> Device::GetCommandBuffer(const string& name, vk::QueueFlags queueFlags, vk::CommandBufferLevel level) {
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
		SetObjectName(commandPool, "CommandPool");
	}

	if (level == vk::CommandBufferLevel::ePrimary)
		for (auto it = commandBuffers.begin(); it != commandBuffers.end(); it++)
			if ((*it)->CheckDone()) {
				shared_ptr<CommandBuffer> commandBuffer = *it;
				commandBuffers.erase(it);
				commandBuffer->Reset(name);
				return commandBuffer;
			}
	return make_shared<CommandBuffer>(*this, name, queueFamily, level);
}
void Device::Execute(shared_ptr<CommandBuffer> commandBuffer, bool pool) {
	ProfilerRegion ps("CommandBuffer::Execute");

	vector<vk::PipelineStageFlags> waitStages;	
	vector<vk::Semaphore> waitSemaphores;	
	vector<vk::Semaphore> signalSemaphores;
	vector<vk::CommandBuffer> commandBuffers { **commandBuffer };
	for (auto& [stage, semaphore] : commandBuffer->mWaitSemaphores) {
		waitStages.push_back(stage);
		waitSemaphores.push_back(*semaphore);
	}
	for (auto& semaphore : commandBuffer->mSignalSemaphores) signalSemaphores.push_back(**semaphore);
	(*commandBuffer)->end();
	commandBuffer->mQueueFamily->mQueues[0].submit({ vk::SubmitInfo(waitSemaphores, waitStages, commandBuffers, signalSemaphores) }, **commandBuffer->mCompletionFence);
	commandBuffer->mState = CommandBuffer::CommandBufferState::eInFlight;

	if (pool) {
		scoped_lock l(mQueueFamilies.m());
		commandBuffer->mQueueFamily->mCommandBuffers.at(this_thread::get_id()).second.emplace_front(commandBuffer);
	}
}
void Device::Flush() {
	mDevice.waitIdle();
	auto queueFamilies = mQueueFamilies.lock();
	for (auto& [idx,queueFamily] : *queueFamilies)
		for (auto& [tid,p] : queueFamily.mCommandBuffers)
			for (auto& commandBuffer : p.second)
				commandBuffer->CheckDone();
}

vk::SampleCountFlagBits Device::GetMaxUsableSampleCount() {
	vk::PhysicalDeviceProperties physicalDeviceProperties;
	mPhysicalDevice.getProperties(&physicalDeviceProperties);

	vk::SampleCountFlags counts = min(physicalDeviceProperties.limits.framebufferColorSampleCounts, physicalDeviceProperties.limits.framebufferDepthSampleCounts);
	if (counts & vk::SampleCountFlagBits::e64) { return vk::SampleCountFlagBits::e64; }
	if (counts & vk::SampleCountFlagBits::e32) { return vk::SampleCountFlagBits::e32; }
	if (counts & vk::SampleCountFlagBits::e16) { return vk::SampleCountFlagBits::e16; }
	if (counts & vk::SampleCountFlagBits::e8) { return vk::SampleCountFlagBits::e8; }
	if (counts & vk::SampleCountFlagBits::e4) { return vk::SampleCountFlagBits::e4; }
	if (counts & vk::SampleCountFlagBits::e2) { return vk::SampleCountFlagBits::e2; }
	return vk::SampleCountFlagBits::e1;
}