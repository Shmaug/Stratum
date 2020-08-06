#pragma once

#include <Core/Instance.hpp>
#include <Core/RenderPass.hpp>
#include <ShaderCompiler/ShaderCompiler.hpp>

class PipelineVariant {
public:
	ShaderVariant* mShaderVariant;

	VkPipelineLayout mPipelineLayout;
	std::vector<VkDescriptorSetLayout> mDescriptorSetLayouts;

	inline uint32_t GetDescriptorLocation(const std::string& descriptorName) const { return mShaderVariant->mDescriptorSetBindings.at(descriptorName).mBinding.binding; }
	inline uint32_t GetDescriptorSet(const std::string& descriptorName) const { return mShaderVariant->mDescriptorSetBindings.at(descriptorName).mSet; }
};
class ComputePipeline : public PipelineVariant {
public:
	VkPipeline mPipeline;
};
class GraphicsPipeline : public PipelineVariant {
public:
	std::string mName;
	Device* mDevice;
	std::unordered_map<PipelineInstance, VkPipeline> mPipelines;

	STRATUM_API VkPipeline GetPipeline(CommandBuffer* commandBuffer, const VertexInput* vertexInput,
		VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		VkCullModeFlags cullMode = VK_CULL_MODE_FLAG_BITS_MAX_ENUM,
		VkPolygonMode polyMode = VK_POLYGON_MODE_MAX_ENUM);
};

class Pipeline {
public:
	const std::string mName;

	STRATUM_API ~Pipeline();

	// Returns a pipeline for a specific shader pass and set of keywords, or nullptr if none exists
	STRATUM_API GraphicsPipeline* GetGraphics(const std::string& shaderPass, const std::set<std::string>& keywords = {}) const;
	// Returns a pipeline for a specific compute kernel and set of keywords, or nullptr if none exists
	STRATUM_API ComputePipeline* GetCompute(const std::string& kernel, const std::set<std::string>& keywords = {}) const;

	inline bool HasKeyword(const std::string& kw) const { return mKeywords.count(kw); }

	inline ::Device* Device() const { return mDevice; }

private:
	friend class GraphicsPipeline;
	friend class AssetManager;
	STRATUM_API Pipeline(const std::string& name, ::Device* device, const std::string& shaderFilename);

	::Device* mDevice;
	std::set<std::string> mKeywords;
	Shader* mShader;

	std::unordered_map<std::string, ComputePipeline*> mComputeVariants;
	std::unordered_map<std::string, GraphicsPipeline*> mGraphicsVariants;
};