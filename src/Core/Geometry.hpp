#pragma once

#include "Buffer.hpp"

namespace stm {

class Geometry {
public:
	struct Attribute {
	private:
		Buffer::View<byte> mBuffer;
		vk::Format mFormat;
		uint32_t mOffset;
		vk::VertexInputRate mInputRate;

	public:
		Attribute() = default;
		Attribute(const Attribute&) = default;
		Attribute(Attribute&&) = default;
		inline Attribute(const Buffer::View<byte>& v) : mBuffer(v) {}
		inline Attribute(Buffer::View<byte>&& v) : mBuffer(v) {}
		inline Attribute(const shared_ptr<Buffer>& b, vk::Format fmt, uint32_t offset = 0, vk::VertexInputRate inputRate = vk::VertexInputRate::eVertex, vk::DeviceSize bufferOffset = 0, vk::DeviceSize elementCount = VK_WHOLE_SIZE)
			: mBuffer(b, bufferOffset, elementCount), mFormat(fmt), mOffset(offset), mInputRate(inputRate) {}
		template<typename T>
		inline Attribute(const Buffer::View<T>& view, vk::Format fmt, uint32_t offset = 0, vk::VertexInputRate inputRate = vk::VertexInputRate::eVertex)
			: mBuffer(view.buffer_ptr(), view.offset(), view.size_bytes()), mFormat(fmt), mOffset(offset), mInputRate(inputRate) {}
		
		Attribute& operator=(const Attribute&) = default;
		Attribute& operator=(Attribute&&) = default;
		inline bool operator==(const Attribute&) const = default;
		
		inline operator bool() const { return !mBuffer.empty(); }
		
		inline const auto& buffer_view() const { return mBuffer; }

		inline vk::Format format() const { return mFormat; }
    inline uint32_t offset() const { return mOffset; }
		inline uint32_t stride() const { return texel_size(mFormat);}
		inline const vk::VertexInputRate& input_rate() const { return mInputRate; }
	};
	struct VertexAttributeArrayView {
		// helper object to allow Geometry[attrib][idx] to work
		Geometry& mGeometry;
		VertexAttributeType mAttributeType;
		inline Attribute& at(uint32_t idx) { return mGeometry.mAttributes.at(VertexAttributeId(mAttributeType, idx)); }
		inline Attribute& operator[](uint32_t idx) { return mGeometry.mAttributes[VertexAttributeId(mAttributeType,idx)]; }
	};
	
	using iterator = unordered_map<VertexAttributeId, Attribute>::iterator;
	using const_iterator = unordered_map<VertexAttributeId, Attribute>::const_iterator;

	Geometry() = default;
	Geometry(Geometry&&) = default;
	Geometry(const Geometry&) = default;
	inline Geometry(vk::PrimitiveTopology topology) : mTopology(topology) {}
	inline Geometry(vk::PrimitiveTopology topology, const unordered_map<VertexAttributeId, Attribute>& attributes) : mTopology(topology), mAttributes(attributes) {}

	Geometry& operator=(const Geometry&) = default;
	Geometry& operator=(Geometry&&) = default;

	inline vk::PrimitiveTopology& topology() { return mTopology; }
	inline const vk::PrimitiveTopology& topology() const { return mTopology; }

	inline Attribute& operator[](const VertexAttributeId& id) { return mAttributes[id]; }
	inline VertexAttributeArrayView operator[](const VertexAttributeType& type) { return VertexAttributeArrayView(*this, type); }
	inline Attribute& at(const VertexAttributeId& id) { return mAttributes.at(id); }
	inline Attribute& at(const VertexAttributeType& type, uint32_t idx = 0) { return mAttributes.at(VertexAttributeId(type, idx)); }

	inline iterator begin() { return mAttributes.begin(); }
	inline iterator end() { return mAttributes.end(); }
	inline const_iterator begin() const { return mAttributes.begin(); }
	inline const_iterator end() const { return mAttributes.end(); }

	STRATUM_API void bind(CommandBuffer& commandBuffer) const;
	STRATUM_API void draw(CommandBuffer& commandBuffer, uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) const;
	STRATUM_API void drawIndexed(CommandBuffer& commandBuffer, const Buffer::StrideView& indices, uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, uint32_t vertexOffset = 0, uint32_t firstInstance = 0) const;

private:
	vk::PrimitiveTopology mTopology;
	unordered_map<VertexAttributeId, Attribute> mAttributes;
};

}

namespace std {
	
template<>
struct hash<stm::Geometry::Attribute> {
	inline size_t operator()(const stm::Geometry::Attribute& v) const {
		return stm::hash_args(v.format(), v.offset(), v.input_rate());
	}
};

}