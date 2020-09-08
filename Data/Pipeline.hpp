#pragma once

#include <Data/Asset.hpp>
#include <Core/RenderPass.hpp>
#include <Data/Shader.hpp>

class PipelineVariant {
public:
	ShaderVariant* mShaderVariant;

	vk::PipelineLayout mPipelineLayout;
	std::vector<vk::DescriptorSetLayout> mDescriptorSetLayouts;

	inline uint32_t GetDescriptorLocation(const std::string& descriptorName) const { return mShaderVariant->mDescriptorSetBindings.at(descriptorName).mBinding.binding; }
	inline uint32_t GetDescriptorSet(const std::string& descriptorName) const { return mShaderVariant->mDescriptorSetBindings.at(descriptorName).mSet; }
};
class ComputePipeline : public PipelineVariant {
public:
	vk::Pipeline mPipeline;
};
class GraphicsPipeline : public PipelineVariant {
public:
	std::string mName;
	Device* mDevice;
	std::unordered_map<PipelineInstance, vk::Pipeline> mPipelines;

	STRATUM_API vk::Pipeline GetPipeline(stm_ptr<CommandBuffer> commandBuffer,
		vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList,
		const vk::PipelineVertexInputStateCreateInfo& vertexInput = vk::PipelineVertexInputStateCreateInfo(),
		vk::Optional<const vk::CullModeFlags> cullModeOverride = nullptr,
		vk::Optional<const vk::PolygonMode> polyModeOverride = nullptr);
};

class Pipeline : public Asset {
public:
	STRATUM_API ~Pipeline();

	// Returns a pipeline for a specific shader pass and set of keywords, or nullptr if none exists
	STRATUM_API GraphicsPipeline* GetGraphics(const std::string& shaderPass, const std::set<std::string>& keywords = {}) const;
	// Returns a pipeline for a specific compute kernel and set of keywords, or nullptr if none exists
	STRATUM_API ComputePipeline* GetCompute(const std::string& kernel, const std::set<std::string>& keywords = {}) const;

	inline bool HasKeyword(const std::string& kw) const { return mKeywords.count(kw); }

private:
	friend class GraphicsPipeline;
	friend class AssetManager;
	STRATUM_API Pipeline(const fs::path& shaderfile, ::Device* device, const std::string& name);

	std::set<std::string> mKeywords;
	Shader* mShader;

	std::unordered_map<std::string, ComputePipeline*> mComputeVariants;
	std::unordered_map<std::string, GraphicsPipeline*> mGraphicsVariants;
};