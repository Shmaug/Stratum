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
		inline ShaderStage(const shared_ptr<SpirvModule>& spirv, const unordered_map<string, uint32_t>& specializationConstants) : mSpirv(spirv) {
			for (const auto&[name, p] : mSpirv->specialization_constants()) {
				const auto[id,defaultValue] = p;
				if (auto it = specializationConstants.find(name); it != specializationConstants.end())
					mSpecializationConstants.emplace(name, it->second);
				else
					mSpecializationConstants.emplace(name, defaultValue);
			}
		}

		ShaderStage& operator=(const ShaderStage&) = default;
		ShaderStage& operator=(ShaderStage&&) = default;

		inline const auto& spirv() const { return mSpirv; }
		inline const auto& specialization_constants() const { return mSpecializationConstants; }
		inline void specialization_info(vector<vk::SpecializationMapEntry>& entries, vector<uint32_t>& data) const {
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

	inline Pipeline(Device& device, const string& name, const vk::ArrayProxy<const ShaderStage>& stages, const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers = {}) : DeviceResource(device, name) {
		mStages.resize(stages.size());
		ranges::copy(stages, mStages.begin());

		vector<vk::PushConstantRange> pcranges;
		vector<unordered_map<uint32_t, DescriptorSetLayout::Binding>> bindings;

		for (const auto& stage : mStages) {
			// gather bindings
			for (const auto&[id, binding] : stage.spirv()->descriptors()) {
				uint32_t c = 1;
				for (const auto& v : binding.mArraySize)
					if (v.index() == 0)
						// array size is a literal
						c *= get<uint32_t>(v);
					else
						// array size is a specialization constant
						c *= stage.specialization_constants().at(get<string>(v));

				if (bindings.size() <= binding.mSet) bindings.resize(binding.mSet + 1);
				auto it = bindings[binding.mSet].find(binding.mBinding);
				if (it == bindings[binding.mSet].end())
					it = bindings[binding.mSet].emplace(binding.mBinding, DescriptorSetLayout::Binding(binding.mDescriptorType, stage.spirv()->stage(), c)).first;
				else {
					it->second.mStageFlags |= stage.spirv()->stage();
					if (it->second.mDescriptorCount != c) throw logic_error("SPIR-V modules contain with different counts at the same binding");
					if (it->second.mDescriptorType != binding.mDescriptorType) throw logic_error("SPIR-V modules contain descriptors of different types at the same binding");
				}
				if (auto s = immutableSamplers.find(id); s != immutableSamplers.end())
					it->second.mImmutableSamplers.push_back(s->second);
			}
			
			// gather push constants
			if (!stage.spirv()->push_constants().empty()) {
				uint32_t rangeBegin = numeric_limits<uint32_t>::max();
				uint32_t rangeEnd = 0;
				for (const auto& [id, p] : stage.spirv()->push_constants()) {
					uint32_t sz = p.mTypeSize;
					if (!p.mArraySize.empty()) {
						sz = p.mArrayStride;
						for (const auto& v : p.mArraySize)
							if (v.index() == 0)
								// array size is a literal
								sz *= get<uint32_t>(v);
							else
								// array size is a specialization constant
								sz *= stage.specialization_constants().at(get<string>(v));
					}
					rangeBegin = min(rangeBegin, p.mOffset);
					rangeEnd = max(rangeEnd, p.mOffset + sz);

					if (auto it = mPushConstants.find(id); it != mPushConstants.end()) {
						if (it->second.offset != p.mOffset || it->second.size != sz)
							throw logic_error("Push constant [" + id + ", stage = " + to_string(stage.spirv()->stage()) + " offset = " + to_string(p.mOffset) + ", size = " + to_string(sz) + " ] does not match push constant found other stage(s): " + to_string(it->second));
						it->second.stageFlags |= stage.spirv()->stage();
					} else
						mPushConstants.emplace(id, vk::PushConstantRange(stage.spirv()->stage(), p.mOffset, sz));
				}
				pcranges.emplace_back(stage.spirv()->stage(), rangeBegin, rangeEnd);
			}
		}

		// create descriptorsetlayouts
		mDescriptorSetLayouts.resize(bindings.size());
		for (uint32_t i = 0; i < bindings.size(); i++)
			mDescriptorSetLayouts[i] = make_shared<DescriptorSetLayout>(device, name+"/DescriptorSet"+to_string(i), bindings[i]);
		
		vector<vk::DescriptorSetLayout> layouts(mDescriptorSetLayouts.size());
		ranges::transform(mDescriptorSetLayouts, layouts.begin(), &DescriptorSetLayout::operator const vk::DescriptorSetLayout &);

		mLayout = mDevice->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts, pcranges));
	}

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
	inline ComputePipeline(Device& device, const string& name, const ShaderStage& stage, const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers = {})
		: Pipeline(device, name, stage, immutableSamplers) {
		vector<uint32_t> data;
		vector<vk::SpecializationMapEntry> entries;
		stage.specialization_info(entries, data);
		vk::SpecializationInfo specializationInfo((uint32_t)entries.size(), entries.data(), data.size(), data.data());
		vk::PipelineShaderStageCreateInfo stageInfo({}, stage.spirv()->stage(), **stage.spirv(), stage.spirv()->entry_point().c_str(), specializationInfo.mapEntryCount ? nullptr : &specializationInfo);
		mPipeline = mDevice->createComputePipeline(mDevice.pipeline_cache(), vk::ComputePipelineCreateInfo({}, stageInfo, mLayout)).value;
		mDevice.set_debug_name(mPipeline, name);
	}
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
	uint32_t mRenderQueue;

	unordered_map<Geometry::AttributeType, unordered_map<uint32_t, uint32_t>> mVertexBufferMap; // mVertexBufferMap[attributeType][typeindex] = bindingIndex

public:
	inline GraphicsPipeline(const string& name, const stm::RenderPass& renderPass, uint32_t subpassIndex, const Geometry& geometry,
		const vk::ArrayProxy<const ShaderStage>& stages, const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers = {},
		const vk::PipelineRasterizationStateCreateInfo& rasterState = { {}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack },
		bool sampleShading = false,
		const vk::PipelineDepthStencilStateCreateInfo& depthStencilState = { {}, true, true, vk::CompareOp::eLessOrEqual, {}, {}, {}, {}, 0, 1 },
		const vector<vk::PipelineColorBlendAttachmentState>& blendStates = {},
		const vector<vk::DynamicState>& dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eLineWidth }) : Pipeline(renderPass.mDevice, name, stages, immutableSamplers) {
		
		vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1;
		for (const auto&[type,blendState,desc] : renderPass.subpasses()[subpassIndex].attachments() | views::values) {
			if (type == RenderPass::AttachmentTypeFlags::eColor || type == RenderPass::AttachmentTypeFlags::eDepthStencil)
				sampleCount = desc.samples;
			if (blendStates.empty() && type == RenderPass::AttachmentTypeFlags::eColor)
				mBlendStates.emplace_back(blendState);
		}
		if (!blendStates.empty()) mBlendStates = blendStates;

		for (auto& [id, desc] : renderPass.subpasses()[subpassIndex].attachments())
			if (get<RenderPass::AttachmentTypeFlags>(desc) & (RenderPass::AttachmentTypeFlags::eColor | RenderPass::AttachmentTypeFlags::eDepthStencil)) {
				mMultisampleState.rasterizationSamples = get<vk::AttachmentDescription>(desc).samples;
				break;
			}

		struct stride_view_hash {
			inline size_t operator()(const Buffer::StrideView& v) const {
				return hash_args(v.buffer().get(), v.offset(), v.size_bytes(), v.stride());
			}
		};

		vector<vk::VertexInputAttributeDescription> attributes;
		vector<vk::VertexInputBindingDescription> bindings;
		unordered_map<Buffer::StrideView, uint32_t, stride_view_hash> uniqueBuffers;
		for (const auto& [n, v] : stages.front().spirv()->stage_inputs()) {
			const Geometry::Attribute& attrib = geometry[v.mAttributeType][v.mTypeIndex];
			uint32_t i = (uint32_t)uniqueBuffers.size();
			if (auto it = uniqueBuffers.find(attrib.buffer()); it == uniqueBuffers.end()) {
				bindings.emplace_back(i, (uint32_t)attrib.buffer().stride(), attrib.input_rate());
				uniqueBuffers.emplace(attrib.buffer(), i);
			} else
				i = it->second;
			attributes.emplace_back(v.mLocation, i, attrib.format(), attrib.offset());
			mVertexBufferMap[v.mAttributeType][v.mTypeIndex] = i;
		}
		vk::PipelineVertexInputStateCreateInfo vertexInfo({}, bindings, attributes);

		vector<uint32_t> data;
		vector<vk::SpecializationMapEntry> entries;
		vector<vk::SpecializationInfo> specializationInfos(stages.size());
		ranges::transform(stages, specializationInfos.begin(), [&](const auto& stage) {
			size_t s = entries.size();
			stage.specialization_info(entries, data);
			vk::SpecializationInfo r;
			r.mapEntryCount = (uint32_t)(entries.size() - s);
			r.pMapEntries = reinterpret_cast<vk::SpecializationMapEntry*>(entries.size());
			return r;
		});
		
		vector<vk::PipelineShaderStageCreateInfo> vkstages(stages.size());
		uint32_t i = 0;
		ranges::transform(stages, vkstages.begin(), [&](const auto& stage) {
			specializationInfos[i].pMapEntries = entries.data() + reinterpret_cast<size_t>(specializationInfos[i].pMapEntries);
			specializationInfos[i].dataSize = data.size()*sizeof(uint32_t);
			specializationInfos[i].pData = data.data();
			vk::PipelineShaderStageCreateInfo r({}, stage.spirv()->stage(), **stage.spirv(), stage.spirv()->entry_point().c_str(), specializationInfos[i].mapEntryCount ? nullptr : &specializationInfos[i]);
			i++;
			return r;
		});
		mDepthStencilState = depthStencilState;
		mDynamicStates = dynamicStates;
		mInputAssemblyState = { {}, geometry.topology() };
		mViewportState = { {}, 1, nullptr, 1, nullptr };
		mRasterizationState = rasterState;
		mMultisampleState = { {}, sampleCount, sampleShading };
		vk::PipelineColorBlendStateCreateInfo blendState({}, false, vk::LogicOp::eCopy, mBlendStates);
		vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamicStates);
		mPipeline = mDevice->createGraphicsPipeline(mDevice.pipeline_cache(), 
			vk::GraphicsPipelineCreateInfo({}, vkstages, &vertexInfo, &mInputAssemblyState, nullptr, &mViewportState,
			&mRasterizationState, &mMultisampleState, &mDepthStencilState, &blendState, &dynamicState, mLayout, *renderPass, subpassIndex)).value;
		mDevice.set_debug_name(mPipeline, name);
	}
	
	inline vk::PipelineBindPoint bind_point() const override { return vk::PipelineBindPoint::eGraphics; }
};

}

namespace std {
template<> struct hash<stm::Pipeline::ShaderStage> {
	inline size_t operator()(const stm::Pipeline::ShaderStage& r) const {
		vector<vk::SpecializationMapEntry> entries;
		vector<uint32_t> data;
		r.specialization_info(entries, data);
		return stm::hash_args(*r.spirv(), entries, data);
	}
};
}
