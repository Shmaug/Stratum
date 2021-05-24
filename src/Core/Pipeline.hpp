#pragma once

#include "RenderPass.hpp"
#include "SpirvModule.hpp"

namespace stm {

// TODO: descriptor_set_layouts have no way of being passed into a pipeline

using VertexInputBindingData = pair< vector<vk::VertexInputAttributeDescription>, vector<vk::VertexInputBindingDescription> >;
inline VertexInputBindingData create_vertex_bindings(const Geometry& geometry, const SpirvModule& vertexShader) {
	vector<Buffer::View<byte>> buffers;
	vector<vk::VertexInputAttributeDescription> attributes;
	vector<vk::VertexInputBindingDescription> bindings;

	for (const auto& [n, v] : vertexShader.stage_inputs()) {
		const Geometry::Attribute& attrib = geometry[v.mAttributeType][v.mAttributeIndex];
		uint32_t bindingindex;
		for (bindingindex = 0; bindingindex < bindings.size(); bindingindex++)
			if (buffers[bindingindex] == attrib.buffer_view() && bindings[bindingindex].stride == attrib.stride() && bindings[bindingindex].inputRate == attrib.input_rate())
				break;
		if (bindingindex == bindings.size()) {
			bindings.emplace_back(bindingindex, attrib.stride(), attrib.input_rate());
			buffers.emplace_back(attrib.buffer_view());
		}
		attributes.emplace_back(v.mLocation, bindingindex, attrib.format(), attrib.offset());
	}
	return make_pair(move(attributes), move(bindings));
}

class Pipeline : public DeviceResource {
protected:
	vk::Pipeline mPipeline;
	vk::PipelineLayout mLayout;
	size_t mHash = 0;

	vector<shared_ptr<SpirvModule>> mSpirvModules;
	vector<shared_ptr<DescriptorSetLayout>> mDescriptorSetLayouts;
	unordered_map<string, byte_blob> mSpecializationConstants;
	unordered_map<string, vk::PushConstantRange> mPushConstants;
	
	inline void set_spirv(const vk::ArrayProxy<const shared_ptr<SpirvModule>>& modules) {
		for (const shared_ptr<SpirvModule>& spirv : modules) {
			mSpirvModules.emplace_back(spirv);
		
			for (const auto& [id, p] : spirv->push_constants()) {
				auto it = mPushConstants.find(id);
				if (it == mPushConstants.end())
					mPushConstants.emplace(id, vk::PushConstantRange(spirv->stage(), p.first, p.second));
				else {
					if (it->second.offset != p.first || it->second.size != p.second) throw runtime_error("spirv modules share the same push constant names with different offsets/sizes"); 
					it->second.stageFlags |= spirv->stage();
				}
			}

			for (const auto& [id, binding] : spirv->descriptors())
				if (auto it = mDescriptorBindings.find(id); it != mDescriptorBindings.end()) {
					if (it->second.mBinding == binding.mBinding && it->second.mSet == binding.mSet) {
						if (it->second.mDescriptorType != binding.mDescriptorType) throw invalid_argument("spirv modules share the same descriptor binding with different descriptor types");
						it->second.mStageFlags |= binding.mStageFlags;
						it->second.mDescriptorCount = max(it->second.mDescriptorCount, binding.mDescriptorCount);
					} else
						mDescriptorBindings[to_string(binding.mSet)+"."+to_string(binding.mBinding) + id] = binding;
				} else
					mDescriptorBindings[id] = binding;
		}
	}

	// requires mSpirvModules and mDescriptorSetLayouts to be assigned
	// populates mPushConstants, mDescriptorBindings; hashes spirv into mHash
	inline vk::PipelineLayout create_layout(const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers) {
		unordered_map<uint32_t, unordered_map<uint32_t, DescriptorSetLayout::Binding>> setBindings;
		for (const auto&[name, b] : mDescriptorBindings) {
			if (b.mSet >= mDescriptorSetLayouts.size()) mDescriptorSetLayouts.resize(b.mSet + 1);
			setBindings[b.mSet][b.mBinding] = DescriptorSetLayout::Binding(b.mDescriptorType, b.mStageFlags, b.mDescriptorCount);
			if (auto it = immutableSamplers.find(name); it != immutableSamplers.end())
				setBindings[b.mSet][b.mBinding].mImmutableSamplers.emplace_back(it->second);
		}
		for (uint32_t set = 0; set < mDescriptorSetLayouts.size(); set++)
			if (setBindings.count(set))
				mDescriptorSetLayouts[set] = make_shared<DescriptorSetLayout>(mDevice, name(), setBindings.at(set));
			else
				mDescriptorSetLayouts[set] = make_shared<DescriptorSetLayout>(mDevice, name());

		vector<vk::PushConstantRange> pushConstantRanges;
		for (const auto& spirv : mSpirvModules) {
			uint32_t first = ~0;
			uint32_t last = 0;
			for (const auto& [id, p] : spirv->push_constants()) {
				first = min(first, p.first);
				last = max(last, p.first + p.second);
			}
			pushConstantRanges.emplace_back(spirv->stage(), first, last - first);
			mHash = hash_args(mHash, spirv);
		}
		mHash = hash_args(mHash, mSpecializationConstants);	

		for (const auto& l : mDescriptorSetLayouts)
			for (const auto&[i,b] : l->bindings())
				for (const auto& s : b.mImmutableSamplers)
					mHash = hash_args(mHash, s->create_info()); // only have to hash the immutable samplers; hashing the spir-v takes care of bindings/etc
	
		vector<vk::DescriptorSetLayout> tmp(mDescriptorSetLayouts.size());
		ranges::transform(mDescriptorSetLayouts, tmp.begin(), &DescriptorSetLayout::operator const vk::DescriptorSetLayout &);
		mLayout = mDevice->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, tmp, pushConstantRanges));
		return mLayout;
	}
	
	inline Pipeline(Device& device, const string& name) : DeviceResource(device, name) {}

public:
	inline ~Pipeline() {
		if (mLayout) mDevice->destroyPipelineLayout(mLayout);
		if (mPipeline) mDevice->destroyPipeline(mPipeline);
	}

	virtual vk::PipelineBindPoint bind_point() const = 0;
	
	inline vk::Pipeline& operator*() { return mPipeline; }
	inline vk::Pipeline* operator->() { return &mPipeline; }
	inline const vk::Pipeline& operator*() const { return mPipeline; }
	inline const vk::Pipeline* operator->() const { return &mPipeline; }
	inline operator bool() const { return mPipeline; }

	inline bool operator==(const Pipeline& rhs) const { return rhs.mPipeline == mPipeline; }
	inline size_t hash() const { return mHash; }
	inline vk::PipelineLayout layout() const { return mLayout; }
	inline const auto& modules() const { return mSpirvModules; };
	inline const auto& descriptor_set_layouts() const { return mDescriptorSetLayouts; };
	// most of the time, a pipeline wont have named descriptors with different bindings, so there is usually only one item in this range
	inline const auto& descriptor_bindings() { return mDescriptorBindings; };
	// most of the time, a pipeline wont have named push constants with different ranges, so there is usually only one item in this range
	inline const auto& push_constants() { return mPushConstants; };

	inline const DescriptorSetLayout::Binding& binding(const string& name) const {
		auto it = mDescriptorBindings.find(name);
		if (it == mDescriptorBindings.end()) throw invalid_argument("no descriptor named " + name);
		return it->second;
	}
	};

class ComputePipeline : public Pipeline {
public:
	inline ComputePipeline(Device& device, const string& name, shared_ptr<SpirvModule> spirv,
		const unordered_map<string, byte_blob>& specializationConstants = {},
		const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers = {}) : Pipeline(device, name) {
			
		mSpecializationConstants = specializationConstants;
		set_spirv(spirv);
		create_layout(immutableSamplers);
		
		vector<vk::SpecializationMapEntry> mapEntries;
		byte_blob specData;
		for (const auto&[name,data] : mSpecializationConstants)
			if (auto it = spirv->specialization_map().find(name); it != spirv->specialization_map().end()) {
				specData.resize(max(specData.size(), it->second.offset + it->second.size));
				memcpy(specData.data() + it->second.offset, data.data(), min(data.size(), it->second.size));
			}
		vk::SpecializationInfo specInfo((uint32_t)mapEntries.size(), mapEntries.data(), (uint32_t)specData.size(), specData.data());
		mPipeline = mDevice->createComputePipeline(mDevice.pipeline_cache(),
			vk::ComputePipelineCreateInfo({},
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, **spirv, spirv->entry_point().c_str(), mapEntries.size() ? &specInfo : nullptr),
				mLayout)).value;
		mDevice.set_debug_name(mPipeline, name);
	}

	inline vk::PipelineBindPoint bind_point() const override { return vk::PipelineBindPoint::eCompute; }
	inline vk::Extent3D workgroup_size() const { return mSpirvModules[0]->workgroup_size(); }
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
	inline GraphicsPipeline(const string& name, const stm::RenderPass& renderPass, uint32_t subpassIndex, const Geometry& geometry,
		const vk::ArrayProxy<const shared_ptr<SpirvModule>>& spirv, const unordered_map<string, byte_blob>& specializationConstants = {},
		const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers = {},
		vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack, vk::PolygonMode polygonMode = vk::PolygonMode::eFill, bool sampleShading = false,
		const vk::PipelineDepthStencilStateCreateInfo& depthStencilState = { {}, true, true, vk::CompareOp::eLessOrEqual, {}, {}, {}, {}, 0, 1 },
		const vector<vk::PipelineColorBlendAttachmentState>& blendStates = {},
		const vector<vk::DynamicState>& dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eLineWidth }) : Pipeline(renderPass.mDevice, name) {
		
		mSpecializationConstants = specializationConstants;
		set_spirv(spirv);
		create_layout(immutableSamplers);

		vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1;
		for (const auto&[type,blendState,desc] : renderPass.subpasses()[subpassIndex].attachments() | views::values) {
			if (type == RenderPass::AttachmentTypeFlags::eColor || type == RenderPass::AttachmentTypeFlags::eDepthStencil)
				sampleCount = desc.samples;
			if (blendStates.empty() && type == RenderPass::AttachmentTypeFlags::eColor)
				mBlendStates.emplace_back(blendState);
		}
		if (!blendStates.empty()) mBlendStates = blendStates;

		mHash = hash_args(renderPass, subpassIndex, geometry.topology(), cullMode, polygonMode, sampleShading);
		for (const auto state : mBlendStates) mHash = hash_args(mHash, state);
		for (const auto state : dynamicStates) mHash = hash_args(mHash, state);

		for (auto& [id, desc] : renderPass.subpasses()[subpassIndex].attachments())
			if (get<RenderPass::AttachmentTypeFlags>(desc) & (RenderPass::AttachmentTypeFlags::eColor | RenderPass::AttachmentTypeFlags::eDepthStencil)) {
				mMultisampleState.rasterizationSamples = get<vk::AttachmentDescription>(desc).samples;
				break;
			}

		auto vertexInput = create_vertex_bindings(geometry, *spirv.front());
		if (!vertexInput.first.empty()) mHash = hash_args(mHash, vertexInput);
		vk::PipelineVertexInputStateCreateInfo vertexInfo({}, vertexInput.second, vertexInput.first);

		mDepthStencilState = depthStencilState;
		mDynamicStates = dynamicStates;
		mInputAssemblyState = { {}, geometry.topology() };
		mViewportState = { {}, 1, nullptr, 1, nullptr };
		mRasterizationState = { {}, false, false, polygonMode, cullMode };
		mMultisampleState = { {}, sampleCount, sampleShading };
		vk::PipelineColorBlendStateCreateInfo blendState({}, false, vk::LogicOp::eCopy, mBlendStates);
		vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamicStates);

		list<byte_blob> specDatas;
		list<vector<vk::SpecializationMapEntry>> specMapEntries;
		vector<vk::PipelineShaderStageCreateInfo> stages(mSpirvModules.size());
		ranges::transform(mSpirvModules, stages.begin(), [&](const auto& spirv) {
			auto& mapEntries = specMapEntries.emplace_front(vector<vk::SpecializationMapEntry>());
			auto& specData = specDatas.emplace_front(byte_blob());
			for (const auto&[name,data] : mSpecializationConstants)
				if (auto it = spirv->specialization_map().find(name); it != spirv->specialization_map().end()) {
					auto& entry = mapEntries.emplace_back(it->second.constantID, (uint32_t)specData.size(), it->second.size);
					specData.resize(entry.offset + entry.size);
					memcpy(specData.data() + entry.offset, data.data(), min(data.size(), it->second.size));
				}
			vk::SpecializationInfo specInfo((uint32_t)mapEntries.size(), mapEntries.data(), (uint32_t)specData.size(), specData.data());
			return vk::PipelineShaderStageCreateInfo({}, spirv->stage(), **spirv, spirv->entry_point().c_str(), specInfo.dataSize ? &specInfo : nullptr);
		});

		mPipeline = mDevice->createGraphicsPipeline(mDevice.pipeline_cache(), 
			vk::GraphicsPipelineCreateInfo({}, stages, &vertexInfo, &mInputAssemblyState, nullptr, &mViewportState,
			&mRasterizationState, &mMultisampleState, &mDepthStencilState, &blendState, &dynamicState, mLayout, *renderPass, subpassIndex)).value;
		mDevice.set_debug_name(mPipeline, name);
	}
	
	inline vk::PipelineBindPoint bind_point() const override { return vk::PipelineBindPoint::eGraphics; }
};

}

namespace std {
template<class T> requires(is_base_of_v<stm::Pipeline,T>)
struct hash<T> {
	inline size_t operator()(const T& pipeline) { return pipeline.hash(); }
};
}