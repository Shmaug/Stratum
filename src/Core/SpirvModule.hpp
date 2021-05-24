#pragma once

#include "Geometry.hpp"
#include "DescriptorSet.hpp"

namespace stm {

struct RasterStageVariable {
	uint32_t mLocation;
	vk::Format mFormat;
	Geometry::AttributeType mAttributeType;
	uint32_t mAttributeIndex;
};

class SpirvModule : public DeviceResource {
private:
	vk::ShaderModule mShaderModule; // created when the SpirvModule is used to create a Pipeline
	vk::ShaderStageFlagBits mStage;
	string mEntryPoint;
	vector<shared_ptr<DescriptorSetLayout>> mDescriptorSetLayouts;
	unordered_map<string, pair<uint32_t,uint32_t>> mDescriptorMap;
	unordered_map<string, vk::SpecializationMapEntry> mSpecializationMap;
	unordered_map<string, pair<uint32_t/*offset*/, uint32_t/*size*/>> mPushConstants;
	unordered_map<string, RasterStageVariable> mStageInputs;
	unordered_map<string, RasterStageVariable> mStageOutputs;
	vk::Extent3D mWorkgroupSize;
	
public:
	STRATUM_API SpirvModule(Device& device, const fs::path& spvasm);
	inline ~SpirvModule() { if (mShaderModule) mDevice->destroyShaderModule(mShaderModule); }
	
	inline vk::ShaderModule* operator->() { return &mShaderModule; }
	inline vk::ShaderModule& operator*() { return mShaderModule; }
	inline const vk::ShaderModule* operator->() const { return &mShaderModule; }
	inline const vk::ShaderModule& operator*() const { return mShaderModule; }

	inline const auto& stage() const { return mStage; }
	inline const auto& entry_point() const { return mEntryPoint; }
	inline const auto& specialization_map() const { return mSpecializationMap; }
	inline const auto& push_constants() const { return mPushConstants; }
	inline const auto& stage_inputs() const { return mStageInputs; }
	inline const auto& stage_outputs() const { return mStageInputs; }
	inline const auto& descriptor_set_layouts() const { return mDescriptorSetLayouts; }
	inline const auto& workgroup_size() const { return mWorkgroupSize; }
	inline const DescriptorSetLayout::Binding* binding(const string& name) const {
		auto it = mDescriptorMap.find(name);
		if (it == mDescriptorMap.end()) return nullptr;
		return &mDescriptorSetLayouts[it->second.first]->at(it->second.second);
	}
};

}