#include "Material.hpp"
#include "Profiler.hpp"

using namespace stm;

shared_ptr<GraphicsPipeline> Material::CreatePipeline(RenderPass& renderPass, uint32_t subpassIndex, const GeometryData& geometry) {
	shared_ptr<SpirvModule> vert, frag;
	for (uint32_t i = 0; i < mModules.size(); i++) {
		auto& spirv = mModules[i];
		if (!spirv->mShaderModule) {
			spirv->mDevice = *renderPass.mDevice;
			spirv->mShaderModule = renderPass.mDevice->createShaderModule(vk::ShaderModuleCreateInfo({}, spirv->mSpirv));
		}
		if (mModules[i]->mStage == vk::ShaderStageFlagBits::eFragment) frag = mModules[i];
		else if (mModules[i]->mStage == vk::ShaderStageFlagBits::eVertex) vert = mModules[i];
	}
	return make_shared<GraphicsPipeline>(mName, renderPass, subpassIndex, geometry, vert, frag, mSpecializationConstants, mImmutableSamplers, mCullMode, mPolygonMode, mSampleShading, mDepthStencilState, mBlendStates);
}

shared_ptr<GraphicsPipeline> Material::Bind(CommandBuffer& commandBuffer, const GeometryData& geometry) {
	ProfilerRegion ps("Material::Bind");

	size_t key = hash_combine(commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(), geometry.mPrimitiveTopology, geometry.mAttributes | views::values);

	shared_ptr<GraphicsPipeline> pipeline;
	{
		auto pipelines = mPipelines.lock();
		auto p_it = pipelines->find(key);
		if (p_it == pipelines->end()) pipeline = pipelines->emplace(key, CreatePipeline(commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(), geometry)).first->second;
		else pipeline = p_it->second;
	}

	commandBuffer.BindPipeline(pipeline);

	unordered_map<uint32_t, shared_ptr<DescriptorSet>> descriptorSets;

	for (auto& [name, entries] : mDescriptors) {
		auto it = pipeline->DescriptorBindings().find(name);
		if (it == pipeline->DescriptorBindings().end()) continue;

		uint32_t setIndex = it->second.mSet;
		if (descriptorSets.count(setIndex) == 0)
			descriptorSets.emplace(setIndex, make_shared<DescriptorSet>(pipeline->DescriptorSetLayouts()[setIndex], mName+"/DescriptorSet"+to_string(setIndex)));
		for (const auto&[arrayIndex,entry] : entries)
			if (descriptorSets.at(setIndex)->find(it->second.mBinding, arrayIndex) != entry)
				descriptorSets.at(setIndex)->insert_or_assign(it->second.mBinding, arrayIndex, entry);
	}
	
	for (const auto&[index, descriptorSet] : descriptorSets) commandBuffer.BindDescriptorSet(index, descriptorSet);
	for (const auto&[name, data] : mPushConstants) commandBuffer.push_constant(name, data);

	return pipeline;
}