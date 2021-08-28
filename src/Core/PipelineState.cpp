#include "PipelineState.hpp"

using namespace stm;

uint32_t PipelineState::descriptor_count(const string& name) const {
	for (const auto&[stage, spirv] : mModules)
		if (auto it = spirv->descriptors().find(name); it != spirv->descriptors().end()) {
			uint32_t count = 1;
			for (const auto& v : it->second.mArraySize)
				if (v.index() == 0)
					// array size is a literal
					count *= get<uint32_t>(v);
				else
					// array size is a specialization constant
					count *= specialization_constant(get<string>(v));
			return count;
		}
	return 0;
}

stm::Descriptor& PipelineState::descriptor(const string& name, uint32_t arrayIndex) {		
	auto desc_it = mDescriptors.find(name);
	if (desc_it != mDescriptors.end()) {
		auto it = desc_it->second.find(arrayIndex);
		if (it != desc_it->second.end())
			return it->second;
	}

	for (const auto&[stage, spirv] : mModules)
		if (spirv->descriptors().find(name) != spirv->descriptors().end()) {
			auto desc_it = mDescriptors.emplace(name, unordered_map<uint32_t, stm::Descriptor>()).first;
			auto it = desc_it->second.emplace(arrayIndex, stm::Descriptor()).first;
			return it->second;
		}
	
	throw invalid_argument("Descriptor " + name + " does not exist");
}

void PipelineState::transition_images(CommandBuffer& commandBuffer) const {
	for (auto& [name, p] : mDescriptors)
		for (auto& [arrayIndex, d] : p)
			if (d.index() == 0)
				if (Texture::View view = get<Texture::View>(d))
					view.texture()->transition_barrier(commandBuffer, get<vk::ImageLayout>(d));
}

const shared_ptr<GraphicsPipeline>& PipelineState::get_pipeline(const RenderPass& renderPass, uint32_t subpassIndex, const GeometryStateDescription& geometryDescription, vk::ShaderStageFlags stageMask) {
	vector<Pipeline::ShaderStage> stages;
	stages.reserve(mModules.size());
	for (const auto& [stage, spirv] : mModules)
		if (stage & stageMask)
			stages.emplace_back(spirv, mSpecializationConstants);

	ProfilerRegion ps("PipelineState::get_pipeline");
	size_t key = hash_args(renderPass, subpassIndex, geometryDescription, stageMask, mSpecializationConstants|views::values, mImmutableSamplers|views::values, mRasterState, mSampleShading, mDepthStencilState, mBlendStates);
	
	auto p_it = mPipelines.find(key);
	if (p_it == mPipelines.end())
		p_it = mPipelines.emplace(key, make_shared<GraphicsPipeline>(mName, renderPass, subpassIndex, geometryDescription, stages, mImmutableSamplers, mRasterState, mSampleShading, mDepthStencilState, mBlendStates)).first;
	return p_it->second;
}

void PipelineState::bind_descriptor_sets(CommandBuffer& commandBuffer, const unordered_map<string, uint32_t>& dynamicOffsets) {
	ProfilerRegion ps("PipelineState::bind_descriptor_sets");
	
	const Pipeline& pipeline = *commandBuffer.bound_pipeline();

	vector<shared_ptr<DescriptorSet>> descriptorSets(pipeline.descriptor_set_layouts().size());
	for (uint32_t i = 0; i < descriptorSets.size(); i++) {
		// fetch the bindings for the current set index
		unordered_map<string, const SpirvModule::DescriptorBinding*> bindings;
			for (auto& [id, descriptors] : mDescriptors)
				for (const auto& stage : pipeline.stages())
					if (auto it = stage.spirv()->descriptors().find(id); it != stage.spirv()->descriptors().end() && it->second.mSet == i)
						bindings[id] = &it->second;
		
		// find DescriptorSets matching current layout
		const shared_ptr<DescriptorSetLayout>& layout = pipeline.descriptor_set_layouts()[i];
		auto[first, last] = mDescriptorSets.equal_range(layout.get());
		for (auto[layout, descriptorSet] : ranges::subrange(first, last)) {
			// find outdated/nonexistant descriptors
			bool found = true;
			for (const auto& [id, descriptors] : mDescriptors) {
				for (const auto&[arrayIndex, descriptor] : descriptors)
					if (const Descriptor* d = descriptorSet->find(bindings.at(id)->mBinding, arrayIndex);
							d && *d == descriptor)
						continue;
					else if (!descriptorSet->in_use()) {
						// write the descriptor if possible
						descriptorSet->insert_or_assign(bindings.at(id)->mBinding, arrayIndex, descriptor);
					} else {
						found = false;
						break;
					}
				if (!found) break;
			}
			if (found) {
				descriptorSets[i] = descriptorSet;
				break;
			}
		}
		// create new DescriptorSet if necessary
		if (!descriptorSets[i]) {
			descriptorSets[i] = make_shared<DescriptorSet>(layout, mName+"/DescriptorSet"+to_string(i));
			for (auto& [id, descriptors] : mDescriptors)
				if (auto it = bindings.find(id); it != bindings.end())
					for (const auto&[arrayIndex, descriptor] : descriptors)
						descriptorSets[i]->insert_or_assign(it->second->mBinding, arrayIndex, descriptor);
		}
	}
	unordered_map<uint32_t, vector<pair<uint32_t, uint32_t>>> offsetMap;
	for (const auto&[id,offset] : dynamicOffsets)
		for (const auto& stage : commandBuffer.bound_pipeline()->stages())
			if (auto it = stage.spirv()->descriptors().find(id); it != stage.spirv()->descriptors().end())
				offsetMap[it->second.mSet].emplace_back(make_pair(it->second.mBinding, offset));
	vector<uint32_t> offsets;
	for (uint32_t i = 0; i < descriptorSets.size(); i++) {
		offsets.clear();
		if (auto it = offsetMap.find(i); it != offsetMap.end()) {
			offsets.resize(it->second.size());
			ranges::transform(it->second, offsets.begin(), &pair<uint32_t,uint32_t>::second);
		}
		commandBuffer.bind_descriptor_set(i, descriptorSets[i], offsets);
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