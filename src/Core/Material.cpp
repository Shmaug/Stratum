#include "Material.hpp"
#include "../Scene/Camera.hpp"
#include "../Scene/Scene.hpp"

using namespace stm;

void Material::SetUniformBuffer(const string& name, const Buffer::ArrayView<>& buffer, uint32_t arrayIndex) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = vec[arrayIndex];
	p.mType = vk::DescriptorType::eUniformBuffer;
	p.mArrayIndex = arrayIndex;
	p.mBufferView = buffer;

	vec[arrayIndex] = p;
	mCacheValid = false;
}
void Material::SetStorageBuffer(const string& name, const Buffer::ArrayView<>& buffer, uint32_t arrayIndex) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = {};
	p.mType = vk::DescriptorType::eStorageBuffer;
	p.mArrayIndex = arrayIndex;
	p.mBufferView = buffer;

	vec[arrayIndex] = p;
	mCacheValid = false;
}
void Material::SetSampledTexture(const string& name, const TextureView& texture, uint32_t arrayIndex, vk::ImageLayout layout) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = {};
	p.mType = vk::DescriptorType::eSampledImage;
	p.mArrayIndex = arrayIndex;
	p.mTextureView = texture;
	p.mImageLayout = layout;
	p.mSampler = nullptr;

	vec[arrayIndex] = p;
	mCacheValid = false;
}
void Material::SetStorageTexture(const string& name, const TextureView& texture, uint32_t arrayIndex, vk::ImageLayout layout) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = {};
	p.mType = vk::DescriptorType::eStorageImage;
	p.mArrayIndex = arrayIndex;
	p.mTextureView = texture;
	p.mImageLayout = layout;
	p.mSampler = nullptr;
	
	vec[arrayIndex] = p;
	mCacheValid = false;
}
void Material::SetSampler(const string& name, shared_ptr<Sampler> sampler, uint32_t arrayIndex) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = {};
	p.mType = vk::DescriptorType::eSampler;
	p.mArrayIndex = arrayIndex;
	p.mSampler = sampler;
	p.mImageLayout = vk::ImageLayout::eUndefined;
	p.mTextureView = {};

	vec[arrayIndex] = p;
	mCacheValid = false;
}

shared_ptr<GraphicsPipeline> Material::Bind(CommandBuffer& commandBuffer, optional<GeometryData> g) {
	ProfilerRegion ps("Material::Bind");

	const auto& attachmentDescriptions = commandBuffer.CurrentRenderPass()->SubpassDescriptions()[commandBuffer.CurrentSubpassIndex()].mAttachmentDescriptions;
	vector<vk::PipelineColorBlendAttachmentState> blendStates(commandBuffer.CurrentRenderPass()->AttachmentDescriptions().size());
	for (auto&[id, it] : attachmentDescriptions)
		blendStates[ commandBuffer.CurrentRenderPass()->AttachmentMap().at(id) ] = get<vk::PipelineColorBlendAttachmentState>(it);

	vector<byte_blob> specializationData(mModules.size());
	vector<vector<vk::SpecializationMapEntry>> specializationEntries(mModules.size());
	vector<vk::SpecializationInfo> specializationInfos(mModules.size());
	vector<vk::SpecializationInfo*> specializationInfoPtrs(mModules.size());
	uint32_t i = 0;
	for (auto&[entryp, spirv] : mModules) {
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


	vector<vk::VertexInputAttributeDescription> attributes;
	vector<vk::VertexInputBindingDescription> bindings(g->mBindings.size());
	vk::PipelineVertexInputStateCreateInfo vertexInfo = {};
	if (g) {
		for (auto& [id, attrib] : g->mAttributes)
			if (mModules.at("vsmain").mStageInputs.count(id))
				attributes.push_back(attrib);
		for (uint32_t i = 0; i < bindings.size(); i++) {
			bindings[i].binding = i;
			bindings[i].stride = (uint32_t)g->mBindings[i].mBuffer.stride();
			bindings[i].inputRate = g->mBindings[i].mInputRate;
		}
		vertexInfo.setVertexAttributeDescriptions(attributes);
		vertexInfo.setVertexBindingDescriptions(bindings);
	}

	// TODO: cache pipeline

	shared_ptr<GraphicsPipeline> pipeline = make_shared<GraphicsPipeline>(mName, **commandBuffer.CurrentRenderPass(), commandBuffer.CurrentSubpassIndex(),
		mModules, specializationInfos,
		vertexInfo, g->mPrimitiveTopology, mCullMode, mPolygonMode,
		mSampleShading, vk::PipelineDepthStencilStateCreateInfo({}, mDepthTest, mDepthWrite, vk::CompareOp::eLessOrEqual, {}, {}, {}, {}, 0, 1),
		blendStates);


	commandBuffer.BindPipeline(pipeline);
	

	if (!mCacheValid) {
		// Create a descriptorset with the currently bound inputs
		for (auto& [name, entries] : mDescriptorParameters) {
			auto it = pipeline->DescriptorBindings().find(name);
			if (it == pipeline->DescriptorBindings().end()) continue;

			uint32_t setIndex = it->second.mSet;

			if (mDescriptorSetCache.count(setIndex) && mDescriptorSetCache.at(setIndex)->Layout() != pipeline->DescriptorSetLayouts()[setIndex]) {
				commandBuffer.TrackResource(mDescriptorSetCache.at(setIndex));
				mDescriptorSetCache.erase(setIndex);
			}
			if (mDescriptorSetCache.count(setIndex) == 0) 
				mDescriptorSetCache.emplace(setIndex, commandBuffer.mDevice.GetPooledDescriptorSet(mName+"/DescriptorSet"+to_string(setIndex), pipeline->DescriptorSetLayouts()[setIndex]));

			for (uint32_t i = 0; i < entries.size(); i++) {
				if (!entries[i]) continue;
				mDescriptorSetCache.at(setIndex)->CreateDescriptor(it->second.mBinding.binding, entries[i]);
			}
		}
		mCacheValid = true;
	}

	for (auto& [index, descriptorSet] : mDescriptorSetCache) commandBuffer.BindDescriptorSet(descriptorSet, index);
	for (auto& [name, data] : mPushParameters) commandBuffer.PushConstant(name, data);

	return pipeline;
}


shared_ptr<GraphicsPipeline> MaterialDerivative::Bind(CommandBuffer& commandBuffer, optional<GeometryData> g) {
	ProfilerRegion ps("MaterialDerivative::Bind");

	shared_ptr<GraphicsPipeline> pipeline; // TODO: create ot fetch pipeline for mesh

	Material::Bind(commandBuffer, g);
	return pipeline;
}