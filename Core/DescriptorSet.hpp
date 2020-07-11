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

	ENGINE_EXPORT DescriptorSetEntry();
	ENGINE_EXPORT DescriptorSetEntry(const DescriptorSetEntry& ds);
	ENGINE_EXPORT ~DescriptorSetEntry();

	ENGINE_EXPORT bool IsNull() const;

	ENGINE_EXPORT bool operator==(const DescriptorSetEntry& rhs) const;
};

class DescriptorSet {
public:
	ENGINE_EXPORT DescriptorSet(const std::string& name, Device* device, VkDescriptorSetLayout layout);
	ENGINE_EXPORT ~DescriptorSet();

	ENGINE_EXPORT void CreateDescriptor(uint32_t binding, const DescriptorSetEntry& entry);

	ENGINE_EXPORT void CreateUniformBufferDescriptor(const variant_ptr<Buffer>& buffer, VkDeviceSize offset, VkDeviceSize range, uint32_t binding, uint32_t arrayIndex = 0);
	ENGINE_EXPORT void CreateUniformBufferDescriptor(const variant_ptr<Buffer>& buffer, uint32_t binding, uint32_t arrayIndex = 0);
	ENGINE_EXPORT void CreateStorageBufferDescriptor(const variant_ptr<Buffer>& buffer, VkDeviceSize offset, VkDeviceSize range, uint32_t binding, uint32_t arrayIndex = 0);
	ENGINE_EXPORT void CreateStorageBufferDescriptor(const variant_ptr<Buffer>& buffer, uint32_t binding, uint32_t arrayIndex = 0);
	ENGINE_EXPORT void CreateStorageTexelBufferDescriptor(const variant_ptr<Buffer>& buffer, uint32_t binding, uint32_t arrayIndex = 0);

	ENGINE_EXPORT void CreateStorageTextureDescriptor(const variant_ptr<Texture>& texture, uint32_t binding, uint32_t arrayIndex = 0, VkImageView view = VK_NULL_HANDLE, VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL);	
	ENGINE_EXPORT void CreateSampledTextureDescriptor(const variant_ptr<Texture>& texture, uint32_t binding, uint32_t arrayIndex = 0, VkImageView view = VK_NULL_HANDLE, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	
	ENGINE_EXPORT void CreateSamplerDescriptor(const variant_ptr<Sampler>& sampler, uint32_t binding, uint32_t index = 0);

	ENGINE_EXPORT void FlushWrites();

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