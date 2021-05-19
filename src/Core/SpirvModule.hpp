#pragma once

#include "../Common/common.hpp"

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
 
class SpirvModule {
public:
	vk::ShaderModule mShaderModule; // created when the SpirvModule is used to create a Pipeline
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
};

inline void binary_read(istream& lhs, SpirvModule& m) {
	binary_read(lhs, m.mSpirv);
	binary_read(lhs, m.mStage);
	binary_read(lhs, m.mEntryPoint);
	binary_read(lhs, m.mSpecializationMap);
	binary_read(lhs, m.mStageInputs);
	binary_read(lhs, m.mStageOutputs);
	binary_read(lhs, m.mDescriptorBindings);
	binary_read(lhs, m.mPushConstants);
	binary_read(lhs, m.mWorkgroupSize);
}
inline void binary_write(ostream& lhs, const SpirvModule& m) {
	binary_write(lhs, m.mSpirv);
	binary_write(lhs, m.mStage);
	binary_write(lhs, m.mEntryPoint);
	binary_write(lhs, m.mSpecializationMap);
	binary_write(lhs, m.mStageInputs);
	binary_write(lhs, m.mStageOutputs);
	binary_write(lhs, m.mDescriptorBindings);
	binary_write(lhs, m.mPushConstants);
	binary_write(lhs, m.mWorkgroupSize);
}

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

	template<>
	struct hash<stm::VertexAttributeId> {
		inline size_t operator()(const stm::VertexAttributeId& v) const {
			return stm::hash_args(v.mType, v.mTypeIndex);
		}
	};
}