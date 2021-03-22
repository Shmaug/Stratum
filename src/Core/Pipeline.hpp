#pragma once

#include "RenderPass.hpp"
#include "DescriptorSet.hpp"
#include "GeometryData.hpp"

namespace stm {

using VertexInputBindingData = pair< vector<vk::VertexInputAttributeDescription>, vector<vk::VertexInputBindingDescription> >;
inline VertexInputBindingData CreateInputBindings(const GeometryData& geometry, const SpirvModule& vs) {
	VertexInputBindingData d = {};
	for (auto& [id, attrib] : geometry.mAttributes) {
		auto it = ranges::find_if(vs.mStageInputs, [&](const auto& p) { return p.second.mAttributeId == id; });
		if (it != vs.mStageInputs.end())
			d.first.emplace_back(it->second.mLocation, attrib.mBinding, attrib.mFormat, (uint32_t)attrib.mOffset);
	}
	d.second.resize(geometry.mBindings.size());
	for (uint32_t i = 0; i < geometry.mBindings.size(); i++) {
		d.second[i].binding = i;
		d.second[i].stride = (uint32_t)get<0>(geometry.mBindings[i]).stride();
		d.second[i].inputRate = get<1>(geometry.mBindings[i]);
	}
	ranges::sort(d.first, {}, &vk::VertexInputAttributeDescription::location);
	return d;
}

class Pipeline : public DeviceResource {
protected:
	vk::Pipeline mPipeline;
	vk::PipelineLayout mLayout;
	size_t mHash = 0;

	unordered_map<vk::ShaderStageFlagBits, pair<shared_ptr<SpirvModule>, vk::Optional<vk::SpecializationInfo>>> mSpirvModules;
	vector<shared_ptr<DescriptorSetLayout>> mDescriptorSetLayouts;
	
	// multimap for entries with the same name and different bindings between stages
	unordered_multimap<string, DescriptorBinding> mDescriptorBindings;
	// multimap for entries with the same name and different ranges between stages
	unordered_multimap<string, vk::PushConstantRange> mPushConstants;
	
	unordered_set<shared_ptr<DeviceResource>> mResources;

	// expects mSpirvModules to be populated
	// populates mPushConstants, mDescriptorBindings; hashes spirv into mHash
	inline vk::PipelineLayout CreatePipelineLayout() {
		
		if (mDescriptorSetLayouts.empty()) {
			vector<unordered_map<uint32_t, DescriptorSetLayout::Binding>> setBindings;
			for (const auto&[stage, p] : mSpirvModules)
				for (const auto&[id, b] : p.first->mDescriptorBindings) {
					if (b.mSet >= setBindings.size()) setBindings.resize(b.mSet + 1);
					auto it = setBindings[b.mSet].find(b.mBinding);
					if (it != setBindings[b.mSet].end() && it->second.mDescriptorType == b.mDescriptorType && it->second.mDescriptorCount == b.mDescriptorCount)
						setBindings[b.mSet][b.mBinding].mStageFlags |= b.mStageFlags;
					else
						setBindings[b.mSet][b.mBinding] = DescriptorSetLayout::Binding(b.mDescriptorType, b.mStageFlags, b.mDescriptorCount);
				}
			for (const auto& s : setBindings)
				mDescriptorSetLayouts.emplace_back(make_shared<DescriptorSetLayout>(mDevice, mName, s));
		}

		unordered_map<uint32_t, unordered_map<uint32_t, vk::DescriptorSetLayoutBinding>> descriptorSetBindings;
		vector<vk::PushConstantRange> pushConstantRanges;

		for (const auto&[stage, p] : mSpirvModules) {
			auto& spirv = p.first;
			if (!spirv->mShaderModule) {
				spirv->mDevice = *mDevice;
				spirv->mShaderModule = mDevice->createShaderModule(vk::ShaderModuleCreateInfo({}, spirv->mSpirv));
			}
			
			for (const auto& [id, range] : spirv->mPushConstants) {
				mPushConstants.emplace(id, range);
				pushConstantRanges.emplace_back(range); // TODO: combine push constant ranges between stages
			}

			bool add = true;
			for (const auto& [id, binding] : spirv->mDescriptorBindings) {
				for (auto it : ranges::subrange(mDescriptorBindings.find(id), mDescriptorBindings.end()))
					if (it.second.mBinding == binding.mBinding && it.second.mSet == binding.mSet && it.second.mDescriptorType == binding.mDescriptorType && it.second.mDescriptorCount == binding.mDescriptorCount) {
						it.second.mStageFlags |= binding.mStageFlags;
						descriptorSetBindings[binding.mSet][binding.mBinding].stageFlags = it.second.mStageFlags;
						add = false;
					}
				if (add) {
					mDescriptorBindings.emplace(id, binding);
					descriptorSetBindings[binding.mSet].emplace(binding.mBinding, vk::DescriptorSetLayoutBinding(binding.mBinding, binding.mDescriptorType, binding.mDescriptorCount, binding.mStageFlags));
				}
			}

			mHash = hash_combine(mHash, spirv);
			if (p.second) {
				mHash = hash_combine(mHash, ranges::subrange(p.second->pMapEntries, p.second->pMapEntries + p.second->mapEntryCount));
				mHash = hash_combine(mHash, ranges::subrange((const byte*)p.second->pData, (const byte*)p.second->pData + p.second->dataSize));
			}
			for (const auto& l : mDescriptorSetLayouts)
				for (const auto&[i,b] : l->Bindings())
					for (const auto& s : b.mImmutableSamplers)
						mHash = hash_combine(mHash, s->CreateInfo()); // only have to hash the immutable samplers; hashing the spir-v and specialization constants takes care of bindings/push constants
		};
		
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

	virtual vk::PipelineBindPoint BindPoint() const = 0;
	
	inline const vk::Pipeline& operator*() const { return mPipeline; }
	inline const vk::Pipeline* operator->() const { return &mPipeline; }
	inline operator bool() const { return mPipeline; }

	inline bool operator==(const Pipeline& rhs) const { return rhs.mPipeline == mPipeline; }
	inline size_t Hash() const { return mHash; }
	inline vk::PipelineLayout Layout() const { return mLayout; }
	inline const auto& SpirvModules() const { return mSpirvModules; };
	inline const auto& DescriptorSetLayouts() const { return mDescriptorSetLayouts; };
	// most of the time, a pipeline wont have named descriptors with different bindings, so there is usually only one item in this range
	inline const auto& DescriptorBindings() { return mDescriptorBindings; };
	// most of the time, a pipeline wont have named push constants with different ranges, so there is usually only one item in this range
	inline const auto& PushConstants() { return mPushConstants; };

	inline const DescriptorBinding& binding(const string& name) const {
		auto it = mDescriptorBindings.find(name);
		if (it == mDescriptorBindings.end()) throw invalid_argument("no descriptor named " + name);
		return it->second;
	}
};

class ComputePipeline : public Pipeline {
public:
	inline ComputePipeline(Device& device, const string& name, shared_ptr<SpirvModule> spirv, vk::Optional<vk::SpecializationInfo> specializationInfo = nullptr) : Pipeline(device, name) {
		mSpirvModules.emplace(vk::ShaderStageFlagBits::eCompute, make_pair(spirv, specializationInfo));
		CreatePipelineLayout();
		mPipeline = mDevice->createComputePipeline(mDevice.PipelineCache(),
			vk::ComputePipelineCreateInfo({},
				vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, spirv->mShaderModule, spirv->mEntryPoint.c_str(), specializationInfo),
				mLayout)).value;
		mDevice.SetObjectName(mPipeline, name);
	}

	inline vk::PipelineBindPoint BindPoint() const override { return vk::PipelineBindPoint::eCompute; }
	inline vk::Extent3D WorkgroupSize() const { return mSpirvModules.at(vk::ShaderStageFlagBits::eCompute).first->mWorkgroupSize; }
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
	inline GraphicsPipeline(const string& name, const stm::RenderPass& renderPass, uint32_t subpassIndex, const GeometryData& geometry,
		shared_ptr<SpirvModule> vs, shared_ptr<SpirvModule> fs, vk::Optional<vk::SpecializationInfo> vs_spec = nullptr, vk::Optional<vk::SpecializationInfo> fs_spec = nullptr,
		vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack, vk::PolygonMode polygonMode = vk::PolygonMode::eFill,
		bool sampleShading = false, const vk::PipelineDepthStencilStateCreateInfo& depthStencilState = { {}, true, true, vk::CompareOp::eLessOrEqual, {}, {}, {}, {}, 0, 1 },
		const vector<vk::PipelineColorBlendAttachmentState>& blendStates = {},
		const vector<vk::DynamicState>& dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eLineWidth }) : Pipeline(renderPass.mDevice, name) {
		
		mSpirvModules.emplace(vk::ShaderStageFlagBits::eVertex, make_pair(vs, vs_spec));
		mSpirvModules.emplace(vk::ShaderStageFlagBits::eFragment, make_pair(fs, fs_spec));

		CreatePipelineLayout();

		vector<shared_ptr<SpirvModule>> spirv { vs, fs };

		vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1;
		for (const auto&[desc,type,blendState] : renderPass.SubpassDescriptions()[subpassIndex].mAttachmentDescriptions | views::values) {
			if (type == RenderPass::AttachmentType::eColor || type == RenderPass::AttachmentType::eDepthStencil)
				sampleCount = desc.samples;
			if (blendStates.empty() && type == RenderPass::AttachmentType::eColor)
				mBlendStates.emplace_back(blendState);
		}
		if (!blendStates.empty()) mBlendStates = blendStates;

		mHash = hash_combine(renderPass, subpassIndex, geometry.mPrimitiveTopology, cullMode, polygonMode, sampleShading);
		for (const auto state : mBlendStates) mHash = hash_combine(mHash, state);
		for (const auto state : dynamicStates) mHash = hash_combine(mHash, state);

		for (auto& [id, desc] : renderPass.SubpassDescriptions()[subpassIndex].mAttachmentDescriptions)
			if (get<RenderPass::AttachmentType>(desc) & (RenderPass::AttachmentType::eColor | RenderPass::AttachmentType::eDepthStencil)) {
				mMultisampleState.rasterizationSamples = get<vk::AttachmentDescription>(desc).samples;
				break;
			}

		auto vertexInput = CreateInputBindings(geometry, *vs);
		if (!vertexInput.first.empty()) mHash = hash_combine(mHash, vertexInput);
		vk::PipelineVertexInputStateCreateInfo vertexInfo({}, vertexInput.second, vertexInput.first);

		mDepthStencilState = depthStencilState;
		mDynamicStates = dynamicStates;
		mInputAssemblyState = { {}, geometry.mPrimitiveTopology };
		mViewportState = { {}, 1, nullptr, 1, nullptr };
		mRasterizationState = { {}, false, false, polygonMode, cullMode };
		mMultisampleState = { {}, sampleCount, sampleShading };
		vk::PipelineColorBlendStateCreateInfo blendState({}, false, vk::LogicOp::eCopy, mBlendStates);
		vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamicStates);
		vector<vk::PipelineShaderStageCreateInfo> stages(mSpirvModules.size());
		ranges::transform(mSpirvModules | views::values, stages.begin(), [&](const auto& p) {
			return vk::PipelineShaderStageCreateInfo({}, p.first->mStage, p.first->mShaderModule, p.first->mEntryPoint.c_str(), p.second);
		});
		mPipeline = mDevice->createGraphicsPipeline(mDevice.PipelineCache(), 
			vk::GraphicsPipelineCreateInfo({}, stages, &vertexInfo, &mInputAssemblyState, nullptr, &mViewportState,
			&mRasterizationState, &mMultisampleState, &mDepthStencilState, &blendState, &dynamicState, mLayout, *renderPass, subpassIndex)).value;
		mDevice.SetObjectName(mPipeline, mName);
	}
	
	inline vk::PipelineBindPoint BindPoint() const override { return vk::PipelineBindPoint::eGraphics; }
};

}

namespace std {
template<class T> requires(is_base_of_v<stm::Pipeline,T>)
struct hash<T> {
	inline size_t operator()(const T& pipeline) { return pipeline.Hash(); }
};
}