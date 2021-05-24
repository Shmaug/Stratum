#pragma once

#include "Texture.hpp"

namespace stm {

using Descriptor = variant<
	tuple<Texture::View, vk::ImageLayout, shared_ptr<Sampler>>, // sampler/image/combined image sampler
	Buffer::StrideView,											// storage/uniform buffer, stride used for dynamic offsets to work
	Buffer::TexelView, 											// texel buffer
	byte_blob, 													// inline buffer data
	vk::AccelerationStructureKHR 								// acceleration structure
>;

template<typename T> requires(is_same_v<T,Texture::View> || is_same_v<T,vk::ImageLayout> || is_same_v<T,shared_ptr<Sampler>>)
constexpr T& get(stm::Descriptor& d) { return get<T>(get<0>(d)); }
template<typename T> requires(is_same_v<T,Texture::View> || is_same_v<T,vk::ImageLayout> || is_same_v<T,shared_ptr<Sampler>>)
constexpr const T& get(const stm::Descriptor& d) { return get<T>(get<0>(d)); }

inline Descriptor texture_descriptor(const Texture::View& texture, const vk::ImageLayout& layout, const shared_ptr<Sampler>& sampler) {
	return variant_alternative_t<0,Descriptor>{texture, layout, sampler};
}
inline Descriptor sampler_descriptor(const shared_ptr<Sampler>& sampler) {
	return texture_descriptor(Texture::View(), vk::ImageLayout::eUndefined, sampler);
}
inline Descriptor storage_texture_descriptor(const Texture::View& texture) {
	return texture_descriptor(texture, vk::ImageLayout::eGeneral, nullptr);
}
inline Descriptor sampled_texture_descriptor(const Texture::View& texture, const shared_ptr<Sampler>& sampler = nullptr) {
	return texture_descriptor(texture, vk::ImageLayout::eShaderReadOnlyOptimal, sampler);
}

class DescriptorSetLayout : public DeviceResource {
public:
	struct Binding {
		vk::DescriptorType mDescriptorType;
		vk::ShaderStageFlags mStageFlags;
		uint32_t mDescriptorCount;
		vector<shared_ptr<Sampler>> mImmutableSamplers;
	};

	inline DescriptorSetLayout(Device& device, const string& name, const unordered_map<uint32_t, Binding>& bindings = {}, vk::DescriptorSetLayoutCreateFlags flags = {}) : DeviceResource(device, name), mFlags(flags), mBindings(bindings) {
		forward_list<vector<vk::Sampler>> immutableSamplers; // store for duration of vk::DescriptorSetLayoutCreateInfo
		vector<vk::DescriptorSetLayoutBinding> vkbindings;
		for (auto&[i, binding] : mBindings) {
			if (binding.mDescriptorType == vk::DescriptorType::eUniformBufferDynamic || binding.mDescriptorType == vk::DescriptorType::eStorageBufferDynamic)
				mDynamicBindings.emplace(i);
			auto& b = vkbindings.emplace_back(i, binding.mDescriptorType, binding.mDescriptorCount, binding.mStageFlags);
			if (!binding.mImmutableSamplers.empty()) {
					b.pImmutableSamplers = immutableSamplers.emplace_front(binding.mImmutableSamplers.size()).data(),
				ranges::transform(binding.mImmutableSamplers, const_cast<vk::Sampler*>(b.pImmutableSamplers), [](const shared_ptr<Sampler>& v){ return **v; });
			}
		}
		mLayout = mDevice->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo(mFlags, vkbindings));
	}
	inline ~DescriptorSetLayout() {
		mDevice->destroyDescriptorSetLayout(mLayout);
	}

	inline operator vk::DescriptorSetLayout*() { return &mLayout; }
	inline operator vk::DescriptorSetLayout&() { return mLayout; }
	inline operator const vk::DescriptorSetLayout*() const { return &mLayout; }
	inline operator const vk::DescriptorSetLayout&() const { return mLayout; }

	inline const Binding& at(uint32_t binding) const { return mBindings.at(binding); }
	inline const Binding& operator[](uint32_t binding) const { return mBindings.at(binding); }

	inline const unordered_map<uint32_t, Binding>& bindings() const { return mBindings; }
	inline const unordered_set<uint32_t>& dynamic_bindings() const { return mDynamicBindings; }

private:
	vk::DescriptorSetLayout mLayout;
	vk::DescriptorSetLayoutCreateFlags mFlags;
	unordered_map<uint32_t, Binding> mBindings;
	unordered_set<uint32_t> mDynamicBindings;
};

class DescriptorSet : public DeviceResource {
private:
	friend class Device;
	friend class CommandBuffer;
	vk::DescriptorSet mDescriptorSet;
	shared_ptr<const DescriptorSetLayout> mLayout;
	
	unordered_map<uint64_t/*{binding,arrayIndex}*/, Descriptor> mDescriptors;
	unordered_set<uint64_t> mPendingWrites;

public:
	inline DescriptorSet(shared_ptr<const DescriptorSetLayout> layout, const string& name) : DeviceResource(layout->mDevice, name), mLayout(layout) {
		auto descriptorPool = mDevice.mDescriptorPool.lock();
		vk::DescriptorSetAllocateInfo allocInfo = {};
		allocInfo.descriptorPool = *descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &**mLayout;
		mDescriptorSet = mDevice->allocateDescriptorSets(allocInfo)[0];
		mDevice.set_debug_name(mDescriptorSet, name);
	}
	inline DescriptorSet(shared_ptr<const DescriptorSetLayout> layout, const string& name, const unordered_map<uint32_t, Descriptor>& bindings) : DescriptorSet(layout, name) {
		for (const auto&[binding, d] : bindings)
			insert_or_assign(binding, d);
	}
	inline DescriptorSet(shared_ptr<const DescriptorSetLayout> layout, const string& name, const unordered_map<uint32_t, unordered_map<uint32_t, Descriptor>>& bindings) : DescriptorSet(layout, name) {
		for (const auto&[binding, entries] : bindings)
			for (const auto&[arrayIndex, d] : entries)
				insert_or_assign(binding, arrayIndex, d);
	}
	inline ~DescriptorSet() {
		mDescriptors.clear();
		mPendingWrites.clear();
		auto descriptorPool = mDevice.mDescriptorPool.lock();
		mLayout.reset();
		mDevice->freeDescriptorSets(*descriptorPool, { mDescriptorSet });
	}

	inline operator const vk::DescriptorSet*() const { return &mDescriptorSet; }
	inline operator vk::DescriptorSet() const { return mDescriptorSet; }

	inline auto layout() const { return mLayout; }
	inline const DescriptorSetLayout::Binding& layout_at(uint32_t binding) const { return mLayout->at(binding); }

	inline const Descriptor* find(uint32_t binding, uint32_t arrayIndex = 0) const {
		auto it = mDescriptors.find((uint64_t(binding)<<32)|arrayIndex);
		if (it == mDescriptors.end()) nullptr;
		return &it->second;
	}
	inline const Descriptor& at(uint32_t binding, uint32_t arrayIndex = 0) const { return mDescriptors.at((uint64_t(binding)<<32)|arrayIndex); }
	inline const Descriptor& operator[](uint32_t binding) const { return at(binding); }

	inline void insert_or_assign(uint32_t binding, uint32_t arrayIndex, const Descriptor& entry) {
		uint64_t key = (uint64_t(binding)<<32)|arrayIndex;
		mPendingWrites.emplace(key);
		mDescriptors[key] = entry;
	}
	inline void insert_or_assign(uint32_t binding, const Descriptor& entry) { insert_or_assign(binding, 0, entry); }

	inline void flush_writes() {
		if (mPendingWrites.empty()) return;

		struct WriteInfo {
			vk::DescriptorImageInfo  mImageInfo;
			vk::DescriptorBufferInfo mBufferInfo;
			vk::WriteDescriptorSetInlineUniformBlockEXT mInlineInfo;
			vk::WriteDescriptorSetAccelerationStructureKHR mAccelerationStructureInfo;
		};
		vector<WriteInfo> infos;
		vector<vk::WriteDescriptorSet> writes;
		vector<vk::CopyDescriptorSet> copies;
		writes.reserve(mPendingWrites.size());
		infos.reserve(mPendingWrites.size());
		for (uint64_t idx : mPendingWrites) {
			const auto& binding = mLayout->at(idx >> 32);
			auto& entry = mDescriptors.at(idx);
			auto& info = infos.emplace_back();
			auto& write = writes.emplace_back(vk::WriteDescriptorSet(mDescriptorSet, idx >> 32, idx & ~uint32_t(0), 1, binding.mDescriptorType));
			switch (write.descriptorType) {
			case vk::DescriptorType::eInputAttachment:
			case vk::DescriptorType::eSampledImage:
			case vk::DescriptorType::eStorageImage:
			case vk::DescriptorType::eCombinedImageSampler:
			case vk::DescriptorType::eSampler: {
				info.mImageInfo.imageLayout = get<vk::ImageLayout>(entry);
				info.mImageInfo.imageView = *get<Texture::View>(entry);
				if (write.descriptorType == vk::DescriptorType::eCombinedImageSampler || write.descriptorType == vk::DescriptorType::eSampler)
					info.mImageInfo.sampler = **get<shared_ptr<Sampler>>(entry);
				write.pImageInfo = &info.mImageInfo;
				break;
			}

			case vk::DescriptorType::eUniformBufferDynamic:
			case vk::DescriptorType::eStorageBufferDynamic:
			case vk::DescriptorType::eUniformBuffer:
			case vk::DescriptorType::eStorageBuffer: {
				const auto& view = get<Buffer::StrideView>(entry);
				info.mBufferInfo.buffer = *view.buffer();
				info.mBufferInfo.offset = view.offset();
				if (write.descriptorType == vk::DescriptorType::eUniformBufferDynamic || write.descriptorType == vk::DescriptorType::eStorageBufferDynamic)
					info.mBufferInfo.range = view.stride();
				else
					info.mBufferInfo.range = view.size_bytes();
				write.pBufferInfo = &info.mBufferInfo;
				break;
			}

			case vk::DescriptorType::eUniformTexelBuffer:
			case vk::DescriptorType::eStorageTexelBuffer:
				write.pTexelBufferView = get<Buffer::TexelView>(entry).operator->();
				break;

			case vk::DescriptorType::eInlineUniformBlockEXT:
				info.mInlineInfo.setData<byte>(get<byte_blob>(entry));
				write.descriptorCount = info.mInlineInfo.dataSize;
				write.pNext = &info.mInlineInfo;
				break;

			case vk::DescriptorType::eAccelerationStructureKHR:
				info.mAccelerationStructureInfo.accelerationStructureCount = 1;
				info.mAccelerationStructureInfo.pAccelerationStructures = &get<vk::AccelerationStructureKHR>(entry);
				write.pNext = &info.mAccelerationStructureInfo;
				break;
			}
		}
		mDevice->updateDescriptorSets(writes, copies);
		mPendingWrites.clear();
	}
};

}