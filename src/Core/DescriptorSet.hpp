#pragma once

#include "Image.hpp"

namespace stm {

using Descriptor = variant<
	tuple<Image::View, vk::ImageLayout, vk::AccessFlags, shared_ptr<Sampler>>, // sampler/image/combined image sampler
	Buffer::StrideView,											// storage/uniform buffer, stride used for dynamic offsets to work
	Buffer::TexelView, 											// texel buffer
	vector<byte>, 													// inline uniform buffer data
	vk::AccelerationStructureKHR						// acceleration structure
>;

template<typename T> requires(same_as<T,Image::View> || same_as<T,vk::ImageLayout> || same_as<T, vk::AccessFlags> || same_as<T,shared_ptr<Sampler>>)
constexpr T& get(stm::Descriptor& d) { return get<T>(get<0>(d)); }
template<typename T> requires(same_as<T,Image::View> || same_as<T,vk::ImageLayout> || same_as<T, vk::AccessFlags> || same_as<T,shared_ptr<Sampler>>)
constexpr const T& get(const stm::Descriptor& d) { return get<T>(get<0>(d)); }

inline Descriptor image_descriptor(const Image::View& image, const vk::ImageLayout& layout, const vk::AccessFlags& access, const shared_ptr<Sampler>& sampler = nullptr) {
	return variant_alternative_t<0,Descriptor>{image, layout, access, sampler};
}
inline Descriptor sampler_descriptor(const shared_ptr<Sampler>& sampler) {
	return image_descriptor(Image::View(), vk::ImageLayout::eUndefined, {}, sampler);
}

class DescriptorSetLayout : public DeviceResource {
	friend struct std::hash<DescriptorSetLayout>;
public:
	struct Binding {
		vk::DescriptorType mDescriptorType;
		uint32_t mDescriptorCount;
		vector<shared_ptr<Sampler>> mImmutableSamplers;
		vk::ShaderStageFlags mStageFlags;
		vk::DescriptorBindingFlags mBindingFlags;
	};

	inline DescriptorSetLayout(Device& device, const string& name, const unordered_map<uint32_t, Binding>& bindings = {}, vk::DescriptorSetLayoutCreateFlags flags = {}) : DeviceResource(device, name), mFlags(flags), mBindings(bindings) {
		forward_list<vector<vk::Sampler>> immutableSamplers; // store for duration of vk::DescriptorSetLayoutCreateInfo
		vector<vk::DescriptorSetLayoutBinding> vkbindings;
		vector<vk::DescriptorBindingFlags> bindingFlags;
		bool hasBindingFlags = false;
		for (auto&[i, binding] : mBindings) {
			if (binding.mDescriptorType == vk::DescriptorType::eUniformBufferDynamic || binding.mDescriptorType == vk::DescriptorType::eStorageBufferDynamic)
				mDynamicBindings.emplace(i);
			auto& b = vkbindings.emplace_back(i, binding.mDescriptorType, binding.mDescriptorCount, binding.mStageFlags);
			bindingFlags.emplace_back(binding.mBindingFlags);
			if (binding.mBindingFlags != vk::DescriptorBindingFlags{0}) hasBindingFlags = true;
			if (!binding.mImmutableSamplers.empty())
					b.pImmutableSamplers = immutableSamplers.emplace_front(
						binding.mImmutableSamplers.size()).data(),
						ranges::transform(binding.mImmutableSamplers, const_cast<vk::Sampler*>(b.pImmutableSamplers), [](const shared_ptr<Sampler>& v){ return **v; });
		}
		vk::DescriptorSetLayoutCreateInfo createInfo(mFlags, vkbindings);
		vk::DescriptorSetLayoutBindingFlagsCreateInfo flagInfo(bindingFlags);
		if (hasBindingFlags) createInfo.pNext = &flagInfo;
		mLayout = mDevice->createDescriptorSetLayout(createInfo);
		mHashValue = hash_args(mFlags, vkbindings);
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
	size_t mHashValue;
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
		mDevice.mDescriptorSetCount++;
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
		mDevice.mDescriptorSetCount--;
	}

	inline operator const vk::DescriptorSet*() const { return &mDescriptorSet; }
	inline operator vk::DescriptorSet() const { return mDescriptorSet; }

	inline auto layout() const { return mLayout; }
	inline const DescriptorSetLayout::Binding& layout_at(uint32_t binding) const { return mLayout->at(binding); }

	inline const Descriptor* find(uint32_t binding, uint32_t arrayIndex = 0) const {
		auto it = mDescriptors.find((uint64_t(binding)<<32)|arrayIndex);
		if (it == mDescriptors.end()) return nullptr;
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

	STRATUM_API void flush_writes();
	STRATUM_API void transition_images(CommandBuffer& commandBuffer, const vk::PipelineStageFlags& dstStage) const;
};

}

namespace std {
template<> struct hash<stm::DescriptorSetLayout> {
	inline size_t operator()(const stm::DescriptorSetLayout& l) const {
		return l.mHashValue;
	}
};
}
