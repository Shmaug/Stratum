#include "Shader.hpp"

#include "../Core/CommandBuffer.hpp"
#include "../Core/Pipeline.hpp"

using namespace std;
using namespace stm;

std::ostream& operator <<(std::ostream& stream, const Shader& shader) {
	WriteValue<uint64_t>(stream, shader.mVariants.size());
	for (const auto& [idx,variant] : shader.mVariants) stream << variant;
}
std::ostream& operator <<(std::ostream& stream, const Shader::Variant& variant) {
	stream << variant.mKeywords.size();
	for (const auto& kw : variant.mKeywords) stream << kw;

	WriteValue(stream, (uint64_t)mDescriptorSetBindings.size());
	for (const auto&[name, binding] : mDescriptorSetBindings) {
		WriteString(stream, name);
		WriteValue(stream, binding.mSet);
		WriteValue(stream, binding.mBinding);
	}

	WriteValue(stream, (uint64_t)mPushConstants.size());
	for (const auto&[name, range] : mPushConstants) {
		WriteString(stream, name);
		WriteValue(stream, range);
	}
	
	WriteValue(stream, (uint64_t)mImmutableSamplers.size());
	for (const auto&[name, sampler] : mImmutableSamplers) {
		WriteString(stream, name);
		WriteValue(stream, sampler);
	}

	WriteValue(stream, (uint64_t)mModules.size());
	for (const auto& module : mModules) module.Write(stream);
	
	WriteString(stream, mShaderPass);
	WriteVector(stream, mBlendStates);
	WriteValue(stream, mDepthStencilState);
	WriteValue(stream, mRenderQueue);
	WriteValue(stream, mCullMode);
	WriteValue(stream, mPolygonMode);
	WriteValue(stream, mSampleShading);
	WriteValue(stream, mWorkgroupSize);
}

Shader::Variant::Variant(istream& stream) {
	uint64_t keywordCount;
	ReadValue<uint64_t>(stream, keywordCount);
	for (uint32_t i = 0; i < keywordCount; i++) {
		string str;
		ReadString(stream, str);
		mKeywords.insert(str);
	}
	
	uint64_t descriptorCount;
	ReadValue<uint64_t>(stream, descriptorCount);
	for (uint32_t i = 0; i < descriptorCount; i++) {
		string name;
		ReadString(stream, name);
		DescriptorBinding value;
		ReadValue(stream, value.mSet);
		ReadValue(stream, value.mBinding);
		mDescriptorSetBindings.emplace(name, value);
	}

	uint64_t pushConstantCount;
	ReadValue<uint64_t>(stream, pushConstantCount);
	for (uint32_t i = 0; i < pushConstantCount; i++) {
		string name;
		ReadString(stream, name);
		vk::PushConstantRange value;
		ReadValue(stream, value);
		mPushConstants.emplace(name, value);
	}

	uint64_t immutableSamplerCount;
	ReadValue<uint64_t>(stream, immutableSamplerCount);
	for (uint32_t i = 0; i < immutableSamplerCount; i++) {
		string name;
		ReadString(stream, name);
		vk::SamplerCreateInfo value;
		ReadValue(stream, value);
		mImmutableSamplers.emplace(name, value);
	}

	uint64_t moduleCount;
	ReadValue<uint64_t>(stream, moduleCount);
	mModules.resize(moduleCount);
	for (uint32_t i = 0; i < moduleCount; i++) mModules[i].Read(stream);

	ReadString(stream, mShaderPass);
	ReadVector(stream, mBlendStates);
	ReadValue(stream, mDepthStencilState);
	ReadValue(stream, mRenderQueue);
	ReadValue(stream, mCullMode);
	ReadValue(stream, mPolygonMode);
	ReadValue(stream, mSampleShading);
	ReadValue(stream, mWorkgroupSize);

	mDepthStencilState.depthTestEnable = VK_TRUE;
	mDepthStencilState.depthWriteEnable = VK_TRUE;
	mDepthStencilState.depthCompareOp = vk::CompareOp::eLessOrEqual;
	mDepthStencilState.front = mDepthStencilState.back;
	mDepthStencilState.back.compareOp = vk::CompareOp::eAlways;
}

Shader::Shader() : Asset("", nullptr, "") {}
Shader::Shader(const fs::path& filename, Device* device, const string& name) : Asset(filename, device, name) {
	ifstream stream(filename);
	uint64_t variantCount;
	ReadValue<uint64_t>(stream, variantCount);
	for (uint32_t i = 0; i < variantCount; i++) {
		Variant v(stream);
		uint64_t key = 0;
		mVariants.insert(key, v);
	}
}

shared_ptr<ComputePipeline> Shader::GetPipeline(const set<string>& keywords, const string& kernel) const {
	uint64_t key = hash<string>()(kernel);
	for (const string& k : keywords)
		if (mKeywords.count(k))
			key = hash_combine(key, k);

	if (!mVariants.count(key)) throw invalid_argument("Invalid keyword set");
	Variant& variant = mVariants.at(key);

	if (mPipelines.count(key))
		return static_pointer_cast<ComputePipeline>(mPipelines.at(key));
	else
		return static_pointer_cast<ComputePipeline>(*mPipelines.insert(key, make_shared<ComputePipeline>(variant, kernel).first));
}

shared_ptr<GraphicsPipeline> Shader::GetPipeline(const set<string>& keywords, const Subpass& subpass, const string& shaderPass,
	vk::PrimitiveTopology topology, const vk::PipelineVertexInputStateCreateInfo& vertexInput,
	vk::Optional<const vk::CullModeFlags> cullMode, vk::Optional<const vk::PolygonMode> polygonMode) {

	uint64_t key = hash<string>()(shaderPass);
	for (const string& k : keywords)
		if (mKeywords.count(k))
			key = hash_combine(key, k);
	
	if (!mVariants.count(key)) throw invalid_argument("Invalid keyword set");
	Variant& variant = mVariants.at(key);

	vk::CullModeFlags cull = cullMode == nullptr ? variant->mCullMode : *cullMode;
	vk::PolygonMode poly = polygonMode == nullptr ? variant->mPolygonMode : *polygonMode;

	key = hash_combine(key, cull, poly);

	if (mPipelines.count(key))
		return static_pointer_cast<GraphicsPipeline>(mPipelines.at(key));
	else
		return static_pointer_cast<GraphicsPipeline>(*mPipelines.insert(key, make_shared<GraphicsPipeline>(variant, subpass, topology, vertexInput, cull, poly).first));
}