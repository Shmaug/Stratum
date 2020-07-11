#include <Content/Material.hpp>
#include <Scene/Camera.hpp>
#include <Scene/Scene.hpp>
#include <Content/AssetManager.hpp>
#include <Content/Texture.hpp>
#include <Util/Profiler.hpp>

#include <Shaders/include/shadercompat.h>

using namespace std;

Material::Material(const string& name, ::Shader* shader)
	: mName(name), mShader(shader), mDevice(shader->Device()), mCullMode(VK_CULL_MODE_FLAG_BITS_MAX_ENUM), mBlendMode(BLEND_MODE_MAX_ENUM), mRenderQueue(~0), mPassMask(PASS_MASK_MAX_ENUM),
	mCachedDescriptorSet(nullptr), mDescriptorSetDirty(false) {
	CopyInputSignature(mShader->GetGraphics(PASS_MAIN, mShaderKeywords));
}
Material::~Material() {
	safe_delete(mCachedDescriptorSet);
}

void Material::EnableKeyword(const string& kw) {
	if (mShaderKeywords.count(kw) || !mShader->HasKeyword(kw)) return;
	mShaderKeywords.insert(kw);
	CopyInputSignature(mShader->GetGraphics(PASS_MAIN, mShaderKeywords));
	mDescriptorSetDirty = true;
}
void Material::DisableKeyword(const string& kw) {
	if (!mShaderKeywords.count(kw) || !mShader->HasKeyword(kw)) return;
	CopyInputSignature(mShader->GetGraphics(PASS_MAIN, mShaderKeywords));
	mDescriptorSetDirty = true;
}

void Material::CopyInputSignature(GraphicsShader* shader) {
	mDescriptorParameters.clear();
	for (auto& kp : shader->mDescriptorBindings)
		if (kp.second.first == PER_MATERIAL)
			mDescriptorParameters[kp.first].resize(kp.second.second.descriptorCount);
}

void Material::SetUniformBuffer(const string& name, variant_ptr<Buffer> buffer, VkDeviceSize offset, VkDeviceSize range, uint32_t arrayIndex) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = vec[arrayIndex];
	p.mType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	p.mArrayIndex = arrayIndex;
	p.mBufferValue = buffer;
	p.mBufferOffset = offset;
	p.mBufferRange = range;
	p.mTextureValue = nullptr;
	p.mImageView = VK_NULL_HANDLE;
	p.mImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	p.mSamplerValue = nullptr;

	vec[arrayIndex] = p;
	mDescriptorSetDirty = true;
}
void Material::SetStorageBuffer(const string& name, variant_ptr<Buffer> buffer, VkDeviceSize offset, VkDeviceSize range, uint32_t arrayIndex) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = {};
	p.mType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	p.mArrayIndex = arrayIndex;
	p.mBufferValue = buffer;
	p.mBufferOffset = offset;
	p.mBufferRange = range;
	p.mTextureValue = nullptr;
	p.mImageView = VK_NULL_HANDLE;
	p.mImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	p.mSamplerValue = nullptr;

	vec[arrayIndex] = p;
	mDescriptorSetDirty = true;
}
void Material::SetSampledTexture(const string& name, variant_ptr<Texture> texture, uint32_t arrayIndex, VkImageLayout layout) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = {};
	p.mType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	p.mArrayIndex = arrayIndex;
	p.mBufferOffset = 0;
	p.mBufferRange = 0;
	p.mBufferValue = 0;
	p.mTextureValue = texture;
	p.mImageView = texture->View();
	p.mImageLayout = layout;
	p.mSamplerValue = nullptr;

	vec[arrayIndex] = p;
	mDescriptorSetDirty = true;
}
void Material::SetStorageTexture(const string& name, variant_ptr<Texture> texture, uint32_t arrayIndex, VkImageLayout layout) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = {};
	p.mType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	p.mArrayIndex = arrayIndex;
	p.mBufferOffset = 0;
	p.mBufferRange = 0;
	p.mBufferValue = 0;
	p.mTextureValue = texture;
	p.mImageView = texture->View();
	p.mImageLayout = layout;
	p.mSamplerValue = nullptr;
	vec[arrayIndex] = p;
	mDescriptorSetDirty = true;
}
void Material::SetSampler(const string& name, variant_ptr<Sampler> sampler, uint32_t arrayIndex) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = {};
	p.mType = VK_DESCRIPTOR_TYPE_SAMPLER;
	p.mArrayIndex = arrayIndex;
	p.mBufferOffset = 0;
	p.mBufferRange = 0;
	p.mBufferValue = 0;
	p.mTextureValue = nullptr;
	p.mImageView = VK_NULL_HANDLE;
	p.mImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	p.mSamplerValue = sampler;

	vec[arrayIndex] = p;
	mDescriptorSetDirty = true;
}


void Material::SetPushParameter(const string& name, VkDeviceSize dataSize, const void* data) {
	char* dst = new char[dataSize];
	memcpy(dst, data, dataSize);
	mPushParameters[name] = make_pair(dataSize, (void*)dst);
}

bool Material::GetPushParameter(const string& name, VkDeviceSize dataSize, void* data) const {
	if (!mPushParameters.count(name)) return false;
	const auto& p = mPushParameters.at(name);
	memcpy(data, p.second, min(dataSize, p.first));
	return true;
}

GraphicsShader* Material::GetShader(PassType pass) {
	return mShader->GetGraphics(pass, mShaderKeywords);
}


void Material::PreBeginRenderPass(CommandBuffer* commandBuffer, PassType pass) {
	for (auto& kp : mDescriptorParameters)
		for (auto& d : kp.second)
			if (d.mType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || d.mType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || d.mType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
				commandBuffer->TransitionBarrier(d.mTextureValue.get(), d.mImageLayout);

	if (!mDescriptorSetDirty) return;

	if (mCachedDescriptorSet) {
		commandBuffer->TrackResource(mCachedDescriptorSet);
		mCachedDescriptorSet = nullptr;
	}

	GraphicsShader* shader = GetShader(pass);
	if (shader->mDescriptorSetLayouts.size() <= PER_MATERIAL || shader->mDescriptorBindings.empty()) return;
	mCachedDescriptorSet = commandBuffer->Device()->GetPooledDescriptorSet(mName, shader->mDescriptorSetLayouts[PER_MATERIAL]);
	
	// set descriptor parameters
	PROFILER_BEGIN("Write Descriptor Sets");
	for (auto& kp : mDescriptorParameters) {
		if (shader->mDescriptorBindings.count(kp.first) == 0) continue;

		auto& set_binding = shader->mDescriptorBindings.at(kp.first);
		if (set_binding.first != PER_MATERIAL) continue;

		for (uint32_t i = 0; i < kp.second.size(); i++) {
			if (kp.second[i].IsNull()) continue;
			mCachedDescriptorSet->CreateDescriptor(set_binding.second.binding, kp.second[i]);
			switch (kp.second[i].mType) {
				case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
				case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
				case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
					commandBuffer->TransitionBarrier(kp.second[i].mTextureValue.get(), kp.second[i].mImageLayout);
					break;
			}
		}
	}
	mCachedDescriptorSet->FlushWrites();
	PROFILER_END;
	mDescriptorSetDirty = false;
}

void Material::BindDescriptorParameters(CommandBuffer* commandBuffer, PassType pass, Camera* camera) {
	if (!mCachedDescriptorSet) return;

	GraphicsShader* shader = GetShader(pass);
	if (shader->mDescriptorSetLayouts.size() > PER_MATERIAL && shader->mDescriptorBindings.size())
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->mPipelineLayout, PER_MATERIAL, 1, *mCachedDescriptorSet, 0, nullptr);

	if (camera && shader->mDescriptorSetLayouts.size() > PER_CAMERA && shader->mDescriptorBindings.count("Camera")) {
		auto binding = shader->mDescriptorBindings.at("Camera");
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->mPipelineLayout, PER_CAMERA, 1, *camera->DescriptorSet(binding.second.stageFlags), 0, nullptr);
	}
}
void Material::BindPushParameters(CommandBuffer* commandBuffer, PassType pass, Camera* camera) {
	PROFILER_BEGIN("Push Constants");
	// set push constant parameters
	GraphicsShader* shader = GetShader(pass);
	for (auto& m : mPushParameters) {
		if (shader->mPushConstants.count(m.first) == 0) continue;
		auto& range = shader->mPushConstants.at(m.first);
		vkCmdPushConstants(*commandBuffer, shader->mPipelineLayout, range.stageFlags, range.offset, min((uint32_t)m.second.first, range.size), m.second.second);
	}
	PROFILER_END;
}