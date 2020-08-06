#pragma once

#include <Util/Util.hpp>

class Device;
class Texture;
class Buffer;
class Sampler;

struct DescriptorSetEntry {

	VkDescriptorType mType;
	VkDeviceSize mArrayIndex;

	variant_ptr<Buffer> mBufferValue;
	VkDeviceSize mBufferOffset;
	VkDeviceSize mBufferRange;

	variant_ptr<Texture> mTextureValue;
	VkImageView mImageView;
	VkImageLayout mImageLayout;
	
	variant_ptr<Sampler> mSamplerValue;

	void* mInlineUniformData;
	size_t mInlineUniformDataSize;

	STRATUM_API DescriptorSetEntry();
	STRATUM_API DescriptorSetEntry(const DescriptorSetEntry& ds);
	STRATUM_API ~DescriptorSetEntry();

	STRATUM_API bool IsNull() const;

	STRATUM_API DescriptorSetEntry& operator =(const DescriptorSetEntry& rhs);
	STRATUM_API bool operator==(const DescriptorSetEntry& rhs) const;
};

class DescriptorSet {
public:
	STRATUM_API DescriptorSet(const std::string& name, Device* device, VkDescriptorSetLayout layout);
	STRATUM_API ~DescriptorSet();

	STRATUM_API void CreateDescriptor(uint32_t binding, const DescriptorSetEntry& entry);

	STRATUM_API void CreateInlineUniformBlock(void* data, size_t dataSize, uint32_t binding);
	template<typename T>
	inline void CreateInlineUniformBlock(const T& value, uint32_t binding) { CreateInlineUniformBlock((void*)&value, sizeof(T), binding); }

	STRATUM_API void CreateUniformBufferDescriptor(const variant_ptr<Buffer>& buffer, VkDeviceSize offset, VkDeviceSize range, uint32_t binding, uint32_t arrayIndex = 0);
	STRATUM_API void CreateUniformBufferDescriptor(const variant_ptr<Buffer>& buffer, uint32_t binding, uint32_t arrayIndex = 0);
	STRATUM_API void CreateStorageBufferDescriptor(const variant_ptr<Buffer>& buffer, VkDeviceSize offset, VkDeviceSize range, uint32_t binding, uint32_t arrayIndex = 0);
	STRATUM_API void CreateStorageBufferDescriptor(const variant_ptr<Buffer>& buffer, uint32_t binding, uint32_t arrayIndex = 0);
	STRATUM_API void CreateStorageTexelBufferDescriptor(const variant_ptr<Buffer>& buffer, uint32_t binding, uint32_t arrayIndex = 0);

	STRATUM_API void CreateStorageTextureDescriptor(const variant_ptr<Texture>& texture, uint32_t binding, uint32_t arrayIndex = 0, VkImageView view = VK_NULL_HANDLE, VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL);	
	STRATUM_API void CreateSampledTextureDescriptor(const variant_ptr<Texture>& texture, uint32_t binding, uint32_t arrayIndex = 0, VkImageView view = VK_NULL_HANDLE, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	STRATUM_API void CreateInputAttachmentDescriptor(const variant_ptr<Texture>& texture, uint32_t binding, uint32_t arrayIndex = 0, VkImageView view = VK_NULL_HANDLE, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	
	STRATUM_API void CreateSamplerDescriptor(const variant_ptr<Sampler>& sampler, uint32_t binding, uint32_t arrayIndex = 0);

	STRATUM_API void FlushWrites();

	inline VkDescriptorSetLayout Layout() const { return mLayout; }
	inline operator const VkDescriptorSet*() const { return &mDescriptorSet; }
	inline operator VkDescriptorSet() const { return mDescriptorSet; }

private:
	friend class Device;
	std::unordered_map<uint64_t/*<binding,arrayIndex>*/, DescriptorSetEntry> mBoundDescriptors;
	std::unordered_map<uint64_t/*<binding,arrayIndex>*/, DescriptorSetEntry> mPendingWrites;
	
	Device* mDevice;
	VkDescriptorSet mDescriptorSet;
	VkDescriptorSetLayout mLayout;
};