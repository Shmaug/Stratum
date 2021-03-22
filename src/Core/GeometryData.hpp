#pragma once

#include "Buffer.hpp"

namespace stm {

struct GeometryData {
	struct Attribute {
		uint32_t mBinding;
		vk::Format mFormat;
		vk::DeviceSize mOffset;
	};

	vk::PrimitiveTopology mPrimitiveTopology;
	vector<pair<Buffer::RangeView, vk::VertexInputRate>> mBindings;
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
};

}