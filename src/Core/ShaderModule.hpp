#pragma once

#include "Mesh.hpp"
#include "DescriptorSet.hpp"

namespace stm {

using ShaderDatabase = unordered_map<string, shared_ptr<ShaderModule>>;

class ShaderModule : public DeviceResource {
public:
	struct DescriptorBinding {
		uint32_t mSet;
		uint32_t mBinding;
		vk::DescriptorType mDescriptorType;
		vector<variant<uint32_t, string>> mArraySize;
		uint32_t mInputAttachmentIndex;
	};
	struct PushConstant {
		uint32_t mOffset;
		uint32_t mTypeSize;
		uint32_t mArrayStride;
		vector<variant<uint32_t, string>> mArraySize; // uint32_t for literal, string for specialization constant
	};
	struct Variable {
		uint32_t mLocation;
		vk::Format mFormat;
		VertexArrayObject::AttributeType mAttributeType;
		uint32_t mTypeIndex;
	};

	STRATUM_API ShaderModule(Device& device, const fs::path& spv);
	inline ~ShaderModule() { if (mShaderModule) mDevice->destroyShaderModule(mShaderModule); }
	
	inline vk::ShaderModule* operator->() { return &mShaderModule; }
	inline vk::ShaderModule& operator*() { return mShaderModule; }
	inline const vk::ShaderModule* operator->() const { return &mShaderModule; }
	inline const vk::ShaderModule& operator*() const { return mShaderModule; }

	inline const auto& stage() const { return mStage; }
	inline const auto& entry_point() const { return mEntryPoint; }
	inline const auto& specialization_constants() const { return mSpecializationConstants; }
	inline const auto& push_constants() const { return mPushConstants; }
	inline const auto& stage_inputs() const { return mStageInputs; }
	inline const auto& stage_outputs() const { return mStageInputs; }
	inline const auto& descriptors() const { return mDescriptorMap; }
	inline const auto& workgroup_size() const { return mWorkgroupSize; }

	STRATUM_API static void load_from_dir(ShaderDatabase& dst, Device& device, const fs::path& dir);

private:
	vk::ShaderModule mShaderModule; // created when the ShaderModule is used to create a Pipeline
	vk::ShaderStageFlagBits mStage;
	string mEntryPoint;
	unordered_map<string, DescriptorBinding> mDescriptorMap;
	unordered_map<string, pair<uint32_t/*id*/,uint32_t/*default value*/>> mSpecializationConstants;
	unordered_map<string, PushConstant> mPushConstants;
	unordered_map<string, Variable> mStageInputs;
	unordered_map<string, Variable> mStageOutputs;
	array<uint32_t,3> mWorkgroupSize;
};

}

namespace std {

template<> struct hash<stm::ShaderModule> {
	inline size_t operator()(const stm::ShaderModule& v) const {
		return hash<vk::ShaderModule>()(*v);
	}
};

}
