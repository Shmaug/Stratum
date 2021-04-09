#pragma once

#include "RenderNode.hpp"

namespace stm {

class Material {
protected:
	string mName;
	unordered_map<vk::ShaderStageFlagBits, SpirvModule> mModules;
	unordered_map<string, byte_blob> mSpecializationConstants;
	unordered_map<string, byte_blob> mPushParameters;
	unordered_map<string, vector<DescriptorSet::Entry>> mDescriptorParameters;

private:
	unordered_map<uint32_t, shared_ptr<DescriptorSet>> mDescriptorSetCache;
	bool mCacheValid = false;

	vk::CullModeFlags mCullMode = vk::CullModeFlagBits::eBack;
	vk::PolygonMode mPolygonMode = vk::PolygonMode::eFill;
	bool mDepthTest = false;
	bool mDepthWrite = false;
	bool mSampleShading = false;
	
public:
	inline Material(const string& name, const unordered_map<vk::ShaderStageFlagBits, SpirvModule>& modules) : mName(name), mModules(modules) {}

	STRATUM_API virtual shared_ptr<GraphicsPipeline> Bind(CommandBuffer& commandBuffer, optional<GeometryData> g = {});

	inline void SetSpecialization(const string& name, const byte_blob& v) {
		if (mSpecializationConstants.count(name) == 0) return;
		mSpecializationConstants.at(name) = v;
		mCacheValid = false;
	}
	template<typename T> inline void SetSpecialization(const string& name, const T& v) { SetSpecialization(name, make_byte_blob(v)); }

	inline void SetPushParameter(const string& name, const byte_blob& t) { mPushParameters[name] = t; }
	inline const byte_blob& GetPushParameter(const string& name) const { if (mPushParameters.count(name)) return mPushParameters.at(name); return {}; }

	STRATUM_API void SetUniformBuffer(const string& name, const Buffer::RangeView& param, uint32_t arrayIndex = 0);
	STRATUM_API void SetStorageBuffer(const string& name, const Buffer::RangeView& param, uint32_t arrayIndex = 0);
	STRATUM_API void SetStorageTexture(const string& name, const TextureView& param, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eGeneral);
	STRATUM_API void SetSampledTexture(const string& name, const TextureView& param, uint32_t arrayIndex = 0, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);
	STRATUM_API void SetSampler(const string& name, shared_ptr<Sampler> param, uint32_t arrayIndex = 0);

	inline bool HasDescriptorParameter(const string& name, uint32_t arrayIndex = 0) const { return mDescriptorParameters.count(name) && mDescriptorParameters.at(name).size() > arrayIndex; }
	inline DescriptorSet::Entry GetDescriptorParameter(const string& name, uint32_t arrayIndex = 0) const { return mDescriptorParameters.at(name)[arrayIndex]; }
};

class MaterialDerivative : public Material {
private:
	shared_ptr<Material> mBaseMaterial;

	// TODO: implement MaterialDerivative as VkPipeline with derivative flag, ideally support linking assigned inputs when a MaterialDerivative shares most of the same descriptor inputs as its base

public:
	inline MaterialDerivative(const string& name, const shared_ptr<Material>& baseMaterial) : Material(*baseMaterial) { mBaseMaterial = baseMaterial; }

	STRATUM_API virtual shared_ptr<GraphicsPipeline> Bind(CommandBuffer& commandBuffer, optional<GeometryData> g = {});
};

}