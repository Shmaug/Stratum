#pragma once

#include "RenderPass.hpp"
#include "SpirvModule.hpp"

namespace stm {

class Pipeline : public DeviceResource {
protected:
	// TODO: something more elegant than friend class...
	friend class ShaderCompiler;

	vk::Pipeline mPipeline;
	vk::PipelineLayout mLayout;
	vector<vk::DescriptorSetLayout> mDescriptorSetLayouts;

	unordered_map<vk::ShaderStageFlagBits, shared_ptr<SpirvModule>> mSpirvModules;
	vector<vk::PushConstantRange> mPushConstantRanges;
	
	// multimap handles descriptors with the same name and different bindings between stages
	unordered_multimap<string, DescriptorBinding> mDescriptorBindings;
	// multimap handles push constants with the same name and different ranges between stages
	unordered_multimap<string, vk::PushConstantRange> mPushConstants;

	unordered_set<shared_ptr<DeviceResource>> mResources;

	size_t mHash = 0;

	inline void ProcessSpirvModules(list<vector<vk::Sampler>>& immutableSamplers, const vector<shared_ptr<SpirvModule>>& modules) {
		unordered_map<uint32_t, unordered_map<uint32_t, vk::DescriptorSetLayoutBinding>> descriptorSetBindings;
		
		for (const auto& spirv : modules) {
			if (!spirv->mShaderModule) {
				spirv->mDevice = *mDevice;
				spirv->mShaderModule = mDevice->createShaderModule(vk::ShaderModuleCreateInfo({}, spirv->mSpirv));
			}
			mSpirvModules.emplace(spirv->mStage, spirv);

			for (const auto& [id, range] : spirv->mPushConstants)
				mPushConstantRanges.emplace_back(range);

			for (const auto& [id, binding] : spirv->mDescriptorBindings) {
				mDescriptorBindings.emplace(id, binding);
				auto[it,inserted] = descriptorSetBindings[binding.mSet].emplace(binding.mBinding, vk::DescriptorSetLayoutBinding(binding.mBinding, binding.mDescriptorType, binding.mDescriptorCount, binding.mStageFlags));
				if (inserted) {
					if (binding.mImmutableSamplers.size()) {
						vector<vk::Sampler>& samplers = immutableSamplers.emplace_front();
						for (const auto& sinfo : binding.mImmutableSamplers) {
							auto sampler = make_shared<Sampler>(mDevice, "Immutable/"+id, sinfo);
							samplers.emplace_back(**sampler);
							mResources.emplace(sampler);
						}
						it->second.pImmutableSamplers = samplers.data();
					}
				} else
					it->second.stageFlags |= binding.mStageFlags;
			}

			mHash = hash_combine(mHash, spirv);
		}
		
		mDescriptorSetLayouts.resize(ranges::max_element(descriptorSetBindings, {}, &decltype(descriptorSetBindings)::value_type::first)->first + 1);
		for (const auto& [setIndex, bindings] : descriptorSetBindings) {
			vector<vk::DescriptorSetLayoutBinding> b(bindings.size());
			ranges::copy(bindings | views::values, b.begin());
			mDescriptorSetLayouts[setIndex] = mDevice->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, b));
		}
		mLayout = mDevice->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, mDescriptorSetLayouts, mPushConstantRanges));
	}
	
	inline Pipeline(Device& device, const string& name) : DeviceResource(device, name) {}

public:
	inline ~Pipeline() {
		if (mLayout) mDevice->destroyPipelineLayout(mLayout);
		if (mPipeline) mDevice->destroyPipeline(mPipeline);
		ranges::for_each(mDescriptorSetLayouts, [&](auto l){ mDevice->destroyDescriptorSetLayout(l); });
	}

	virtual vk::PipelineBindPoint BindPoint() const = 0;
	
	inline vk::Pipeline operator*() const { return mPipeline; }
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
	inline const auto& PushConstantRanges() const { return mPushConstantRanges; };

	inline uint32_t location(const string& name) const {
		auto it = mDescriptorBindings.find(name);
		if (it == mDescriptorBindings.end()) throw invalid_argument("no descriptor named " + name);
		return it->second.mBinding;
	}
};

class ComputePipeline : public Pipeline {
public:
	inline ComputePipeline(Device& device, const string& name, const shared_ptr<SpirvModule>& spirv, const vk::SpecializationInfo* specializationInfo = nullptr) : Pipeline(device, spirv->mEntryPoint) {
		mHash = 0;
		if (specializationInfo)
			mHash = hash_combine(mHash, span(reinterpret_cast<const byte*>(specializationInfo->pData), specializationInfo->dataSize));
		list<vector<vk::Sampler>> immutableSamplers;
		ProcessSpirvModules(immutableSamplers, {spirv});

		vk::ComputePipelineCreateInfo info;
		info.stage = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, spirv->mShaderModule, spirv->mEntryPoint.c_str(), specializationInfo);
		info.layout = mLayout;
		mPipeline = mDevice->createComputePipeline(mDevice.PipelineCache(), info).value;
		mDevice.SetObjectName(mPipeline, name);
	}

	inline vk::PipelineBindPoint BindPoint() const override { return vk::PipelineBindPoint::eCompute; }
	inline vk::Extent3D WorkgroupSize() const { return mSpirvModules.begin()->second->mWorkgroupSize; }
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
	inline GraphicsPipeline(const stm::RenderPass& renderPass, const string& name, uint32_t subpassIndex,
		const vector<shared_ptr<SpirvModule>>& modules, const unordered_map<string/*entry point*/, vk::SpecializationInfo>& specializationInfos = {},
		const vk::PipelineVertexInputStateCreateInfo& vertexInput = {}, vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList, vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack, vk::PolygonMode polygonMode = vk::PolygonMode::eFill,
		bool sampleShading = false, const vk::PipelineDepthStencilStateCreateInfo& depthStencilState = { {}, true, true, vk::CompareOp::eLessOrEqual, {}, {}, {}, {}, 0, 1 },
		const vector<vk::PipelineColorBlendAttachmentState>& blendStates = {},
		const vector<vk::DynamicState>& dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eLineWidth }) : Pipeline(renderPass.mDevice, modules.front()->mEntryPoint) {
		
		mHash = hash_combine(renderPass, subpassIndex, topology, vertexInput, cullMode, polygonMode, sampleShading);
		for (const auto&[n,s] : specializationInfos)
			mHash = hash_combine(mHash, span(reinterpret_cast<const byte*>(s.pData), s.dataSize));
		for (const auto state : mBlendStates) mHash = hash_combine(mHash, state);
		for (const auto state : dynamicStates) mHash = hash_combine(mHash, state);

		list<vector<vk::Sampler>> immutableSamplers;
		ProcessSpirvModules(immutableSamplers, modules);

		if (blendStates.empty()) {
			for (const auto&[desc,type,blendState] : renderPass.SubpassDescriptions()[subpassIndex].mAttachmentDescriptions | views::values)
				if (type == RenderPass::AttachmentType::eColor)
					mBlendStates.emplace_back(blendState);
		} else
			mBlendStates = blendStates;
		mDepthStencilState = depthStencilState;
		mDynamicStates = dynamicStates;
		mInputAssemblyState = { {}, topology };
		mViewportState = { {}, 1, nullptr, 1, nullptr };
		mRasterizationState = { {}, false, false, polygonMode, cullMode };
		mMultisampleState = { {}, vk::SampleCountFlagBits::e1, sampleShading };
		vk::PipelineColorBlendStateCreateInfo blendState = { {}, false, vk::LogicOp::eCopy, mBlendStates };
		vk::PipelineDynamicStateCreateInfo dynamicState = { {}, dynamicStates };
		for (auto& [id, desc] : renderPass.SubpassDescriptions()[subpassIndex].mAttachmentDescriptions)
			if (get<RenderPass::AttachmentType>(desc) & (RenderPass::AttachmentType::eColor | RenderPass::AttachmentType::eDepthStencil)) {
				mMultisampleState.rasterizationSamples = get<vk::AttachmentDescription>(desc).samples;
				break;
			}
		vector<vk::PipelineShaderStageCreateInfo> stages(mSpirvModules.size());
		ranges::transform(mSpirvModules | views::values, stages.begin(), [&](const auto& spirv) {
			return vk::PipelineShaderStageCreateInfo({}, spirv->mStage, spirv->mShaderModule, spirv->mEntryPoint.c_str(), specializationInfos.count(spirv->mEntryPoint) ? &specializationInfos.at(spirv->mEntryPoint) : nullptr );
		});
		mPipeline = mDevice->createGraphicsPipeline(mDevice.PipelineCache(), 
			vk::GraphicsPipelineCreateInfo({}, stages, &vertexInput, &mInputAssemblyState, nullptr, &mViewportState,
			&mRasterizationState, &mMultisampleState, &mDepthStencilState, &blendState, &dynamicState, mLayout, *renderPass, subpassIndex)).value;
		mDevice.SetObjectName(mPipeline, name);
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