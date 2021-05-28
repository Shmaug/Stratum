#include "Material.hpp"

using namespace stm;

shared_ptr<GraphicsPipeline> Material::bind(CommandBuffer& commandBuffer, const Geometry& geometry) {

	size_t key = hash_args(commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(), geometry.topology(), geometry | views::values);

	auto p_it = mPipelines.find(key);
	if (p_it == mPipelines.end())
		p_it = mPipelines.emplace(key, make_shared<GraphicsPipeline>(mName,
				commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(),
				geometry, mModules, mSpecializationConstants, mImmutableSamplers,
				mCullMode, mPolygonMode, mSampleShading, mDepthStencilState, mBlendStates) ).first;
	
	const auto& pipeline = p_it->second;
	commandBuffer.bind_pipeline(pipeline);
	
	ProfilerRegion ps("Material::bind", commandBuffer);

	auto& descriptorSets = mDescriptorSets[pipeline.get()];
	descriptorSets.clear();

	// update descriptor set entries
	for (auto& [id, entries] : mDescriptors) {
		const DescriptorSetLayout::Binding& b = pipeline->descriptor_binding(id);
		uint32_t setIndex = b.mSet;
		auto ds_it = descriptorSets.find(setIndex);
		if (ds_it == descriptorSets.end())
			ds_it = descriptorSets.emplace(setIndex, make_shared<DescriptorSet>(pipeline->descriptor_set_layouts()[setIndex], mName+"/DescriptorSet"+to_string(setIndex))).first;

		for (const auto&[arrayIndex,entry] : entries) {
			const Descriptor* d = ds_it->second->find(b.mBinding, arrayIndex);
			if (!d || *d != entry) ds_it->second->insert_or_assign(b.mBinding, arrayIndex, entry);
		}
	}

	return pipeline;
}

void Material::bind_descriptor_sets(CommandBuffer& commandBuffer, const unordered_map<string, uint32_t>& dynamicOffsets) const {
	unordered_map<uint32_t, vector<pair<uint32_t, uint32_t>>> offsets;
	for (const auto&[id,offset] : dynamicOffsets) {
		const DescriptorSetLayout::Binding& b = commandBuffer.bound_pipeline()->descriptor_binding(id);
		offsets[b.mSet].emplace_back(make_pair(b.mBinding, offset));
	}
	vector<uint32_t> tmp;
	for (const auto&[index, ds] : mDescriptorSets.at(commandBuffer.bound_pipeline().get())) {
		tmp.clear();
		if (auto it = offsets.find(index); it != offsets.end()) {
			tmp.resize(it->second.size());
			ranges::transform(it->second, tmp.begin(), &pair<uint32_t,uint32_t>::second);
		}
		commandBuffer.bind_descriptor_set(index, ds, tmp);
	}
}