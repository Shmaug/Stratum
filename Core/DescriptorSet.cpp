#include <Core/DescriptorSet.hpp>
#include <Core/CommandBuffer.hpp>
#include <Core/Device.hpp>
#include <Core/Pipeline.hpp>
#include <Core/Buffer.hpp>
#include <Data/Texture.hpp>

using namespace std;

#define DESCRIPTOR_INDEX(binding, arrayIndex) ((((uint64_t)binding) << 32) | ((uint64_t)arrayIndex))
#define BINDING_FROM_INDEX(index) (uint32_t)(index >> 32)

DescriptorSetEntry::DescriptorSetEntry(const DescriptorSetEntry& ds) : mType(ds.mType), mArrayIndex(ds.mArrayIndex) { operator=(ds); }
DescriptorSetEntry::~DescriptorSetEntry() {
	safe_delete(mInlineUniformData);
	mTextureValue.reset();
	mSamplerValue.reset();
	mTextureValue.reset();
	mBufferValue.reset();
}

bool DescriptorSetEntry::IsNull() const {
	switch (mType) {
	case vk::DescriptorType::eCombinedImageSampler:
		return mSamplerValue.get() == nullptr || mTextureValue.get() == nullptr || !mImageView;
	case vk::DescriptorType::eSampler:
		return mSamplerValue.get() == nullptr;
	case vk::DescriptorType::eSampledImage:
	case vk::DescriptorType::eStorageImage:
		return mTextureValue.get() == nullptr || !mImageView;
	case vk::DescriptorType::eUniformTexelBuffer:
	case vk::DescriptorType::eStorageTexelBuffer:
	case vk::DescriptorType::eUniformBuffer:
	case vk::DescriptorType::eStorageBuffer:
	case vk::DescriptorType::eUniformBufferDynamic:
	case vk::DescriptorType::eStorageBufferDynamic:
		return !mBufferValue.get();
	case vk::DescriptorType::eInlineUniformBlockEXT:
		return mInlineUniformData == nullptr;
	}
	return true;
}

DescriptorSetEntry& DescriptorSetEntry::operator=(const DescriptorSetEntry& ds) {
	mType = ds.mType;
	mArrayIndex = ds.mArrayIndex;
	safe_delete(mInlineUniformData);
	mInlineUniformDataSize = 0;
	switch (mType) {
	case vk::DescriptorType::eCombinedImageSampler:
		mTextureValue = ds.mTextureValue;
		mImageView = ds.mImageView;
		mImageLayout = ds.mImageLayout;
	case vk::DescriptorType::eSampler:
		mSamplerValue = ds.mSamplerValue;
		mBufferValue.reset();
		break;

	case vk::DescriptorType::eInputAttachment:
	case vk::DescriptorType::eSampledImage:
	case vk::DescriptorType::eStorageImage:
		mTextureValue = ds.mTextureValue;
		mImageView = ds.mImageView;
		mImageLayout = ds.mImageLayout;
		mBufferValue.reset();
		mSamplerValue.reset();
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
		mTextureValue.reset();
		mSamplerValue.reset();
		break;
	case vk::DescriptorType::eInlineUniformBlockEXT:
		mInlineUniformDataSize = ds.mInlineUniformDataSize;
		mInlineUniformData = new char[mInlineUniformDataSize];
		memcpy(mInlineUniformData, ds.mInlineUniformData, mInlineUniformDataSize);
		break;
	}
	return *this;
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

DescriptorSet::DescriptorSet(const string& name, Device* device, vk::DescriptorSetLayout layout) : mDevice(device), mLayout(layout) {
	vk::DescriptorSetAllocateInfo allocInfo = {};
	allocInfo.descriptorPool = mDevice->mDescriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;
	device->mDescriptorPoolMutex.lock();
	mDescriptorSet = ((vk::Device)*mDevice).allocateDescriptorSets(allocInfo)[0];
	mDevice->SetObjectName(mDescriptorSet, name);
	mDevice->mDescriptorSetCount++;
	device->mDescriptorPoolMutex.unlock();
}
DescriptorSet::~DescriptorSet() {
	mBoundDescriptors.clear();
	mPendingWrites.clear();

	mDevice->mDescriptorPoolMutex.lock();
	((vk::Device)*mDevice).freeDescriptorSets(mDevice->mDescriptorPool, { mDescriptorSet });
	mDevice->mDescriptorSetCount--;
	mDevice->mDescriptorPoolMutex.unlock();
}

void DescriptorSet::CreateDescriptor(uint32_t binding, const DescriptorSetEntry& entry) {
	uint64_t idx = DESCRIPTOR_INDEX(binding, entry.mArrayIndex);

	// check already bound
	if (mBoundDescriptors.count(idx) && mBoundDescriptors.at(idx) == entry) return;
	
	// null check
	switch (entry.mType) {
    case vk::DescriptorType::eCombinedImageSampler:
    case vk::DescriptorType::eSampler:
			if (entry.mSamplerValue == nullptr) {
				fprintf_color(ConsoleColorBits::eRed, stderr, "Error: Binding null sampler\n");
				throw;
				return;
			}
			if (entry.mType == vk::DescriptorType::eSampler) break;
    case vk::DescriptorType::eStorageImage:
    case vk::DescriptorType::eSampledImage:
    case vk::DescriptorType::eInputAttachment:
			if (!entry.mImageView) {
				fprintf_color(ConsoleColorBits::eRed, stderr, "Error: Binding null image\n");
				throw;
				return;
			}
			break;

    case vk::DescriptorType::eUniformBuffer:
    case vk::DescriptorType::eStorageBuffer:
    case vk::DescriptorType::eUniformBufferDynamic:
    case vk::DescriptorType::eStorageBufferDynamic:
    case vk::DescriptorType::eUniformTexelBuffer:
    case vk::DescriptorType::eStorageTexelBuffer:
			if (entry.mBufferValue == nullptr) {
				fprintf_color(ConsoleColorBits::eRed, stderr, "Error: Binding null buffer\n");
				throw;
				return;
			}
			break;

    case vk::DescriptorType::eInlineUniformBlockEXT:
			if (entry.mInlineUniformData == nullptr){
				fprintf_color(ConsoleColorBits::eRed, stderr, "Error: Binding null inline uniform buffer data\n");
				throw;
				return;
			}
			break;

    case vk::DescriptorType::eAccelerationStructureKHR:
    default:
			fprintf_color(ConsoleColorBits::eRed, stderr, "Error: Binding unsupported descriptor type\n");
			throw;
			return;
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

void DescriptorSet::CreateUniformBufferDescriptor(const variant_ptr<Buffer>& buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eUniformBuffer;
	e.mArrayIndex = arrayIndex;
	e.mBufferValue = buffer;
	e.mBufferOffset = offset;
	e.mBufferRange = range;
	CreateDescriptor(binding, e);
}
void DescriptorSet::CreateUniformBufferDescriptor(const variant_ptr<Buffer>& buffer, uint32_t binding, uint32_t arrayIndex) {
	CreateUniformBufferDescriptor(buffer, 0, buffer->Size(), binding, arrayIndex);
}
void DescriptorSet::CreateStorageBufferDescriptor(const variant_ptr<Buffer>& buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eStorageBuffer;
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
	e.mType = vk::DescriptorType::eStorageTexelBuffer;
	e.mBufferValue = buffer;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateStorageTextureDescriptor(const variant_ptr<Texture>& texture, uint32_t binding, uint32_t arrayIndex, vk::ImageLayout layout, vk::ImageView view) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eStorageImage;
	e.mArrayIndex = arrayIndex;
	e.mTextureValue = texture;
	e.mImageView = view ? view : texture->View();
	e.mImageLayout = layout;
	e.mSamplerValue = nullptr;
	CreateDescriptor(binding, e);
}
void DescriptorSet::CreateSampledTextureDescriptor(const variant_ptr<Texture>& texture, uint32_t binding, uint32_t arrayIndex, vk::ImageLayout layout, vk::ImageView view) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eSampledImage;
	e.mArrayIndex = arrayIndex;
	e.mTextureValue = texture;
	e.mImageView = view ? view : texture->View();
	e.mImageLayout = layout;
	e.mSamplerValue = nullptr;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateInputAttachmentDescriptor(const variant_ptr<Texture>& texture, uint32_t binding, uint32_t arrayIndex, vk::ImageLayout layout, vk::ImageView view) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eInputAttachment;
	e.mArrayIndex = arrayIndex;
	e.mTextureValue = texture;
	e.mImageView = view ? view : texture->View();
	e.mImageLayout = layout;
	e.mSamplerValue = nullptr;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateSamplerDescriptor(const variant_ptr<Sampler>& sampler, uint32_t binding, uint32_t arrayIndex) {
	DescriptorSetEntry e = {};
	e.mType = vk::DescriptorType::eSampler;
	e.mArrayIndex = arrayIndex;
	e.mTextureValue = nullptr;
	e.mSamplerValue = sampler;
	e.mImageLayout = vk::ImageLayout::eUndefined;
	CreateDescriptor(binding, e);
}

void DescriptorSet::CreateTextureDescriptor(const std::string& bindingName, const variant_ptr<Texture>& texture, PipelineVariant* pipeline, uint32_t arrayIndex, vk::ImageLayout layout, vk::ImageView view) {
	if (!pipeline->mShaderVariant->mDescriptorSetBindings.count(bindingName)) {
		fprintf_color(ConsoleColorBits::eYellow, stderr, "Warning: Attempt to bind texture %s to descriptor %s, which does not exist\n", texture->mName.c_str(), bindingName.c_str());
		return;
	}
	
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
			fprintf_color(ConsoleColorBits::eYellow, stderr, "Warning: Attempt to bind texture %s to descriptor %s, which is %s\n", texture->mName.c_str(), bindingName, to_string(binding.mBinding.descriptorType).c_str());
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
void DescriptorSet::CreateBufferDescriptor(const std::string& bindingName, const variant_ptr<Buffer>& buffer, vk::DeviceSize offset, vk::DeviceSize range, PipelineVariant* pipeline, uint32_t arrayIndex) {
	if (!pipeline->mShaderVariant->mDescriptorSetBindings.count(bindingName)) {
		fprintf_color(ConsoleColorBits::eYellow, stderr, "Warning: Attempt to bind buffer %s to descriptor %s, which does not exist\n", buffer->mName.c_str(), bindingName.c_str());
		return;
	}
	
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
			fprintf_color(ConsoleColorBits::eYellow, stderr, "Warning: Attempt to bind buffer %s to descriptor %s, which is %s\n", buffer->mName.c_str(), bindingName, to_string(binding.mBinding.descriptorType).c_str());
			break;
	}
}
void DescriptorSet::CreateSamplerDescriptor(const std::string& bindingName, const variant_ptr<Sampler>& sampler, PipelineVariant* pipeline, uint32_t arrayIndex) {
	if (!pipeline->mShaderVariant->mDescriptorSetBindings.count(bindingName)) {
		fprintf_color(ConsoleColorBits::eYellow, stderr, "Warning: Attempt to bind sampler %s to descriptor %s, which does not exist\n", sampler->mName.c_str(), bindingName.c_str());
		return;
	}
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
			fprintf_color(ConsoleColorBits::eYellow, stderr, "Warning: Attempt to bind sampler %s to descriptor %s, which is %s\n", sampler->mName.c_str(), bindingName, to_string(binding.mBinding.descriptorType).c_str());
			break;
	}
}

void DescriptorSet::TransitionTextures(CommandBuffer* commandBuffer) {
	for (auto kp : mBoundDescriptors) {
		switch (kp.second.mType) {
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
				commandBuffer->TransitionBarrier(kp.second.mTextureValue.get(), kp.second.mImageLayout);
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
	for (auto& kp : mPendingWrites) {
		writes[i].dstSet = mDescriptorSet;
		writes[i].dstBinding = BINDING_FROM_INDEX(kp.first);
		writes[i].dstArrayElement = (uint32_t)kp.second.mArrayIndex;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = kp.second.mType;

		switch (kp.second.mType) {
    case vk::DescriptorType::eCombinedImageSampler:
			infos[i].mImageInfo.imageLayout = kp.second.mImageLayout;
			infos[i].mImageInfo.imageView = kp.second.mImageView;
    case vk::DescriptorType::eSampler:
			infos[i].mImageInfo.sampler = *kp.second.mSamplerValue.get();
			writes[i].pImageInfo = &infos[i].mImageInfo;
			break;

    case vk::DescriptorType::eInputAttachment:
    case vk::DescriptorType::eSampledImage:
    case vk::DescriptorType::eStorageImage:
			infos[i].mImageInfo.imageLayout = kp.second.mImageLayout;
			infos[i].mImageInfo.imageView = kp.second.mImageView;
			writes[i].pImageInfo = &infos[i].mImageInfo;
			break;

    case vk::DescriptorType::eUniformTexelBuffer:
    case vk::DescriptorType::eStorageTexelBuffer:
			infos[i].mTexelBufferView = kp.second.mBufferValue->View();
			writes[i].pTexelBufferView = &infos[i].mTexelBufferView;
			break;

    case vk::DescriptorType::eUniformBuffer:
    case vk::DescriptorType::eStorageBuffer:
    case vk::DescriptorType::eUniformBufferDynamic:
    case vk::DescriptorType::eStorageBufferDynamic:
			infos[i].mBufferInfo.buffer = *kp.second.mBufferValue.get();
			infos[i].mBufferInfo.offset = kp.second.mBufferOffset;
			infos[i].mBufferInfo.range = kp.second.mBufferRange;
			writes[i].pBufferInfo = &infos[i].mBufferInfo;
			break;
    case vk::DescriptorType::eInlineUniformBlockEXT:
			infos[i].mInlineInfo.pData = kp.second.mInlineUniformData;
			infos[i].mInlineInfo.dataSize = (uint32_t)kp.second.mInlineUniformDataSize;
			writes[i].descriptorCount = infos[i].mInlineInfo.dataSize;
			writes[i].pNext = &infos[i].mInlineInfo;
			break;
		}

		i++;
	}

	((vk::Device)*mDevice).updateDescriptorSets(writes, {});
	mPendingWrites.clear();
}