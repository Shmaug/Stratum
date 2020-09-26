#include "Device.hpp"

#include <bitset>

#include <Data/Texture.hpp>
#include "Buffer.hpp"
#include "CommandBuffer.hpp"
#include "DescriptorSet.hpp"
#include "Window.hpp"

using namespace std;
using namespace stm;

Device::Device(Instance* instance, vk::PhysicalDevice physicalDevice, uint32_t physicalDeviceIndex, const set<string>& deviceExtensions, vector<const char*> validationLayers)
	: mInstance(instance), mPhysicalDevice(physicalDevice), mPhysicalDeviceIndex(physicalDeviceIndex) {

	mSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)(*mInstance)->getProcAddr("vkSetDebugUtilsObjectNameEXT");

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
	if (!mInstance->GetOptionExists("noPipelineCache")) {
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
		vk::DescriptorPoolSize(vk::DescriptorType::eSampler, 								std::min(1024u, mLimits.maxDescriptorSetSamplers)),
		vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 	std::min(1024u, mLimits.maxDescriptorSetSampledImages)),
    vk::DescriptorPoolSize(vk::DescriptorType::eInputAttachment, 				std::min(1024u, mLimits.maxDescriptorSetInputAttachments)),
		vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, 					std::min(1024u, mLimits.maxDescriptorSetSampledImages)),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 					std::min(1024u, mLimits.maxDescriptorSetStorageImages)),
		vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 					std::min(1024u, mLimits.maxDescriptorSetUniformBuffers)),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, 	std::min(1024u, mLimits.maxDescriptorSetUniformBuffersDynamic)),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 					std::min(1024u, mLimits.maxDescriptorSetStorageBuffers)),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageBufferDynamic, 	std::min(1024u, mLimits.maxDescriptorSetStorageBuffersDynamic))
	};
	mDescriptorPool = mDevice.createDescriptorPool(vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 8192, poolSizes));
	SetObjectName(mDescriptorPool, name);
	#pragma endregion

	for (const vk::DeviceQueueCreateInfo& info : queueCreateInfos) {
		QueueFamily q = {};
		q.mQueue = mDevice.getQueue(info.queueFamilyIndex, 0);
		q.mName = name + "/QueueFamily" + to_string(info.queueFamilyIndex);
		q.mFamilyIndex = info.queueFamilyIndex;
		q.mProperties = queueFamilyProperties[info.queueFamilyIndex];
		SetObjectName(q.mQueue, q.mName);
		mQueueFamilies.emplace(info.queueFamilyIndex, q);
	}

	mDefaultDescriptorSetLayouts.resize(std::max(PER_CAMERA, PER_OBJECT) + 1);
	vk::SamplerCreateInfo shadowSamplerInfo;
	shadowSamplerInfo.addressModeU = shadowSamplerInfo.addressModeV = shadowSamplerInfo.addressModeW = vk::SamplerAddressMode::eClampToBorder;
	shadowSamplerInfo.anisotropyEnable = false;
	shadowSamplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
	shadowSamplerInfo.compareEnable = true;
	shadowSamplerInfo.compareOp = vk::CompareOp::eLess;
	shadowSamplerInfo.magFilter = shadowSamplerInfo.minFilter = vk::Filter::eLinear;
	mDefaultImmutableSamplers.push_back(new Sampler("ShadowSampler", this, shadowSamplerInfo));
	
	vector<vk::DescriptorSetLayoutBinding> bindings {
		{ CAMERA_DATA_BINDING, 					vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, nullptr },
		{ LIGHTING_DATA_BINDING, 				vk::DescriptorType::eInlineUniformBlockEXT, sizeof(LightingBuffer), vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, nullptr },
		{ LIGHTS_BINDING, 							vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, nullptr },
		{ SHADOWS_BINDING, 							vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment, nullptr },
		{ SHADOW_ATLAS_BINDING, 				vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eFragment, nullptr },
		{ ENVIRONMENT_TEXTURE_BINDING, 	vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eFragment, nullptr },
		{ SHADOW_SAMPLER_BINDING, 			vk::DescriptorType::eSampler, 1, vk::ShaderStageFlagBits::eFragment, mDefaultImmutableSamplers[0]->operator->() }
	};
	mDefaultDescriptorSetLayouts[PER_CAMERA] = mDevice.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));
	SetObjectName(mDefaultDescriptorSetLayouts[PER_CAMERA], "PER_CAMERA DescriptorSetLayout");

	bindings = { { INSTANCES_BINDING, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr } };
	mDefaultDescriptorSetLayouts[PER_OBJECT] = mDevice.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));
	SetObjectName(mDefaultDescriptorSetLayouts[PER_OBJECT], "PER_OBJECT DescriptorSetLayout");

	uint8_t whitePixels[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
	uint8_t blackPixels[4] = { 0, 0, 0, 0xFF };
	uint8_t transparentBlackPixels[4] = { 0, 0, 0, 0 };
	uint8_t bumpPixels[4] = { 0x80, 0x80, 0xFF, 0xFF };
	uint8_t noisePixels[4 * 256*256];
	for (uint32_t i = 0; i < 4*256*256; i++) noisePixels[i] = rand() % 0xFF;
	
	hash<string> strh;
	mLoadedAssets.emplace(strh("stm_1x1_white_opaque"), 			new Texture("stm_1x1_white_opaque", 			this, {   1,   1, 1 }, vk::Format::eR8G8B8A8Unorm, whitePixels, 4, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst));
	mLoadedAssets.emplace(strh("stm_1x1_black_opaque"), 			new Texture("stm_1x1_black_opaque", 			this, {   1,   1, 1 }, vk::Format::eR8G8B8A8Unorm, blackPixels, 4, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst));
	mLoadedAssets.emplace(strh("stm_1x1_black_transparent"), 	new Texture("stm_1x1_black_transparent", 	this, {   1,   1, 1 }, vk::Format::eR8G8B8A8Unorm, transparentBlackPixels, 4, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst));
	mLoadedAssets.emplace(strh("stm_1x1_bump"), 							new Texture("stm_1x1_bump", 							this, {   1,   1, 1 }, vk::Format::eR8G8B8A8Unorm, bumpPixels, 4, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst));
	mLoadedAssets.emplace(strh("stm_noise_rgba_256"), 				new Texture("stm_noise_rgba_256", 				this, { 256, 256, 1 }, vk::Format::eR8G8B8A8Unorm, noisePixels, 4*256*256, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst));
}
Device::~Device() {
	Flush();
	PurgeResourcePools(0);

	mLoadedAssets.clear();

	for (auto& [idx,queueFamily] : mQueueFamilies)
		for (auto& [tid, p] : queueFamily.mCommandBuffers) {
			for (CommandBuffer* c : p.second) delete c;
			mDevice.destroyCommandPool(p.first);
		}
	for (auto& s : mDefaultImmutableSamplers) delete s;
	for (auto& l : mDefaultDescriptorSetLayouts) if (l) mDevice.destroyDescriptorSetLayout(l);
	mDevice.destroyDescriptorPool(mDescriptorPool);
	
	mMemoryPool.clear();

	if (!mInstance->GetOptionExists("noPipelineCache")) {
		try {
			vector<uint8_t> cacheData = mDevice.getPipelineCacheData(mPipelineCache);
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
	if (allocBegin + requirements.size > mSize) return Block(nullptr, 0);
	mBlocks.emplace(allocBegin, allocBegin + requirements.size);
	return Block(this, allocBegin);
}
void Device::Memory::ReturnBlock(const Device::Memory::Block& block) { mBlocks.erase(block.mOffset); }

Device::Memory::Block Device::AllocateMemory(const vk::MemoryRequirements& requirements, vk::MemoryPropertyFlags properties, const string& tag) {
	uint32_t memoryTypeIndex = mMemoryProperties.memoryTypeCount;
	uint32_t best = numeric_limits<uint32_t>::max();
	for (uint32_t i = 0; i < mMemoryProperties.memoryTypeCount; i++) {
		// skip invalid heaps
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
	for (Memory* memory : mMemoryPool[memoryTypeIndex])
		if (Memory::Block block = memory->GetBlock(requirements))
			return block;
	// Failed to sub-allocate a block, allocate new memory
	return mMemoryPool[memoryTypeIndex].emplace_back(new Memory(this, memoryTypeIndex, AlignUp(requirements.size, mMemoryBlockSize)))->GetBlock(requirements);
}
void Device::FreeMemory(const Memory::Block& block) {
	lock_guard<mutex> lock(mMemoryMutex);
	block.mMemory->ReturnBlock(block);
	auto& allocs = mMemoryPool[block.mMemory->mMemoryTypeIndex];
	for (auto it = allocs.begin(); it != allocs.end();) {
		if ((*it)->empty()) {
			delete *it;
			it = allocs.erase(it);
		} else
			it++;
	}
}


shared_ptr<Buffer> Device::GetPooledBuffer(const string& name, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryFlags) {
	{
		lock_guard lock(mBufferPool.mMutex);
		auto& pool = mBufferPool.mResources[hash_combine(usage, memoryFlags)];
		auto best = pool.end();
		for (auto it = pool.begin(); it != pool.end(); it++)
			if (it->mResource->Size() >= size && (best == pool.end() || it->mResource->Size() < best->mResource->Size())) {
				best = it;
				if (it->mResource->Size() == size) break;
			}
		if (best != pool.end()) {
			auto b = best->mResource;
			pool.erase(best);
			return b;
		}
	}
	return make_shared<Buffer>(name, this, size, usage, memoryFlags);
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
	return make_shared<DescriptorSet>(name, this, layout);
}
shared_ptr<Texture> Device::GetPooledTexture(const string& name, const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels, vk::SampleCountFlagBits sampleCount, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags memoryProperties) {
	{
		lock_guard lock(mTexturePool.mMutex);
		auto& pool = mTexturePool.mResources[hash_combine(extent, format, mipLevels, sampleCount)];
		for (auto it = pool.begin(); it != pool.end(); it++)
			if ((it->mResource->Usage() & usage) && (it->mResource->MemoryProperties() & memoryProperties)) {
				auto tex = it->mResource;
				pool.erase(it);
				return tex;
			}
	}
	return make_shared<Texture>(name, this, extent, format, nullptr, 0, usage, mipLevels, sampleCount, memoryProperties);
}
void Device::PoolResource(shared_ptr<Buffer> buffer) {
	lock_guard lock(mBufferPool.mMutex);
	mBufferPool.mResources[hash_combine(buffer->Usage(), buffer->MemoryProperties())].push_back({ buffer, mFrameCount });
}
void Device::PoolResource(shared_ptr<DescriptorSet> descriptorSet) {
	lock_guard lock(mDescriptorSetPool.mMutex);
	mDescriptorSetPool.mResources[descriptorSet->Layout()].push_back({ descriptorSet, mFrameCount });
}
void Device::PoolResource(shared_ptr<Texture> texture) {
	lock_guard lock(mTexturePool.mMutex);
	mTexturePool.mResources[hash_combine(texture->Extent(), texture->Format(), texture->MipLevels(), texture->SampleCount())].push_back({ texture, mFrameCount });
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

CommandBuffer* Device::GetCommandBuffer(const std::string& name, vk::QueueFlags queueFlags, vk::CommandBufferLevel level) {
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
	return new CommandBuffer(name, this, queueFamily, level);
}
void Device::Execute(CommandBuffer* commandBuffer, bool wait) {
	ProfileRegion ps("CommandBuffer::Execute");

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
	commandBuffer->mQueueFamily->mQueue.submit({ vk::SubmitInfo(waitSemaphores, waitStages, commandBuffers, signalSemaphores) }, **commandBuffer->mSignalFence);
	commandBuffer->mState = CommandBuffer::CommandBufferState::eInFlight;

	lock_guard lock(mQueueMutex);
	commandBuffer->mQueueFamily->mCommandBuffers.at(this_thread::get_id()).second.push_back(commandBuffer);
	if (wait) {
		mDevice.waitForFences({ **commandBuffer->mSignalFence }, true, numeric_limits<uint64_t>::max());
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

	vk::SampleCountFlags counts = std::min(physicalDeviceProperties.limits.framebufferColorSampleCounts, physicalDeviceProperties.limits.framebufferDepthSampleCounts);
	if (counts & vk::SampleCountFlagBits::e64) { return vk::SampleCountFlagBits::e64; }
	if (counts & vk::SampleCountFlagBits::e32) { return vk::SampleCountFlagBits::e32; }
	if (counts & vk::SampleCountFlagBits::e16) { return vk::SampleCountFlagBits::e16; }
	if (counts & vk::SampleCountFlagBits::e8) { return vk::SampleCountFlagBits::e8; }
	if (counts & vk::SampleCountFlagBits::e4) { return vk::SampleCountFlagBits::e4; }
	if (counts & vk::SampleCountFlagBits::e2) { return vk::SampleCountFlagBits::e2; }
	return vk::SampleCountFlagBits::e1;
}