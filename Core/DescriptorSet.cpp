#include <Core/DescriptorSet.hpp>
#include <Core/Device.hpp>
#include <Core/Buffer.hpp>
#include <Content/Texture.hpp>

using namespace std;

#define DESCRIPTOR_INDEX(binding, arrayIndex) ((((uint64_t)binding) << 32) | ((uint64_t)arrayIndex))
#define BINDING_FROM_INDEX(index) (uint32_t)(index >> 32)

DescriptorSetEntry::DescriptorSetEntry() : mType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), mArrayIndex(0), mBufferValue((Buffer*)nullptr), mBufferOffset(0), mBufferRange(0) {}
DescriptorSetEntry::DescriptorSetEntry(const DescriptorSetEntry& ds) : mType(ds.mType), mArrayIndex(ds.mArrayIndex) {
	switch (mType) {
	case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		mTextureValue = ds.mTextureValue;
		mImageView = ds.mImageView;
	case VK_DESCRIPTOR_TYPE_SAMPLER:
		mSamplerValue = ds.mSamplerValue;
		mBufferValue.reset();
		break;

	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		mTextureValue = ds.mTextureValue;
		mImageView = ds.mImageView;
		mBufferValue.reset();
		mSamplerValue.reset();
		break;

	case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
	case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
		mBufferValue = ds.mBufferValue;
		mBufferOffset = ds.mBufferOffset;
		mBufferRange = ds.mBufferRange;
		mTextureValue.reset();
		mSamplerValue.reset();
		break;
	case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
	case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT:
		// TODO: VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT
		break;
	}
}
DescriptorSetEntry::~DescriptorSetEntry() {
	switch (mType) {
	case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		mTextureValue.reset();
	case VK_DESCRIPTOR_TYPE_SAMPLER:
		mSamplerValue.reset();
		break;

	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		mTextureValue.reset();
		break;

	case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
	case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
		mBufferValue.reset();
		break;
	case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
	case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT:
		// TODO: VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT
		break;
	}
}

bool DescriptorSetEntry::IsNull() const {
	switch (mType) {
	case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		return mSamplerValue.get() == nullptr || mTextureValue.get() == nullptr || mImageView == VK_NULL_HANDLE;
	case VK_DESCRIPTOR_TYPE_SAMPLER:
		return mSamplerValue.get() == nullptr;
	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		return mTextureValue.get() == nullptr || mImageView == VK_NULL_HANDLE;
	case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
	case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
		return !mBufferValue.get();
	case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT:
		// TODO: VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT
		break;
	}
	return true;
}

bool DescriptorSetEntry::operator==(const DescriptorSetEntry& rhs) const {
	if (rhs.mType != mType || rhs.mArrayIndex != mArrayIndex) return false;

	switch (mType) {
	case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		return rhs.mTextureValue == mTextureValue && rhs.mImageView == mImageView && rhs.mImageLayout == mImageLayout && rhs.mSamplerValue == mSamplerValue;
	case VK_DESCRIPTOR_TYPE_SAMPLER:
		return rhs.mSamplerValue == mSamplerValue;

	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		return rhs.mTextureValue == mTextureValue && rhs.mImageView == mImageView && rhs.mImageLayout == mImageLayout;

	case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
	case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
		return rhs.mBufferValue == mBufferValue && rhs.mBufferOffset == mBufferOffset && rhs.mBufferValue == mBufferValue;

	case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
	case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT:
		// TODO: VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT
		return false;
	}
	return false;
}

DescriptorSet::DescriptorSet(const string& name, Device* device, VkDescriptorSetLayout layout) : mDevice(device), mLayout(layout) {
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = mDevice->mDescriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;
	device->mDescriptorPoolMutex.lock();
	ThrowIfFailed(vkAllocateDescriptorSets(*mDevice, &allocInfo, &mDescriptorSet), "vkAllocateDescriptorSets failed");
	mDevice->SetObjectName(mDescriptorSet, name, VK_OBJECT_TYPE_DESCRIPTOR_SET);
	mDevice->mDescriptorSetCount++;
	device->mDescriptorPoolMutex.unlock();
}
DescriptorSet::~DescriptorSet() {
	mBoundDescriptors.clear();
	mPendingWrites.clear();

	mDevice->mDescriptorPoolMutex.lock();
	ThrowIfFailed(vkFreeDescriptorSets(*mDevice, mDevice->mDescriptorPool, 1, &mDescriptorSet), "vkFreeDescriptorSets failed");
	mDevice->mDescriptorSetCount--;
	mDevice->mDescriptorPoolMutex.unlock();
}

void DescriptorSet::CreateDescriptor(uint32_t binding, const DescriptorSetEntry& entry) {
	uint64_t idx = DESCRIPTOR_INDEX(binding, entry.mArrayIndex);
	if (mBoundDescriptors.count(idx) && mBoundDescriptors.at(idx) == entry) return;
	mPendingWrites[idx] = entry;
}
void DescriptorSet::CreateUniformBufferDescriptor(const variant_ptr<Buffer>& buffer, VkDeviceSize offset, VkDeviceSize range, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	e.mArrayIndex = arrayIndex;
	e.mBufferValue = buffer;
	e.mBufferOffset = offset;
	e.mBufferRange = range;
	CreateDescriptor(binding, e);
}
void DescriptorSet::CreateUniformBufferDescriptor(const variant_ptr<Buffer>& buffer, uint32_t binding, uint32_t arrayIndex) {
	CreateUniformBufferDescriptor(buffer, 0, buffer->Size(), binding, arrayIndex);
}
void DescriptorSet::CreateStorageBufferDescriptor(const variant_ptr<Buffer>& buffer, VkDeviceSize offset, VkDeviceSize range, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	e.mArrayIndex = arrayIndex;
	e.mBufferValue = buffer;
	e.mBufferOffset = offset;
	e.mBufferRange = range;
	CreateDescriptor(binding, e);
}
void DescriptorSet::CreateStorageBufferDescriptor(const variant_ptr<Buffer>& buffer, uint32_t binding, uint32_t arrayIndex) {
	CreateStorageBufferDescriptor(buffer, 0, buffer->Size(), binding, arrayIndex);
}
void DescriptorSet::CreateStorageTexelBufferDescriptor(const variant_ptr<Buffer>& buffer, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mArrayIndex = arrayIndex;
	e.mType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
	e.mBufferValue = buffer;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateStorageTextureDescriptor(const variant_ptr<Texture>& texture, uint32_t binding, uint32_t arrayIndex, VkImageView view, VkImageLayout layout) {
	DescriptorSetEntry e = {};
	e.mType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	e.mArrayIndex = arrayIndex;
	e.mTextureValue = texture;
	e.mImageView = view == VK_NULL_HANDLE ? texture->View() : view;
	e.mImageLayout = layout;
	e.mSamplerValue = nullptr;
	CreateDescriptor(binding, e);
}
void DescriptorSet::CreateSampledTextureDescriptor(const variant_ptr<Texture>& texture, uint32_t binding, uint32_t arrayIndex, VkImageView view, VkImageLayout layout) {
	DescriptorSetEntry e = {};
	e.mType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	e.mArrayIndex = arrayIndex;
	e.mTextureValue = texture;
	e.mImageView = view == VK_NULL_HANDLE ? texture->View() : view;
	e.mImageLayout = layout;
	e.mSamplerValue = nullptr;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateSamplerDescriptor(const variant_ptr<Sampler>& sampler, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	e.mArrayIndex = arrayIndex;
	e.mTextureValue = nullptr;
	e.mSamplerValue = sampler;
	e.mImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	CreateDescriptor(binding, e);
}

void DescriptorSet::FlushWrites() {
	if (mPendingWrites.empty()) return;

	union WriteInfo {
    VkDescriptorImageInfo  mImageInfo;
    VkDescriptorBufferInfo mBufferInfo;
    VkBufferView           mTexelBufferView;
	};
	vector<WriteInfo> infos(mPendingWrites.size());
	vector<VkWriteDescriptorSet> writes(mPendingWrites.size());
	uint32_t i = 0;
	for (auto& kp : mPendingWrites) {
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = mDescriptorSet;
		writes[i].dstBinding = BINDING_FROM_INDEX(kp.first);
		writes[i].dstArrayElement = kp.second.mArrayIndex;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = kp.second.mType;

		switch (kp.second.mType) {
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			infos[i].mImageInfo.imageLayout = kp.second.mImageLayout;
			infos[i].mImageInfo.imageView = kp.second.mImageView;
    case VK_DESCRIPTOR_TYPE_SAMPLER:
			infos[i].mImageInfo.sampler = *kp.second.mSamplerValue.get();
			writes[i].pImageInfo = &infos[i].mImageInfo;
			break;

    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			infos[i].mImageInfo.imageLayout = kp.second.mImageLayout;
			infos[i].mImageInfo.imageView = kp.second.mImageView;
			writes[i].pImageInfo = &infos[i].mImageInfo;
			break;

    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
			infos[i].mTexelBufferView = kp.second.mBufferValue->View();
			writes[i].pTexelBufferView = &infos[i].mTexelBufferView;
			break;

    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
			infos[i].mBufferInfo.buffer = *kp.second.mBufferValue.get();
			infos[i].mBufferInfo.offset = kp.second.mBufferOffset;
			infos[i].mBufferInfo.range = kp.second.mBufferRange;
			writes[i].pBufferInfo = &infos[i].mBufferInfo;
			break;
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
    case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT:
			// TODO: VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT
			break;
		}

		i++;
	}

	vkUpdateDescriptorSets(*mDevice, (uint32_t)writes.size(), writes.data(), 0, nullptr);
	mPendingWrites.clear();
}