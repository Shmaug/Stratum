#pragma once

#include "Buffer.hpp"

namespace stm {

class ShaderModule;

class VertexArrayObject {
public:
	struct AttributeDescription {
		uint32_t mStride;
		vk::Format mFormat;
		uint32_t mOffset;
		vk::VertexInputRate mInputRate;
	};
	
	using Attribute = pair<AttributeDescription, Buffer::View<byte>>;

	enum class AttributeType {
		ePosition,
		eNormal,
		eTangent,
		eBinormal,
		eColor,
		eTexcoord,
		ePointSize,
		eBlendIndex,
		eBlendWeight
	};
	
	VertexArrayObject() = default;
	VertexArrayObject(VertexArrayObject&&) = default;
	VertexArrayObject(const VertexArrayObject&) = default;
	inline VertexArrayObject(const unordered_map<AttributeType, vector<Attribute>>& attributes) : mAttributes(attributes) {}

	VertexArrayObject& operator=(const VertexArrayObject&) = default;
	VertexArrayObject& operator=(VertexArrayObject&&) = default;

	inline auto begin() { return mAttributes.begin(); }
	inline auto end() { return mAttributes.end(); }
	inline auto begin() const { return mAttributes.begin(); }
	inline auto end() const { return mAttributes.end(); }

	inline optional<Attribute> find(const AttributeType& t, uint32_t index = 0) const {
		auto it = mAttributes.find(t);
		if (it != mAttributes.end() && it->second.size() > index)
			return it->second[index];
		else
			return nullopt;
	}
	inline const vector<Attribute>& at(const AttributeType& t) const { return mAttributes.at(t); }
	
	inline vector<Attribute>& operator[](const AttributeType& t) { return mAttributes[t]; }
	inline const vector<Attribute>& operator[](const AttributeType& t) const { return mAttributes.at(t); }

	STRATUM_API void bind(CommandBuffer& commandBuffer) const;

private:
	unordered_map<AttributeType, vector<Attribute>> mAttributes;
};

struct VertexLayoutDescription {
	unordered_map<VertexArrayObject::AttributeType, vector<pair<VertexArrayObject::AttributeDescription, uint32_t/*binding index*/>>> mAttributes;
	vk::PrimitiveTopology mTopology;
	vk::IndexType mIndexType;
	
	VertexLayoutDescription() = default;
	VertexLayoutDescription(const VertexLayoutDescription&) = default;
	VertexLayoutDescription(VertexLayoutDescription&&) = default;
	VertexLayoutDescription& operator=(const VertexLayoutDescription&) = default;
	VertexLayoutDescription& operator=(VertexLayoutDescription&&) = default;
	inline VertexLayoutDescription(vk::PrimitiveTopology topo, vk::IndexType indexType = vk::IndexType::eUint16) : mTopology(topo), mIndexType(indexType) {}
	STRATUM_API VertexLayoutDescription(const ShaderModule& vertexShader, const VertexArrayObject& vertexData, vk::PrimitiveTopology topology, vk::IndexType indexType); 
};

class Mesh {
private:
	shared_ptr<VertexArrayObject> mVertices;
	Buffer::StrideView mIndices;
	vk::PrimitiveTopology mTopology = vk::PrimitiveTopology::eTriangleList;
public:
	Mesh() = default;
	Mesh(const Mesh&) = default;
	Mesh(Mesh&&) = default;
	Mesh& operator=(const Mesh&) = default;
	Mesh& operator=(Mesh&&) = default;
	inline Mesh(const shared_ptr<VertexArrayObject> vertexData, Buffer::StrideView indices, vk::PrimitiveTopology topology)
		: mVertices(vertexData), mIndices(indices), mTopology(topology) {}
	
	inline auto& vertices() { return mVertices; }
	inline auto& indices() { return mIndices; }
	inline auto& topology() { return mTopology; }
	inline const auto& vertices() const { return mVertices; }
	inline const auto& indices() const { return mIndices; }
	inline const auto& topology() const { return mTopology; }
	inline vk::IndexType index_type() const { return (mIndices.stride() == sizeof(uint32_t)) ? vk::IndexType::eUint32 : (mIndices.stride() == sizeof(uint16_t)) ? vk::IndexType::eUint16 : vk::IndexType::eUint8EXT; }

	inline VertexLayoutDescription vertex_layout(const ShaderModule& vertexShader) const {
		return VertexLayoutDescription(vertexShader, *mVertices, mTopology, index_type());
	}

	inline vector<VertexArrayObject::Attribute>& operator[](const VertexArrayObject::AttributeType& t) { return mVertices->operator[](t); }
	inline const vector<VertexArrayObject::Attribute>& operator[](const VertexArrayObject::AttributeType& t) const { return mVertices->operator[](t); }

	STRATUM_API void bind(CommandBuffer& commandBuffer) const;
};

}

namespace std {
template<>
struct hash<stm::VertexArrayObject::AttributeDescription> {
	inline size_t operator()(const stm::VertexArrayObject::AttributeDescription& v) const {
		return stm::hash_args(v.mFormat, v.mOffset, v.mInputRate);
	}
};

template<>
struct hash<stm::VertexLayoutDescription> {
	inline size_t operator()(const stm::VertexLayoutDescription& v) const {
		return stm::hash_args(v.mAttributes, v.mTopology, v.mIndexType);
	}
};

inline string to_string(const stm::VertexArrayObject::AttributeType& value) {
	switch (value) {
		case stm::VertexArrayObject::AttributeType::ePosition: return "Position";
		case stm::VertexArrayObject::AttributeType::eNormal: return "Normal";
		case stm::VertexArrayObject::AttributeType::eTangent: return "Tangent";
		case stm::VertexArrayObject::AttributeType::eBinormal: return "Binormal";
		case stm::VertexArrayObject::AttributeType::eBlendIndex: return "BlendIndex";
		case stm::VertexArrayObject::AttributeType::eBlendWeight: return "BlendWeight";
		case stm::VertexArrayObject::AttributeType::eColor: return "Color";
		case stm::VertexArrayObject::AttributeType::ePointSize: return "PointSize";
		case stm::VertexArrayObject::AttributeType::eTexcoord: return "Texcoord";
		default: return "invalid ( " + vk::toHexString( static_cast<uint32_t>( value ) ) + " )";
	}
}
}