#pragma once

#include "../Stratum.hpp"

namespace stm {

struct DescriptorBinding {
	vk::DescriptorSetLayoutBinding mBinding;
	uint32_t mSet;
	vector<vk::Sampler> mImmutableSamplers;
	vk::ShaderStageFlags mStageFlags;
};

enum class VertexAttributeType {
	ePosition,
	eNormal,
	eTangent,
	eBitangent,
	eTexcoord,
	eColor,
	ePointSize,
	eOther
};
struct VertexStageVariable {
	uint32_t mLocation;
	vk::Format mFormat;
	struct {
		VertexAttributeType mType : 24;
		unsigned int mTypeIndex : 8;
	};
};

struct SpirvModule {
	vk::ShaderModule mShaderModule;
	vk::Device mDevice;

	vector<uint32_t> mSpirv;
	
	vk::ShaderStageFlagBits mStage;
	string mEntryPoint;
	map<string, vk::SpecializationMapEntry> mSpecializationMap;
	map<string, DescriptorBinding> mDescriptorBindings;
	map<string, vk::PushConstantRange> mPushConstants;
	map<string, VertexStageVariable> mStageInputs;
	map<string, VertexStageVariable> mStageOutputs;
	uint3 mWorkgroupSize;

	SpirvModule() = default;
	SpirvModule(const SpirvModule&) = default;
	inline SpirvModule(const fs::path& file, vk::Device device = nullptr) : mDevice(device) {
		binary_stream stream(file);
		stream >> *this;
		if (mDevice) mShaderModule = device.createShaderModule(vk::ShaderModuleCreateInfo({}, (uint32_t)mSpirv.size()*sizeof(uint32_t), mSpirv.data()));
	}
	inline SpirvModule(binary_stream& stream) { stream >> *this; }
	inline ~SpirvModule() { if (mShaderModule) mDevice.destroyShaderModule(mShaderModule); }

	inline uint32_t SetIndex(const string& descriptor) const { return mDescriptorBindings.at(descriptor).mSet; }
	inline uint32_t Location(const string& descriptor) const { return mDescriptorBindings.at(descriptor).mBinding.binding; }
	
	inline friend binary_stream& operator<<(binary_stream& stream, const SpirvModule& m) {
		stream << m.mSpirv;
		stream << m.mStage;
		stream << m.mEntryPoint;
		stream << m.mSpecializationMap;
		stream << m.mStageInputs;
		stream << m.mStageOutputs;
		stream << m.mDescriptorBindings;
		stream << m.mPushConstants;
		stream << m.mWorkgroupSize;
		return stream;
	}
	inline friend binary_stream& operator>>(binary_stream& stream, SpirvModule& m) {
		stream >> m.mSpirv;
		stream >> m.mStage;
		stream >> m.mEntryPoint;
		stream >> m.mSpecializationMap;
		stream >> m.mStageInputs;
		stream >> m.mStageOutputs;
		stream >> m.mDescriptorBindings;
		stream >> m.mPushConstants;
		stream >> m.mWorkgroupSize;
		return stream;
	}
};
struct SpirvModuleGroup {
	using value_type      = vector<SpirvModule>::value_type;
	using allocator_type  = vector<SpirvModule>::allocator_type;
	using pointer         = vector<SpirvModule>::pointer;
	using const_pointer   = vector<SpirvModule>::const_pointer;
	using reference       = vector<SpirvModule>::reference;
	using const_reference = vector<SpirvModule>::const_reference;
	using size_type       = vector<SpirvModule>::size_type;
	using difference_type = vector<SpirvModule>::difference_type;

	vector<SpirvModule> mModules;

	SpirvModuleGroup() = default;
	SpirvModuleGroup(const SpirvModuleGroup&) = default;
	SpirvModuleGroup(SpirvModuleGroup&&) = default;
	SpirvModuleGroup& operator=(const SpirvModuleGroup&) = default;
	SpirvModuleGroup& operator=(SpirvModuleGroup&&) = default;

	inline SpirvModuleGroup(const initializer_list<SpirvModule>& args) : mModules(args) {};
	inline SpirvModuleGroup(const fs::path& file, vk::Device device = nullptr) {
		binary_stream stream(file);
		stream >> *this;
		if (device)
			for (SpirvModule& m : mModules) {
				m.mDevice = device;
				m.mShaderModule = device.createShaderModule(vk::ShaderModuleCreateInfo({}, (uint32_t)m.mSpirv.size()*sizeof(uint32_t), m.mSpirv.data()));
			}
	}

	inline size_type size() const { return mModules.size(); }

	inline vector<SpirvModule>::iterator begin() { return mModules.begin(); }
	inline vector<SpirvModule>::iterator end() { return mModules.end(); }
	inline vector<SpirvModule>::const_iterator cbegin() const { return mModules.cbegin(); }
	inline vector<SpirvModule>::const_iterator cend() const { return mModules.cend(); }

	inline SpirvModule& operator[](vector<SpirvModule>::size_type index) { return mModules[index]; }
	inline const SpirvModule& operator[](vector<SpirvModule>::size_type index) const { return mModules[index]; }

	inline friend binary_stream& operator<<(binary_stream& stream, const SpirvModuleGroup& m) { stream << m.mModules; return stream; }
	inline friend binary_stream& operator>>(binary_stream& stream, SpirvModuleGroup& m) { stream >> m.mModules; return stream; }
};

template<> inline size_t basic_hash(const stm::SpirvModule& m) {
	return basic_hash(
		m.mSpirv, // TODO: optimize hashing entire spirv
		m.mStage,
		m.mEntryPoint,
		m.mSpecializationMap,
		m.mStageInputs,
		m.mStageOutputs,
		m.mDescriptorBindings,
		m.mPushConstants,
		m.mWorkgroupSize);
};

}