#include "Material.hpp"
#include "../Scene/Camera.hpp"
#include "../Scene/Scene.hpp"
#include "Asset/Texture.hpp"


using namespace stm;

void Material::SetSpecialization(const string& name, const byte_blob& v) {
	if (mSpecializationConstants.count(name) == 0) return;
	mSpecializationConstants.at(name) = v;
	mCacheValid = false;
}


void Material::SetUniformBuffer(const string& name, shared_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t arrayIndex) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = vec[arrayIndex];
	p.mType = vk::DescriptorType::eUniformBuffer;
	p.mArrayIndex = arrayIndex;
	p.mBufferView.mBuffer = buffer;
	p.mBufferView.mOffset = offset;
	p.mBufferView.mRange = range;

	vec[arrayIndex] = p;
	mCacheValid = false;
}
void Material::SetStorageBuffer(const string& name, shared_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t arrayIndex) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = {};
	p.mType = vk::DescriptorType::eStorageBuffer;
	p.mArrayIndex = arrayIndex;
	p.mBufferView.mBuffer = buffer;
	p.mBufferView.mOffset = offset;
	p.mBufferView.mRange = range;

	vec[arrayIndex] = p;
	mCacheValid = false;
}
void Material::SetSampledTexture(const string& name, shared_ptr<Texture> texture, uint32_t arrayIndex, vk::ImageLayout layout) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = {};
	p.mType = vk::DescriptorType::eSampledImage;
	p.mArrayIndex = arrayIndex;
	p.mImageView.mTexture = texture;
	p.mImageView.mSampler = nullptr;
	p.mImageView.mView = texture->View();
	p.mImageView.mLayout = layout;

	vec[arrayIndex] = p;
	mCacheValid = false;
}
void Material::SetStorageTexture(const string& name, shared_ptr<Texture> texture, uint32_t arrayIndex, vk::ImageLayout layout) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = {};
	p.mType = vk::DescriptorType::eStorageImage;
	p.mArrayIndex = arrayIndex;
	p.mImageView.mTexture = texture;
	p.mImageView.mSampler = nullptr;
	p.mImageView.mView = texture->View();
	p.mImageView.mLayout = layout;
	vec[arrayIndex] = p;
	mCacheValid = false;
}
void Material::SetSampler(const string& name, shared_ptr<Sampler> sampler, uint32_t arrayIndex) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = {};
	p.mType = vk::DescriptorType::eSampler;
	p.mArrayIndex = arrayIndex;
	p.mImageView.mSampler = sampler;
	p.mImageView.mTexture = nullptr;
	p.mImageView.mView = nullptr;
	p.mImageView.mLayout = vk::ImageLayout::eUndefined;

	vec[arrayIndex] = p;
	mCacheValid = false;
}

shared_ptr<GraphicsPipeline> Material::Bind(CommandBuffer& commandBuffer, Mesh* mesh) {
	ProfilerRegion ps("Material::Bind");

	// TODO: cache pipeline
	vector<vk::SpecializationInfo*> specializationInfos;
	vector<vk::PipelineColorBlendAttachmentState> blendStates;
	// TODO: populate specializationInfos, blendStates
	vk::PipelineVertexInputStateCreateInfo vertexInput = mesh->CreateInput(mModules[0]);

	shared_ptr<GraphicsPipeline> pipeline = make_shared<GraphicsPipeline>(mName, *commandBuffer.CurrentRenderPass(), commandBuffer.CurrentSubpassIndex(),
		mModules, specializationInfos,
		vertexInput, mesh->Topology(), mCullMode, mPolygonMode,
		mSampleShading, vk::PipelineDepthStencilStateCreateInfo({}, mDepthTest, mDepthWrite, vk::CompareOp::eLessOrEqual, {}, {}, {}, {}, 0, 1), blendStates);

	if (!mCacheValid) {
		for (auto& [name, entries] : mDescriptorParameters) {
			auto it = pipeline->DescriptorBindings().find(name);
			if (it == pipeline->DescriptorBindings().end()) continue;

			uint32_t setIndex = it->second.mSet;

			if (mDescriptorSetCache.count(setIndex) && mDescriptorSetCache.at(setIndex)->Layout() != pipeline->DescriptorSetLayouts()[setIndex]) {
				commandBuffer.TrackResource(mDescriptorSetCache.at(setIndex));
				mDescriptorSetCache.erase(setIndex);
			}
			if (mDescriptorSetCache.count(setIndex) == 0) 
				mDescriptorSetCache.emplace(setIndex, commandBuffer.Device().GetPooledDescriptorSet(mName+"/DescriptorSet"+to_string(setIndex), pipeline->DescriptorSetLayouts()[setIndex]));

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


shared_ptr<GraphicsPipeline> MaterialDerivative::Bind(CommandBuffer& commandBuffer, Mesh* mesh) {
	ProfilerRegion ps("MaterialDerivative::Bind");

	shared_ptr<GraphicsPipeline> pipeline; // TODO: create ot fetch pipeline for mesh

	Material::Bind(commandBuffer, mesh);
	return pipeline;
}