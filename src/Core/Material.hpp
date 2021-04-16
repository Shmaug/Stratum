#pragma once

#include "CommandBuffer.hpp"

namespace stm {

class Material {
private:
	vector<shared_ptr<SpirvModule>> mModules;
	unordered_map<string, byte_blob> mSpecializationConstants;
	unordered_map<string, byte_blob> mPushConstants;
	unordered_map<string, unordered_map<uint32_t, stm::Descriptor>> mDescriptors;
	unordered_map<string, shared_ptr<Sampler>> mImmutableSamplers;
	vk::CullModeFlags mCullMode = vk::CullModeFlagBits::eBack;
	vk::PolygonMode mPolygonMode = vk::PolygonMode::eFill;
	bool mSampleShading = false;
	vk::PipelineDepthStencilStateCreateInfo mDepthStencilState = vk::PipelineDepthStencilStateCreateInfo({}, true, true, vk::CompareOp::eLessOrEqual);
	vector<vk::PipelineColorBlendAttachmentState> mBlendStates;

	locked_object<unordered_map<size_t, shared_ptr<GraphicsPipeline>>> mPipelines;

	STRATUM_API shared_ptr<GraphicsPipeline> CreatePipeline(RenderPass& renderPass, uint32_t subpassIndex, const GeometryData& geometryData);
	
public:
	string mName;

	inline Material(const string& name, const vector<shared_ptr<SpirvModule>>& modules) : mName(name), mModules(modules) {}
	template<convertible_to<SpirvModule>... Args>
	inline Material(const string& name, const shared_ptr<Args>&... args) : Material(name, { args... }) {}

	inline void TransitionTextures(CommandBuffer& commandBuffer) {
		for (auto& [name, darray] : mDescriptors)
			for (auto& [arrayIndex, d] : darray)
				if (d.index() == 0)
					if (Texture::View view = get<Texture::View>(d))
						view.texture().TransitionBarrier(commandBuffer, get<vk::ImageLayout>(d));
	}

	STRATUM_API virtual shared_ptr<GraphicsPipeline> Bind(CommandBuffer& commandBuffer, const GeometryData& g = {});
	inline shared_ptr<GraphicsPipeline> Bind(CommandBuffer& commandBuffer, vk::PrimitiveTopology topo) { return Bind(commandBuffer, GeometryData { topo, {}, {}}); }

	inline void SetImmutableSampler(const string& name, const shared_ptr<Sampler>& sampler) {
		auto it = mImmutableSamplers.find(name);
		mImmutableSamplers.emplace(name, sampler);
		auto pipelines = mPipelines.lock();
		pipelines->clear();
	}
	
	template<typename T = byte_blob>
	inline void SpecializationConstant(const string& name, const T& v) {
		auto it = mSpecializationConstants.find(name);
		size_t sz = 0;
		if (it == mSpecializationConstants.end()) {
			for (const auto& m : mModules)
				if (auto it = m->mSpecializationMap.find(name); it != m->mSpecializationMap.end()) {
					sz = it->second.size;
					break;
				}
			if (sz == 0) throw invalid_argument("No specialization constant named " + name);
		} else
			sz = it->second.size();

		if constexpr (is_same_v<T,byte_blob>) {
			if (v.size() != sz) throw invalid_argument("Argument must match specialization constant size");
			mSpecializationConstants[name] = v;
		} else {
			if (sizeof(T) != sz) throw invalid_argument("Argument must match specialization constant size");
			auto& c = mSpecializationConstants[name];
			if (c.size() != sizeof(T)) c.resize(sizeof(T));
			memcpy(c.data(), &v, sizeof(T));
		}
		auto pipelines = mPipelines.lock();
		pipelines->clear();
	}
	template<typename T = byte_blob>
	inline const T& SpecializationConstant(const string& name) {
		auto it = mSpecializationConstants.find(name);
		if (it == mSpecializationConstants.end()) throw invalid_argument("No specialization constant named " + name);
		if constexpr (is_same_v<T,byte_blob>)
			return it->second;
		else {
			if (it->second.size() != sizeof(T)) throw invalid_argument("Argument size must match specialization constant size");
			return *reinterpret_cast<T*>(it->second.data());
		}
	}
	
	template<typename T = byte_blob>
	inline void PushConstant(const string& name, const T& v) {
		auto it = mPushConstants.find(name);
		size_t sz = 0;
		if (it == mPushConstants.end()) {
			for (const auto& m : mModules)
				if (auto it = m->mPushConstants.find(name); it != m->mPushConstants.end()) {
					sz = it->second.second;
					break;
			}
			if (sz == 0) throw invalid_argument("No push constant named " + name);
		} else
			sz = it->second.size();

		if constexpr (is_same_v<T,byte_blob>) {
			if (v.size() != sz) throw invalid_argument("Argument must match push constant size");
			mPushConstants[name] = v;
		} else {
			if (sizeof(T) != sz) throw invalid_argument("Argument must match push constant size");
			auto& c = mPushConstants[name];
			if (c.size() != sizeof(T)) c.resize(sizeof(T));
			memcpy(c.data(), &v, sizeof(T));
		}
	}
	template<typename T = byte_blob>
	inline const T& PushConstant(const string& name) const {
		auto it = mPushConstants.find(name);
		if (it == mPushConstants.end()) throw invalid_argument("No push constant named " + name);
		if constexpr (is_same_v<T,byte_blob>)
			return it->second;
		else {
			if (it->second.size() != sizeof(T)) throw invalid_argument("Type size must match push constant size");
			return *reinterpret_cast<T*>(it->second.data());
		}
	}

	inline stm::Descriptor& Descriptor(const string& name, uint32_t arrayIndex = 0) {
		auto desc_it = mDescriptors.find(name);
		if (desc_it != mDescriptors.end()) {
			auto it = desc_it->second.find(arrayIndex);
			if (it != desc_it->second.end())
			return it->second;
		}

		for (const auto& spirv : mModules)
			if (spirv->mDescriptorBindings.find(name) != spirv->mDescriptorBindings.end())
				return mDescriptors.emplace(name, unordered_map<uint32_t, stm::Descriptor>()).first->second.emplace(arrayIndex, stm::Descriptor()).first->second;
		
		throw invalid_argument("Descriptor " + name + " does not exist");
	}
	inline const stm::Descriptor& Descriptor(const string& name, uint32_t arrayIndex = 0) const {
		return mDescriptors.at(name).at(arrayIndex);
	}
};

}