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
};

}