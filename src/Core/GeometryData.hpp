#pragma once

#include "Buffer.hpp"
#include "SpirvModule.hpp"

namespace stm {

struct VertexAttributeData {
	Buffer::ArrayView<> mBuffer;
	vk::VertexInputRate mInputRate;
};

struct GeometryData {
	vk::PrimitiveTopology mPrimitiveTopology;
	unordered_map<VertexAttributeId, vk::VertexInputAttributeDescription> mAttributes;
	vector<VertexAttributeData> mBindings;

	struct VertexAttributeArrayView {
		// intermediate object to allow Mesh[attrib][idx] to work
		GeometryData& mGeometry;
		VertexAttributeType mAttributeType;
		inline vk::VertexInputAttributeDescription& at(uint32_t idx) { return mGeometry.mAttributes.at(VertexAttributeId(mAttributeType, idx)); }
		inline vk::VertexInputAttributeDescription& operator[](uint32_t idx) { return mGeometry.mAttributes[VertexAttributeId(mAttributeType,idx)]; }
	};
	inline vk::VertexInputAttributeDescription& operator[](const VertexAttributeId& id) { return mAttributes[id]; }
	inline VertexAttributeArrayView operator[](const VertexAttributeType& type) { return VertexAttributeArrayView(*this, type); }
	inline vk::VertexInputAttributeDescription& at(const VertexAttributeId& id) { return mAttributes.at(id); }
	inline vk::VertexInputAttributeDescription& at(const VertexAttributeType& type, uint32_t idx = 0) { return mAttributes.at(VertexAttributeId(type, idx)); }
};

}