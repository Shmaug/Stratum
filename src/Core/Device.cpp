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

shared_ptr<Device::Memory::Block> Device::Memory::AllocateBlock(const vk::MemoryRequirements& requirements) {
	if (mUnallocated.empty()) return nullptr;
	
	vk::DeviceSize blockSize = 0;
	vk::DeviceSize memLocation = 0;
	vk::DeviceSize memSize = 0;

	// find smallest unallocated block that can fit the requirements
	auto block = mUnallocated.end();
	for (auto it = mUnallocated.begin(); it != mUnallocated.end(); it++) {
		vk::DeviceSize offset = it->first ? AlignUp(it->first, requirements.alignment) : 0;
		vk::DeviceSize blockEnd = AlignUp(offset + requirements.size, Device::mMemoryBlockSize);

		if (blockEnd > it->first + it->second) continue;

		if (block == mUnallocated.end() || it->second < block->second) {
			memLocation = offset;
			memSize = blockEnd - offset;
			blockSize = blockEnd - it->first;
			block = it;
		}
	}
	if (block == mUnallocated.end()) return nullptr;

	if (block->second > blockSize) {
		// still room left after this allocation, shift this block over
		block->first += blockSize;
		block->second -= blockSize;
	} else
		mUnallocated.erase(block);

	return shared_ptr<Block>(new Block(*this, memLocation, memSize));
}
Device::Memory::Block::~Block() {
	auto memoryPool = mMemory.mDevice.mMemoryPool.lock();

	vk::DeviceSize end = mOffset + mSize;

	auto firstAfter = mMemory.mUnallocated.end();
	auto startBlock = mMemory.mUnallocated.end();
	auto endBlock = mMemory.mUnallocated.end();

	for (auto it = mMemory.mUnallocated.begin(); it != mMemory.mUnallocated.end(); it++) {
		if (it->first > mOffset && (firstAfter == mMemory.mUnallocated.end() || it->first < firstAfter->first)) firstAfter = it;

		if (it->first == end)
			endBlock = it;
		if (it->first + it->second == mOffset)
			startBlock = it;
	}

	//if (startBlock == endBlock && startBlock != mMemory.mUnallocated.end()) throw; // this should NOT happen

	// merge blocks

	if (startBlock == mMemory.mUnallocated.end() && endBlock == mMemory.mUnallocated.end())
		// block isn't adjacent to any other blocks
		mMemory.mUnallocated.insert(firstAfter, make_pair(mOffset, mSize));
	else if (startBlock == mMemory.mUnallocated.end()) {
		//  --------     |---- allocation ----|---- endBlock ----|
		endBlock->first = mOffset;
		endBlock->second += mSize;
	} else if (endBlock == mMemory.mUnallocated.end()) {
		//  |---- startBlock ----|---- allocation ----|     --------
		startBlock->second += mSize;
	} else {
		//  |---- startBlock ----|---- allocation ----|---- endBlock ----|
		startBlock->second += mSize + endBlock->second;
		mMemory.mUnallocated.erase(endBlock);
	}
}

shared_ptr<Device::Memory::Block> Device::AllocateMemory(const vk::MemoryRequirements& requirements, vk::MemoryPropertyFlags properties) {
	uint32_t memoryTypeIndex = mMemoryProperties.memoryTypeCount;
	for (uint32_t i = 0; i < mMemoryProperties.memoryTypeCount; i++)
		if ((requirements.memoryTypeBits & (1 << i)) && (mMemoryProperties.memoryTypes[i].propertyFlags & properties)) {
			memoryTypeIndex = i;
			break;
	}
	if (memoryTypeIndex == mMemoryProperties.memoryTypeCount)
		throw invalid_argument("could not find compatible memory type");
	
	auto memoryPool = mMemoryPool.lock();
	for (auto& memory : (*memoryPool)[memoryTypeIndex])
		if (auto block = memory->AllocateBlock(requirements))
			return block;
	// Failed to sub-allocate a block, allocate new memory
	return (*memoryPool)[memoryTypeIndex].emplace_back(make_unique<Memory>(*this, memoryTypeIndex, AlignUp(requirements.size, mMemoryMinAlloc)))->AllocateBlock(requirements);
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

	shared_ptr<CommandBuffer> commandBuffer;
	if (level == vk::CommandBufferLevel::ePrimary) {
		auto ret = ranges::remove_if(commandBuffers, &CommandBuffer::CheckDone);
		if (!ret.empty()) {
				auto u = ranges::find(ret, 1, &shared_ptr<CommandBuffer>::use_count);
				if (u != ret.end()) commandBuffer = *u;
				commandBuffers.erase(ret.begin(), ret.end());
		}
	}
	if (commandBuffer) {
		commandBuffer->Reset(name);
		return commandBuffer;
	} else
		return make_shared<CommandBuffer>(*this, name, queueFamily, level);
}
void Device::Execute(shared_ptr<CommandBuffer> commandBuffer) {
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
	
	scoped_lock l(mQueueFamilies.m());
	commandBuffer->mQueueFamily->mCommandBuffers.at(this_thread::get_id()).second.emplace_back(commandBuffer);
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