#include "PipelineState.hpp"

using namespace stm;

void PipelineState::transition_images(CommandBuffer& commandBuffer) const {
	for (auto& [name, p] : mDescriptors)
		for (auto& [arrayIndex, d] : p)
			if (d.index() == 0)
				if (Texture::View view = get<Texture::View>(d))
					view.texture()->transition_barrier(commandBuffer, get<vk::ImageLayout>(d));
}

shared_ptr<GraphicsPipeline> PipelineState::bind_pipeline(CommandBuffer& commandBuffer, const Geometry& geometry, vk::ShaderStageFlags stageMask) {
	ProfilerRegion ps("PipelineState::bind");

	vector<Pipeline::ShaderStage> stages;
	stages.reserve(mModules.size());
	for (const shared_ptr<SpirvModule>& spirv : mModules)
		if (spirv->stage() & stageMask)
			stages.emplace_back(spirv, mSpecializationConstants);

	size_t key;
	{
		ProfilerRegion ps("hash_args");
		key = hash_args(
			commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(), geometry,
			stages, mImmutableSamplers|views::transform([](const auto& s) -> const vk::SamplerCreateInfo& { return s.second->create_info(); }),
			mRasterState, mSampleShading, mDepthStencilState, mBlendStates);
	}
	auto p_it = mPipelines.find(key);
	if (p_it == mPipelines.end())
		p_it = mPipelines.emplace(key, make_shared<GraphicsPipeline>(mName,
				commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(),
				geometry, stages, mImmutableSamplers, mRasterState, mSampleShading, mDepthStencilState, mBlendStates) ).first;
	
	const auto& pipeline = p_it->second;
	commandBuffer.bind_pipeline(pipeline);
	
	{
		ProfilerRegion ps("Update mDescriptorSets");
		
		auto& descriptorSets = mDescriptorSets[pipeline.get()];
		descriptorSets.clear();

		for (auto& [id, entries] : mDescriptors) {
			for (const auto& stage : pipeline->stages())
				if (auto it = stage.spirv()->descriptors().find(id); it != stage.spirv()->descriptors().end()) {
					auto ds_it = descriptorSets.find(it->second.mSet);
					if (ds_it == descriptorSets.end())
						ds_it = descriptorSets.emplace(it->second.mSet,
							make_shared<DescriptorSet>(pipeline->descriptor_set_layouts()[it->second.mSet], mName+"/DescriptorSet"+to_string(it->second.mSet))).first;
					for (const auto&[arrayIndex,entry] : entries) {
						const Descriptor* d = ds_it->second->find(it->second.mBinding, arrayIndex);
						if (!d || *d != entry) ds_it->second->insert_or_assign(it->second.mBinding, arrayIndex, entry);
					}
				}
		}
	}
	
	return pipeline;
}

void PipelineState::bind_descriptor_sets(CommandBuffer& commandBuffer, const unordered_map<string, uint32_t>& dynamicOffsets) const {
	ProfilerRegion ps("PipelineState::bind_descriptor_sets");
	
	unordered_map<uint32_t, vector<pair<uint32_t, uint32_t>>> offsetMap;
	for (const auto&[id,offset] : dynamicOffsets)
		for (const auto& stage : commandBuffer.bound_pipeline()->stages())
			if (auto it = stage.spirv()->descriptors().find(id); it != stage.spirv()->descriptors().end())
				offsetMap[it->second.mSet].emplace_back(make_pair(it->second.mBinding, offset));

	vector<uint32_t> offsetVec;
	for (const auto&[index, ds] : mDescriptorSets.at(commandBuffer.bound_pipeline().get())) {
		offsetVec.clear();
		if (auto it = offsetMap.find(index); it != offsetMap.end()) {
			offsetVec.resize(it->second.size());
			ranges::transform(it->second, offsetVec.begin(), &pair<uint32_t,uint32_t>::second);
		}

		commandBuffer.bind_descriptor_set(index, ds, offsetVec);
	}
}

void PipelineState::push_constants(CommandBuffer& commandBuffer) const {
	ProfilerRegion ps("PipelineState::push_constants");
	for (const auto&[name, range] : commandBuffer.bound_pipeline()->push_constants())
		if (auto it = mPushConstants.find(name); it != mPushConstants.end()) {
			const auto& value = it->second;
			if (range.size != value.size()) throw invalid_argument("argument size (" + to_string(value.size()) + ") must match push constant size (" + to_string(range.size) +")");
			commandBuffer->pushConstants(commandBuffer.bound_pipeline()->layout(), range.stageFlags, range.offset, (uint32_t)value.size(), value.data());
		}
}