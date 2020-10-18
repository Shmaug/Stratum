#pragma once


#include "../Stratum.hpp"
#include "RenderPass.hpp"

namespace stm {

class SpirvModule {
public:
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

	inline SpirvModule() = default;
	inline SpirvModule(std::istream& stream) {
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
	inline friend std::ostream& operator<<(std::ostream& stream, const SpirvModule& module) {
		stream << module.mEntryPoint;
		stream << module.mStage;
		stream.write(module.mSpirvBinary.data(), module.mSpirvBinary.size());
		
		stream << module.mInputs.size();
		for (const auto&[name,input] : module.mInputs) {
			stream << name;
			stream << input.mLocation;
			stream << input.mFormat;
			stream << input.mType;
			stream << input.mTypeIndex;
		}
	}
};

class Pipeline {
	friend struct std::hash<stm::Pipeline>;
protected:
	vk::Pipeline mPipeline;
	vk::PipelineLayout mLayout;
	std::vector<vk::DescriptorSetLayout> mDescriptorSetLayouts;
	std::map<std::string, DescriptorBinding> mDescriptorSetBindings;
	std::map<std::string, vk::PushConstantRange> mPushConstants;
	std::map<std::string, vk::SamplerCreateInfo> mImmutableSamplers;
	std::vector<SpirvModule> mModules;

public:
	Pipeline(const Pipeline& c) = default;
	STRATUM_API Pipeline(vk::Pipeline pipeline, vk::PipelineLayout layout, std::vector<vk::DescriptorSetLayout> descriptorSetLayouts={});
	inline vk::Pipeline operator*() const { return mPipeline; }
	inline const vk::Pipeline* operator->() const { return &mPipeline; }
	inline bool operator==(const Pipeline& rhs) const { return rhs.mPipeline == mPipeline; }

	inline vk::PipelineLayout Layout() const { return mLayout; }
	
	inline uint32_t GetDescriptorSet(const std::string& descriptorName) const { return mDescriptorSetBindings.at(descriptorName).mSet; }
	inline uint32_t GetDescriptorLocation(const std::string& descriptorName) const { return mDescriptorSetBindings.at(descriptorName).mBinding.binding; }
};

class ComputePipeline : public Pipeline {
protected:
	uint3 mWorkgroupSize;
	STRATUM_API ComputePipeline();
public:
	STRATUM_API ComputePipeline(const std::string& name, const SpirvModule& module);
	inline uint3 WorkgroupSize() const { return mWorkgroupSize; }
};

class GraphicsPipeline : public Pipeline {
protected:
	std::string mShaderPass;
	std::vector<vk::PipelineColorBlendAttachmentState> mBlendStates;
	vk::PipelineDepthStencilStateCreateInfo mDepthStencilState;
	bool mSampleShading = false;
	uint32_t mRenderQueue = 1000;

	uint64_t mRenderPassHash;
	uint32_t mSubpassIndex;
	vk::PrimitiveTopology mTopology;
	vk::CullModeFlags mCullMode;
	vk::PolygonMode mPolygonMode;
	vk::PipelineVertexInputStateCreateInfo mVertexInput;

	STRATUM_API GraphicsPipeline(const std::string& name, const std::vector<SpirvModule>& modules, uint32_t subpassIndex,
		vk::PrimitiveTopology topology, const vk::PipelineVertexInputStateCreateInfo& vertexInput, vk::CullModeFlags cullMode, vk::PolygonMode polyMode);
};

}

namespace std {
	template<>
	struct hash<vk::VertexInputBindingDescription> {
		size_t operator()(const vk::VertexInputBindingDescription& b) {
			return stm::hash_combine(b.binding, b.inputRate, b.stride);
		}
	};
	template<>
	struct hash<vk::VertexInputAttributeDescription> {
		size_t operator()(const vk::VertexInputAttributeDescription& a) {
			return stm::hash_combine(a.binding, a.location, a.format, a.offset);
		}
	};
}