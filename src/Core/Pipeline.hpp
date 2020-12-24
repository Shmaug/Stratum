#pragma once

#include "RenderPass.hpp"
#include "SpirvModule.hpp"

namespace stm {

class Pipeline {
protected:
	// TODO: something more elegant than friend class...
	friend class ShaderCompiler;

	vk::Pipeline mPipeline;
	vk::PipelineLayout mLayout;
	vector<vk::DescriptorSetLayout> mDescriptorSetLayouts;

	SpirvModuleGroup mSpirvModules;
	vector<vk::PushConstantRange> mPushConstantRanges;
	// multimap handles descriptors with the same name and different bindings between stages
	multimap<string, DescriptorBinding> mDescriptorBindings;
	// multimap handles push constants with the same name and different ranges between stages
	multimap<string, vk::PushConstantRange> mPushConstants;
	map<string, vk::SamplerCreateInfo> mImmutableSamplers;

	size_t mHash = 0;

public:
	inline vk::Pipeline operator*() const { return mPipeline; }
	inline const vk::Pipeline* operator->() const { return &mPipeline; }
	inline bool operator==(const Pipeline& rhs) const { return rhs.mPipeline == mPipeline; }
	inline size_t Hash() const { return mHash; }

	virtual vk::PipelineBindPoint BindPoint() const = 0;

	inline vk::PipelineLayout Layout() const { return mLayout; }
	inline const SpirvModuleGroup& SpirvModules() const { return mSpirvModules; };
	inline const vector<vk::DescriptorSetLayout>& DescriptorSetLayouts() const { return mDescriptorSetLayouts; };
	// most of the time, a pipeline wont have named descriptors with different bindings, so there is usually only one item in this range
	inline const multimap<string, DescriptorBinding>& DescriptorBindings() { return mDescriptorBindings; };
	// most of the time, a pipeline wont have named push constants with different ranges, so there is usually only one item in this range
	inline const multimap<string, vk::PushConstantRange>& PushConstants() { return mPushConstants; };
	inline const vector<vk::PushConstantRange>& PushConstantRanges() const { return mPushConstantRanges; };

	inline friend binary_stream& operator<<(binary_stream& stream, const Pipeline& pipeline) {
		stream << pipeline.mSpirvModules;
		stream << pipeline.mPushConstantRanges;
		stream << pipeline.mDescriptorBindings;
		stream << pipeline.mPushConstants;
		stream << pipeline.mImmutableSamplers;
		return stream;
	}
	inline friend binary_stream& operator>>(binary_stream& stream, Pipeline& pipeline) {
		stream >> pipeline.mSpirvModules;
		stream >> pipeline.mPushConstantRanges;
		stream >> pipeline.mDescriptorBindings;
		stream >> pipeline.mPushConstants;
		stream >> pipeline.mImmutableSamplers;
		return stream;
	}
};

class ComputePipeline : public Pipeline {
public:
	inline ComputePipeline(const string& name, Device& device, const SpirvModule& spirv) {
		mSpirvModules = { spirv };
		mHash = basic_hash(spirv);

		unordered_map<uint32_t, vector<vk::DescriptorSetLayoutBinding>> descriptorSetBindings;
		for (const auto&[name, binding] : spirv.mDescriptorBindings) {
			mDescriptorBindings.insert({ name, binding });
			descriptorSetBindings[binding.mSet].push_back(binding.mBinding);
		}
		for (const auto& [name,range] : spirv.mPushConstants) {
			mPushConstants.insert({ name, range });
			mPushConstantRanges.push_back(range);
		}

		for (auto& [index, bindings] : descriptorSetBindings)
			mDescriptorSetLayouts[index] = device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));
		mLayout = device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, mDescriptorSetLayouts, mPushConstantRanges));

		vk::ComputePipelineCreateInfo info;
		info.stage = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, spirv.mShaderModule, spirv.mEntryPoint.c_str(), {});
		info.layout = mLayout;
		mPipeline = device->createComputePipeline(device.PipelineCache(), info).value;
		device.SetObjectName(mPipeline, name);
	}

	inline vk::PipelineBindPoint BindPoint() const override { return vk::PipelineBindPoint::eCompute; }
	inline uint3 WorkgroupSize() const { return mSpirvModules[0].mWorkgroupSize; }
	
	inline friend binary_stream& operator<<(binary_stream& stream, const ComputePipeline& pipeline) { return operator<<(stream, *(Pipeline*)&pipeline); }
	inline friend binary_stream& operator>>(binary_stream& stream, ComputePipeline& pipeline) { return operator>>(stream, *(Pipeline*)&pipeline); }
};

class GraphicsPipeline : public Pipeline {
private:
	friend class ShaderCompiler;
	vector<vk::PipelineColorBlendAttachmentState> mBlendStates;
	vk::PipelineDepthStencilStateCreateInfo mDepthStencilState;
	vector<vk::DynamicState> mDynamicStates;
	vk::PipelineInputAssemblyStateCreateInfo mInputAssemblyState;
	vk::PipelineMultisampleStateCreateInfo mMultisampleState;
	vk::PipelineRasterizationStateCreateInfo mRasterizationState;
	vk::PipelineViewportStateCreateInfo mViewportState;
	uint32_t mRenderQueue;

public:
	inline GraphicsPipeline(const string& name, const RenderPass& renderPass, uint32_t subpassIndex,
		const SpirvModuleGroup& spirv, const vector<vk::SpecializationInfo*>& specializationInfos = {},
		const vk::PipelineVertexInputStateCreateInfo& vertexInput = {}, vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList, vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack, vk::PolygonMode polygonMode = vk::PolygonMode::eFill,
		bool sampleShading = false, const vk::PipelineDepthStencilStateCreateInfo& depthStencilState = { {}, true, true, vk::CompareOp::eLessOrEqual, {}, {}, {}, {}, 0, 1 },
		const vector<vk::PipelineColorBlendAttachmentState>& blendStates = { vk::PipelineColorBlendAttachmentState() },
		const vector<vk::DynamicState>& dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eLineWidth }) {
		
		mSpirvModules = spirv;

		mHash = basic_hash(renderPass, subpassIndex, topology, vertexInput, cullMode, polygonMode, sampleShading);
		for (auto state : mBlendStates) mHash = basic_hash(mHash, state);
		for (auto state : dynamicStates) mHash = basic_hash(mHash, state);
		for (const SpirvModule& spirv : mSpirvModules) {
			mHash = basic_hash(mHash, spirv);
			for (const auto& [name, binding] : spirv.mDescriptorBindings) mDescriptorBindings.insert({ name, binding });
			for (const auto& [name, range] : spirv.mPushConstants) mPushConstantRanges.push_back(range);
		}
		
		Device& device = renderPass.Device();
		mLayout = device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, mDescriptorSetLayouts, mPushConstantRanges));
		
		mBlendStates = blendStates;
		mDepthStencilState = depthStencilState;
		mDynamicStates = dynamicStates;
		mInputAssemblyState = { {}, topology };
		mViewportState = { {}, 1, nullptr, 1, nullptr };
		mRasterizationState = { {}, false, false, polygonMode, cullMode };
		mMultisampleState = { {}, vk::SampleCountFlagBits::e1, sampleShading };
		vk::PipelineColorBlendStateCreateInfo blendState = { {}, false, vk::LogicOp::eCopy, mBlendStates };
		vk::PipelineDynamicStateCreateInfo dynamicState = { {}, dynamicStates };
		for (auto& [id, attachment] : renderPass.Subpass(subpassIndex).mAttachments)
			if (attachment.mType == AttachmentType::eColor || attachment.mType == AttachmentType::eDepthStencil) {
				mMultisampleState.rasterizationSamples = attachment.mSamples;
				break;
			}
		vector<vk::PipelineShaderStageCreateInfo> stages;
		for (size_t i = 0; i < mSpirvModules.size(); i++)
			stages.push_back(vk::PipelineShaderStageCreateInfo({}, mSpirvModules[i].mStage, mSpirvModules[i].mShaderModule, mSpirvModules[i].mEntryPoint.c_str(), specializationInfos[i] ));

		vk::GraphicsPipelineCreateInfo info({}, stages, &vertexInput, &mInputAssemblyState, nullptr, &mViewportState, &mRasterizationState, &mMultisampleState, &mDepthStencilState, &blendState, &dynamicState, mLayout, *renderPass, subpassIndex);
		mPipeline = device->createGraphicsPipeline(device.PipelineCache(), info).value;
		device.SetObjectName(mPipeline, name);
	}
	
	inline vk::PipelineBindPoint BindPoint() const override { return vk::PipelineBindPoint::eGraphics; }

	inline friend binary_stream& operator<<(binary_stream& stream, const GraphicsPipeline& pipeline) {
		stream << pipeline.mBlendStates;
		stream << pipeline.mDepthStencilState;
		stream << pipeline.mDynamicStates;
		stream << pipeline.mInputAssemblyState;
		stream << pipeline.mMultisampleState;
		stream << pipeline.mRasterizationState;
		stream << pipeline.mViewportState;
		stream << pipeline.mRenderQueue;
		return operator<<(stream, *(Pipeline*)&pipeline);
	}
	inline friend binary_stream& operator>>(binary_stream& stream, GraphicsPipeline& pipeline) {
		stream >> pipeline.mBlendStates;
		stream >> pipeline.mDepthStencilState;
		stream >> pipeline.mDynamicStates;
		stream >> pipeline.mInputAssemblyState;
		stream >> pipeline.mMultisampleState;
		stream >> pipeline.mRasterizationState;
		stream >> pipeline.mViewportState;
		stream >> pipeline.mRenderQueue;
		return operator>>(stream, *(Pipeline*)&pipeline);
	}
};

template<> inline size_t basic_hash(const stm::Pipeline& pipeline) { return pipeline.Hash(); }
template<> inline size_t basic_hash(const stm::ComputePipeline& pipeline) { return pipeline.Hash(); }
template<> inline size_t basic_hash(const stm::GraphicsPipeline& pipeline) { return pipeline.Hash(); }

}