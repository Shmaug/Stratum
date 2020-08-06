#include <Core/Device.hpp>
#include <Data/AssetManager.hpp>
#include <Data/Texture.hpp>
#include <Core/Buffer.hpp>
#include <Core/CommandBuffer.hpp>
#include <Core/DescriptorSet.hpp>
#include <Core/Window.hpp>
#include <Util/Profiler.hpp>

//#define PRINT_VK_ALLOCATIONS

// 4kb blocks
#define MEM_BLOCK_SIZE (4*1024)
// 4mb min allocation
#define MEM_MIN_ALLOC (4*1024*1024)

using namespace std;

inline size_t HashTextureData(VkExtent3D extent, VkFormat format, uint32_t mipLevels, VkSampleCountFlagBits sampleCount) {
	size_t value = 0;
	hash_combine(value, extent.width);
	hash_combine(value, extent.height);
	hash_combine(value, extent.depth);
	hash_combine(value, format);
	hash_combine(value, mipLevels);
	hash_combine(value, sampleCount);
	return value;
}
inline size_t HashTextureData(const Texture* tex) { return HashTextureData(tex->Extent(), tex->Format(), tex->MipLevels(), tex->SampleCount()); }


Device::Device(::Instance* instance, VkPhysicalDevice physicalDevice, uint32_t physicalDeviceIndex, uint32_t graphicsQueueFamily, uint32_t presentQueueFamily, const set<string>& deviceExtensions, vector<const char*> validationLayers)
	: mInstance(instance), mGraphicsQueueFamilyIndex(graphicsQueueFamily), mPresentQueueFamilyIndex(presentQueueFamily),
	mCommandBufferCount(0), mDescriptorSetCount(0), mMemoryAllocationCount(0), mMemoryUsage(0), mFrameCount(0) {

	#ifdef ENABLE_DEBUG_LAYERS
	SetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(*instance, "vkSetDebugUtilsObjectNameEXT");
	CmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(*instance, "vkCmdBeginDebugUtilsLabelEXT");
	CmdEndDebugUtilsLabelEXT   = (PFN_vkCmdEndDebugUtilsLabelEXT)  vkGetInstanceProcAddr(*instance, "vkCmdEndDebugUtilsLabelEXT");
	#endif

	mPhysicalDevice = physicalDevice;
	mMaxMSAASamples = GetMaxUsableSampleCount();
	mPhysicalDeviceIndex = physicalDeviceIndex;

	vector<const char*> deviceExts;
	for (const string& s : deviceExtensions)
		deviceExts.push_back(s.c_str());

	#pragma region get queue info
	set<uint32_t> uniqueQueueFamilies{ mGraphicsQueueFamilyIndex, mPresentQueueFamilyIndex };
	vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	float queuePriority = 1.0f;
	for (uint32_t queueFamily : uniqueQueueFamilies) {
		VkDeviceQueueCreateInfo queueCreateInfo = {};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;
		queueCreateInfos.push_back(queueCreateInfo);
	}
	#pragma endregion

	#pragma region create logical device and queues
	VkPhysicalDeviceFeatures deviceFeatures = {};
	deviceFeatures.fillModeNonSolid = VK_TRUE;
	deviceFeatures.sparseBinding = VK_TRUE;
	deviceFeatures.samplerAnisotropy = VK_TRUE;
	deviceFeatures.shaderImageGatherExtended = VK_TRUE;
	deviceFeatures.shaderStorageImageExtendedFormats = VK_TRUE;
	deviceFeatures.wideLines = VK_TRUE;
	deviceFeatures.largePoints = VK_TRUE;
	deviceFeatures.sampleRateShading = VK_TRUE;

	VkPhysicalDeviceDescriptorIndexingFeaturesEXT indexingFeatures = {};
	indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
	indexingFeatures.pNext = nullptr;
	indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
	indexingFeatures.runtimeDescriptorArray = VK_TRUE;

	VkDeviceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
	createInfo.pQueueCreateInfos = queueCreateInfos.data();
	createInfo.pEnabledFeatures = &deviceFeatures;
	createInfo.enabledExtensionCount = (uint32_t)deviceExts.size();
	createInfo.ppEnabledExtensionNames = deviceExts.data();
	createInfo.enabledLayerCount = (uint32_t)validationLayers.size();
	createInfo.ppEnabledLayerNames = validationLayers.data();
	createInfo.pNext = &indexingFeatures;
	ThrowIfFailed(vkCreateDevice(mPhysicalDevice, &createInfo, nullptr, &mDevice), "vkCreateDevice failed");

	VkPhysicalDeviceProperties properties = {};
	vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
	string name = "Device " + to_string(properties.deviceID) + ": " + properties.deviceName;
	SetObjectName(mDevice, name, VK_OBJECT_TYPE_DEVICE);
	mLimits = properties.limits;

	mGraphicsQueueIndex = 0;
	mPresentQueueIndex = 0;

	vkGetDeviceQueue(mDevice, mGraphicsQueueFamilyIndex, mGraphicsQueueIndex, &mGraphicsQueue);
	vkGetDeviceQueue(mDevice, mPresentQueueFamilyIndex, mPresentQueueIndex, &mPresentQueue);
	SetObjectName(mGraphicsQueue, name + " Graphics Queue", VK_OBJECT_TYPE_QUEUE);
	SetObjectName(mPresentQueue, name + " Present Queue", VK_OBJECT_TYPE_QUEUE);
	#pragma endregion

	#pragma region PipelineCache and DesriptorPool
	char* cacheData = nullptr;
	ifstream cacheFile("./pcache", ios::binary | ios::ate);

	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	if (cacheFile.is_open()) {
		size_t size = cacheFile.tellg();
		cacheData = new char[size];
		cacheFile.seekg(0, ios::beg);
		cacheFile.read(cacheData, size);
		
		cacheInfo.pInitialData = cacheData;
		cacheInfo.initialDataSize = size;
	}
	vkCreatePipelineCache(mDevice, &cacheInfo, nullptr, &mPipelineCache);
	safe_delete_array(cacheData);
	
	VkDescriptorPoolSize type_count[5] {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,			min(4096u, mLimits.maxDescriptorSetUniformBuffers) },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	min(4096u, mLimits.maxDescriptorSetSampledImages) },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,				min(4096u, mLimits.maxDescriptorSetSampledImages) },
		{ VK_DESCRIPTOR_TYPE_SAMPLER,					min(4096u, mLimits.maxDescriptorSetSamplers) },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,			min(4096u, mLimits.maxDescriptorSetStorageBuffers) },
	};

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.poolSizeCount = 5;
	poolInfo.pPoolSizes = type_count;
	poolInfo.maxSets = 8192;

	ThrowIfFailed(vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &mDescriptorPool), "vkCreateDescriptorPool failed");
	SetObjectName(mDescriptorPool, name, VK_OBJECT_TYPE_DESCRIPTOR_POOL);
	#pragma endregion

	vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &mMemoryProperties);

	mAssetManager = new ::AssetManager(this);
	
	vector<VkDescriptorSetLayoutBinding> bindings(6);
	bindings[0] = { CAMERA_BUFFER_BINDING, 				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
	bindings[1] = { LIGHT_BUFFER_BINDING, 				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
	bindings[2] = { SHADOW_BUFFER_BINDING, 				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
	bindings[3] = { SHADOW_ATLAS_BINDING, 				VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
	bindings[4] = { ENVIRONMENT_TEXTURE_BINDING, 	VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
	bindings[5] = { SHADOW_SAMPLER_BINDING, 			VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = (uint32_t)bindings.size(); 
	layoutInfo.pBindings = bindings.data(); 
	vkCreateDescriptorSetLayout(mDevice, &layoutInfo, nullptr, &mCameraSetLayout);
	SetObjectName(mCameraSetLayout, "PER_CAMERA DescriptorSetLayout", VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT);

	bindings.resize(1);
	bindings[0] = { INSTANCE_BUFFER_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr };
	layoutInfo.bindingCount = (uint32_t)bindings.size(); 
	layoutInfo.pBindings = bindings.data(); 
	vkCreateDescriptorSetLayout(mDevice, &layoutInfo, nullptr, &mObjectSetLayout);
	SetObjectName(mObjectSetLayout, "PER_OBJECT DescriptorSetLayout", VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT);
}
Device::~Device() {
	Flush();

	for (auto& b : mBufferPool) safe_delete(b.mResource);
	for (auto& kp : mTexturePool)
		for (auto& t : kp.second)
			safe_delete(t.mResource);
	for (auto& kp : mDescriptorSetPool)
		for (auto& ds : kp.second)
			safe_delete(ds.mResource);
			
	delete mAssetManager;

	vkDestroyDescriptorSetLayout(mDevice, mCameraSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(mDevice, mObjectSetLayout, nullptr);
	
	size_t size = 0;
	vkGetPipelineCacheData(mDevice, mPipelineCache, &size, nullptr);
	char* cacheData = new char[size];
	vkGetPipelineCacheData(mDevice, mPipelineCache, &size, cacheData);
	ofstream output("./pcache", ios::binary);
	output.write(cacheData, size);
	delete[] cacheData;

	vkDestroyPipelineCache(mDevice, mPipelineCache, nullptr);
	vkDestroyDescriptorPool(mDevice, mDescriptorPool, nullptr);
	for (auto& p : mCommandPools)
		vkDestroyCommandPool(mDevice, p.second, nullptr);
	
	for (auto kp : mMemoryAllocations)
		for (uint32_t i = 0; i < kp.second.size(); i++) {
			for (auto it = kp.second[i].mAllocations.begin(); it != kp.second[i].mAllocations.begin(); it++)
				fprintf_color(COLOR_RED, stderr, "Device memory leak detected [%s]\n", it->mTag.c_str());
			vkFreeMemory(mDevice, kp.second[i].mMemory, nullptr);
		}

	vkDestroyDevice(mDevice, nullptr);
}

bool Device::Allocation::SubAllocate(const VkMemoryRequirements& requirements, DeviceMemoryAllocation& allocation, const string& tag) {
if (mAvailable.empty()) return false;

	VkDeviceSize blockSize = 0;
	VkDeviceSize memLocation = 0;
	VkDeviceSize memSize = 0;

	// find smallest block that can fit the allocation
	auto block = mAvailable.end();
	for (auto it = mAvailable.begin(); it != mAvailable.end(); it++) {
		VkDeviceSize offset = it->first ? AlignUp(it->first, requirements.alignment) : 0;
		VkDeviceSize blockEnd = AlignUp(offset + requirements.size, MEM_BLOCK_SIZE);

		if (blockEnd > it->first + it->second) continue;

		if (block == mAvailable.end() || it->second < block->second) {
			memLocation = offset;
			memSize = blockEnd - offset;
			blockSize = blockEnd - it->first;
			block = it;
		}
	}
	if (block == mAvailable.end()) return false;

	allocation.mDeviceMemory = mMemory;
	allocation.mOffset = memLocation;
	allocation.mSize = memSize;
	allocation.mMapped = ((uint8_t*)mMapped) + memLocation;
	allocation.mTag = tag;

	if (block->second > blockSize) {
		// still room left after this allocation, shift this block over
		block->first += blockSize;
		block->second -= blockSize;
	} else
		mAvailable.erase(block);

	mAllocations.push_front(allocation);

	return true;
}
void Device::Allocation::Deallocate(const DeviceMemoryAllocation& allocation) {
	if (allocation.mDeviceMemory != mMemory) return;

	for (auto it = mAllocations.begin(); it != mAllocations.end(); it++)
		if (it->mOffset == allocation.mOffset) {
			mAllocations.erase(it);
			break;
		}

	VkDeviceSize end = allocation.mOffset + allocation.mSize;

	auto firstAfter = mAvailable.end();
	auto startBlock = mAvailable.end();
	auto endBlock = mAvailable.end();

	for (auto it = mAvailable.begin(); it != mAvailable.end(); it++) {
		if (it->first > allocation.mOffset && (firstAfter == mAvailable.end() || it->first < firstAfter->first)) firstAfter = it;

		if (it->first == end)
			endBlock = it;
		if (it->first + it->second == allocation.mOffset)
			startBlock = it;
	}

	if (startBlock == endBlock && startBlock != mAvailable.end()) throw; // this should NOT happen

	// merge blocks

	if (startBlock == mAvailable.end() && endBlock == mAvailable.end())
		// block isn't adjacent to any other blocks
		mAvailable.insert(firstAfter, make_pair(allocation.mOffset, allocation.mSize));
	else if (startBlock == mAvailable.end()) {
		//  --------     |---- allocation ----|---- endBlock ----|
		endBlock->first = allocation.mOffset;
		endBlock->second += allocation.mSize;
	} else if (endBlock == mAvailable.end()) {
		//  |---- startBlock ----|---- allocation ----|     --------
		startBlock->second += allocation.mSize;
	} else {
		//  |---- startBlock ----|---- allocation ----|---- endBlock ----|
		startBlock->second += allocation.mSize + endBlock->second;
		mAvailable.erase(endBlock);
	}
}

DeviceMemoryAllocation Device::AllocateMemory(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags properties, const string& tag) {
	lock_guard<mutex> lock(mMemoryMutex);

	int32_t memoryType = -1;
	for (uint32_t i = 0; i < mMemoryProperties.memoryTypeCount; i++) {
		if ((requirements.memoryTypeBits & (1 << i)) && ((mMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)) {
			memoryType = i;
			break;
		}
	}
	if (memoryType == -1) {
		fprintf_color(COLOR_RED, stderr, "%s", "Failed to find suitable memory type!");
		throw;
	}

	DeviceMemoryAllocation alloc = {};
	alloc.mMemoryType = memoryType;

	vector<Allocation>& allocations = mMemoryAllocations[memoryType];

	for (uint32_t i = 0; i < allocations.size(); i++)
		if (allocations[i].SubAllocate(requirements, alloc, tag))
			return alloc;


	// Failed to sub-allocate, make a new allocation

	allocations.push_back({});
	Allocation& allocation = allocations.back();
	
	VkMemoryAllocateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	info.memoryTypeIndex = memoryType;
	info.allocationSize = max((uint64_t)MEM_MIN_ALLOC, AlignUp(requirements.size, MEM_BLOCK_SIZE));
	if (VkResult err = vkAllocateMemory(mDevice, &info, nullptr, &allocation.mMemory)) {
		VkDeviceSize deviceMemSize = 0;
		for (uint32_t i = 0; i < mMemoryProperties.memoryHeapCount; i++)
			if (mMemoryProperties.memoryHeaps[i].flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
				deviceMemSize += mMemoryProperties.memoryHeaps[i].size;

		switch (err) {
		case VK_ERROR_OUT_OF_HOST_MEMORY:
			fprintf_color(COLOR_RED, stderr, "vkAllocateMemory failed: VK_ERROR_OUT_OF_HOST_MEMORY\n");
			throw;
		case VK_ERROR_OUT_OF_DEVICE_MEMORY:
			fprintf_color(COLOR_RED, stderr, "vkAllocateMemory failed: VK_ERROR_OUT_OF_DEVICE_MEMORY (%.3f / %.3f)\n", (mMemoryUsage + info.allocationSize) / (1024.f * 1024.f), deviceMemSize / (1024.f * 1024.f));
			throw;
		case VK_ERROR_TOO_MANY_OBJECTS:
			fprintf_color(COLOR_RED, stderr, "vkAllocateMemory failed: VK_ERROR_TOO_MANY_OBJECTS\n");
			throw;
		case VK_ERROR_INVALID_EXTERNAL_HANDLE:
			fprintf_color(COLOR_RED, stderr, "vkAllocateMemory failed: VK_ERROR_INVALID_EXTERNAL_HANDLE\n");
			throw;
		default:
			break;
		}
	}
	allocation.mSize = info.allocationSize;
	allocation.mAvailable = { make_pair((VkDeviceSize)0, allocation.mSize) };
	mMemoryAllocationCount++;

	if (mMemoryProperties.memoryTypes[memoryType].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
		mMemoryUsage += allocation.mSize;

	if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
		vkMapMemory(mDevice, allocation.mMemory, 0, allocation.mSize, 0, &allocation.mMapped);

	if (!allocation.SubAllocate(requirements, alloc, tag)) {
		fprintf_color(COLOR_RED, stderr, "%s", "Failed to allocate memory\n");
		throw;
	}

	#ifdef PRINT_VK_ALLOCATIONS
	if (info.allocationSize < 1024)
		printf_color(COLOR_YELLOW, "Allocated %llu B of type %u\t-- ", info.allocationSize, info.memoryTypeIndex);
	else if (info.allocationSize < 1024 * 1024)
		printf_color(COLOR_YELLOW, "Allocated %.3f KiB of type %u\t-- ", info.allocationSize / 1024, info.memoryTypeIndex);
	else if (info.allocationSize < 1024 * 1024 * 1024)
		printf_color(COLOR_YELLOW, "Allocated %.3f MiB of type %u\t-- ", info.allocationSize / (1024.f * 1024.f), info.memoryTypeIndex);
	else
		printf_color(COLOR_YELLOW, "Allocated %.3f GiB of type %u\t-- ", info.allocationSize / (1024.f * 1024.f * 1024.f), info.memoryTypeIndex);
	PrintAllocations();
	printf_color(COLOR_YELLOW, "\n");
	#endif

	return alloc;
}
void Device::FreeMemory(const DeviceMemoryAllocation& allocation) {
	lock_guard<mutex> lock(mMemoryMutex);

	vector<Allocation>& allocations = mMemoryAllocations[allocation.mMemoryType];
	for (auto it = allocations.begin(); it != allocations.end();){
		if (it->mMemory == allocation.mDeviceMemory) {
			it->Deallocate(allocation);
			if (it->mAvailable.size() == 1 && it->mAvailable.begin()->second == it->mSize) {
				vkFreeMemory(mDevice, it->mMemory, nullptr);
				mMemoryAllocationCount--;
				if (mMemoryProperties.memoryTypes[allocation.mMemoryType].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
					mMemoryUsage -= it->mSize;
				#ifdef PRINT_VK_ALLOCATIONS
				if (allocation.mSize < 1024)
					printf_color(COLOR_YELLOW, "Freed %lu B of type %u\t- ", allocation.mSize, allocation.mMemoryType);
				else if (allocation.mSize < 1024 * 1024)
					printf_color(COLOR_YELLOW, "Freed %.3f KiB of type %u\t-- ", allocation.mSize / 1024.f, allocation.mMemoryType);
				else if (allocation.mSize < 1024 * 1024 * 1024)
					printf_color(COLOR_YELLOW, "Freed %.3f MiB of type %u\t-- ", allocation.mSize / (1024.f * 1024.f), allocation.mMemoryType);
				else
					printf_color(COLOR_YELLOW, "Freed %.3f GiB of type %u\t-- ", allocation.mSize / (1024.f * 1024.f * 1024), allocation.mMemoryType);
				PrintAllocations();
				printf_color(COLOR_YELLOW, "\n");
				#endif
				it = allocations.erase(it);
				continue;
			}
		}
		it++;
	}
}

Buffer* Device::GetPooledBuffer(const string& name, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProperties) {
	Buffer* b = nullptr;
	mBufferPoolMutex.lock();
	auto best = mBufferPool.end();
	for (auto it = mBufferPool.begin(); it != mBufferPool.end(); it++)
		if ((it->mResource->Usage() & usage) && (it->mResource->MemoryProperties() & memoryProperties) && it->mResource->Size() >= size) {
			if (best == mBufferPool.end() || it->mResource->Size() < it->mResource->Size())
				best = it;
			if (it->mResource->Size() == size) break;
		}
	if (best != mBufferPool.end()) {
		b = best->mResource;
		mBufferPool.erase(best);
	}
	mBufferPoolMutex.unlock();
	return b ? b : new Buffer(name, this, size, usage, memoryProperties);
}
DescriptorSet* Device::GetPooledDescriptorSet(const string& name, VkDescriptorSetLayout layout) {
	DescriptorSet* ds = nullptr;
	mDescriptorSetPoolMutex.lock();
	auto& sets = mDescriptorSetPool[layout];
	if (sets.size()) {
		auto front = sets.front();
		ds = front.mResource;
		sets.pop_front();
		ds->mPendingWrites.clear();
	}
	mDescriptorSetPoolMutex.unlock();
	return ds ? ds : new DescriptorSet(name, this, layout);
}
Texture* Device::GetPooledTexture(const string& name, const VkExtent3D& extent, VkFormat format, uint32_t mipLevels, VkSampleCountFlagBits sampleCount, VkImageUsageFlags usage, VkMemoryPropertyFlags memoryProperties) {
	Texture* tex = nullptr;
	mTexturePoolMutex.lock();
	auto& pool = mTexturePool[HashTextureData(extent, format, mipLevels, sampleCount)];
	auto best = pool.end();
	for (auto it = pool.begin(); it != pool.end(); it++)
		if ((it->mResource->Usage() & usage) && (it->mResource->MemoryProperties() & memoryProperties))
			if (best == pool.end()) {
				tex = it->mResource;
				pool.erase(it);
				break;
			}
	mTexturePoolMutex.unlock();
	return tex ? tex : new Texture(name, this, extent, format, mipLevels, sampleCount, usage, memoryProperties);
}
void Device::PoolResource(Buffer* buffer) {
	mBufferPoolMutex.lock();
	mBufferPool.push_back({ mFrameCount, buffer });
	mBufferPoolMutex.unlock();
}
void Device::PoolResource(DescriptorSet* descriptorSet) {
	mDescriptorSetPoolMutex.lock();
	mDescriptorSetPool[descriptorSet->Layout()].push_back({ mFrameCount, descriptorSet });
	mDescriptorSetPoolMutex.unlock();
}
void Device::PoolResource(Texture* texture) {
	mTexturePoolMutex.lock();
	mTexturePool[HashTextureData(texture)].push_back({ mFrameCount, texture });
	mTexturePoolMutex.unlock();
}

void Device::PurgePooledResources(uint32_t maxAge) {

	mCommandBufferPoolMutex.lock();
	for (auto& kp : mCommandBufferPool)
		for (auto& it = kp.second.begin(); it != kp.second.end();) {
			if (it->mResource->State() != CMDBUF_STATE_DONE) continue;
			it->mResource->Clear();
			if (mFrameCount - it->mLastFrameUsed >= maxAge) {
				safe_delete(it->mResource);
				it = kp.second.erase(it);
			} else it++;
		}
	mCommandBufferPoolMutex.unlock();

	mBufferPoolMutex.lock();
	for (auto& it = mBufferPool.begin(); it != mBufferPool.end();)
		if (mFrameCount - it->mLastFrameUsed >= maxAge) {
			safe_delete(it->mResource);
			it = mBufferPool.erase(it);
		} else it++;
	mBufferPoolMutex.unlock();
			
	mTexturePoolMutex.lock();
	for (auto& kp : mTexturePool)
		for (auto& it = kp.second.begin(); it != kp.second.end();)
			if (mFrameCount - it->mLastFrameUsed >= maxAge) {
				safe_delete(it->mResource);
				it = kp.second.erase(it);
			} else it++;
	mTexturePoolMutex.unlock();
	
	mDescriptorSetPoolMutex.lock();
	for (auto& kp : mDescriptorSetPool)
		for (auto& it = kp.second.begin(); it != kp.second.end();)
			if (mFrameCount - it->mLastFrameUsed >= maxAge) {
				safe_delete(it->mResource);
				it = kp.second.erase(it);
			} else it++;
	mDescriptorSetPoolMutex.unlock();
}

CommandBuffer* Device::GetCommandBuffer(const std::string& name, VkCommandBufferLevel level) {
	// get a commandpool for the current thread
	mCommandPoolMutex.lock();
	VkCommandPool& commandPool = mCommandPools[this_thread::get_id()];
	mCommandPoolMutex.unlock();
	if (!commandPool) {
		VkCommandPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = mGraphicsQueueFamilyIndex;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		ThrowIfFailed(vkCreateCommandPool(mDevice, &poolInfo, nullptr, &commandPool), "vkCreateCommandPool failed");
		SetObjectName(commandPool, name + " Graphics Command Pool", VK_OBJECT_TYPE_COMMAND_POOL);
	}

	if (level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
		auto& pool = mCommandBufferPool[commandPool];
		if (!pool.empty()) {
			if (pool.front().mResource->State() == CMDBUF_STATE_DONE) {
				CommandBuffer* commandBuffer = pool.front().mResource;
				pool.pop_front();
				commandBuffer->Reset(name);
				return commandBuffer;
			}
		}
	}

	return new CommandBuffer(name, this, commandPool, level);
}
void Device::Execute(CommandBuffer* commandBuffer) {
	if (commandBuffer->State() != CMDBUF_STATE_RECORDING)
		fprintf_color(COLOR_YELLOW, stderr, "Warning: Execute() expected CommandBuffer to be in CMDBUF_STATE_RECORDING\n");
	ThrowIfFailed(vkEndCommandBuffer(commandBuffer->mCommandBuffer), "vkEndCommandBuffer failed");

	vector<VkPipelineStageFlags> waitStages;	
	vector<VkSemaphore> waitSemaphores;	
	for (uint32_t i = 0; i < commandBuffer->mWaitSemaphores.size(); i++) {
		waitStages.push_back(commandBuffer->mWaitSemaphores[i].first);
		waitSemaphores.push_back(*commandBuffer->mWaitSemaphores[i].second);
	}

	vector<VkSemaphore> signalSemaphores;
	for (uint32_t i = 0; i < commandBuffer->mSignalSemaphores.size(); i++)
		signalSemaphores.push_back(*commandBuffer->mSignalSemaphores[i]);

	PROFILER_BEGIN("vkQueueSubmit");
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.signalSemaphoreCount = (uint32_t)signalSemaphores.size();
	submitInfo.pSignalSemaphores = signalSemaphores.data();
	submitInfo.pWaitDstStageMask = waitStages.data();
	submitInfo.waitSemaphoreCount = (uint32_t)waitSemaphores.size();
	submitInfo.pWaitSemaphores = waitSemaphores.data();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer->mCommandBuffer;
	vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, commandBuffer->mSignalFence);
	PROFILER_END;

	commandBuffer->mState = CMDBUF_STATE_PENDING;

	mCommandBufferPoolMutex.lock();
	mCommandBufferPool[commandBuffer->mCommandPool].push_back({ mFrameCount, commandBuffer });
	mCommandBufferPoolMutex.unlock();
}
void Device::Flush() {
	mCommandBufferPoolMutex.lock();

	vkDeviceWaitIdle(mDevice);

	for (auto& kp : mCommandBufferPool)
		while (!kp.second.empty()) {
			CommandBuffer* commandBuffer = kp.second.front().mResource;
			kp.second.pop_front();
			safe_delete(commandBuffer);
		}
	
	mCommandBufferPoolMutex.unlock();
}

void Device::SetObjectName(void* object, const string& name, VkObjectType type) const {
	#ifdef ENABLE_DEBUG_LAYERS
	VkDebugUtilsObjectNameInfoEXT info = {};
	info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	info.objectHandle = (uint64_t)object;
	info.objectType = type;
	info.pObjectName = name.c_str();
	SetDebugUtilsObjectNameEXT(mDevice, &info);
	#endif
}

VkSampleCountFlagBits Device::GetMaxUsableSampleCount() {
	VkPhysicalDeviceProperties physicalDeviceProperties;
	vkGetPhysicalDeviceProperties(mPhysicalDevice, &physicalDeviceProperties);

	VkSampleCountFlags counts = std::min(physicalDeviceProperties.limits.framebufferColorSampleCounts, physicalDeviceProperties.limits.framebufferDepthSampleCounts);
	if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
	if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
	if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
	if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }
	return VK_SAMPLE_COUNT_1_BIT;
}

void Device::PrintAllocations() {
	VkDeviceSize used = 0;
	VkDeviceSize available = 0;
	VkDeviceSize total = 0;

	for (uint32_t i = 0; i < mMemoryProperties.memoryHeapCount; i++)
		total += mMemoryProperties.memoryHeaps[i].size;

	for (auto kp : mMemoryAllocations)
		for (auto a : kp.second) {
			used += a.mSize;
			for (auto av : a.mAvailable) available += av.second;
		}

	if (used == 0) {
		printf_color(COLOR_YELLOW, "%s", "Using 0 B");
		return;
	}

	float percentTotal = 100.f * (float)used / (float)total;
	float percentWasted = 100.f * (float)available / (float)used;

	if (used < 1024)
		printf_color(COLOR_YELLOW, "Using %lu B (%.1f%%) - %.1f%% wasted", used, percentTotal, percentWasted);
	else if (used < 1024 * 1024)
		printf_color(COLOR_YELLOW, "Using %.3f KiB (%.1f%%) - %.1f%%wasted", used / 1024.f, percentTotal, percentWasted);
	else
		printf_color(COLOR_YELLOW, "Using %.3f MiB (%.1f%%) - %.1f%% wasted", used / (1024.f * 1024.f), percentTotal, percentWasted);
}
