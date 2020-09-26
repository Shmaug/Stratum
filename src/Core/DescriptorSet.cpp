#include <Core/DescriptorSet.hpp>
#include <Core/CommandBuffer.hpp>
#include <Core/Device.hpp>
#include <Core/Buffer.hpp>
#include <Data/Texture.hpp>
#include <Data/Pipeline.hpp>

using namespace std;
using namespace stm;

inline constexpr uint64_t HashFromBinding(uint64_t binding, uint64_t arrayIndex) { return (binding << 32) | arrayIndex; }
inline constexpr uint32_t BindingFromHash(uint64_t index) { return (uint32_t)(index >> 32); }

DescriptorSetEntry::operator bool() const {
	switch (mType) {
	case vk::DescriptorType::eCombinedImageSampler:
		return mSamplerValue == nullptr || mTextureValue == nullptr || !mImageView;
	case vk::DescriptorType::eSampler:
		return mSamplerValue == nullptr;
	case vk::DescriptorType::eSampledImage:
	case vk::DescriptorType::eStorageImage:
		return mTextureValue == nullptr || !mImageView;
	case vk::DescriptorType::eUniformTexelBuffer:
	case vk::DescriptorType::eStorageTexelBuffer:
	case vk::DescriptorType::eUniformBuffer:
	case vk::DescriptorType::eStorageBuffer:
	case vk::DescriptorType::eUniformBufferDynamic:
	case vk::DescriptorType::eStorageBufferDynamic:
		return mBufferValue == nullptr;
	case vk::DescriptorType::eInlineUniformBlockEXT:
		return mInlineUniformData == nullptr;
	}
	return true;
}

void DescriptorSetEntry::Clear() {
	switch (mType) {
	case vk::DescriptorType::eSampler:
		mSamplerValue.reset();
		break;
	case vk::DescriptorType::eCombinedImageSampler:
		mSamplerValue.reset();
	case vk::DescriptorType::eSampledImage:
	case vk::DescriptorType::eStorageImage:
		mTextureValue.reset();
		break;
	case vk::DescriptorType::eUniformTexelBuffer:
	case vk::DescriptorType::eStorageTexelBuffer:
	case vk::DescriptorType::eUniformBuffer:
	case vk::DescriptorType::eStorageBuffer:
	case vk::DescriptorType::eUniformBufferDynamic:
	case vk::DescriptorType::eStorageBufferDynamic:
		mBufferValue.reset();
		break;
	case vk::DescriptorType::eInlineUniformBlockEXT:
		safe_delete(mInlineUniformData);
		mInlineUniformDataSize = 0;
		break;
	}
	mType = {};
}
void DescriptorSetEntry::Assign(const DescriptorSetEntry& ds) {
	mType = ds.mType;
	mArrayIndex = ds.mArrayIndex;

	switch (mType) {
	case vk::DescriptorType::eCombinedImageSampler:
		mTextureValue = ds.mTextureValue;
		mImageView = ds.mImageView;
		mImageLayout = ds.mImageLayout;
	case vk::DescriptorType::eSampler:
		mSamplerValue = ds.mSamplerValue;
		break;

	case vk::DescriptorType::eInputAttachment:
	case vk::DescriptorType::eSampledImage:
	case vk::DescriptorType::eStorageImage:
		mTextureValue = ds.mTextureValue;
		mImageView = ds.mImageView;
		mImageLayout = ds.mImageLayout;
		break;

	case vk::DescriptorType::eUniformTexelBuffer:
	case vk::DescriptorType::eStorageTexelBuffer:
	case vk::DescriptorType::eUniformBuffer:
	case vk::DescriptorType::eStorageBuffer:
	case vk::DescriptorType::eUniformBufferDynamic:
	case vk::DescriptorType::eStorageBufferDynamic:
		mBufferValue = ds.mBufferValue;
		mBufferOffset = ds.mBufferOffset;
		mBufferRange = ds.mBufferRange;
		break;
	case vk::DescriptorType::eInlineUniformBlockEXT:
		safe_delete(mInlineUniformData);
		mInlineUniformDataSize = ds.mInlineUniformDataSize;
		mInlineUniformData = new char[mInlineUniformDataSize];
		memcpy(mInlineUniformData, ds.mInlineUniformData, mInlineUniformDataSize);
		break;
	}
}

bool DescriptorSetEntry::operator==(const DescriptorSetEntry& rhs) const {
	if (rhs.mType != mType || rhs.mArrayIndex != mArrayIndex) return false;
	switch (mType) {
	case vk::DescriptorType::eCombinedImageSampler:
		return rhs.mTextureValue == mTextureValue && rhs.mImageView == mImageView && rhs.mImageLayout == mImageLayout && rhs.mSamplerValue == mSamplerValue;
	case vk::DescriptorType::eSampler:
		return rhs.mSamplerValue == mSamplerValue;

	case vk::DescriptorType::eInputAttachment:
	case vk::DescriptorType::eSampledImage:
	case vk::DescriptorType::eStorageImage:
		return rhs.mTextureValue == mTextureValue && rhs.mImageView == mImageView && rhs.mImageLayout == mImageLayout;

	case vk::DescriptorType::eUniformTexelBuffer:
	case vk::DescriptorType::eStorageTexelBuffer:
	case vk::DescriptorType::eUniformBuffer:
	case vk::DescriptorType::eStorageBuffer:
	case vk::DescriptorType::eUniformBufferDynamic:
	case vk::DescriptorType::eStorageBufferDynamic:
		return rhs.mBufferValue == mBufferValue && rhs.mBufferOffset == mBufferOffset && rhs.mBufferValue == mBufferValue;

	case vk::DescriptorType::eInlineUniformBlockEXT:
		return mInlineUniformDataSize == rhs.mInlineUniformDataSize && 
			(mInlineUniformData == rhs.mInlineUniformData ||
			memcmp(mInlineUniformData, rhs.mInlineUniformData, mInlineUniformDataSize) == 0);
	}
	return false;
}

DescriptorSet::DescriptorSet(const string& name, Device* device, vk::DescriptorSetLayout layout) : mName(name), mDevice(device), mLayout(layout) {
	vk::DescriptorSetAllocateInfo allocInfo = {};
	allocInfo.descriptorPool = mDevice->mDescriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;
	lock_guard lock(mDevice->mDescriptorPoolMutex);
	mDescriptorSet = (*mDevice)->allocateDescriptorSets(allocInfo)[0];
	mDevice->SetObjectName(mDescriptorSet, mName);
}
DescriptorSet::~DescriptorSet() {
	mBoundDescriptors.clear();
	mPendingWrites.clear();
	lock_guard lock(mDevice->mDescriptorPoolMutex);
	(*mDevice)->freeDescriptorSets(mDevice->mDescriptorPool, { mDescriptorSet });
}

void DescriptorSet::CreateDescriptor(uint32_t binding, const DescriptorSetEntry& entry) {
	uint64_t idx = HashFromBinding(binding, entry.mArrayIndex);

	// check already bound
	if (mBoundDescriptors.count(idx) && mBoundDescriptors.at(idx) == entry) return;
	
	// null check
	switch (entry.mType) {
    case vk::DescriptorType::eCombinedImageSampler:
    case vk::DescriptorType::eSampler:
			if (entry.mSamplerValue == nullptr) throw invalid_argument("sampler entry was nullptr");
			if (entry.mType == vk::DescriptorType::eSampler) break;
    case vk::DescriptorType::eStorageImage:
    case vk::DescriptorType::eSampledImage:
    case vk::DescriptorType::eInputAttachment:
			if (!entry.mImageView) throw invalid_argument("image view entry was nullptr\n");
			break;

    case vk::DescriptorType::eUniformBuffer:
    case vk::DescriptorType::eStorageBuffer:
    case vk::DescriptorType::eUniformBufferDynamic:
    case vk::DescriptorType::eStorageBufferDynamic:
    case vk::DescriptorType::eUniformTexelBuffer:
    case vk::DescriptorType::eStorageTexelBuffer:
			if (entry.mBufferValue == nullptr) throw invalid_argument("buffer entry was nullptr\n");
			break;

    case vk::DescriptorType::eInlineUniformBlockEXT:
			if (entry.mInlineUniformData == nullptr) throw invalid_argument("inline uniform buffer data entry was nullptr\n");
			break;

    case vk::DescriptorType::eAccelerationStructureKHR:
    default:
			throw invalid_argument("unsupported descriptor type " + to_string(entry.mType));
	}

	mPendingWrites[idx] = entry;
	mBoundDescriptors[idx] = entry;
}

void DescriptorSet::CreateInlineUniformBlock(void* data, size_t dataSize, uint32_t binding){
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eInlineUniformBlockEXT;
	e.mInlineUniformDataSize = dataSize;
	e.mInlineUniformData = new char[dataSize];
	memcpy(e.mInlineUniformData, data, dataSize);
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateUniformBufferDescriptor(shared_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eUniformBuffer;
	e.mArrayIndex = arrayIndex;
	e.mBufferValue = buffer;
	e.mBufferOffset = offset;
	e.mBufferRange = range;
	CreateDescriptor(binding, e);
}
void DescriptorSet::CreateUniformBufferDescriptor(shared_ptr<Buffer> buffer, uint32_t binding, uint32_t arrayIndex) {
	CreateUniformBufferDescriptor(buffer, 0, buffer->Size(), binding, arrayIndex);
}
void DescriptorSet::CreateStorageBufferDescriptor(shared_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eStorageBuffer;
	e.mArrayIndex = arrayIndex;
	e.mBufferValue = buffer;
	e.mBufferOffset = offset;
	e.mBufferRange = range;
	CreateDescriptor(binding, e);
}
void DescriptorSet::CreateStorageBufferDescriptor(shared_ptr<Buffer> buffer, uint32_t binding, uint32_t arrayIndex) {
	CreateStorageBufferDescriptor(buffer, 0, buffer->Size(), binding, arrayIndex);
}
void DescriptorSet::CreateStorageTexelBufferDescriptor(shared_ptr<Buffer> buffer, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mArrayIndex = arrayIndex;
	e.mType = vk::DescriptorType::eStorageTexelBuffer;
	e.mBufferValue = buffer;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateStorageTextureDescriptor(shared_ptr<Texture> texture, uint32_t binding, uint32_t arrayIndex, vk::ImageLayout layout, vk::ImageView view) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eStorageImage;
	e.mArrayIndex = arrayIndex;
	e.mTextureValue = texture;
	e.mImageView = view ? view : texture->View();
	e.mImageLayout = layout;
	e.mSamplerValue = nullptr;
	CreateDescriptor(binding, e);
}
void DescriptorSet::CreateSampledTextureDescriptor(shared_ptr<Texture> texture, uint32_t binding, uint32_t arrayIndex, vk::ImageLayout layout, vk::ImageView view) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eSampledImage;
	e.mArrayIndex = arrayIndex;
	e.mTextureValue = texture;
	e.mImageView = view ? view : texture->View();
	e.mImageLayout = layout;
	e.mSamplerValue = nullptr;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateInputAttachmentDescriptor(shared_ptr<Texture> texture, uint32_t binding, uint32_t arrayIndex, vk::ImageLayout layout, vk::ImageView view) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eInputAttachment;
	e.mArrayIndex = arrayIndex;
	e.mTextureValue = texture;
	e.mImageView = view ? view : texture->View();
	e.mImageLayout = layout;
	e.mSamplerValue = nullptr;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateSamplerDescriptor(std::shared_ptr<Sampler> sampler, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eSampler;
	e.mArrayIndex = arrayIndex;
	e.mTextureValue = nullptr;
	e.mSamplerValue = sampler;
	e.mImageLayout = vk::ImageLayout::eUndefined;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateTextureDescriptor(const std::string& bindingName, shared_ptr<Texture> texture, PipelineVariant* pipeline, uint32_t arrayIndex, vk::ImageLayout layout, vk::ImageView view) {
	if (!pipeline->mShaderVariant->mDescriptorSetBindings.count(bindingName))
		throw invalid_argument("cannot bind texture to nonexistant descriptor");
	
	auto& binding = pipeline->mShaderVariant->mDescriptorSetBindings.at(bindingName);
	switch (binding.mBinding.descriptorType) {
    case vk::DescriptorType::eSampler:
    case vk::DescriptorType::eUniformTexelBuffer:
    case vk::DescriptorType::eStorageTexelBuffer:
    case vk::DescriptorType::eUniformBuffer:
    case vk::DescriptorType::eStorageBuffer:
    case vk::DescriptorType::eUniformBufferDynamic:
    case vk::DescriptorType::eStorageBufferDynamic:
    case vk::DescriptorType::eInlineUniformBlockEXT:
    case vk::DescriptorType::eAccelerationStructureKHR:
    case vk::DescriptorType::eCombinedImageSampler:
			throw invalid_argument("cannot bind texture to binding of type " + to_string(binding.mBinding.descriptorType));
			break;

    case vk::DescriptorType::eInputAttachment:
			CreateInputAttachmentDescriptor(texture, binding.mBinding.binding, arrayIndex, layout, view);
			break;
    case vk::DescriptorType::eSampledImage:
			CreateSampledTextureDescriptor(texture, binding.mBinding.binding, arrayIndex, layout, view);
			break;
    case vk::DescriptorType::eStorageImage:
			CreateStorageTextureDescriptor(texture, binding.mBinding.binding, arrayIndex, layout, view);
			break;
	}
}
void DescriptorSet::CreateBufferDescriptor(const std::string& bindingName, shared_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, PipelineVariant* pipeline, uint32_t arrayIndex) {
	if (!pipeline->mShaderVariant->mDescriptorSetBindings.count(bindingName))
		throw invalid_argument("cannot bind buffer to nonexistant descriptor");
	
	auto& binding = pipeline->mShaderVariant->mDescriptorSetBindings.at(bindingName);
	switch (binding.mBinding.descriptorType) {
    case vk::DescriptorType::eUniformTexelBuffer:
			CreateUniformBufferDescriptor(buffer, offset, range, binding.mBinding.binding, arrayIndex);
			break;
    case vk::DescriptorType::eStorageTexelBuffer:
			CreateUniformBufferDescriptor(buffer, offset, range, binding.mBinding.binding, arrayIndex);
			break;
    case vk::DescriptorType::eUniformBuffer:
			CreateUniformBufferDescriptor(buffer, offset, range, binding.mBinding.binding, arrayIndex);
			break;
    case vk::DescriptorType::eStorageBuffer:
			CreateStorageBufferDescriptor(buffer, offset, range, binding.mBinding.binding, arrayIndex);
			break;
    case vk::DescriptorType::eUniformBufferDynamic:
    case vk::DescriptorType::eStorageBufferDynamic:

    case vk::DescriptorType::eInlineUniformBlockEXT:
    case vk::DescriptorType::eSampler:
    case vk::DescriptorType::eAccelerationStructureKHR:
    case vk::DescriptorType::eCombinedImageSampler:
    case vk::DescriptorType::eInputAttachment:
    case vk::DescriptorType::eSampledImage:
    case vk::DescriptorType::eStorageImage:
			throw invalid_argument("cannot bind buffer to binding of type " + to_string(binding.mBinding.descriptorType));
	}
}
void DescriptorSet::CreateSamplerDescriptor(const std::string& bindingName, shared_ptr<Sampler> sampler, PipelineVariant* pipeline, uint32_t arrayIndex) {
	if (!pipeline->mShaderVariant->mDescriptorSetBindings.count(bindingName))
		throw invalid_argument("cannot bind sampler to nonexistant descriptor");

	auto& binding = pipeline->mShaderVariant->mDescriptorSetBindings.at(bindingName);
	switch (binding.mBinding.descriptorType) {
    case vk::DescriptorType::eSampler:
			CreateSamplerDescriptor(sampler, binding.mBinding.binding, arrayIndex);
			break;

    case vk::DescriptorType::eUniformTexelBuffer:
    case vk::DescriptorType::eStorageTexelBuffer:
    case vk::DescriptorType::eUniformBuffer:
    case vk::DescriptorType::eStorageBuffer:
    case vk::DescriptorType::eUniformBufferDynamic:
    case vk::DescriptorType::eStorageBufferDynamic:
    case vk::DescriptorType::eInlineUniformBlockEXT:
    case vk::DescriptorType::eAccelerationStructureKHR:
    case vk::DescriptorType::eCombinedImageSampler:
    case vk::DescriptorType::eInputAttachment:
    case vk::DescriptorType::eSampledImage:
    case vk::DescriptorType::eStorageImage:
			throw invalid_argument("cannot bind sampler to binding of type " + to_string(binding.mBinding.descriptorType));
	}
}

void DescriptorSet::TransitionTextures(CommandBuffer& commandBuffer) {
	for (auto&[idx, entry] : mBoundDescriptors) {
		switch (entry.mType) {
			case vk::DescriptorType::eSampler:
			case vk::DescriptorType::eUniformTexelBuffer:
			case vk::DescriptorType::eStorageTexelBuffer:
			case vk::DescriptorType::eUniformBuffer:
			case vk::DescriptorType::eStorageBuffer:
			case vk::DescriptorType::eUniformBufferDynamic:
			case vk::DescriptorType::eStorageBufferDynamic:
			case vk::DescriptorType::eInlineUniformBlockEXT:
			case vk::DescriptorType::eAccelerationStructureKHR:
				break;

			case vk::DescriptorType::eCombinedImageSampler:
			case vk::DescriptorType::eInputAttachment:
			case vk::DescriptorType::eSampledImage:
			case vk::DescriptorType::eStorageImage:
				commandBuffer.TransitionBarrier(*entry.mTextureValue, entry.mImageLayout);
				break;
		}
	}
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
			infos[i].mImageInfo.imageView = entry.mImageView;
    case vk::DescriptorType::eSampler:
			infos[i].mImageInfo.sampler = **entry.mSamplerValue;
			writes[i].pImageInfo = &infos[i].mImageInfo;
			break;

    case vk::DescriptorType::eInputAttachment:
    case vk::DescriptorType::eSampledImage:
    case vk::DescriptorType::eStorageImage:
			infos[i].mImageInfo.imageLayout = entry.mImageLayout;
			infos[i].mImageInfo.imageView = entry.mImageView;
			writes[i].pImageInfo = &infos[i].mImageInfo;
			break;

    case vk::DescriptorType::eUniformTexelBuffer:
    case vk::DescriptorType::eStorageTexelBuffer:
			// TODO: infos[i].mTexelBufferView = entry.mBufferValue->View();
			throw exception("texel buffers not currently supported in Stratum");
			writes[i].pTexelBufferView = &infos[i].mTexelBufferView;
			break;

    case vk::DescriptorType::eUniformBuffer:
    case vk::DescriptorType::eStorageBuffer:
    case vk::DescriptorType::eUniformBufferDynamic:
    case vk::DescriptorType::eStorageBufferDynamic:
			infos[i].mBufferInfo.buffer = **entry.mBufferValue;
			infos[i].mBufferInfo.offset = entry.mBufferOffset;
			infos[i].mBufferInfo.range = entry.mBufferRange;
			writes[i].pBufferInfo = &infos[i].mBufferInfo;
			break;
    case vk::DescriptorType::eInlineUniformBlockEXT:
			infos[i].mInlineInfo.pData = entry.mInlineUniformData;
			infos[i].mInlineInfo.dataSize = (uint32_t)entry.mInlineUniformDataSize;
			writes[i].descriptorCount = infos[i].mInlineInfo.dataSize;
			writes[i].pNext = &infos[i].mInlineInfo;
			break;
		}

		i++;
	}

	(*mDevice)->updateDescriptorSets(writes, {});
	mPendingWrites.clear();
}