#pragma once

#include "../Stratum.hpp"

namespace stm {

enum class VertexAttributeType {
	eSystemGenerated,
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
struct RasterStageVariable {
	uint32_t mLocation;
	vk::Format mFormat;
	VertexAttributeId mAttributeId;
};
struct DescriptorBinding {
	uint32_t mSet = 0;
	uint32_t mBinding = 0;
	uint32_t mDescriptorCount = 0;
	vk::DescriptorType mDescriptorType = vk::DescriptorType::eSampler;
	vk::ShaderStageFlags mStageFlags = {};
};

struct SpirvModule {
	vk::ShaderModule mShaderModule;
	vk::Device mDevice;

	vector<uint32_t> mSpirv;
	
	vk::ShaderStageFlagBits mStage;
	string mEntryPoint;
	unordered_map<string, vk::SpecializationMapEntry> mSpecializationMap;
	unordered_map<string, DescriptorBinding> mDescriptorBindings;
	unordered_map<string, pair<uint32_t/*offset*/, uint32_t/*size*/>> mPushConstants;
	unordered_map<string, RasterStageVariable> mStageInputs;
	unordered_map<string, RasterStageVariable> mStageOutputs;
	vk::Extent3D mWorkgroupSize;

	inline ~SpirvModule() { if (mShaderModule) mDevice.destroyShaderModule(mShaderModule); }

	inline const DescriptorBinding& Binding(const string& descriptor) const { return mDescriptorBindings.at(descriptor); }
};

template<> struct tuplefier<RasterStageVariable> {
	inline auto operator()(RasterStageVariable&& m) {
		return forward_as_tuple(m.mLocation, m.mFormat, m.mAttributeId);
	}
};
template<> struct tuplefier<VertexAttributeId> {
	inline auto operator()(VertexAttributeId&& m) {
		return forward_as_tuple(m.mType, m.mTypeIndex);
	}
};
template<> struct tuplefier<DescriptorBinding> {

	inline auto operator()(stm::DescriptorBinding&& m) {
		return forward_as_tuple(
			m.mSet,
			m.mBinding,
			m.mDescriptorCount,
			m.mDescriptorType,
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

static_assert(tuplefiable<DescriptorBinding>);
static_assert(tuplefiable<SpirvModule>);
static_assert(Hashable<SpirvModule>);
static_assert(Hashable<unordered_map<string,RasterStageVariable>>);

}

namespace std {
	inline string to_string(const stm::VertexAttributeType& value) {
		switch (value) {
			case stm::VertexAttributeType::eSystemGenerated: return "SystemGenerated";
			case stm::VertexAttributeType::ePosition: return "Position";
			case stm::VertexAttributeType::eNormal: return "Normal";
			case stm::VertexAttributeType::eTangent: return "Tangent";
			case stm::VertexAttributeType::eBinormal: return "Binormal";
			case stm::VertexAttributeType::eBlendIndices: return "BlendIndices";
			case stm::VertexAttributeType::eBlendWeight: return "BlendWeight";
			case stm::VertexAttributeType::eColor: return "Color";
			case stm::VertexAttributeType::ePointSize: return "PointSize";
			case stm::VertexAttributeType::eTexcoord: return "Texcoord";
			default: return "invalid ( " + vk::toHexString( static_cast<uint32_t>( value ) ) + " )";
		}
	}
	inline string to_string(const stm::VertexAttributeId& value) {
		return to_string(value.mType) + "[" + to_string(value.mTypeIndex) + "]";
	}
}