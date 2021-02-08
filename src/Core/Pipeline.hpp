#pragma once

#include "RenderPass.hpp"
#include "Asset/SpirvModuleGroup.hpp"

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
	unordered_map<string, vk::SamplerCreateInfo> mImmutableSamplers;

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
};

class ComputePipeline : public Pipeline {
public:
	inline ComputePipeline(const string& name, Device& device, const SpirvModule& spirv) {
		mSpirvModules = SpirvModuleGroup(device, { { spirv.mEntryPoint, spirv } });
		mHash = hash<SpirvModule>()(spirv);

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
	inline Vector3u WorkgroupSize() const { return mSpirvModules.begin()->second.mWorkgroupSize; }
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
		const SpirvModuleGroup& spirv, const unordered_map<string, vk::SpecializationInfo*>& specializationInfos = {},
		const vk::PipelineVertexInputStateCreateInfo& vertexInput = {}, vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList, vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack, vk::PolygonMode polygonMode = vk::PolygonMode::eFill,
		bool sampleShading = false, const vk::PipelineDepthStencilStateCreateInfo& depthStencilState = { {}, true, true, vk::CompareOp::eLessOrEqual, {}, {}, {}, {}, 0, 1 },
		const vector<vk::PipelineColorBlendAttachmentState>& blendStates = { vk::PipelineColorBlendAttachmentState() },
		const vector<vk::DynamicState>& dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eLineWidth }) {
		
		mSpirvModules = spirv;

		mHash = hash_combine(renderPass, subpassIndex, topology, vertexInput, cullMode, polygonMode, sampleShading);
		for (auto state : mBlendStates) mHash = hash_combine(mHash, state);
		for (auto state : dynamicStates) mHash = hash_combine(mHash, state);
		for (const auto& [entryp,spirv] : mSpirvModules) {
			mHash = hash_combine(mHash, spirv);
			for (const auto& [name, binding] : spirv.mDescriptorBindings) mDescriptorBindings.insert({ name, binding });
			for (const auto& [name, range] : spirv.mPushConstants) mPushConstantRanges.push_back(range);
		}
		
		Device& device = renderPass.mDevice;
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
		for (auto& [id, desc] : renderPass.SubpassDescriptions()[subpassIndex].mAttachmentDescriptions)
			if (get<RenderPass::AttachmentType>(desc) & (RenderPass::AttachmentType::eColor | RenderPass::AttachmentType::eDepthStencil)) {
				mMultisampleState.rasterizationSamples = get<vk::AttachmentDescription>(desc).samples;
				break;
			}
		vector<vk::PipelineShaderStageCreateInfo> stages(mSpirvModules.size());
		uint32_t i = 0;
		for (const auto& [entryp,spirv] : mSpirvModules)
			stages[i++] = vk::PipelineShaderStageCreateInfo({}, spirv.mStage, spirv.mShaderModule, entryp.c_str(), specializationInfos.count(entryp) ? specializationInfos.at(entryp) : nullptr );

		vk::GraphicsPipelineCreateInfo info({}, stages, &vertexInput, &mInputAssemblyState, nullptr, &mViewportState, &mRasterizationState, &mMultisampleState, &mDepthStencilState, &blendState, &dynamicState, mLayout, *renderPass, subpassIndex);
		mPipeline = device->createGraphicsPipeline(device.PipelineCache(), info).value;
		device.SetObjectName(mPipeline, name);
	}
	
	inline vk::PipelineBindPoint BindPoint() const override { return vk::PipelineBindPoint::eGraphics; }
};

}

namespace std {
template<class T> requires(is_base_of_v<stm::Pipeline,T>)
struct hash<T> { inline size_t operator()(const T& pipeline) { return pipeline.Hash(); } };
}