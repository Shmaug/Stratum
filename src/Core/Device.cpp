#include "Device.hpp"

#include <bitset>

#include "Buffer.hpp"
#include "CommandBuffer.hpp"
#include "DescriptorSet.hpp"
#include "Window.hpp"

#include "Asset/Texture.hpp"


using namespace stm;

Device::Device(vk::PhysicalDevice physicalDevice, uint32_t physicalDeviceIndex, stm::Instance& instance, const set<string>& deviceExtensions, vector<const char*> validationLayers)
	: mPhysicalDevice(physicalDevice), mPhysicalDeviceIndex(physicalDeviceIndex), mInstance(instance) {

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
	mDescriptorPool = mDevice.createDescriptorPool(vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 8192, poolSizes));
	SetObjectName(mDescriptorPool, name);
	#pragma endregion

	for (const vk::DeviceQueueCreateInfo& info : queueCreateInfos) {
		stm::QueueFamily q = {};
		q.mName = name + "/QueueFamily" + to_string(info.queueFamilyIndex);
		q.mFamilyIndex = info.queueFamilyIndex;
		q.mProperties = queueFamilyProperties[info.queueFamilyIndex];
		q.mSurfaceSupport = mPhysicalDevice.getSurfaceSupportKHR(info.queueFamilyIndex, mInstance.Window().Surface());
		// TODO: create more queues to utilize more parallelization (if necessary?)
		for (uint32_t i = 0; i < 1; i++) {
			q.mQueues.push_back(mDevice.getQueue(info.queueFamilyIndex, i));
			SetObjectName(q.mQueues[i], q.mName+"/Queue"+to_string(i));
		}
		mQueueFamilies.emplace(q.mFamilyIndex, q);
		mQueueFamilyIndices.push_back(q.mFamilyIndex);
	}

	array<uint8_t,4> whitePixels { 0xFF, 0xFF, 0xFF, 0xFF };
	array<uint8_t,4> blackPixels { 0, 0, 0, 0xFF };
	array<uint8_t,4> zeroPixels { 0, 0, 0, 0 };
	array<uint8_t,4> bumpPixels { 0x80, 0x80, 0xFF, 0xFF };
	array<uint8_t,4*256*256> noisePixels;
	for (auto& pixel : noisePixels) pixel = rand() % 0xFF;
	
	mLoadedAssets.emplace(basic_hash("stm_1x1_white_opaque"), 		 make_shared<Texture>("stm_1x1_white_opaque", 		  *this, vk::Extent3D(  1,   1, 1), vk::Format::eR8G8B8A8Unorm, whitePixels, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage));
	mLoadedAssets.emplace(basic_hash("stm_1x1_black_opaque"), 		 make_shared<Texture>("stm_1x1_black_opaque", 		  *this, vk::Extent3D(  1,   1, 1), vk::Format::eR8G8B8A8Unorm, blackPixels, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage));
	mLoadedAssets.emplace(basic_hash("stm_1x1_black_transparent"), make_shared<Texture>("stm_1x1_black_transparent",  *this, vk::Extent3D(  1,   1, 1), vk::Format::eR8G8B8A8Unorm, zeroPixels,  vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage));
	mLoadedAssets.emplace(basic_hash("stm_1x1_bump"), 						 make_shared<Texture>("stm_1x1_bump", 						  *this, vk::Extent3D(  1,   1, 1), vk::Format::eR8G8B8A8Unorm, bumpPixels,  vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage));
	mLoadedAssets.emplace(basic_hash("stm_noise_rgba_256"), 			 make_shared<Texture>("stm_noise_rgba_256", 			  *this, vk::Extent3D(256, 256, 1), vk::Format::eR8G8B8A8Unorm, noisePixels, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc));
}
Device::~Device() {
	Flush();

	UnloadAssets();
	Flush();
	
	PurgeResourcePools(0);

	for (auto& [idx,queueFamily] : mQueueFamilies)
		for (auto& [tid, p] : queueFamily.mCommandBuffers) {
			for (CommandBuffer* c : p.second) delete c;
			mDevice.destroyCommandPool(p.first);
		}

	mDevice.destroyDescriptorPool(mDescriptorPool);
	
	mMemoryPool.clear();

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
	// find first bit of free space that can fit the required size
	vk::DeviceSize allocBegin = 0;
	for (auto[blockStart, blockEnd] : mBlocks) {
		if (allocBegin + requirements.size <= blockStart) break;
		allocBegin = AlignUp(blockEnd, requirements.alignment);
	}
	if (allocBegin + requirements.size > mSize) return Block();
	mBlocks.emplace(allocBegin, allocBegin + requirements.size);
	return Block(*this, allocBegin);
}
void Device::Memory::ReturnBlock(const Device::Memory::Block& block) { mBlocks.erase(block.mOffset); }

Device::Memory::Block Device::AllocateMemory(const vk::MemoryRequirements& requirements, vk::MemoryPropertyFlags properties, const string& tag) {
	uint32_t memoryTypeIndex = mMemoryProperties.memoryTypeCount;
	uint32_t best = numeric_limits<uint32_t>::max();
	for (uint32_t i = 0; i < mMemoryProperties.memoryTypeCount; i++) {
		// skip invalid memory types
		if ((requirements.memoryTypeBits & (1 << i)) == 0 || (mMemoryProperties.memoryTypes[i].propertyFlags & properties) != properties) continue;

		bitset<32> mask = (uint32_t)(mMemoryProperties.memoryTypes[i].propertyFlags ^ properties);
		if (mask.count() < best) {
			best = (uint32_t)mask.count();
			memoryTypeIndex = i;
			if (best == 0) break;
		}
	}
	if (memoryTypeIndex == mMemoryProperties.memoryTypeCount)
		throw invalid_argument("could not find compatible memory type");
	
	lock_guard<mutex> lock(mMemoryMutex);
	for (auto& memory : mMemoryPool[memoryTypeIndex]) {
		Memory::Block block = memory->GetBlock(requirements);
		if (block.mMemory) return block;
	}
	// Failed to sub-allocate a block, allocate new memory
	return mMemoryPool[memoryTypeIndex].emplace_back(make_unique<Memory>(*this, memoryTypeIndex, AlignUp(requirements.size, mMemoryBlockSize)))->GetBlock(requirements);
}
void Device::FreeMemory(const Memory::Block& block) {
	lock_guard<mutex> lock(mMemoryMutex);
	block.mMemory->ReturnBlock(block);
	auto& allocs = mMemoryPool[block.mMemory->mMemoryTypeIndex];
	// free allocations without any blocks in use
	ranges::remove_if(allocs, [](const auto& x){ x->empty(); });
}

inline QueueFamily* Device::FindQueueFamily(vk::SurfaceKHR surface) {
	for (auto& [queueFamilyIndex, queueFamily] : mQueueFamilies)
		if (mPhysicalDevice.getSurfaceSupportKHR(queueFamilyIndex, surface))
			return &queueFamily;
	return nullptr;
}

shared_ptr<Buffer> Device::GetPooledBuffer(const string& name, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryFlags) {
	{
		lock_guard lock(mBufferPool.mMutex);
		auto& pool = mBufferPool.mResources[basic_hash(usage, memoryFlags)];
		auto best = pool.end();
		best = ranges::(pool, []() { return it->mResource->Size() >= size && (best == pool.end() || it->mResource->Size() < best->mResource->Size()) } );
		if (best != pool.end()) {
			auto b = best->mResource;
			pool.erase(best);
			return b;
		}
	}
	return make_shared<Buffer>(name, *this, size, usage, memoryFlags);
}
shared_ptr<DescriptorSet> Device::GetPooledDescriptorSet(const string& name, vk::DescriptorSetLayout layout) {
	{
		lock_guard lock(mDescriptorSetPool.mMutex);
		auto& sets = mDescriptorSetPool.mResources[layout];
		if (sets.size()) {
			auto ds = sets.front().mResource;
			sets.pop_front();
			ds->mPendingWrites.clear();
			return ds;
		}
	}
	return make_shared<DescriptorSet>(name, *this, layout);
}
shared_ptr<Texture> Device::GetPooledTexture(const string& name, const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels, vk::SampleCountFlagBits sampleCount, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags memoryProperties) {
	{
		lock_guard lock(mTexturePool.mMutex);
		auto& pool = mTexturePool.mResources[basic_hash(extent, format, mipLevels, sampleCount)];
		auto it = ranges::find(pool, [](auto i){ return (i.mResource->Usage() & usage) && (i.mResource->MemoryProperties() & memoryProperties) });
		if (it != pool.end()) {
			auto tex = it->mResource;
			pool.erase(it);
			return tex;
		}
	}
	return make_shared<Texture>(name, *this, extent, format, byte_blob(), usage, mipLevels, sampleCount, memoryProperties);
}
void Device::PoolResource(shared_ptr<Buffer> buffer) {
	lock_guard lock(mBufferPool.mMutex);
	mBufferPool.mResources[basic_hash(buffer->Usage(), buffer->MemoryProperties())].push_back({ buffer, mFrameCount });
}
void Device::PoolResource(shared_ptr<DescriptorSet> descriptorSet) {
	lock_guard lock(mDescriptorSetPool.mMutex);
	mDescriptorSetPool.mResources[descriptorSet->Layout()].push_back({ descriptorSet, mFrameCount });
}
void Device::PoolResource(shared_ptr<Texture> texture) {
	lock_guard lock(mTexturePool.mMutex);
	mTexturePool.mResources[basic_hash(texture->Extent(), texture->Format(), texture->MipLevels(), texture->SampleCount())].push_back({ texture, mFrameCount });
}
void Device::PurgeResourcePools(uint32_t maxAge) {
	mDescriptorSetPool.EraseOld(mFrameCount, maxAge);
	mBufferPool.EraseOld(mFrameCount, maxAge);
	mTexturePool.EraseOld(mFrameCount, maxAge);
	lock_guard lock(mQueueMutex);
	for (auto& [idx,queueFamily] : mQueueFamilies)
		for (auto& [tid,p] : queueFamily.mCommandBuffers)
			for (auto& commandBuffer : p.second)
				commandBuffer->CheckDone();
}

CommandBuffer* Device::GetCommandBuffer(const string& name, vk::QueueFlags queueFlags, vk::CommandBufferLevel level) {
	// find most specific queue family
	QueueFamily* queueFamily = nullptr;
	for (auto& [i,q] : mQueueFamilies)
		if (q.mProperties.queueFlags & queueFlags)
			queueFamily = &q;

	if (queueFamily == nullptr) throw invalid_argument("invalid queueFlags " + to_string(queueFlags));

	lock_guard lock(mQueueMutex);
	auto& [commandPool,commandBuffers] = queueFamily->mCommandBuffers[this_thread::get_id()];
	if (!commandPool) {
		commandPool = mDevice.createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamily->mFamilyIndex));
		SetObjectName(commandPool, name + "/CommandPool");
	}

	if (level == vk::CommandBufferLevel::ePrimary)
		for (auto it = commandBuffers.begin(); it != commandBuffers.end(); it++)
			if ((*it)->CheckDone()) {
				CommandBuffer* commandBuffer = *it;
				commandBuffers.erase(it);
				commandBuffer->Reset(name);
				return commandBuffer;
			}
	return new CommandBuffer(name, *this, queueFamily, level);
}
void Device::Execute(CommandBuffer* commandBuffer, bool wait) {
	ProfilerRegion ps("CommandBuffer::Execute");

	vector<vk::PipelineStageFlags> waitStages;	
	vector<vk::Semaphore> waitSemaphores;	
	vector<vk::Semaphore> signalSemaphores;
	vector<vk::CommandBuffer> commandBuffers { commandBuffer->mCommandBuffer };
	for (auto& [stage, semaphore] : commandBuffer->mWaitSemaphores) {
		waitStages.push_back(stage);
		waitSemaphores.push_back(**semaphore);
	}
	for (auto& semaphore : commandBuffer->mSignalSemaphores) signalSemaphores.push_back(**semaphore);
	(*commandBuffer)->end();
	commandBuffer->mQueueFamily->mQueues[0].submit({ vk::SubmitInfo(waitSemaphores, waitStages, commandBuffers, signalSemaphores) }, **commandBuffer->mSignalFence);
	commandBuffer->mState = CommandBuffer::CommandBufferState::eInFlight;

	lock_guard lock(mQueueMutex);
	commandBuffer->mQueueFamily->mCommandBuffers.at(this_thread::get_id()).second.push_back(commandBuffer);
	if (wait) {
		auto result = mDevice.waitForFences({ **commandBuffer->mSignalFence }, true, numeric_limits<uint64_t>::max());
		commandBuffer->mState = CommandBuffer::CommandBufferState::eDone;
		commandBuffer->Clear();
	}
}
void Device::Flush() {
	mDevice.waitIdle();
	lock_guard lock(mQueueMutex);
	for (auto& [idx,queueFamily] : mQueueFamilies)
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