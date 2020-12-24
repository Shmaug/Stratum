#pragma once

#include "Buffer.hpp"
#include "Pipeline.hpp"

namespace stm {

class DescriptorSetEntry {
private:
	STRATUM_API void Clear();
	STRATUM_API void Assign(const DescriptorSetEntry& ds);

public:
	vk::DescriptorType mType = {};
	vk::DeviceSize mArrayIndex = 0;
	union {
		struct {
			shared_ptr<Texture> mTexture;
			shared_ptr<Sampler> mSampler;
			vk::ImageView mView;
			vk::ImageLayout mLayout;
		} mImageView;
		struct {
			shared_ptr<Buffer> mBuffer;
			vk::DeviceSize mOffset;
			vk::DeviceSize mRange;
		} mBufferView;
		byte_blob mInlineUniformData;
	};

	inline DescriptorSetEntry() : mImageView({}) {};
	inline DescriptorSetEntry(const DescriptorSetEntry& ds) { Assign(ds); }
	inline DescriptorSetEntry(DescriptorSetEntry&& ds) { Assign(ds); ds.Clear(); }
	inline ~DescriptorSetEntry() { Clear(); }

	inline DescriptorSetEntry& operator=(const DescriptorSetEntry& ds) { Clear(); Assign(ds); return *this; }	

	STRATUM_API bool operator==(const DescriptorSetEntry& rhs) const;
	STRATUM_API operator bool() const;
};

class DescriptorSet {
private:
	friend class Device;
	friend class CommandBuffer;
	vk::DescriptorSet mDescriptorSet;
	vk::DescriptorSetLayout mLayout;
	
	unordered_map<uint64_t/*{binding,arrayIndex}*/, DescriptorSetEntry> mBoundDescriptors;
	unordered_map<uint64_t/*{binding,arrayIndex}*/, DescriptorSetEntry> mPendingWrites;
	
  stm::Device& mDevice;
  string mName;

public:
	STRATUM_API DescriptorSet(const string& name, stm::Device& device, vk::DescriptorSetLayout layout);
	STRATUM_API ~DescriptorSet();

	inline operator const vk::DescriptorSet*() const { return &mDescriptorSet; }
	inline operator vk::DescriptorSet() const { return mDescriptorSet; }
	inline vk::DescriptorSetLayout Layout() const { return mLayout; }
	inline stm::Device& Device() const { return mDevice; }
	inline const string& Name() const { return mName; }

	STRATUM_API void CreateDescriptor(uint32_t binding, const DescriptorSetEntry& entry);

	STRATUM_API void CreateInlineUniformBlock(uint32_t binding, const byte_blob& data);
	template<typename T>
	inline void CreateInlineUniformBlock(uint32_t binding, const T& value) { CreateInlineUniformBlock(binding, value); }

	STRATUM_API void CreateUniformBufferDescriptor(shared_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t binding, uint32_t arrayIndex = 0);
	STRATUM_API void CreateUniformBufferDescriptor(shared_ptr<Buffer> buffer, uint32_t binding, uint32_t arrayIndex = 0);
	STRATUM_API void CreateStorageBufferDescriptor(shared_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t binding, uint32_t arrayIndex = 0);
	STRATUM_API void CreateStorageBufferDescriptor(shared_ptr<Buffer> buffer, uint32_t binding, uint32_t arrayIndex = 0);
	STRATUM_API void CreateStorageTexelBufferDescriptor(shared_ptr<Buffer> buffer, uint32_t binding, uint32_t arrayIndex = 0);

	STRATUM_API void CreateStorageTextureDescriptor(shared_ptr<Texture> texture, uint32_t binding, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eGeneral, vk::ImageView view = nullptr);	
	STRATUM_API void CreateSampledTextureDescriptor(shared_ptr<Texture> texture, uint32_t binding, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageView view = nullptr);
	STRATUM_API void CreateInputAttachmentDescriptor(shared_ptr<Texture> texture, uint32_t binding, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageView view = nullptr);
	
	STRATUM_API void CreateSamplerDescriptor(shared_ptr<Sampler> sampler, uint32_t binding, uint32_t arrayIndex = 0);

	STRATUM_API void CreateTextureDescriptor(const string& bindingName, shared_ptr<Texture> texture, Pipeline& pipeline, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eGeneral, vk::ImageView view = nullptr);
	STRATUM_API void CreateBufferDescriptor(const string& bindingName, shared_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, Pipeline& pipeline, uint32_t arrayIndex = 0);
	inline void CreateBufferDescriptor(const string& bindingName, shared_ptr<Buffer> buffer, Pipeline& pipeline, uint32_t arrayIndex = 0) { CreateBufferDescriptor(bindingName, buffer, 0, buffer->Size(), pipeline, arrayIndex); }
	STRATUM_API void CreateSamplerDescriptor(const string& bindingName, shared_ptr<Sampler> sampler, Pipeline& shader, uint32_t arrayIndex = 0);

	STRATUM_API void FlushWrites();
};

}