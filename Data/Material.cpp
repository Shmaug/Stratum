#include <Data/Material.hpp>
#include <Scene/Camera.hpp>
#include <Scene/Scene.hpp>
#include <Data/AssetManager.hpp>
#include <Data/Texture.hpp>
#include <Util/Profiler.hpp>

using namespace std;

Material::Material(const string& name, ::Pipeline* pipeline)
	: mName(name), mPipeline(pipeline), mDevice(pipeline->Device()), mCullMode(nullptr), mPolygonMode(nullptr), mCachedDescriptorSet(nullptr), mDescriptorSetDirty(false) {
	if (GraphicsPipeline* pipeline = mPipeline->GetGraphics("main/forward", mShaderKeywords)) CopyInputSignature(pipeline);
}
Material::~Material() {
	safe_delete(mCachedDescriptorSet);
}

void Material::EnableKeyword(const string& kw) {
	if (mShaderKeywords.count(kw) || !mPipeline->HasKeyword(kw)) return;
	mShaderKeywords.insert(kw);
	if (GraphicsPipeline* pipeline = mPipeline->GetGraphics("main/forward", mShaderKeywords)) CopyInputSignature(pipeline);
	mDescriptorSetDirty = true;
}
void Material::DisableKeyword(const string& kw) {
	if (!mShaderKeywords.count(kw) || !mPipeline->HasKeyword(kw)) return;
	if (GraphicsPipeline* pipeline = mPipeline->GetGraphics("main/forward", mShaderKeywords)) CopyInputSignature(pipeline);
	mDescriptorSetDirty = true;
}

void Material::CopyInputSignature(GraphicsPipeline* pipeline) {
	mDescriptorParameters.clear();
	for (auto& kp : pipeline->mShaderVariant->mDescriptorSetBindings)
		if (kp.second.mSet == PER_MATERIAL)
			mDescriptorParameters[kp.first].resize(kp.second.mBinding.descriptorCount);
}

void Material::SetUniformBuffer(const string& name, variant_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t arrayIndex) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = vec[arrayIndex];
	p.mType = vk::DescriptorType::eUniformBuffer;
	p.mArrayIndex = arrayIndex;
	p.mBufferValue = buffer;
	p.mBufferOffset = offset;
	p.mBufferRange = range;
	p.mTextureValue = nullptr;
	p.mImageView = nullptr;
	p.mImageLayout = vk::ImageLayout::eUndefined;
	p.mSamplerValue = nullptr;

	vec[arrayIndex] = p;
	mDescriptorSetDirty = true;
}
void Material::SetStorageBuffer(const string& name, variant_ptr<Buffer> buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t arrayIndex) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = {};
	p.mType = vk::DescriptorType::eStorageBuffer;
	p.mArrayIndex = arrayIndex;
	p.mBufferValue = buffer;
	p.mBufferOffset = offset;
	p.mBufferRange = range;
	p.mTextureValue = nullptr;
	p.mImageView = nullptr;
	p.mImageLayout = vk::ImageLayout::eUndefined;
	p.mSamplerValue = nullptr;

	vec[arrayIndex] = p;
	mDescriptorSetDirty = true;
}
void Material::SetSampledTexture(const string& name, variant_ptr<Texture> texture, uint32_t arrayIndex, vk::ImageLayout layout) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = {};
	p.mType = vk::DescriptorType::eSampledImage;
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
void Material::SetStorageTexture(const string& name, variant_ptr<Texture> texture, uint32_t arrayIndex, vk::ImageLayout layout) {
	vector<DescriptorSetEntry>& vec = mDescriptorParameters[name];
	if (vec.size() <= arrayIndex) vec.resize(arrayIndex + 1);
	DescriptorSetEntry p = {};
	p.mType = vk::DescriptorType::eStorageImage;
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
	p.mType = vk::DescriptorType::eSampler;
	p.mArrayIndex = arrayIndex;
	p.mBufferOffset = 0;
	p.mBufferRange = 0;
	p.mBufferValue = 0;
	p.mTextureValue = nullptr;
	p.mImageView = nullptr;
	p.mImageLayout = vk::ImageLayout::eUndefined;
	p.mSamplerValue = sampler;

	vec[arrayIndex] = p;
	mDescriptorSetDirty = true;
}

void Material::SetPushParameter(const string& name, vk::DeviceSize dataSize, const void* data) {
	char* dst = new char[dataSize];
	memcpy(dst, data, dataSize);
	mPushParameters[name] = make_pair(dataSize, (void*)dst);
}

bool Material::GetPushParameter(const string& name, vk::DeviceSize dataSize, void* data) const {
	if (!mPushParameters.count(name)) return false;
	const auto& p = mPushParameters.at(name);
	memcpy(data, p.second, min(dataSize, p.first));
	return true;
}

void Material::OnLateUpdate(CommandBuffer* commandBuffer) {
	for (auto& kp : mDescriptorParameters)
		for (auto& d : kp.second)
			if (d.mType == vk::DescriptorType::eStorageImage || d.mType == vk::DescriptorType::eSampledImage || d.mType == vk::DescriptorType::eCombinedImageSampler)
				commandBuffer->TransitionBarrier(d.mTextureValue.get(), d.mImageLayout);
}

void Material::BindDescriptorParameters(CommandBuffer* commandBuffer) {
	GraphicsPipeline* pipeline = GetPassPipeline(commandBuffer->CurrentShaderPass());
	if (pipeline->mShaderVariant->mDescriptorSetBindings.empty() || pipeline->mDescriptorSetLayouts.size() < PER_MATERIAL) return;

	if (mDescriptorSetDirty || !mCachedDescriptorSet) {
		if (mCachedDescriptorSet) {
			commandBuffer->TrackResource(mCachedDescriptorSet);
			mCachedDescriptorSet = nullptr;
		}

		mCachedDescriptorSet = commandBuffer->Device()->GetPooledDescriptorSet(mName, pipeline->mDescriptorSetLayouts[PER_MATERIAL]);
		
		// set descriptor parameters
		PROFILER_BEGIN("Write Descriptor Sets");
		for (auto& kp : mDescriptorParameters) {
			if (pipeline->mShaderVariant->mDescriptorSetBindings.count(kp.first) == 0) continue;

			auto& binding = pipeline->mShaderVariant->mDescriptorSetBindings.at(kp.first);
			if (binding.mSet != PER_MATERIAL) continue;

			for (uint32_t i = 0; i < kp.second.size(); i++) {
				if (kp.second[i].IsNull()) continue;
				mCachedDescriptorSet->CreateDescriptor(binding.mBinding.binding, kp.second[i]);
			}
		}
		PROFILER_END;
		mDescriptorSetDirty = false;
	}
	commandBuffer->BindDescriptorSet(mCachedDescriptorSet, PER_MATERIAL);
}
void Material::PushConstants(CommandBuffer* commandBuffer) {
	for (auto& m : mPushParameters)
		commandBuffer->PushConstant(m.first, m.second.second, (uint32_t)m.second.first);
}