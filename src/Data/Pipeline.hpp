#pragma once

#include <Data/Asset.hpp>
#include <Core/RenderPass.hpp>
#include <Data/Shader.hpp>

namespace stm {
// Represents a pipeline with various parameters
struct PipelineInstance {
	public:
	const uint64_t mRenderPassHash;
	const uint32_t mSubpassIndex;
	const vk::PrimitiveTopology mTopology;
	const vk::CullModeFlags mCullMode;
	const vk::PolygonMode mPolygonMode;
	const vk::PipelineVertexInputStateCreateInfo mVertexInput;

	PipelineInstance() = delete;
	inline PipelineInstance(uint64_t renderPassHash, uint32_t subpassIndex, vk::PrimitiveTopology topology, vk::CullModeFlags cullMode, vk::PolygonMode polyMode, const vk::PipelineVertexInputStateCreateInfo& vertexInput)
		: mRenderPassHash(renderPassHash), mSubpassIndex(subpassIndex), mTopology(topology), mCullMode(cullMode), mPolygonMode(polyMode), mVertexInput(vertexInput) {
			// Compute hash once upon creation
			mHash = hash_combine(mRenderPassHash, mSubpassIndex, mTopology, mCullMode, mPolygonMode);
			for (uint32_t i = 0; i < mVertexInput.vertexBindingDescriptionCount; i++)
				mHash = hash_combine(mHash, mVertexInput.pVertexBindingDescriptions[i].binding, mVertexInput.pVertexBindingDescriptions[i].inputRate, mVertexInput.pVertexBindingDescriptions[i].stride);
			for (uint32_t i = 0; i < mVertexInput.vertexAttributeDescriptionCount; i++)
				mHash = hash_combine(mHash, mVertexInput.pVertexAttributeDescriptions[i].binding, mVertexInput.pVertexAttributeDescriptions[i].format, mVertexInput.pVertexAttributeDescriptions[i].location, mVertexInput.pVertexAttributeDescriptions[i].offset);
		};

	inline bool operator==(const PipelineInstance& rhs) const { return rhs.mHash == mHash; }
	
private:
	friend struct std::hash<PipelineInstance>;
	size_t mHash;
};
}

namespace std {
template<>
struct hash<stm::PipelineInstance> {
	inline size_t operator()(const stm::PipelineInstance& p) const { return p.mHash; }
};
}

namespace stm {
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

	STRATUM_API vk::Pipeline GetPipeline(CommandBuffer& commandBuffer,
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
	friend class Device;
	STRATUM_API Pipeline(const fs::path& shaderfile, stm::Device* device, const std::string& name);

	std::set<std::string> mKeywords;
	Shader* mShader;

	std::map<std::string, ComputePipeline*> mComputeVariants;
	std::map<std::string, GraphicsPipeline*> mGraphicsVariants;
};

}