#pragma once

#include "Texture.hpp"
#include "SpirvModule.hpp"

namespace stm {

class DescriptorSetLayout : public DeviceResource {
public:
	struct Binding {
		vk::DescriptorType mDescriptorType;
		vk::ShaderStageFlags mStageFlags;
		uint32_t mDescriptorCount;
		vector<shared_ptr<Sampler>> mImmutableSamplers;
	};

	inline DescriptorSetLayout(Device& device, const string& name, const unordered_map<uint32_t, Binding>& bindings, vk::DescriptorSetLayoutCreateFlags flags = {}) : DeviceResource(device, name), mFlags(flags), mBindings(bindings) {
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

	inline const unordered_map<uint32_t, Binding>& Bindings() const { return mBindings; }

private:
	vk::DescriptorSetLayout mLayout;
	vk::DescriptorSetLayoutCreateFlags mFlags;
	unordered_map<uint32_t, Binding> mBindings;
};

class DescriptorSet : public DeviceResource {
public:
	using TextureEntry = tuple<shared_ptr<Sampler>, TextureView, vk::ImageLayout>;
	using Entry = variant<
		Buffer::RangeView, // storage/uniform buffer
		TexelBufferView, // texel buffer
		TextureEntry, // sampler/image/combined image sampler
		byte_blob, // inline uniform buffer
		vk::AccelerationStructureKHR>; // acceleration structure

private:
	friend class Device;
	friend class CommandBuffer;
	vk::DescriptorSet mDescriptorSet;
	shared_ptr<const DescriptorSetLayout> mLayout;
	
	unordered_map<uint64_t/*{binding,arrayIndex}*/, Entry> mBoundDescriptors;
	unordered_set<uint64_t> mPendingWrites;

public:
	inline DescriptorSet(shared_ptr<const DescriptorSetLayout> layout, const string& name) : DeviceResource(layout->mDevice, name), mLayout(layout) {
		auto descriptorPool = mDevice.mDescriptorPool.lock();
		vk::DescriptorSetAllocateInfo allocInfo = {};
		allocInfo.descriptorPool = *descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &**mLayout;
		mDescriptorSet = mDevice->allocateDescriptorSets(allocInfo)[0];
		mDevice.SetObjectName(mDescriptorSet, mName);
	}
	inline DescriptorSet(shared_ptr<const DescriptorSetLayout> layout, const string& name, const unordered_map<uint32_t, Entry>& bindings) : DescriptorSet(layout, name) {
		for (const auto&[binding, entry] : bindings)
			insert(binding, Entry(entry));
	}
	inline DescriptorSet(shared_ptr<const DescriptorSetLayout> layout, const string& name, const unordered_map<uint32_t, unordered_map<uint32_t, Entry>>& bindings) : DescriptorSet(layout, name) {
		for (const auto&[binding, entries] : bindings)
			for (const auto&[arrayIndex, entry] : entries)
				insert(binding, arrayIndex, Entry(entry));
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

	inline const string& Name() const { return mName; }
	inline auto DescriptorSetLayout() const { return mLayout; }

	inline const DescriptorSetLayout::Binding& layout_at(uint32_t binding) const { return mLayout->at(binding); }
	inline const Entry& at(uint32_t binding, uint32_t arrayIndex = 0) const {
		return mBoundDescriptors.at((uint64_t(binding)<<32)|arrayIndex);
	}

	inline void insert(uint32_t binding, uint32_t arrayIndex, Entry&& entry) {
		mBoundDescriptors[*mPendingWrites.emplace((uint64_t(binding)<<32)|arrayIndex).first] = entry;
	}
	inline void insert(uint32_t binding, Entry&& entry) { insert(binding, 0, forward<Entry>(entry)); }

	inline void insert(uint32_t binding, uint32_t arrayIndex, const TextureView& view, vk::ImageLayout layout = vk::ImageLayout::eGeneral, const shared_ptr<Sampler>& sampler = nullptr) {
		mBoundDescriptors[*mPendingWrites.emplace((uint64_t(binding)<<32)|arrayIndex).first] = TextureEntry(sampler, view, layout);
	}
	inline void insert(uint32_t binding, const TextureView& view, vk::ImageLayout layout = vk::ImageLayout::eGeneral, const shared_ptr<Sampler>& sampler = nullptr) {
		insert(binding, 0, view, layout, sampler);
	}
	inline void insert(uint32_t binding, uint32_t arrayIndex, shared_ptr<Sampler> sampler) {
		insert(binding, arrayIndex, {}, vk::ImageLayout::eUndefined, sampler);
	}
	inline void insert(uint32_t binding, shared_ptr<Sampler> sampler) {
		insert(binding, 0, {}, vk::ImageLayout::eUndefined, sampler);
	}

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
		writes.reserve(mPendingWrites.size());
		infos.reserve(mPendingWrites.size());
		for (uint64_t idx : mPendingWrites) {
			const auto& binding = mLayout->at(idx >> 32);
			auto& entry = mBoundDescriptors.at(idx);
			auto& info = infos.emplace_back(WriteInfo{});
			auto& write = writes.emplace_back(vk::WriteDescriptorSet(mDescriptorSet, idx >> 32, idx & uint64_t(~uint32_t(0)), 1, binding.mDescriptorType));
			switch (write.descriptorType) {
			case vk::DescriptorType::eUniformBuffer:
			case vk::DescriptorType::eStorageBuffer:
			case vk::DescriptorType::eUniformBufferDynamic:
			case vk::DescriptorType::eStorageBufferDynamic: {
				const auto& view = get<Buffer::RangeView>(entry);
				info.mBufferInfo.buffer = *view.buffer();
				info.mBufferInfo.offset = view.offset();
				info.mBufferInfo.range = view.size_bytes();
				write.pBufferInfo = &info.mBufferInfo;
				break;
			}

			case vk::DescriptorType::eUniformTexelBuffer:
			case vk::DescriptorType::eStorageTexelBuffer:
				write.pTexelBufferView = get<TexelBufferView>(entry).operator->();
				break;

			case vk::DescriptorType::eInputAttachment:
			case vk::DescriptorType::eSampledImage:
			case vk::DescriptorType::eStorageImage:
			case vk::DescriptorType::eCombinedImageSampler:
			case vk::DescriptorType::eSampler: {
				const auto& t = get<TextureEntry>(entry);
				info.mImageInfo.imageLayout = get<vk::ImageLayout>(t);
				info.mImageInfo.imageView = *get<TextureView>(t);
				if (write.descriptorType == vk::DescriptorType::eCombinedImageSampler || write.descriptorType == vk::DescriptorType::eSampler)
					info.mImageInfo.sampler = **get<shared_ptr<Sampler>>(t);
				write.pImageInfo = &info.mImageInfo;
				break;
			}

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
		vector<vk::CopyDescriptorSet> copies;
		mDevice->updateDescriptorSets(writes, copies);
		mPendingWrites.clear();
	}
};

}