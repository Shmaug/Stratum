#pragma once

#include "Buffer.hpp"

namespace stm {

class GeometryDescription {
	vk::Format mFormat;
	uint32_t mElementStride;
	uint32_t mOffset;
	vk::VertexInputRate mInputRate;
};

class Geometry {
public:
	struct Attribute {
	private:
		Buffer::StrideView mBuffer;
		vk::Format mFormat;
		uint32_t mOffset;
		vk::VertexInputRate mInputRate;

	public:
		Attribute() = default;
		Attribute(const Attribute&) = default;
		Attribute(Attribute&&) = default;
		inline Attribute(const Buffer::StrideView& b, vk::Format fmt, uint32_t offset = 0, vk::VertexInputRate inputRate = vk::VertexInputRate::eVertex)
			: mBuffer(b), mFormat(fmt), mOffset(offset), mInputRate(inputRate) {}
		
		Attribute& operator=(const Attribute&) = default;
		Attribute& operator=(Attribute&&) = default;
		bool operator==(const Attribute&) const = default;

		inline Attribute& operator=(const Buffer::StrideView& b) {
			if (mBuffer && b.stride() != mBuffer.stride()) throw invalid_argument("Cannot assign buffer with different stride");
			mBuffer = b;
			return *this;
		}
		
		inline operator bool() const { return !mBuffer.empty(); }
		
		inline const auto& buffer() const { return mBuffer; }
		inline vk::Format format() const { return mFormat; }
    inline uint32_t offset() const { return mOffset; }
		inline uint32_t stride() const { return texel_size(mFormat);}
		inline const vk::VertexInputRate& input_rate() const { return mInputRate; }
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

	using attribute_map_t = unordered_map<AttributeType, vector<Attribute>>;
	using iterator = attribute_map_t::iterator;
	using const_iterator = attribute_map_t::const_iterator;

	Geometry() = default;
	Geometry(Geometry&&) = default;
	Geometry(const Geometry&) = default;
	inline Geometry(vk::PrimitiveTopology topology) : mTopology(topology) {}
	inline Geometry(vk::PrimitiveTopology topology, const unordered_map<AttributeType, vector<Attribute>>& attributes) : mTopology(topology), mAttributes(attributes) {}

	Geometry& operator=(const Geometry&) = default;
	Geometry& operator=(Geometry&&) = default;

	inline vk::PrimitiveTopology& topology() { return mTopology; }
	inline const vk::PrimitiveTopology& topology() const { return mTopology; }

	inline iterator begin() { return mAttributes.begin(); }
	inline iterator end() { return mAttributes.end(); }
	inline const_iterator begin() const { return mAttributes.begin(); }
	inline const_iterator end() const { return mAttributes.end(); }

	inline const vector<Attribute>& at(const AttributeType& t) const { return mAttributes.at(t); }
	
	inline vector<Attribute>& operator[](const AttributeType& t) { return mAttributes[t]; }
	inline const vector<Attribute>& operator[](const AttributeType& t) const { return mAttributes.at(t); }

	STRATUM_API void bind(CommandBuffer& commandBuffer) const;
	STRATUM_API void draw(CommandBuffer& commandBuffer, uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) const;
	STRATUM_API void drawIndexed(CommandBuffer& commandBuffer, const Buffer::StrideView& indices, uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, uint32_t vertexOffset = 0, uint32_t firstInstance = 0) const;

private:
	vk::PrimitiveTopology mTopology;
	attribute_map_t mAttributes;
};

}

namespace std {
template<>
struct hash<stm::Geometry::Attribute> {
	inline size_t operator()(const stm::Geometry::Attribute& v) const {
		return stm::hash_args(v.format(), v.offset(), v.input_rate());
	}
};
template<>
struct hash<stm::Geometry> {
	inline size_t operator()(const stm::Geometry& g) const {
		return stm::hash_args(g.topology(), g|views::values);
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