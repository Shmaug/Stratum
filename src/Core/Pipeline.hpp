#pragma once

#include "RenderPass.hpp"
#include "SpirvModule.hpp"

namespace std {
inline string to_string(const vk::PushConstantRange& r) {
	return "stageFlags = " + to_string(r.stageFlags) + ", " + ", offset = " + to_string(r.offset) + ", size = " + to_string(r.size);
}
}

namespace stm {

class Pipeline : public DeviceResource {
public:
	struct ShaderStage {
	private:
		shared_ptr<SpirvModule> mSpirv;
		unordered_map<string, uint32_t> mSpecializationConstants;

	public:
		ShaderStage() = default;
		ShaderStage(ShaderStage&&) = default;
		ShaderStage(const ShaderStage&) = default;
		STRATUM_API ShaderStage(const shared_ptr<SpirvModule>& spirv, const unordered_map<string, uint32_t>& specializationConstants);

		ShaderStage& operator=(const ShaderStage&) = default;
		ShaderStage& operator=(ShaderStage&&) = default;

		inline const auto& spirv() const { return mSpirv; }
		inline const auto& specialization_constants() const { return mSpecializationConstants; }
		inline void get_specialization_info(vector<vk::SpecializationMapEntry>& entries, vector<uint32_t>& data) const {
			for (const auto&[name, v] : mSpecializationConstants) {
				entries.emplace_back(mSpirv->specialization_constants().at(name).first, (uint32_t)(data.size()*sizeof(uint32_t)), sizeof(uint32_t));
				data.emplace_back(v);
			}
		}
	};

protected:
	vk::Pipeline mPipeline;
	vk::PipelineLayout mLayout;
	vector<ShaderStage> mStages;
	vector<shared_ptr<DescriptorSetLayout>> mDescriptorSetLayouts;
	unordered_multimap<string, vk::PushConstantRange> mPushConstants;

	STRATUM_API Pipeline(const string& name, const vk::ArrayProxy<const ShaderStage>& stages, const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers = {});

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
	inline const auto& stages() const { return mStages; };
	inline const auto& push_constants() const { return mPushConstants; };
	inline const auto& descriptor_set_layouts() const { return mDescriptorSetLayouts; };
};

class ComputePipeline : public Pipeline {
public:
	STRATUM_API ComputePipeline(const string& name, const ShaderStage& stage, const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers = {});
	inline vk::PipelineBindPoint bind_point() const override { return vk::PipelineBindPoint::eCompute; }
	inline const auto& workgroup_size() const { return stages().front().spirv()->workgroup_size(); }
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
	GeometryStateDescription mGeometryDescription;

public:
	STRATUM_API GraphicsPipeline(const string& name, const stm::RenderPass& renderPass, uint32_t subpassIndex,
		const GeometryStateDescription& geometryDescription,
		const vk::ArrayProxy<const ShaderStage>& stages, const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers = {},
		const vk::PipelineRasterizationStateCreateInfo& rasterState = { {}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack },
		bool sampleShading = false,
		const vk::PipelineDepthStencilStateCreateInfo& depthStencilState = { {}, true, true, vk::CompareOp::eLessOrEqual, {}, {}, {}, {}, 0, 1 },
		const vector<vk::PipelineColorBlendAttachmentState>& blendStates = {},
		const vector<vk::DynamicState>& dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eLineWidth });

	inline vk::PipelineBindPoint bind_point() const override { return vk::PipelineBindPoint::eGraphics; }
	inline const GeometryStateDescription& geometry_state() const { return mGeometryDescription; }
};

}

namespace std {
template<> struct hash<stm::Pipeline::ShaderStage> {
	inline size_t operator()(const stm::Pipeline::ShaderStage& r) const {
		return hash_args(*r.spirv(), r.specialization_constants()|views::values);
	}
};
}
