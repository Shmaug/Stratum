#pragma once

#include "CommandBuffer.hpp"
#include "SpirvModule.hpp"

namespace stm {

class PipelineState {
public:
	inline PipelineState(const string& name, const vk::ArrayProxy<const shared_ptr<SpirvModule>>& modules) : mName(name) {
		for (const auto& spirv : modules)
			mModules.emplace(spirv->stage(), spirv);
	}
	template<convertible_to<SpirvModule>... Args>
	inline PipelineState(const string& name, const shared_ptr<Args>&... args) : PipelineState(name, { args... }) {}

	inline string& name() { return mName; }
	inline const string& name() const { return mName; }

	inline const auto& immutable_samplers() const { return mImmutableSamplers; }
	inline void set_immutable_sampler(const string& name, const shared_ptr<Sampler>& sampler) {
		auto it = mImmutableSamplers.find(name);
		mImmutableSamplers.emplace(name, sampler);
		mPipelines.clear();
	}

	inline uint32_t& specialization_constant(const string& name) { return mSpecializationConstants[name]; }
	inline uint32_t specialization_constant(const string& name) const {
		auto it = mSpecializationConstants.find(name);
		if (it != mSpecializationConstants.end())
			return it->second;
		for (const auto& [stage, spirv] : mModules)
			if (auto it2 = spirv->specialization_constants().find(name); it2 != spirv->specialization_constants().end())
				return it2->second.second; // default value
		throw invalid_argument("No specialization constant named " + name);
	}
	
	inline shared_ptr<SpirvModule> stage(vk::ShaderStageFlagBits stage) const { 
		auto it = mModules.find(stage);
		return (it == mModules.end()) ? nullptr : it->second;
	}

	template<typename T>
	inline T& push_constant(const string& name) {
		auto it = mPushConstants.find(name);
		size_t sz = 0;
		if (it == mPushConstants.end()) {
			for (const auto&[stage, spirv] : mModules)
				if (auto it = spirv->push_constants().find(name); it != spirv->push_constants().end()) {
					sz = it->second.mTypeSize;
					break;
			}
			if (sz == 0) throw invalid_argument("No push constant named " + name);
		} else
			sz = it->second.size();

		if (sizeof(T) != sz) throw invalid_argument("Argument must match push constant size");
		auto& c = mPushConstants[name];
		c.resize(sizeof(T));
		return *reinterpret_cast<T*>(c.data());
	}
	template<typename T>
	inline T& push_constant(const string& name) const {
		auto it = mPushConstants.find(name);
		if (it == mPushConstants.end()) throw invalid_argument("No push constant named " + name);
		if (it->second.size() != sizeof(T)) throw invalid_argument("Type size must match push constant size");
		return *reinterpret_cast<T*>(it->second.data());
	}
	STRATUM_API uint32_t descriptor_count(const string& name) const;
	STRATUM_API stm::Descriptor& descriptor(const string& name, uint32_t arrayIndex = 0);
	inline const stm::Descriptor& descriptor(const string& name, uint32_t arrayIndex = 0) const {
		return mDescriptors.at(name).at(arrayIndex);
	}

	STRATUM_API void transition_images(CommandBuffer& commandBuffer) const;

	inline auto descriptor_sets() const { return mDescriptorSets | views::values; }
	STRATUM_API void bind_descriptor_sets(CommandBuffer& commandBuffer, const unordered_map<string,uint32_t>& dynamicOffsets = {});
	STRATUM_API void push_constants(CommandBuffer& commandBuffer) const;
	
	inline auto pipelines() const { return mPipelines | views::values; }

protected:
	string mName;

	map<vk::ShaderStageFlags, shared_ptr<SpirvModule>> mModules;
	unordered_map<string, uint32_t> mSpecializationConstants;
	unordered_map<string, shared_ptr<Sampler>> mImmutableSamplers;
	unordered_map<string, unordered_map<uint32_t, stm::Descriptor>> mDescriptors;
	unordered_map<string, vector<byte>> mPushConstants;

	unordered_map<size_t, shared_ptr<Pipeline>> mPipelines;
	unordered_multimap<const DescriptorSetLayout*, shared_ptr<DescriptorSet>> mDescriptorSets;
	
	inline shared_ptr<Pipeline> find_pipeline(size_t key) const {
		auto it = mPipelines.find(key);
		if (it == mPipelines.end())
			return {};
		else
			return it->second;
	}
	inline void add_pipeline(size_t key, const shared_ptr<Pipeline>& pipeline) { 
		mPipelines.emplace(key, pipeline);
	}
};

class ComputePipelineState : public PipelineState {
public:
	inline ComputePipelineState(const string& name, const shared_ptr<SpirvModule>& module) : PipelineState(name, { module }) {}
	STRATUM_API shared_ptr<ComputePipeline> get_pipeline();
};

class GraphicsPipelineState : public PipelineState {
public:
	inline GraphicsPipelineState(const string& name, const vk::ArrayProxy<const shared_ptr<SpirvModule>>& modules) : PipelineState(name, modules) {}
	template<convertible_to<SpirvModule>... Args>
	inline GraphicsPipelineState(const string& name, const shared_ptr<Args>&... args) : PipelineState(name, { args... }) {}


	inline void sample_shading(bool v) { mSampleShading = v; }
	inline const auto& sample_shading() const { return mSampleShading; }

	inline auto& raster_state() { return mRasterState; }
	inline const auto& raster_state() const { return mRasterState; }

	inline auto& blend_states() { return mBlendStates; }
	inline const auto& blend_states() const { return mBlendStates; }

	inline auto& depth_stencil() { return mDepthStencilState; }
	inline const auto& depth_stencil() const { return mDepthStencilState; }

	STRATUM_API shared_ptr<GraphicsPipeline> get_pipeline(const RenderPass& renderPass, uint32_t subpassIndex, const GeometryStateDescription& geometryDescription = {}, vk::ShaderStageFlags stageMask = vk::ShaderStageFlagBits::eAll);

private:
	vk::PipelineRasterizationStateCreateInfo mRasterState = { {}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack };
	vk::PipelineDepthStencilStateCreateInfo mDepthStencilState = vk::PipelineDepthStencilStateCreateInfo({}, true, true, vk::CompareOp::eLessOrEqual);
	vector<vk::PipelineColorBlendAttachmentState> mBlendStates;
	bool mSampleShading = false;
};

}

