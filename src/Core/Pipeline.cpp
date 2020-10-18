#include "Pipeline.hpp"

#include "../Data/Shader.hpp"
#include "../Scene/Scene.hpp"

#include "RenderPass.hpp"
#include "CommandBuffer.hpp"

using namespace std;
using namespace stm;

Pipeline::Pipeline(vk::Pipeline pipeline, vk::PipelineLayout layout, vector<vk::DescriptorSetLayout> descriptorSetLayouts)
	: mPipeline(pipeline), mLayout(layout), mDescriptorSetLayouts(descriptorSetLayouts) {}
Pipeline::Pipeline(const fs::path& shaderFilename, stm::Device* device, const string& name) : Asset(shaderFilename, device, name) {}
Pipeline::~Pipeline() {}

ComputePipeline::ComputePipeline(const string& name, const SpirvModule& module) {
	vk::ComputePipelineCreateInfo info;
	info.stage = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, module.mShaderModule, module.mEntryPoint.c_str(), {});
	info.layout = mLayout;
	mPipeline = (*mDevice)->createComputePipelines(mDevice->PipelineCache(), { info }).value[0];
	mDevice->SetObjectName(mPipeline, name);
	mWorkgroupSize = module.
}

GraphicsPipeline::GraphicsPipeline(const string& name, const vector<SpirvModule>& modules,
	const Subpass& subpass, vk::PrimitiveTopology topology, const vk::PipelineVertexInputStateCreateInfo& vertexInput,
	vk::CullModeFlags cullMode, vk::PolygonMode polygonMode) {

	vector<vk::PipelineShaderStageCreateInfo> stageInfos(modules.size());
	for (uint32_t i = 0; i < modules.size(); i++) {
		stageInfos[i].stage = modules[i].mStage;
		stageInfos[i].pName = modules[i].mEntryPoint.c_str();
		stageInfos[i].module = modules[i].mShaderModule;
	}

	vk::PipelineViewportStateCreateInfo viewportState = {};
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eLineWidth };
	vk::PipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
	dynamicState.pDynamicStates = dynamicStates.data();

	vk::PipelineRasterizationStateCreateInfo rasterizationState = {};
	rasterizationState.cullMode = cullMode;
	rasterizationState.polygonMode = polygonMode;

	vk::PipelineColorBlendStateCreateInfo blendState = {};
	blendState.attachmentCount = (uint32_t)mShaderVariant->mBlendStates.size();
	blendState.pAttachments = mShaderVariant->mBlendStates.data();

	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
	inputAssemblyState.topology = topology;

	vk::PipelineMultisampleStateCreateInfo multisampleState = {};
	multisampleState.sampleShadingEnable = mShaderVariant->mSampleShading;
	multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;
	for (auto& kp : subpass.mAttachments)
		if (kp.second.mType == AttachmentType::eColor || kp.second.mType == AttachmentType::eDepthStencil) {
			multisampleState.rasterizationSamples = kp.second.mSamples;
			break;
		}

	vk::GraphicsPipelineCreateInfo info = {};
	info.stageCount = (uint32_t)stageInfos.size();
	info.pStages = stageInfos.data();
	info.pInputAssemblyState = &inputAssemblyState;
	info.pVertexInputState = &vertexInput;
	info.pViewportState = &viewportState;
	info.pRasterizationState = &rasterizationState;
	info.pMultisampleState = &multisampleState;
	info.pDepthStencilState = &mShaderVariant->mDepthStencilState;
	info.pColorBlendState = &blendState;
	info.pDynamicState = &dynamicState;
	info.layout = mLayout;
	info.renderPass = **;
	info.subpass = subpass.mIndex;
	mPipeline = (*mDevice)->createGraphicsPipelines(mDevice->PipelineCache(), { info }).value[0];
	mDevice->SetObjectName(mPipeline, mName);
}