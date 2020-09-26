#pragma once

#include <Util/Util.hpp>

namespace stm {

struct SpirvModule {
	struct StageInput {
		uint32_t mLocation;
		vk::Format mFormat;
		VertexAttributeType mType;
		uint32_t mTypeIndex;
	};
	
	vk::ShaderModule mShaderModule;
	std::vector<uint32_t> mSpirvBinary;
	vk::ShaderStageFlagBits mStage;
	std::string mEntryPoint;
	std::map<std::string, StageInput> mInputs;

	inline void Write(std::ostream& stream) const {
		WriteString(stream, mEntryPoint);
		WriteValue(stream, mStage);
		WriteVector(stream, mSpirvBinary);
		
		WriteValue(stream, (uint64_t)mInputs.size());
		for (const auto&[name,input] : mInputs) {
			WriteString(stream, name);
			WriteValue(stream, input.mLocation);
			WriteValue(stream, input.mFormat);
			WriteValue(stream, input.mType);
			WriteValue(stream, input.mTypeIndex);
		}
	}
	inline void Read(std::istream& stream) {
		ReadString(stream, mEntryPoint);
		ReadValue(stream, mStage);
		ReadVector(stream, mSpirvBinary);
		
		uint64_t inputCount;
		ReadValue<uint64_t>(stream, inputCount);
		for (uint32_t i = 0; i < inputCount; i++) {
			std::string name;
			ReadString(stream, name);
			StageInput input;
			ReadValue(stream, input.mLocation);
			ReadValue(stream, input.mFormat);
			ReadValue(stream, input.mType);
			ReadValue(stream, input.mTypeIndex);
			mInputs.emplace(name, input);
		}
	}
};

struct DescriptorBinding {
	vk::DescriptorSetLayoutBinding mBinding;
	uint32_t mSet;
	std::vector<vk::Sampler> mImmutableSamplers;
};

struct ShaderVariant {
	std::set<std::string> mKeywords;
	std::map<std::string, DescriptorBinding> mDescriptorSetBindings;
	std::map<std::string, vk::PushConstantRange> mPushConstants;
	std::map<std::string, vk::SamplerCreateInfo> mImmutableSamplers;
	std::vector<SpirvModule> mModules; // vert/frag stages or just a compute kernel

	// Graphics variant data

	std::string mShaderPass; // will be empty if a it this a compute variant
	std::vector<vk::PipelineColorBlendAttachmentState> mBlendStates;
	vk::PipelineDepthStencilStateCreateInfo mDepthStencilState;
	vk::CullModeFlags mCullMode = vk::CullModeFlagBits::eBack;
	vk::PolygonMode mPolygonMode = vk::PolygonMode::eFill;
	bool mSampleShading = false;
	uint32_t mRenderQueue = 1000;

	// Compute variant data

	uint3 mWorkgroupSize;

	inline ShaderVariant() {
		mDepthStencilState.depthTestEnable = VK_TRUE;
		mDepthStencilState.depthWriteEnable = VK_TRUE;
		mDepthStencilState.depthCompareOp = vk::CompareOp::eLessOrEqual;
		mDepthStencilState.front = mDepthStencilState.back;
		mDepthStencilState.back.compareOp = vk::CompareOp::eAlways;
	}
		
	inline void Write(std::ostream& stream) const {
		WriteValue(stream, (uint64_t)mKeywords.size());
		for (const std::string& kw : mKeywords)
			WriteString(stream, kw);

		WriteValue(stream, (uint64_t)mDescriptorSetBindings.size());
		for (const auto&[name, binding] : mDescriptorSetBindings) {
			WriteString(stream, name);
			WriteValue(stream, binding.mSet);
			WriteValue(stream, binding.mBinding);
		}

		WriteValue(stream, (uint64_t)mPushConstants.size());
		for (const auto&[name, range] : mPushConstants) {
			WriteString(stream, name);
			WriteValue(stream, range);
		}
		
		WriteValue(stream, (uint64_t)mImmutableSamplers.size());
		for (const auto&[name, sampler] : mImmutableSamplers) {
			WriteString(stream, name);
			WriteValue(stream, sampler);
		}

		WriteValue(stream, (uint64_t)mModules.size());
		for (const auto& module : mModules) module.Write(stream);
		
		WriteString(stream, mShaderPass);
		WriteVector(stream, mBlendStates);
		WriteValue(stream, mDepthStencilState);
		WriteValue(stream, mRenderQueue);
		WriteValue(stream, mCullMode);
		WriteValue(stream, mPolygonMode);
		WriteValue(stream, mSampleShading);
		WriteValue(stream, mWorkgroupSize);
	}
	inline void Read(std::istream& stream) {
		uint64_t keywordCount;
		ReadValue<uint64_t>(stream, keywordCount);
		for (uint32_t i = 0; i < keywordCount; i++) {
			std::string str;
			ReadString(stream, str);
			mKeywords.insert(str);
		}
		
		uint64_t descriptorCount;
		ReadValue<uint64_t>(stream, descriptorCount);
		for (uint32_t i = 0; i < descriptorCount; i++) {
			std::string name;
			ReadString(stream, name);
			DescriptorBinding value;
			ReadValue(stream, value.mSet);
			ReadValue(stream, value.mBinding);
			mDescriptorSetBindings.emplace(name, value);
		}

		uint64_t pushConstantCount;
		ReadValue<uint64_t>(stream, pushConstantCount);
		for (uint32_t i = 0; i < pushConstantCount; i++) {
			std::string name;
			ReadString(stream, name);
			vk::PushConstantRange value;
			ReadValue(stream, value);
			mPushConstants.emplace(name, value);
		}

		uint64_t immutableSamplerCount;
		ReadValue<uint64_t>(stream, immutableSamplerCount);
		for (uint32_t i = 0; i < immutableSamplerCount; i++) {
			std::string name;
			ReadString(stream, name);
			vk::SamplerCreateInfo value;
			ReadValue(stream, value);
			mImmutableSamplers.emplace(name, value);
		}

		uint64_t moduleCount;
		ReadValue<uint64_t>(stream, moduleCount);
		mModules.resize(moduleCount);
		for (uint32_t i = 0; i < moduleCount; i++) mModules[i].Read(stream);

		ReadString(stream, mShaderPass);
		ReadVector(stream, mBlendStates);
		ReadValue(stream, mDepthStencilState);
		ReadValue(stream, mRenderQueue);
		ReadValue(stream, mCullMode);
		ReadValue(stream, mPolygonMode);
		ReadValue(stream, mSampleShading);
		ReadValue(stream, mWorkgroupSize);
	}
};
struct Shader {
	std::vector<ShaderVariant> mVariants;

	inline void Write(std::ostream& stream) const {
		WriteValue<uint64_t>(stream, mVariants.size());
		for (uint32_t i = 0; i < mVariants.size(); i++)
			mVariants[i].Write(stream);
	}
	inline void Read(std::istream& stream) {
		uint64_t variantCount;
		ReadValue<uint64_t>(stream, variantCount);
		mVariants.resize(variantCount);
		for (uint32_t i = 0; i < mVariants.size(); i++)
			mVariants[i].Read(stream);
	}
};

}