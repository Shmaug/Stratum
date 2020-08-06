#pragma once

#include <Util/Util.hpp>

struct DescriptorBinding {
	VkDescriptorSetLayoutBinding mBinding;
	uint32_t mSet;
	std::vector<VkSampler> mImmutableSamplers;
};

struct SpirvModule {
	std::string mEntryPoint;
	VkShaderStageFlagBits mStage;
	std::vector<uint32_t> mSpirvBinary;
	VkShaderModule mShaderModule;

	inline void Write(std::ostream& stream) const {
		WriteString(stream, mEntryPoint);
		WriteValue(stream, mStage);
		WriteVector(stream, mSpirvBinary);
	}
	inline void Read(std::istream& stream) {
		ReadString(stream, mEntryPoint);
		ReadValue(stream, mStage);
		ReadVector(stream, mSpirvBinary);
	}
};
struct ShaderVariant {
	std::set<std::string> mKeywords;
	std::unordered_map<std::string, DescriptorBinding> mDescriptorSetBindings;
	std::unordered_map<std::string, VkPushConstantRange> mPushConstants;
	std::unordered_map<std::string, VkSamplerCreateInfo> mImmutableSamplers;
	std::vector<SpirvModule> mModules; // vs/ps or cs

	// Graphics variant data

	std::vector<VkPipelineColorBlendAttachmentState> mBlendStates;
	std::string mShaderPass;
	VkPipelineDepthStencilStateCreateInfo mDepthStencilState;
	VkCullModeFlags mCullMode;
	VkPolygonMode mPolygonMode;
	VkBool32 mSampleShading;
	uint32_t mRenderQueue;

	// Compute variant data

	uint3 mWorkgroupSize;

	inline ShaderVariant() : mCullMode(VK_CULL_MODE_BACK_BIT), mPolygonMode(VK_POLYGON_MODE_FILL), mSampleShading(VK_FALSE), mDepthStencilState({}), mRenderQueue(1000) {
		mDepthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		mDepthStencilState.depthTestEnable = VK_TRUE;
		mDepthStencilState.depthWriteEnable = VK_TRUE;
		mDepthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		mDepthStencilState.front = mDepthStencilState.back;
		mDepthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
	}
		
	inline void Write(std::ostream& stream) const {
		WriteValue(stream, (uint64_t)mKeywords.size());
		for (const std::string& kw : mKeywords)
			WriteString(stream, kw);

		WriteValue(stream, (uint64_t)mDescriptorSetBindings.size());
		for (const auto& kp : mDescriptorSetBindings) {
			WriteString(stream, kp.first);
			WriteValue(stream, kp.second.mSet);
			WriteValue(stream, kp.second.mBinding);
		}

		WriteValue(stream, (uint64_t)mPushConstants.size());
		for (const auto& kp : mPushConstants) {
			WriteString(stream, kp.first);
			WriteValue(stream, kp.second);
		}
		
		WriteValue(stream, (uint64_t)mImmutableSamplers.size());
		for (const auto& kp : mImmutableSamplers) {
			WriteString(stream, kp.first);
			WriteValue(stream, kp.second);
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
			VkPushConstantRange value;
			ReadValue(stream, value);
			mPushConstants.emplace(name, value);
		}

		uint64_t immutableSamplerCount;
		ReadValue<uint64_t>(stream, immutableSamplerCount);
		for (uint32_t i = 0; i < immutableSamplerCount; i++) {
			std::string name;
			ReadString(stream, name);
			VkSamplerCreateInfo value;
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
