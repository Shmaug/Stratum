#pragma once

#include "RenderNode.hpp"

namespace stm {

class Material {
private:
	vector<SpirvModule> mModules;
	unordered_map<string, byte_blob> mSpecializationConstants;
	unordered_map<string, byte_blob> mPushConstants;
	unordered_map<string, unordered_map<uint32_t, DescriptorSet::Entry>> mDescriptors;
	vk::CullModeFlags mCullMode = vk::CullModeFlagBits::eBack;
	vk::PolygonMode mPolygonMode = vk::PolygonMode::eFill;
	bool mDepthTest = false;
	bool mDepthWrite = false;
	bool mSampleShading = false;

	shared_ptr<Pipeline> mPipeline;
	unordered_map<uint32_t, shared_ptr<DescriptorSet>> mDescriptorSets;
	
public:
	string mName;

	inline Material(const string& name, const vector<SpirvModule>& modules) : mName(name), mModules(modules) {}

	STRATUM_API virtual shared_ptr<GraphicsPipeline> Bind(CommandBuffer& commandBuffer, optional<GeometryData> g = {});

	inline void SetSpecialization(const string& name, const byte_blob& v) {
		if (mSpecializationConstants.count(name) == 0) return;
		mSpecializationConstants.at(name) = v;
		mPipeline.reset();
		mDescriptorSets.clear();
	}
	template<typename T> inline void SetSpecialization(const string& name, const T& v) {
		SetSpecialization(name, make_byte_blob(v));
	}
	
	inline void SetPushConstant(const string& name, const byte_blob& t) { mPushConstants[name] = t; }
	inline const byte_blob& GetPushConstant(const string& name) const {
		auto it = mPushConstants.find(name);
		if (it == mPushConstants.end()) return {};
		return it->second;
	}

	inline DescriptorSet::Entry& GetDescriptor(const string& name, uint32_t arrayIndex = 0) {
		return mDescriptors[name][arrayIndex];
	}
	inline const DescriptorSet::Entry& GetDescriptor(const string& name, uint32_t arrayIndex = 0) const {
		auto it = mDescriptors.find(name);
		if (it == mDescriptors.end() || it->second.size() <= arrayIndex) return {};
		return it->second.at(arrayIndex);
	}
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