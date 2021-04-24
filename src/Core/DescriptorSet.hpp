#pragma once

#include "Texture.hpp"
#include "Sampler.hpp"
#include "SpirvModule.hpp"

namespace stm {

using Descriptor = variant<
	tuple<Texture::View, vk::ImageLayout, shared_ptr<Sampler>>, // sampler/image/combined image sampler
	Buffer::View<byte>, // storage/uniform buffer
	Buffer::TexelView, // texel buffer
	byte_blob, // inline buffer data
	vk::AccelerationStructureKHR // acceleration structure
>;

template<typename T> requires(is_same_v<T,Texture::View> || is_same_v<T,vk::ImageLayout> || is_same_v<T,shared_ptr<Sampler>>)
constexpr T& get(stm::Descriptor& d) { return get<T>(get<0>(d)); }
template<typename T> requires(is_same_v<T,Texture::View> || is_same_v<T,vk::ImageLayout> || is_same_v<T,shared_ptr<Sampler>>)
constexpr const T& get(const stm::Descriptor& d) { return get<T>(get<0>(d)); }

inline Descriptor texture_descriptor(Texture::View&& texture, vk::ImageLayout&& layout, shared_ptr<Sampler>&& sampler) {
	return make_tuple<Texture::View, vk::ImageLayout, shared_ptr<Sampler>>(forward<Texture::View>(texture), forward<vk::ImageLayout>(layout), forward<shared_ptr<Sampler>>(sampler));
}
inline Descriptor sampler_descriptor(shared_ptr<Sampler>&& sampler) {
	return texture_descriptor(Texture::View(), vk::ImageLayout::eUndefined, forward<shared_ptr<Sampler>>(sampler));
}
inline Descriptor storage_texture_descriptor(Texture::View&& texture) {
	return texture_descriptor(forward<Texture::View>(texture), vk::ImageLayout::eGeneral, nullptr);
}
inline Descriptor sampled_texture_descriptor(Texture::View&& texture, shared_ptr<Sampler> sampler = nullptr) {
	return texture_descriptor(forward<Texture::View>(texture), vk::ImageLayout::eShaderReadOnlyOptimal, forward<shared_ptr<Sampler>>(sampler));
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
		forward_list<vector<vk::Sampler>> immutableSamplers;
		vector<vk::DescriptorSetLayoutBinding> b;
		for (auto&[i, binding] : mBindings) {
			if (binding.mImmutableSamplers.size()) {
				auto& v = immutableSamplers.emplace_front(vector<vk::Sampler>(binding.mImmutableSamplers.size()));
				ranges::transform(binding.mImmutableSamplers, v.begin(), &Sampler::operator*);
				b.emplace_back(i, binding.mDescriptorType, binding.mDescriptorCount, binding.mStageFlags, v.data());
			} else 
				b.emplace_back(i, binding.mDescriptorType, binding.mDescriptorCount, binding.mStageFlags);
		}
		mLayout = mDevice->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo(mFlags, b));
	}
	inline ~DescriptorSetLayout() {
		mDevice->destroyDescriptorSetLayout(mLayout);
	}

	inline operator const vk::DescriptorSetLayout*() const { return &mLayout; }
	inline operator const vk::DescriptorSetLayout&() const { return mLayout; }

	inline const Binding& at(uint32_t binding) const { return mBindings.at(binding); }
	inline const Binding& operator[](uint32_t binding) const { return mBindings.at(binding); }

	inline const unordered_map<uint32_t, Binding>& bindings() const { return mBindings; }

private:
	vk::DescriptorSetLayout mLayout;
	vk::DescriptorSetLayoutCreateFlags mFlags;
	unordered_map<uint32_t, Binding> mBindings;
};

class DescriptorSet : public DeviceResource {
public:

private:
	friend class Device;
	friend class CommandBuffer;
	vk::DescriptorSet mDescriptorSet;
	shared_ptr<const DescriptorSetLayout> mLayout;
	
	unordered_map<uint64_t/*{binding,arrayIndex}*/, Descriptor> mBoundDescriptors;
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
		mBoundDescriptors.clear();
		mPendingWrites.clear();
		auto descriptorPool = mDevice.mDescriptorPool.lock();
		mLayout.reset();
		mDevice->freeDescriptorSets(*descriptorPool, { mDescriptorSet });
	}

	inline operator const vk::DescriptorSet*() const { return &mDescriptorSet; }
	inline operator vk::DescriptorSet() const { return mDescriptorSet; }

	inline auto layout() const { return mLayout; }

	inline const DescriptorSetLayout::Binding& layout_at(uint32_t binding) const { return mLayout->at(binding); }

	inline const Descriptor& find(uint32_t binding, uint32_t arrayIndex = 0) const {
		auto it = mBoundDescriptors.find((uint64_t(binding)<<32)|arrayIndex);
		if (it == mBoundDescriptors.end()) return {};
		return it->second;
	}
	inline const Descriptor& at(uint32_t binding, uint32_t arrayIndex = 0) const { return mBoundDescriptors.at((uint64_t(binding)<<32)|arrayIndex); }
	inline const Descriptor& operator[](uint32_t binding) const { return at(binding); }

	inline void insert_or_assign(uint32_t binding, uint32_t arrayIndex, const Descriptor& entry) {
		mBoundDescriptors[*mPendingWrites.emplace((uint64_t(binding)<<32)|arrayIndex).first] = entry;
	}
	inline void insert_or_assign(uint32_t binding, const Descriptor& entry) { insert_or_assign(binding, 0, entry); }

	inline void FlushWrites() {
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
			auto& entry = mBoundDescriptors.at(idx);
			auto& info = infos.emplace_back(WriteInfo{});
			auto& write = writes.emplace_back(vk::WriteDescriptorSet(mDescriptorSet, idx >> 32, idx & uint64_t(~uint32_t(0)), 1, binding.mDescriptorType));
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

			case vk::DescriptorType::eUniformBuffer:
			case vk::DescriptorType::eStorageBuffer:
			case vk::DescriptorType::eUniformBufferDynamic:
			case vk::DescriptorType::eStorageBufferDynamic: {
				const auto& view = get<Buffer::View<byte>>(entry);

				info.mBufferInfo.buffer = *view.buffer();
				info.mBufferInfo.offset = view.offset();
				info.mBufferInfo.range = view.size_bytes();
				write.pBufferInfo = &info.mBufferInfo;
				break;
			}

			case vk::DescriptorType::eUniformTexelBuffer:
			case vk::DescriptorType::eStorageTexelBuffer:
				write.pTexelBufferView = get<Buffer::TexelView>(entry).operator->();
				break;

			case vk::DescriptorType::eInlineUniformBlockEXT: {
				const auto& data = get<byte_blob>(entry);
				info.mInlineInfo.pData = data.data();
				info.mInlineInfo.dataSize = (uint32_t)data.size();
				write.descriptorCount = info.mInlineInfo.dataSize;
				write.pNext = &info.mInlineInfo;
				break;
			}

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