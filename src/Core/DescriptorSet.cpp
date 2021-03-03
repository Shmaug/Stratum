#include "DescriptorSet.hpp"
#include "CommandBuffer.hpp"

using namespace stm;

constexpr uint64_t HashFromBinding(uint64_t binding, uint64_t arrayIndex) { return (binding << 32) | arrayIndex; }
constexpr uint32_t BindingFromHash(uint64_t index) { return (uint32_t)(index >> 32); }

DescriptorSetEntry::operator bool() const {
	switch (mType) {
	case vk::DescriptorType::eCombinedImageSampler:
		if (!mTextureView.get().get()) return false;
	case vk::DescriptorType::eSampler:
		return mSampler.get();
	case vk::DescriptorType::eSampledImage:
	case vk::DescriptorType::eStorageImage:
		return mTextureView.get().get();
	case vk::DescriptorType::eUniformTexelBuffer:
	case vk::DescriptorType::eStorageTexelBuffer:
	case vk::DescriptorType::eUniformBuffer:
	case vk::DescriptorType::eStorageBuffer:
	case vk::DescriptorType::eUniformBufferDynamic:
	case vk::DescriptorType::eStorageBufferDynamic:
		return mBufferView.get().get();
	case vk::DescriptorType::eInlineUniformBlockEXT:
		return mInlineUniformData.size();
	}
	return false;
}
void DescriptorSetEntry::reset() {
	switch (mType) {
	case vk::DescriptorType::eSampler:
		mSampler.reset();
		break;
	case vk::DescriptorType::eCombinedImageSampler:
		mSampler.reset();
	case vk::DescriptorType::eSampledImage:
	case vk::DescriptorType::eStorageImage:
		mTextureView = {};
		break;
	case vk::DescriptorType::eUniformTexelBuffer:
	case vk::DescriptorType::eStorageTexelBuffer:
		mTexelBufferView = {};
		break;
	case vk::DescriptorType::eUniformBuffer:
	case vk::DescriptorType::eStorageBuffer:
	case vk::DescriptorType::eUniformBufferDynamic:
	case vk::DescriptorType::eStorageBufferDynamic:
		mBufferView = {};
		break;
	case vk::DescriptorType::eInlineUniformBlockEXT:
		mInlineUniformData.clear();
		break;
	}
	mType = {};
}
bool DescriptorSetEntry::operator==(const DescriptorSetEntry& rhs) const {
	if (rhs.mType != mType || rhs.mArrayIndex != mArrayIndex) return false;
	switch (mType) {
	case vk::DescriptorType::eCombinedImageSampler:
		return rhs.mTextureView == mTextureView && rhs.mImageLayout == mImageLayout;
	case vk::DescriptorType::eSampler:
		return rhs.mSampler == mSampler;

	case vk::DescriptorType::eInputAttachment:
	case vk::DescriptorType::eSampledImage:
	case vk::DescriptorType::eStorageImage:
		return rhs.mTextureView == mTextureView && rhs.mImageLayout == mImageLayout;

	case vk::DescriptorType::eUniformTexelBuffer:
	case vk::DescriptorType::eStorageTexelBuffer:
	case vk::DescriptorType::eUniformBuffer:
	case vk::DescriptorType::eStorageBuffer:
	case vk::DescriptorType::eUniformBufferDynamic:
	case vk::DescriptorType::eStorageBufferDynamic:
		return rhs.mBufferView == mBufferView;

	case vk::DescriptorType::eInlineUniformBlockEXT:
		return rhs.mInlineUniformData == mInlineUniformData;
	}
	return false;
}

void DescriptorSet::CreateDescriptor(uint32_t binding, const DescriptorSetEntry& entry) {
	uint64_t idx = HashFromBinding(binding, entry.mArrayIndex);

	// check already bound
	if (mBoundDescriptors.count(idx) && mBoundDescriptors.at(idx) == entry) return;
	
	// null check
	switch (entry.mType) {
    case vk::DescriptorType::eCombinedImageSampler:
    case vk::DescriptorType::eSampler:
			if (entry.mSampler == nullptr) throw invalid_argument("sampler entry was nullptr");
			if (entry.mType == vk::DescriptorType::eSampler) break;
    case vk::DescriptorType::eStorageImage:
    case vk::DescriptorType::eSampledImage:
    case vk::DescriptorType::eInputAttachment:
			if (!entry.mTextureView.get().get()) throw invalid_argument("image view entry was nullptr\n");
			break;

    case vk::DescriptorType::eUniformBuffer:
    case vk::DescriptorType::eStorageBuffer:
    case vk::DescriptorType::eUniformBufferDynamic:
    case vk::DescriptorType::eStorageBufferDynamic:
			if (entry.mBufferView.get() == nullptr) throw invalid_argument("buffer entry was nullptr\n");
			break;
    case vk::DescriptorType::eUniformTexelBuffer:
    case vk::DescriptorType::eStorageTexelBuffer:
			if (!((const Buffer::ArrayView<>&)entry.mTexelBufferView).get()) throw invalid_argument("buffer entry was nullptr\n");
			break;

    case vk::DescriptorType::eInlineUniformBlockEXT:
			if (!entry.mInlineUniformData.size()) throw invalid_argument("inline uniform buffer data was null\n");
			break;

    case vk::DescriptorType::eAccelerationStructureKHR:
    default:
			throw invalid_argument("unsupported descriptor type " + to_string(entry.mType));
	}

	mPendingWrites[idx] = entry;
	mBoundDescriptors[idx] = entry;
}

void DescriptorSet::CreateInlineUniformBlock(uint32_t binding, const byte_blob& type){
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eInlineUniformBlockEXT;
	e.mInlineUniformData = type;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateUniformBufferDescriptor(const Buffer::ArrayView<>& buffer, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eUniformBuffer;
	e.mArrayIndex = arrayIndex;
	e.mBufferView = buffer;
	CreateDescriptor(binding, e);
}
void DescriptorSet::CreateStorageBufferDescriptor(const Buffer::ArrayView<>& buffer, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eStorageBuffer;
	e.mArrayIndex = arrayIndex;
	e.mBufferView = buffer;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateStorageTexelBufferDescriptor(const BufferView& buffer, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eStorageTexelBuffer;
	e.mArrayIndex = arrayIndex;
	e.mTexelBufferView = buffer;
	CreateDescriptor(binding, e);
}
void DescriptorSet::CreateUniformTexelBufferDescriptor(const BufferView& buffer, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eUniformTexelBuffer;
	e.mArrayIndex = arrayIndex;
	e.mTexelBufferView = buffer;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateStorageTextureDescriptor(const TextureView& texture, uint32_t binding, uint32_t arrayIndex, vk::ImageLayout layout) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eStorageImage;
	e.mArrayIndex = arrayIndex;
	e.mTextureView = texture;
	e.mImageLayout = layout;
	CreateDescriptor(binding, e);
}
void DescriptorSet::CreateSampledTextureDescriptor(const TextureView& texture, uint32_t binding, uint32_t arrayIndex, vk::ImageLayout layout) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eSampledImage;
	e.mArrayIndex = arrayIndex;
	e.mTextureView = texture;
	e.mImageLayout = layout;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateInputAttachmentDescriptor(const TextureView& texture, uint32_t binding, uint32_t arrayIndex, vk::ImageLayout layout) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eInputAttachment;
	e.mArrayIndex = arrayIndex;
	e.mTextureView = texture;
	e.mImageLayout = layout;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateSamplerDescriptor(shared_ptr<Sampler> sampler, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eSampler;
	e.mArrayIndex = arrayIndex;
	e.mSampler = sampler;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateTextureDescriptor(const string& bindingName, const TextureView& texture, Pipeline& pipeline, uint32_t arrayIndex, vk::ImageLayout layout) {
	for (auto it = pipeline.DescriptorBindings().find(bindingName); it != pipeline.DescriptorBindings().end(); it++)
		switch (it->second.mBinding.descriptorType) {
			case vk::DescriptorType::eInputAttachment: 	CreateInputAttachmentDescriptor(texture, it->second.mBinding.binding, arrayIndex, layout); continue;
			case vk::DescriptorType::eSampledImage: 		CreateSampledTextureDescriptor(texture, it->second.mBinding.binding, arrayIndex, layout); continue;
			case vk::DescriptorType::eStorageImage: 		CreateStorageTextureDescriptor(texture, it->second.mBinding.binding, arrayIndex, layout); continue;
		}
}
void DescriptorSet::CreateTexelBufferDescriptor(const string& bindingName, const BufferView& buffer, Pipeline& pipeline, uint32_t arrayIndex) {
	for (auto it = pipeline.DescriptorBindings().find(bindingName); it != pipeline.DescriptorBindings().end(); it++)
		switch (it->second.mBinding.descriptorType) {
			case vk::DescriptorType::eUniformTexelBuffer: 	CreateUniformTexelBufferDescriptor(buffer, it->second.mBinding.binding, arrayIndex); continue;
			case vk::DescriptorType::eStorageTexelBuffer: 	CreateStorageTexelBufferDescriptor(buffer, it->second.mBinding.binding, arrayIndex); continue;
		}
}
void DescriptorSet::CreateBufferDescriptor(const string& bindingName, const Buffer::ArrayView<>& buffer, Pipeline& pipeline, uint32_t arrayIndex) {
	for (auto it = pipeline.DescriptorBindings().find(bindingName); it != pipeline.DescriptorBindings().end(); it++)
		switch (it->second.mBinding.descriptorType) {
			case vk::DescriptorType::eUniformBuffer: 				CreateUniformBufferDescriptor(buffer, it->second.mBinding.binding, arrayIndex); continue;
			case vk::DescriptorType::eStorageBuffer: 				CreateStorageBufferDescriptor(buffer, it->second.mBinding.binding, arrayIndex); continue;
			case vk::DescriptorType::eUniformBufferDynamic: continue; // TODO: dynamic descriptors
			case vk::DescriptorType::eStorageBufferDynamic: continue; // TODO: dynamic descriptors
		}
}
void DescriptorSet::CreateSamplerDescriptor(const string& bindingName, shared_ptr<Sampler> sampler, Pipeline& pipeline, uint32_t arrayIndex) {
	for (auto it = pipeline.DescriptorBindings().find(bindingName); it != pipeline.DescriptorBindings().end(); it++)
		if (it->second.mBinding.descriptorType == vk::DescriptorType::eSampler)
			CreateSamplerDescriptor(sampler, it->second.mBinding.binding, arrayIndex);
}

void DescriptorSet::FlushWrites() {
	if (mPendingWrites.empty()) return;

	struct WriteInfo {
    vk::DescriptorImageInfo  mImageInfo;
    vk::DescriptorBufferInfo mBufferInfo;
    vk::BufferView           mTexelBufferView;
		vk::WriteDescriptorSetInlineUniformBlockEXT mInlineInfo;
	};
	vector<WriteInfo> infos(mPendingWrites.size());
	vector<vk::WriteDescriptorSet> writes(mPendingWrites.size());
	uint32_t i = 0;
	for (auto&[idx, entry] : mPendingWrites) {
		writes[i].dstSet = mDescriptorSet;
		writes[i].dstBinding = BindingFromHash(idx);
		writes[i].dstArrayElement = (uint32_t)entry.mArrayIndex;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = entry.mType;

		switch (entry.mType) {
    case vk::DescriptorType::eCombinedImageSampler:
			infos[i].mImageInfo.imageLayout = entry.mImageLayout;
			infos[i].mImageInfo.imageView = *entry.mTextureView;
    case vk::DescriptorType::eSampler:
			infos[i].mImageInfo.sampler = **entry.mSampler;
			writes[i].pImageInfo = &infos[i].mImageInfo;
			break;

    case vk::DescriptorType::eInputAttachment:
    case vk::DescriptorType::eSampledImage:
    case vk::DescriptorType::eStorageImage:
			infos[i].mImageInfo.imageLayout = entry.mImageLayout;
			infos[i].mImageInfo.imageView = *entry.mTextureView;
			writes[i].pImageInfo = &infos[i].mImageInfo;
			break;

    case vk::DescriptorType::eUniformTexelBuffer:
    case vk::DescriptorType::eStorageTexelBuffer:
			infos[i].mTexelBufferView = *entry.mTexelBufferView;
			writes[i].pTexelBufferView = &infos[i].mTexelBufferView;
			break;

    case vk::DescriptorType::eUniformBuffer:
    case vk::DescriptorType::eStorageBuffer:
    case vk::DescriptorType::eUniformBufferDynamic:
    case vk::DescriptorType::eStorageBufferDynamic:
			infos[i].mBufferInfo.buffer = **entry.mBufferView;
			infos[i].mBufferInfo.offset = entry.mBufferView.offset();
			infos[i].mBufferInfo.range = entry.mBufferView.size_bytes();
			writes[i].pBufferInfo = &infos[i].mBufferInfo;
			break;
    case vk::DescriptorType::eInlineUniformBlockEXT:
			infos[i].mInlineInfo.pData = entry.mInlineUniformData.data();
			infos[i].mInlineInfo.dataSize = (uint32_t)entry.mInlineUniformData.size();
			writes[i].descriptorCount = infos[i].mInlineInfo.dataSize;
			writes[i].pNext = &infos[i].mInlineInfo;
			break;
		}

		i++;
	}

	mDevice->updateDescriptorSets(writes, {});
	mPendingWrites.clear();
}