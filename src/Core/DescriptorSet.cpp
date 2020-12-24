#include "DescriptorSet.hpp"

#include "CommandBuffer.hpp"
#include "Asset/Texture.hpp"


using namespace stm;

constexpr inline uint64_t HashFromBinding(uint64_t binding, uint64_t arrayIndex) { return (binding << 32) | arrayIndex; }
constexpr inline uint32_t BindingFromHash(uint64_t index) { return (uint32_t)(index >> 32); }

DescriptorSetEntry::operator bool() const {
	switch (mType) {
	case vk::DescriptorType::eCombinedImageSampler:
		if (!(mImageView.mTexture && mImageView.mView)) return false;
	case vk::DescriptorType::eSampler:
		return mImageView.mSampler.get();
	case vk::DescriptorType::eSampledImage:
	case vk::DescriptorType::eStorageImage:
		return mImageView.mTexture && mImageView.mView;
	case vk::DescriptorType::eUniformTexelBuffer:
	case vk::DescriptorType::eStorageTexelBuffer:
	case vk::DescriptorType::eUniformBuffer:
	case vk::DescriptorType::eStorageBuffer:
	case vk::DescriptorType::eUniformBufferDynamic:
	case vk::DescriptorType::eStorageBufferDynamic:
		return mBufferView.mBuffer.get();
	case vk::DescriptorType::eInlineUniformBlockEXT:
		return mInlineUniformData.size();
	}
	return false;
}

void DescriptorSetEntry::Clear() {
	switch (mType) {
	case vk::DescriptorType::eSampler:
		mImageView.mSampler.reset();
		break;
	case vk::DescriptorType::eCombinedImageSampler:
		mImageView.mSampler.reset();
	case vk::DescriptorType::eSampledImage:
	case vk::DescriptorType::eStorageImage:
		mImageView.mTexture.reset();
		break;
	case vk::DescriptorType::eUniformTexelBuffer:
	case vk::DescriptorType::eStorageTexelBuffer:
	case vk::DescriptorType::eUniformBuffer:
	case vk::DescriptorType::eStorageBuffer:
	case vk::DescriptorType::eUniformBufferDynamic:
	case vk::DescriptorType::eStorageBufferDynamic:
		mBufferView.mBuffer.reset();
		break;
	case vk::DescriptorType::eInlineUniformBlockEXT:
		mInlineUniformData.clear();
		break;
	}
	mType = {};
}
void DescriptorSetEntry::Assign(const DescriptorSetEntry& ds) {
	mType = ds.mType;
	mArrayIndex = ds.mArrayIndex;

	switch (mType) {
	case vk::DescriptorType::eCombinedImageSampler:
		mImageView.mTexture = ds.mImageView.mTexture;
		mImageView = ds.mImageView;
		mImageView.mLayout = ds.mImageView.mLayout;
	case vk::DescriptorType::eSampler:
		mImageView.mSampler = ds.mImageView.mSampler;
		break;

	case vk::DescriptorType::eInputAttachment:
	case vk::DescriptorType::eSampledImage:
	case vk::DescriptorType::eStorageImage:
		mImageView.mTexture = ds.mImageView.mTexture;
		mImageView = ds.mImageView;
		mImageView.mLayout = ds.mImageView.mLayout;
		break;

	case vk::DescriptorType::eUniformTexelBuffer:
	case vk::DescriptorType::eStorageTexelBuffer:
	case vk::DescriptorType::eUniformBuffer:
	case vk::DescriptorType::eStorageBuffer:
	case vk::DescriptorType::eUniformBufferDynamic:
	case vk::DescriptorType::eStorageBufferDynamic:
		mBufferView.mBuffer = ds.mBufferView.mBuffer;
		mBufferView.mOffset = ds.mBufferView.mOffset;
		mBufferView.mRange = ds.mBufferView.mRange;
		break;
	case vk::DescriptorType::eInlineUniformBlockEXT:
		mInlineUniformData = ds.mInlineUniformData;
		break;
	}
}

bool DescriptorSetEntry::operator==(const DescriptorSetEntry& rhs) const {
	if (rhs.mType != mType || rhs.mArrayIndex != mArrayIndex) return false;
	switch (mType) {
	case vk::DescriptorType::eCombinedImageSampler:
		return rhs.mImageView.mTexture == mImageView.mTexture && rhs.mImageView.mView == mImageView.mView && rhs.mImageView.mLayout == mImageView.mLayout && rhs.mImageView.mSampler == mImageView.mSampler;
	case vk::DescriptorType::eSampler:
		return rhs.mImageView.mSampler == mImageView.mSampler;

	case vk::DescriptorType::eInputAttachment:
	case vk::DescriptorType::eSampledImage:
	case vk::DescriptorType::eStorageImage:
		return rhs.mImageView.mTexture == mImageView.mTexture && rhs.mImageView.mView == mImageView.mView && rhs.mImageView.mLayout == mImageView.mLayout;

	case vk::DescriptorType::eUniformTexelBuffer:
	case vk::DescriptorType::eStorageTexelBuffer:
	case vk::DescriptorType::eUniformBuffer:
	case vk::DescriptorType::eStorageBuffer:
	case vk::DescriptorType::eUniformBufferDynamic:
	case vk::DescriptorType::eStorageBufferDynamic:
		return rhs.mBufferView.mBuffer == mBufferView.mBuffer && rhs.mBufferView.mOffset == mBufferView.mOffset && rhs.mBufferView.mRange == mBufferView.mRange;

	case vk::DescriptorType::eInlineUniformBlockEXT:
		return mInlineUniformData == rhs.mInlineUniformData;
	}
	return false;
}

DescriptorSet::DescriptorSet(const string& name, stm::Device& device, vk::DescriptorSetLayout layout) : mName(name), mDevice(device), mLayout(layout) {
	vk::DescriptorSetAllocateInfo allocInfo = {};
	allocInfo.descriptorPool = mDevice.mDescriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;
	mDescriptorSet = mDevice->allocateDescriptorSets(allocInfo)[0];
	mDevice.SetObjectName(mDescriptorSet, mName);
}
DescriptorSet::~DescriptorSet() {
	mBoundDescriptors.clear();
	mPendingWrites.clear();
	lock_guard lock(mDevice.mDescriptorPoolMutex);
	mDevice->freeDescriptorSets(mDevice.mDescriptorPool, { mDescriptorSet });
}

void DescriptorSet::CreateDescriptor(uint32_t binding, const DescriptorSetEntry& entry) {
	uint64_t idx = HashFromBinding(binding, entry.mArrayIndex);

	// check already bound
	if (mBoundDescriptors.count(idx) && mBoundDescriptors.at(idx) == entry) return;
	
	// null check
	switch (entry.mType) {
    case vk::DescriptorType::eCombinedImageSampler:
    case vk::DescriptorType::eSampler:
			if (entry.mImageView.mSampler == nullptr) throw invalid_argument("sampler entry was nullptr");
			if (entry.mType == vk::DescriptorType::eSampler) break;
    case vk::DescriptorType::eStorageImage:
    case vk::DescriptorType::eSampledImage:
    case vk::DescriptorType::eInputAttachment:
			if (!entry.mImageView.mView) throw invalid_argument("image view entry was nullptr\n");
			break;

    case vk::DescriptorType::eUniformBuffer:
    case vk::DescriptorType::eStorageBuffer:
    case vk::DescriptorType::eUniformBufferDynamic:
    case vk::DescriptorType::eStorageBufferDynamic:
    case vk::DescriptorType::eUniformTexelBuffer:
    case vk::DescriptorType::eStorageTexelBuffer:
			if (entry.mBufferView.mBuffer == nullptr) throw invalid_argument("buffer entry was nullptr\n");
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

void DescriptorSet::CreateUniformBufferDescriptor(shared_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eUniformBuffer;
	e.mArrayIndex = arrayIndex;
	e.mBufferView.mBuffer = buffer;
	e.mBufferView.mOffset = offset;
	e.mBufferView.mRange = range;
	CreateDescriptor(binding, e);
}
void DescriptorSet::CreateUniformBufferDescriptor(shared_ptr<Buffer> buffer, uint32_t binding, uint32_t arrayIndex) {
	CreateUniformBufferDescriptor(buffer, 0, buffer->Size(), binding, arrayIndex);
}
void DescriptorSet::CreateStorageBufferDescriptor(shared_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eStorageBuffer;
	e.mArrayIndex = arrayIndex;
	e.mBufferView.mBuffer = buffer;
	e.mBufferView.mOffset = offset;
	e.mBufferView.mRange = range;
	CreateDescriptor(binding, e);
}
void DescriptorSet::CreateStorageBufferDescriptor(shared_ptr<Buffer> buffer, uint32_t binding, uint32_t arrayIndex) {
	CreateStorageBufferDescriptor(buffer, 0, buffer->Size(), binding, arrayIndex);
}
void DescriptorSet::CreateStorageTexelBufferDescriptor(shared_ptr<Buffer> buffer, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mArrayIndex = arrayIndex;
	e.mType = vk::DescriptorType::eStorageTexelBuffer;
	e.mBufferView.mBuffer = buffer;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateStorageTextureDescriptor(shared_ptr<Texture> texture, uint32_t binding, uint32_t arrayIndex, vk::ImageLayout layout, vk::ImageView view) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eStorageImage;
	e.mArrayIndex = arrayIndex;
	e.mImageView.mTexture = texture;
	e.mImageView.mView = view ? view : texture->View();
	e.mImageView.mLayout = layout;
	e.mImageView.mSampler = nullptr;
	CreateDescriptor(binding, e);
}
void DescriptorSet::CreateSampledTextureDescriptor(shared_ptr<Texture> texture, uint32_t binding, uint32_t arrayIndex, vk::ImageLayout layout, vk::ImageView view) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eSampledImage;
	e.mArrayIndex = arrayIndex;
	e.mImageView.mTexture = texture;
	e.mImageView.mView = view ? view : texture->View();
	e.mImageView.mLayout = layout;
	e.mImageView.mSampler = nullptr;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateInputAttachmentDescriptor(shared_ptr<Texture> texture, uint32_t binding, uint32_t arrayIndex, vk::ImageLayout layout, vk::ImageView view) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eInputAttachment;
	e.mArrayIndex = arrayIndex;
	e.mImageView.mTexture = texture;
	e.mImageView.mView = view ? view : texture->View();
	e.mImageView.mLayout = layout;
	e.mImageView.mSampler = nullptr;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateSamplerDescriptor(shared_ptr<Sampler> sampler, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eSampler;
	e.mArrayIndex = arrayIndex;
	e.mImageView.mTexture = nullptr;
	e.mImageView.mLayout = vk::ImageLayout::eUndefined;
	e.mImageView.mSampler = sampler;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateTextureDescriptor(const string& bindingName, shared_ptr<Texture> texture, Pipeline& pipeline, uint32_t arrayIndex, vk::ImageLayout layout, vk::ImageView view) {
	for (auto it = pipeline.DescriptorBindings().find(bindingName); it != pipeline.DescriptorBindings().end(); it++)
		switch (it->second.mBinding.descriptorType) {
			case vk::DescriptorType::eInputAttachment: 	CreateInputAttachmentDescriptor(texture, it->second.mBinding.binding, arrayIndex, layout, view); continue;
			case vk::DescriptorType::eSampledImage: 		CreateSampledTextureDescriptor(texture, it->second.mBinding.binding, arrayIndex, layout, view); continue;
			case vk::DescriptorType::eStorageImage: 		CreateStorageTextureDescriptor(texture, it->second.mBinding.binding, arrayIndex, layout, view); continue;
		}
}
void DescriptorSet::CreateBufferDescriptor(const string& bindingName, shared_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, Pipeline& pipeline, uint32_t arrayIndex) {
	for (auto it = pipeline.DescriptorBindings().find(bindingName); it != pipeline.DescriptorBindings().end(); it++)
		switch (it->second.mBinding.descriptorType) {
			case vk::DescriptorType::eUniformTexelBuffer: 	CreateUniformBufferDescriptor(buffer, offset, range, it->second.mBinding.binding, arrayIndex); continue;
			case vk::DescriptorType::eStorageTexelBuffer: 	CreateUniformBufferDescriptor(buffer, offset, range, it->second.mBinding.binding, arrayIndex); continue;
			case vk::DescriptorType::eUniformBuffer: 				CreateUniformBufferDescriptor(buffer, offset, range, it->second.mBinding.binding, arrayIndex); continue;
			case vk::DescriptorType::eStorageBuffer: 				CreateStorageBufferDescriptor(buffer, offset, range, it->second.mBinding.binding, arrayIndex); continue;
			case vk::DescriptorType::eUniformBufferDynamic: continue;
			case vk::DescriptorType::eStorageBufferDynamic: continue;
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
			infos[i].mImageInfo.imageLayout = entry.mImageView.mLayout;
			infos[i].mImageInfo.imageView = entry.mImageView.mView;
    case vk::DescriptorType::eSampler:
			infos[i].mImageInfo.sampler = **entry.mImageView.mSampler;
			writes[i].pImageInfo = &infos[i].mImageInfo;
			break;

    case vk::DescriptorType::eInputAttachment:
    case vk::DescriptorType::eSampledImage:
    case vk::DescriptorType::eStorageImage:
			infos[i].mImageInfo.imageLayout = entry.mImageView.mLayout;
			infos[i].mImageInfo.imageView = entry.mImageView.mView;
			writes[i].pImageInfo = &infos[i].mImageInfo;
			break;

    case vk::DescriptorType::eUniformTexelBuffer:
    case vk::DescriptorType::eStorageTexelBuffer:
			// TODO: infos[i].mTexelBufferView = entry.mBufferView.mBuffer->View();
			throw exception("texel buffers not currently supported in Stratum");
			writes[i].pTexelBufferView = &infos[i].mTexelBufferView;
			break;

    case vk::DescriptorType::eUniformBuffer:
    case vk::DescriptorType::eStorageBuffer:
    case vk::DescriptorType::eUniformBufferDynamic:
    case vk::DescriptorType::eStorageBufferDynamic:
			infos[i].mBufferInfo.buffer = **entry.mBufferView.mBuffer;
			infos[i].mBufferInfo.offset = entry.mBufferView.mOffset;
			infos[i].mBufferInfo.range = entry.mBufferView.mRange;
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