#pragma once

#include "Buffer.hpp"
#include "Pipeline.hpp"
#include "Texture.hpp"

namespace stm {

class DescriptorSetEntry {
public:
	vk::DescriptorType mType = {};
	vk::DeviceSize mArrayIndex = 0;
	TextureView mTextureView;
	BufferView mTexelBufferView;
	Buffer::ArrayView<> mBufferView;
	byte_blob mInlineUniformData;
	vk::ImageLayout mImageLayout = vk::ImageLayout::eUndefined;
	shared_ptr<Sampler> mSampler;

	STRATUM_API void reset();
	
	DescriptorSetEntry() = default;;
	DescriptorSetEntry(const DescriptorSetEntry& ds) = default;
	inline DescriptorSetEntry(DescriptorSetEntry&& ds) : DescriptorSetEntry(static_cast<DescriptorSetEntry&>(ds)) { ds.reset(); }
	inline ~DescriptorSetEntry() { reset(); }
	DescriptorSetEntry& operator=(const DescriptorSetEntry& rhs) = default;
	DescriptorSetEntry& operator=(DescriptorSetEntry&& rhs) = default;

	STRATUM_API bool operator==(const DescriptorSetEntry& rhs) const;
	STRATUM_API operator bool() const;
};

class DescriptorSet : public DeviceResource {
public:
	inline DescriptorSet(Device& device, const string& name, vk::DescriptorSetLayout layout) : DeviceResource(device, name), mLayout(layout) {
		auto descriptorPool = mDevice.mDescriptorPool.lock();
		vk::DescriptorSetAllocateInfo allocInfo = {};
		allocInfo.descriptorPool = *descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;
		mDescriptorSet = mDevice->allocateDescriptorSets(allocInfo)[0];
		mDevice.SetObjectName(mDescriptorSet, mName);
	}
	inline ~DescriptorSet() {
		mBoundDescriptors.clear();
		mPendingWrites.clear();
		auto descriptorPool = mDevice.mDescriptorPool.lock();
		mDevice->freeDescriptorSets(*descriptorPool, { mDescriptorSet });
	}

	inline operator const vk::DescriptorSet*() const { return &mDescriptorSet; }
	inline operator vk::DescriptorSet() const { return mDescriptorSet; }
	inline vk::DescriptorSetLayout Layout() const { return mLayout; }
	inline const string& Name() const { return mName; }

	STRATUM_API void CreateDescriptor(uint32_t binding, const DescriptorSetEntry& entry);

	STRATUM_API void CreateInlineUniformBlock(uint32_t binding, const byte_blob& data);
	template<typename T>
	inline void CreateInlineUniformBlock(uint32_t binding, const T& value) { CreateInlineUniformBlock(binding, value); }

	STRATUM_API void CreateUniformBufferDescriptor(const Buffer::ArrayView<>& buffer, uint32_t binding, uint32_t arrayIndex = 0);
	STRATUM_API void CreateStorageBufferDescriptor(const Buffer::ArrayView<>& buffer, uint32_t binding, uint32_t arrayIndex = 0);
	
	STRATUM_API void CreateUniformTexelBufferDescriptor(const BufferView& buffer, uint32_t binding, uint32_t arrayIndex = 0);
	STRATUM_API void CreateStorageTexelBufferDescriptor(const BufferView& buffer, uint32_t binding, uint32_t arrayIndex = 0);

	STRATUM_API void CreateStorageTextureDescriptor(const TextureView& texture, uint32_t binding, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eGeneral);	
	STRATUM_API void CreateSampledTextureDescriptor(const TextureView& texture, uint32_t binding, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);
	STRATUM_API void CreateInputAttachmentDescriptor(const TextureView& texture, uint32_t binding, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);
	
	STRATUM_API void CreateSamplerDescriptor(shared_ptr<Sampler> sampler, uint32_t binding, uint32_t arrayIndex = 0);

	STRATUM_API void CreateTextureDescriptor(const string& bindingName, const TextureView& texture, Pipeline& pipeline, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eGeneral);
	STRATUM_API void CreateTexelBufferDescriptor(const string& bindingName, const BufferView& buffer, Pipeline& pipeline, uint32_t arrayIndex = 0);
	STRATUM_API void CreateBufferDescriptor(const string& bindingName, const Buffer::ArrayView<>& buffer, Pipeline& pipeline, uint32_t arrayIndex = 0);
	STRATUM_API void CreateSamplerDescriptor(const string& bindingName, shared_ptr<Sampler> sampler, Pipeline& shader, uint32_t arrayIndex = 0);

	STRATUM_API void FlushWrites();

private:
	friend class Device;
	friend class CommandBuffer;
	vk::DescriptorSet mDescriptorSet;
	vk::DescriptorSetLayout mLayout;
	
	unordered_map<uint64_t/*{binding,arrayIndex}*/, DescriptorSetEntry> mBoundDescriptors;
	unordered_map<uint64_t/*{binding,arrayIndex}*/, DescriptorSetEntry> mPendingWrites;
};

}