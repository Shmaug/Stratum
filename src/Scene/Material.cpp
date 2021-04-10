#include "Material.hpp"

using namespace stm;

shared_ptr<GraphicsPipeline> Material::Bind(CommandBuffer& commandBuffer, optional<GeometryData> g) {
	ProfilerRegion ps("Material::Bind");

	// TODO: cache pipeline

	vector<byte_blob> specializationData(mModules.size());
	vector<vector<vk::SpecializationMapEntry>> specializationEntries(mModules.size());
	vector<vk::SpecializationInfo> specializationInfos(mModules.size());
	vector<vk::SpecializationInfo*> specializationInfoPtrs(mModules.size());
	uint32_t i = 0;
	for (auto& spirv : mModules) {
		if (!spirv.mShaderModule) {
			spirv.mDevice = *commandBuffer.mDevice;
			spirv.mShaderModule = commandBuffer.mDevice->createShaderModule(vk::ShaderModuleCreateInfo({}, spirv.mSpirv));
		}
		auto& mapData = specializationData[i];
		auto& mapEntries = specializationEntries[i];
		for (auto&[id, data] : mSpecializationConstants) {
			if (spirv.mSpecializationMap.count(id) == 0) continue;
			vk::SpecializationMapEntry& entry = mapEntries.emplace_back(spirv.mSpecializationMap.at(id));
			entry.offset = (uint32_t)mapData.size();
			mapData.resize(entry.offset + data.size());
			memcpy(mapData.data() + entry.offset, data.data(), data.size());
		}
		vk::SpecializationInfo& s = specializationInfos[i];
		specializationInfoPtrs[i] = &s;
		s.pData = mapData.data();
		s.dataSize = mapData.size();
		s.setMapEntries(mapEntries);
		i++;
	}

	vk::PipelineVertexInputStateCreateInfo vertexInfo = {};
	if (g) {
		auto[attributes, bindings] = CreateInputBindings(*g, mModules.at(vk::ShaderStageFlagBits::eVertex));
		vertexInfo.setVertexAttributeDescriptions(attributes);
		vertexInfo.setVertexBindingDescriptions(bindings);
	}

	shared_ptr<GraphicsPipeline> pipeline = make_shared<GraphicsPipeline>(**commandBuffer.CurrentRenderPass(), mName, commandBuffer.CurrentSubpassIndex(),
		mModules, specializationInfos,
		vertexInfo, g->mPrimitiveTopology, mCullMode, mPolygonMode,
		mSampleShading, vk::PipelineDepthStencilStateCreateInfo({}, mDepthTest, mDepthWrite, vk::CompareOp::eLessOrEqual, {}, {}, {}, {}, 0, 1));

	commandBuffer.BindPipeline(pipeline);

	for (auto& [name, entries] : mDescriptors) {
		auto it = pipeline->DescriptorBindings().find(name);
		if (it == pipeline->DescriptorBindings().end()) continue;

		uint32_t setIndex = it->second.mSet;

		if (mDescriptorSets.count(setIndex) == 0) 
			mDescriptorSets.emplace(setIndex, make_shared<DescriptorSet>(commandBuffer.mDevice, mName+"/DescriptorSet"+to_string(setIndex), pipeline->DescriptorSetLayouts()[setIndex]));

		for (const auto&[arrayIndex,entry] : entries)
			if (mDescriptorSets.at(setIndex)->at(it->second.mBinding, arrayIndex) != entry)
				mDescriptorSets.at(setIndex)->insert(it->second.mBinding, arrayIndex, entry);
	}
	
	for (const auto&[index, descriptorSet] : mDescriptorSets) commandBuffer.BindDescriptorSet(index, descriptorSet);
	for (const auto&[name, data] : mPushConstants) commandBuffer.PushConstant(name, data);

	return pipeline;
}

shared_ptr<GraphicsPipeline> MaterialDerivative::Bind(CommandBuffer& commandBuffer, optional<GeometryData> g) {
	ProfilerRegion ps("MaterialDerivative::Bind");

	shared_ptr<GraphicsPipeline> pipeline; // TODO: create ot fetch pipeline for mesh

	Material::Bind(commandBuffer, g);
	return pipeline;
}