#pragma once

#include "../Stratum.hpp"

namespace stm {

enum class VertexAttributeType {
	ePosition,
	eNormal,
	eTangent,
	eBinormal,
	eBlendIndices,
	eBlendWeight,
	eColor,
	ePointSize,
	eTexcoord
};
struct VertexAttributeId {
	VertexAttributeType mType;
	uint32_t mTypeIndex;
	bool operator==(const VertexAttributeId&) const = default;
};
struct VertexStageVariable {
	uint32_t mLocation;
	vk::Format mFormat;
	VertexAttributeId mAttributeId;
};

struct DescriptorBinding {
	vk::DescriptorSetLayoutBinding mBinding;
	uint32_t mSet;
	vector<vk::Sampler> mImmutableSamplers;
	vk::ShaderStageFlags mStageFlags;
};

struct SpirvModule {
	vk::ShaderModule mShaderModule;
	vk::Device mDevice;

	vector<uint32_t> mSpirv;
	
	vk::ShaderStageFlagBits mStage;
	string mEntryPoint;
	unordered_map<string, vk::SpecializationMapEntry> mSpecializationMap;
	unordered_map<string, DescriptorBinding> mDescriptorBindings;
	unordered_map<string, vk::PushConstantRange> mPushConstants;
	unordered_map<string, VertexStageVariable> mStageInputs;
	unordered_map<string, VertexStageVariable> mStageOutputs;
	vk::Extent3D mWorkgroupSize;

	inline ~SpirvModule() { if (mShaderModule) mDevice.destroyShaderModule(mShaderModule); }

	inline uint32_t SetIndex(const string& descriptor) const { return mDescriptorBindings.at(descriptor).mSet; }
	inline uint32_t Location(const string& descriptor) const { return mDescriptorBindings.at(descriptor).mBinding.binding; }
};

template<> struct tuplefier<DescriptorBinding> {
	inline auto operator()(stm::DescriptorBinding&& m) {
		return forward_as_tuple(
			m.mBinding.binding,
			m.mBinding.descriptorType,
			m.mBinding.descriptorCount,
			m.mBinding.stageFlags,
			m.mSet,
			m.mImmutableSamplers,
			m.mStageFlags);
	}
};
template<> struct tuplefier<SpirvModule> {
	inline auto operator()(SpirvModule&& m) {
		return forward_as_tuple(
			m.mSpirv,
			m.mStage,
			m.mEntryPoint,
			m.mSpecializationMap,
			m.mStageInputs,
			m.mStageOutputs,
			m.mDescriptorBindings,
			m.mPushConstants,
			m.mWorkgroupSize);
	}
};
template<> struct tuplefier<VertexAttributeId> {
	inline auto operator()(VertexAttributeId&& m) {
		return forward_as_tuple(m.mType, m.mTypeIndex);
	}
};
template<> struct tuplefier<VertexStageVariable> {
	inline auto operator()(VertexStageVariable&& m) {
		return forward_as_tuple(m.mLocation, m.mFormat, m.mAttributeId);
	}
};

static_assert(tuplefiable<DescriptorBinding>);
static_assert(tuplefiable<SpirvModule>);
static_assert(Hashable<SpirvModule>);
static_assert(Hashable<unordered_map<string,VertexStageVariable>>);

}