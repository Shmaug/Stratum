#pragma once

#include "RenderPass.hpp"
#include "Shader.hpp"

namespace std {
inline string to_string(const vk::PushConstantRange& r) {
	return "stageFlags = " + to_string(r.stageFlags) + ", " + ", offset = " + to_string(r.offset) + ", size = " + to_string(r.size);
}
}

namespace stm {

class Pipeline : public DeviceResource {
	friend struct std::hash<Pipeline>;
public:
	inline ~Pipeline() {
		if (mPipeline) mDevice->destroyPipeline(mPipeline);
		if (mLayout) mDevice->destroyPipelineLayout(mLayout);
	}

	virtual vk::PipelineBindPoint bind_point() const = 0;

	inline vk::Pipeline& operator*() { return mPipeline; }
	inline vk::Pipeline* operator->() { return &mPipeline; }
	inline const vk::Pipeline& operator*() const { return mPipeline; }
	inline const vk::Pipeline* operator->() const { return &mPipeline; }
	inline operator bool() const { return mPipeline; }

	inline bool operator==(const Pipeline& rhs) const { return rhs.mPipeline == mPipeline; }
	inline vk::PipelineLayout layout() const { return mLayout; }
	inline const auto& shaders() const { return mShaders; };
	inline const auto& push_constants() const { return mPushConstants; };
	inline const auto& descriptor_set_layouts() const { return mDescriptorSetLayouts; };

protected:
	vk::Pipeline mPipeline;
	vk::PipelineLayout mLayout;
	vector<Shader::Specialization> mShaders;
	vector<shared_ptr<DescriptorSetLayout>> mDescriptorSetLayouts;
	unordered_multimap<string, vk::PushConstantRange> mPushConstants;

	STRATUM_API Pipeline(const string& name, const vk::ArrayProxy<const Shader::Specialization>& shaders, const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers = {});
	STRATUM_API Pipeline(const string& name, const vk::ArrayProxy<const Shader::Specialization>& shaders, const vk::ArrayProxy<shared_ptr<DescriptorSetLayout>>& descriptorSetLayouts);
};

class ComputePipeline : public Pipeline {
public:
	STRATUM_API ComputePipeline(const string& name, const Shader::Specialization& shader, const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers = {});
	STRATUM_API ComputePipeline(const string& name, const Shader::Specialization& shader, const vk::ArrayProxy<shared_ptr<DescriptorSetLayout>>& descriptorSetLayouts = {});
	inline vk::PipelineBindPoint bind_point() const override { return vk::PipelineBindPoint::eCompute; }
	inline const auto& workgroup_size() const { return shaders().front().shader().workgroup_size(); }
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
	VertexLayoutDescription mGeometryDescription;

public:
	STRATUM_API GraphicsPipeline(const string& name, const stm::RenderPass& renderPass, uint32_t subpassIndex,
		const VertexLayoutDescription& vertexDescription,
		const vk::ArrayProxy<const Shader::Specialization>& shaders, const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers = {},
		const vk::PipelineRasterizationStateCreateInfo& rasterState = { {}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack },
		bool sampleShading = false,
		const vk::PipelineDepthStencilStateCreateInfo& depthStencilState = { {}, true, true, vk::CompareOp::eLessOrEqual, {}, {}, {}, {}, 0, 1 },
		const vector<vk::PipelineColorBlendAttachmentState>& blendStates = {},
		const vector<vk::DynamicState>& dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eLineWidth });

	inline vk::PipelineBindPoint bind_point() const override { return vk::PipelineBindPoint::eGraphics; }
	inline const VertexLayoutDescription& vertex_layout() const { return mGeometryDescription; }
};

}