#pragma once

#include "../Stratum.hpp"

namespace stm {

struct DescriptorBinding {
	vk::DescriptorSetLayoutBinding mBinding;
	uint32_t mSet;
	vector<vk::Sampler> mImmutableSamplers;
	vk::ShaderStageFlags mStageFlags;
};

inline binary_stream& operator<<(binary_stream& lhs, const DescriptorBinding& rhs) {
	lhs << rhs.mBinding.binding;
	lhs << rhs.mBinding.descriptorType;
	lhs << rhs.mBinding.descriptorCount;
	lhs << rhs.mBinding.stageFlags;
	lhs << rhs.mSet;
	lhs << rhs.mImmutableSamplers;
	lhs << rhs.mStageFlags;
	return lhs;
}
inline binary_stream& operator>>(binary_stream& lhs, DescriptorBinding& rhs) {
	lhs >> rhs.mBinding.binding;
	lhs >> rhs.mBinding.descriptorType;
	lhs >> rhs.mBinding.descriptorCount;
	lhs >> rhs.mBinding.stageFlags;
	lhs >> rhs.mSet;
	lhs >> rhs.mImmutableSamplers;
	lhs >> rhs.mStageFlags;
	return lhs;
}

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
	VertexAttributeType mType : 4;
	unsigned int mTypeIndex : 28;
	VertexAttributeId() = default;
	VertexAttributeId(const VertexAttributeId&) = default;
	VertexAttributeId(VertexAttributeType type, uint8_t typeIndex=0) : mType(type), mTypeIndex(typeIndex) {}
	inline bool operator==(const VertexAttributeId& rhs) const = default;
};
struct VertexStageVariable {
	uint32_t mLocation;
	vk::Format mFormat;
	VertexAttributeId mAttributeId;
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
	Vector3u mWorkgroupSize;

	inline ~SpirvModule() { if (mShaderModule) mDevice.destroyShaderModule(mShaderModule); }

	inline uint32_t SetIndex(const string& descriptor) const { return mDescriptorBindings.at(descriptor).mSet; }
	inline uint32_t Location(const string& descriptor) const { return mDescriptorBindings.at(descriptor).mBinding.binding; }
};

inline binary_stream& operator<<(binary_stream& stream, const SpirvModule& m) {
	stream << m.mSpirv;
	stream << m.mStage;
	stream << m.mEntryPoint;
	stream << m.mSpecializationMap;
	stream << m.mStageInputs;
	stream << m.mStageOutputs;
	stream << m.mDescriptorBindings;
	stream << m.mPushConstants;
	stream << m.mWorkgroupSize;
	return stream;
}
inline binary_stream& operator>>(binary_stream& stream, SpirvModule& m) {
	stream >> m.mSpirv;
	stream >> m.mStage;
	stream >> m.mEntryPoint;
	stream >> m.mSpecializationMap;
	stream >> m.mStageInputs;
	stream >> m.mStageOutputs;
	stream >> m.mDescriptorBindings;
	stream >> m.mPushConstants;
	stream >> m.mWorkgroupSize;
	return stream;
}

}

namespace std {
template<> struct hash<stm::SpirvModule> {
	inline size_t operator()(const stm::SpirvModule& m) {
		return stm::hash_combine(
			m.mSpirv, // TODO: optimize hashing entire spirv
			m.mStage,
			m.mEntryPoint,
			m.mSpecializationMap);
	}
};
template<> struct hash<stm::VertexAttributeId> { inline size_t operator()(const stm::VertexAttributeId& id) { return *(const uint32_t*)&id; } };
}