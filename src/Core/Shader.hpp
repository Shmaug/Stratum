#pragma once

#include "Mesh.hpp"
#include "DescriptorSet.hpp"

namespace stm {

class Shader : public DeviceResource {
public:
	class Specialization;
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

	STRATUM_API Shader(Device& device, const fs::path& spv);
	inline ~Shader() { if (mShader) mDevice->destroyShaderModule(mShader); }

	inline const vk::ShaderStageFlagBits& stage() const { return mStage; }
	inline const string& entry_point() const { return mEntryPoint; }
	inline const auto& specialization_constants() const { return mSpecializationConstants; }
	inline const auto& push_constants() const { return mPushConstants; }
	inline const auto& stage_inputs() const { return mStageInputs; }
	inline const auto& stage_outputs() const { return mStageInputs; }
	inline const auto& descriptors() const { return mDescriptorMap; }
	inline const vk::Extent3D& workgroup_size() const { return mWorkgroupSize; }

private:
	friend class Specializaiton;
	vk::ShaderModule mShader; // created when the Shader is used to create a Pipeline
	vk::ShaderStageFlagBits mStage;
	string mEntryPoint;
	unordered_map<string, DescriptorBinding> mDescriptorMap;
	unordered_map<string, pair<uint32_t/*id*/,uint32_t/*default value*/>> mSpecializationConstants;
	unordered_map<string, PushConstant> mPushConstants;
	unordered_map<string, Variable> mStageInputs;
	unordered_map<string, Variable> mStageOutputs;
	vk::Extent3D mWorkgroupSize;
};

class Shader::Specialization {
public:
	Specialization() = default;
	Specialization(const Specialization&) = default;
	Specialization(Specialization&&) = default;
	Specialization& operator=(const Specialization&) = default;
	Specialization& operator=(Specialization&&) = default;
	inline Specialization(const shared_ptr<Shader>& shader, const unordered_map<string, uint32_t>& specializationConstants, const unordered_map<string, vk::DescriptorBindingFlags>& descriptorBindingFlags) :
		mShader(shader), mSpecializationConstants(specializationConstants), mDescriptorBindingFlags(descriptorBindingFlags) {
		// use default values for unspecified constants
		for (const auto&[name, id_defaultValue] : mShader->specialization_constants()) {
			auto it = mSpecializationConstants.find(name);
			if (it == mSpecializationConstants.end())
				mSpecializationConstants.emplace(name, id_defaultValue.second);
		}
	}

	inline const Shader& shader() const { return *mShader; }
	inline const vk::ShaderModule& shader_module() const { return mShader->mShader; }
	inline const unordered_map<string, uint32_t>& specialization_constants() const { return mSpecializationConstants; }
	inline const unordered_map<string, vk::DescriptorBindingFlags>& descriptor_binding_flags() const { return mDescriptorBindingFlags; }

	inline void fill_specialization_info(vector<vk::SpecializationMapEntry>& entries, vector<uint32_t>& data) const {
		for (const auto&[name, v] : mSpecializationConstants) {
			auto it = mShader->specialization_constants().find(name);
			if (it != mShader->specialization_constants().end()) {
				entries.emplace_back(it->second.first, (uint32_t)(data.size()*sizeof(uint32_t)), sizeof(uint32_t));
				data.emplace_back(v);
			}
		}
	}

private:
	shared_ptr<Shader> mShader;
	unordered_map<string, uint32_t> mSpecializationConstants;
	unordered_map<string, vk::DescriptorBindingFlags> mDescriptorBindingFlags;
};
}

namespace std {

template<> struct hash<stm::Shader::Specialization> {
	inline size_t operator()(const stm::Shader::Specialization& s) const {
		return stm::hash_args(s.shader_module(), s.specialization_constants(), s.descriptor_binding_flags());
	}
};

}
