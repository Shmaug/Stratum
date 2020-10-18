#pragma once

#include "../Data/Texture.hpp"

#include "Pipeline.hpp"
#include "Buffer.hpp"

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
			std::shared_ptr<Texture> mTextureValue;
			std::shared_ptr<Sampler> mSamplerValue;
			vk::ImageView mImageView;
			vk::ImageLayout mImageLayout;
		};
		struct {
			std::shared_ptr<Buffer> mBufferValue;
			vk::DeviceSize mBufferOffset;
			vk::DeviceSize mBufferRange;
		};
		struct {
			void* mInlineUniformData;
			size_t mInlineUniformDataSize;
		};
	};

	inline DescriptorSetEntry() : mTextureValue(nullptr), mSamplerValue(nullptr) {};
	inline DescriptorSetEntry(const DescriptorSetEntry& ds) { Assign(ds); }
	inline DescriptorSetEntry(DescriptorSetEntry&& ds) { Assign(ds); ds.Clear(); }
	inline ~DescriptorSetEntry() { Clear(); }

	inline DescriptorSetEntry& operator=(const DescriptorSetEntry& ds) { Clear(); Assign(ds); return *this; }	

	STRATUM_API bool operator==(const DescriptorSetEntry& rhs) const;
	STRATUM_API operator bool() const;
};

class DescriptorSet {
private:
	vk::DescriptorSet mDescriptorSet;
	
public:
  const std::string mName;
  stm::Device* const mDevice;
	
	STRATUM_API DescriptorSet(const std::string& name, Device* device, vk::DescriptorSetLayout layout);
	STRATUM_API ~DescriptorSet();


	STRATUM_API void CreateDescriptor(uint32_t binding, const DescriptorSetEntry& entry);


	STRATUM_API void CreateInlineUniformBlock(void* data, size_t dataSize, uint32_t binding);
	template<typename T>
	inline void CreateInlineUniformBlock(const T& value, uint32_t binding) { CreateInlineUniformBlock((void*)&value, sizeof(T), binding); }

	STRATUM_API void CreateUniformBufferDescriptor(std::shared_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t binding, uint32_t arrayIndex = 0);
	STRATUM_API void CreateUniformBufferDescriptor(std::shared_ptr<Buffer> buffer, uint32_t binding, uint32_t arrayIndex = 0);
	STRATUM_API void CreateStorageBufferDescriptor(std::shared_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t binding, uint32_t arrayIndex = 0);
	STRATUM_API void CreateStorageBufferDescriptor(std::shared_ptr<Buffer> buffer, uint32_t binding, uint32_t arrayIndex = 0);
	STRATUM_API void CreateStorageTexelBufferDescriptor(std::shared_ptr<Buffer> buffer, uint32_t binding, uint32_t arrayIndex = 0);

	STRATUM_API void CreateStorageTextureDescriptor(std::shared_ptr<Texture> texture, uint32_t binding, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eGeneral, vk::ImageView view = nullptr);	
	STRATUM_API void CreateSampledTextureDescriptor(std::shared_ptr<Texture> texture, uint32_t binding, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageView view = nullptr);
	STRATUM_API void CreateInputAttachmentDescriptor(std::shared_ptr<Texture> texture, uint32_t binding, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageView view = nullptr);
	
	STRATUM_API void CreateSamplerDescriptor(std::shared_ptr<Sampler> sampler, uint32_t binding, uint32_t arrayIndex = 0);
	

	STRATUM_API void CreateTextureDescriptor(const std::string& bindingName, std::shared_ptr<Texture> texture, PipelineVariant* pipeline, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eGeneral, vk::ImageView view = nullptr);
	STRATUM_API void CreateBufferDescriptor(const std::string& bindingName, std::shared_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, PipelineVariant* pipeline, uint32_t arrayIndex = 0);
	inline void CreateBufferDescriptor(const std::string& bindingName, std::shared_ptr<Buffer> buffer, PipelineVariant* pipeline, uint32_t arrayIndex = 0) { CreateBufferDescriptor(bindingName, buffer, 0, buffer->Size(), pipeline, arrayIndex); }
	STRATUM_API void CreateSamplerDescriptor(const std::string& bindingName, std::shared_ptr<Sampler> sampler, PipelineVariant* pipeline, uint32_t arrayIndex = 0);


	STRATUM_API void FlushWrites();
	STRATUM_API void TransitionTextures(CommandBuffer& commandBuffer);

	inline vk::DescriptorSetLayout Layout() const { return mLayout; }
	inline operator const vk::DescriptorSet*() const { return &mDescriptorSet; }
	inline operator vk::DescriptorSet() const { return mDescriptorSet; }

private:
	friend class Device;
	vk::DescriptorSetLayout mLayout;
	
	std::unordered_map<uint64_t/*<binding,arrayIndex>*/, DescriptorSetEntry> mBoundDescriptors;
	std::unordered_map<uint64_t/*<binding,arrayIndex>*/, DescriptorSetEntry> mPendingWrites;
};

}