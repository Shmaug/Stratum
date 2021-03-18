#pragma once

#include "Buffer.hpp"
#include "SpirvModule.hpp"

namespace stm {

struct GeometryData {
	struct Attribute {
		uint32_t mBinding;
		vk::Format mFormat;
		vk::DeviceSize mOffset;
	};

	vk::PrimitiveTopology mPrimitiveTopology;
	vector<pair<Buffer::ArrayView, vk::VertexInputRate>> mBindings;
	unordered_map<VertexAttributeId, Attribute> mAttributes;

	struct VertexAttributeArrayView {
		// intermediate object to allow Mesh[attrib][idx] to work
		GeometryData& mGeometry;
		VertexAttributeType mAttributeType;
		inline Attribute& at(uint32_t idx) { return mGeometry.mAttributes.at(VertexAttributeId(mAttributeType, idx)); }
		inline Attribute& operator[](uint32_t idx) { return mGeometry.mAttributes[VertexAttributeId(mAttributeType,idx)]; }
	};
	inline Attribute& operator[](const VertexAttributeId& id) { return mAttributes[id]; }
	inline VertexAttributeArrayView operator[](const VertexAttributeType& type) { return VertexAttributeArrayView(*this, type); }
	inline Attribute& at(const VertexAttributeId& id) { return mAttributes.at(id); }
	inline Attribute& at(const VertexAttributeType& type, uint32_t idx = 0) { return mAttributes.at(VertexAttributeId(type, idx)); }
		
	inline auto CreateInputBindings(const SpirvModule& vs) const {
		vector<vk::VertexInputAttributeDescription> attributes;
		vector<vk::VertexInputBindingDescription> bindings(mBindings.size());
		for (auto& [id, attrib] : mAttributes) {
			auto it = ranges::find_if(vs.mStageInputs, [&](const auto& p) { return p.second.mAttributeId == id; });
			if (it != vs.mStageInputs.end())
				attributes.push_back(vk::VertexInputAttributeDescription(it->second.mLocation, attrib.mBinding, attrib.mFormat, (uint32_t)attrib.mOffset));
		}
		for (uint32_t i = 0; i < mBindings.size(); i++) {
			bindings[i].binding = i;
			bindings[i].stride = (uint32_t)get<0>(mBindings[i]).stride();
			bindings[i].inputRate = get<1>(mBindings[i]);
		}
		return make_pair<vector<vk::VertexInputAttributeDescription>, vector<vk::VertexInputBindingDescription>>(forward<vector<vk::VertexInputAttributeDescription>>(attributes), forward<vector<vk::VertexInputBindingDescription>>(bindings));
	}
};

}