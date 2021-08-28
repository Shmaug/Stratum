#pragma once

#include "Buffer.hpp"

namespace stm {

class Geometry {
public:
	struct AttributeDescription {
		uint32_t mElementStride;
		vk::Format mFormat;
		uint32_t mOffset;
		vk::VertexInputRate mInputRate;
	};
	enum class AttributeType {
		ePosition,
		eNormal,
		eTangent,
		eBinormal,
		eColor,
		eTexcoord,
		ePointSize,
		eBlendIndices,
		eBlendWeight
	};
	using Attribute = pair<AttributeDescription, Buffer::StrideView>;

	Geometry() = default;
	Geometry(Geometry&&) = default;
	Geometry(const Geometry&) = default;
	inline Geometry(const unordered_map<AttributeType, vector<Attribute>>& attributes) : mAttributes(attributes) {}
	template<same_as<AttributeType> ... Types>
	inline Geometry(Types...types) { ( mAttributes[types].push_back({}), ...); }

	Geometry& operator=(const Geometry&) = default;
	Geometry& operator=(Geometry&&) = default;

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

class SpirvModule;
struct GeometryStateDescription {
	unordered_map<Geometry::AttributeType, vector<pair<Geometry::AttributeDescription, uint32_t/*binding index*/>>> mAttributes;
	vk::PrimitiveTopology mTopology;
	vk::IndexType mIndexType;
	
	GeometryStateDescription() = default;
	GeometryStateDescription(const GeometryStateDescription&) = default;
	GeometryStateDescription(GeometryStateDescription&&) = default;
	GeometryStateDescription& operator=(const GeometryStateDescription&) = default;
	GeometryStateDescription& operator=(GeometryStateDescription&&) = default;
	inline GeometryStateDescription(vk::PrimitiveTopology topo, vk::IndexType indexType = vk::IndexType::eUint16) : mTopology(topo), mIndexType(indexType) {}
	STRATUM_API GeometryStateDescription(const SpirvModule& vertexShader, const Geometry& geometry, vk::PrimitiveTopology topology, vk::IndexType indexType); 
};

class Mesh {
private:
	shared_ptr<Geometry> mGeometry;
	Buffer::StrideView mIndices;
	vk::PrimitiveTopology mTopology = vk::PrimitiveTopology::eTriangleList;
public:
	Mesh() = default;
	Mesh(const Mesh&) = default;
	Mesh(Mesh&&) = default;
	Mesh& operator=(const Mesh&) = default;
	Mesh& operator=(Mesh&&) = default;
	inline Mesh(const shared_ptr<Geometry> geometry, Buffer::StrideView indices, vk::PrimitiveTopology topology)
		: mGeometry(geometry), mIndices(indices), mTopology(topology) {}
	
	inline auto& geometry() { return mGeometry; }
	inline auto& indices() { return mIndices; }
	inline auto& topology() { return mTopology; }
	inline const auto& geometry() const { return mGeometry; }
	inline const auto& indices() const { return mIndices; }
	inline const auto& topology() const { return mTopology; }

	inline GeometryStateDescription description(const SpirvModule& vertexShader) const {
		return GeometryStateDescription(vertexShader, *mGeometry, mTopology, (mIndices.stride() == sizeof(uint32_t)) ? vk::IndexType::eUint32 : (mIndices.stride() == sizeof(uint16_t)) ? vk::IndexType::eUint16 : vk::IndexType::eUint8EXT);
	}

	inline vector<Geometry::Attribute>& operator[](const Geometry::AttributeType& t) { return mGeometry->operator[](t); }
	inline const vector<Geometry::Attribute>& operator[](const Geometry::AttributeType& t) const { return mGeometry->operator[](t); }

	STRATUM_API void bind(CommandBuffer& commandBuffer) const;
};

}

namespace std {
template<>
struct hash<stm::Geometry::AttributeDescription> {
	inline size_t operator()(const stm::Geometry::AttributeDescription& v) const {
		return stm::hash_args(v.mFormat, v.mOffset, v.mInputRate);
	}
};

template<>
struct hash<stm::GeometryStateDescription> {
	inline size_t operator()(const stm::GeometryStateDescription& v) const {
		return stm::hash_args(v.mAttributes, v.mTopology, v.mIndexType);
	}
};

inline string to_string(const stm::Geometry::AttributeType& value) {
	switch (value) {
		case stm::Geometry::AttributeType::ePosition: return "Position";
		case stm::Geometry::AttributeType::eNormal: return "Normal";
		case stm::Geometry::AttributeType::eTangent: return "Tangent";
		case stm::Geometry::AttributeType::eBinormal: return "Binormal";
		case stm::Geometry::AttributeType::eBlendIndices: return "BlendIndices";
		case stm::Geometry::AttributeType::eBlendWeight: return "BlendWeight";
		case stm::Geometry::AttributeType::eColor: return "Color";
		case stm::Geometry::AttributeType::ePointSize: return "PointSize";
		case stm::Geometry::AttributeType::eTexcoord: return "Texcoord";
		default: return "invalid ( " + vk::toHexString( static_cast<uint32_t>( value ) ) + " )";
	}
}
}