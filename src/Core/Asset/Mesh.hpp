#pragma once

#include "../Buffer.hpp"
#include "../Pipeline.hpp"

namespace stm {

template<typename T> struct TriangleIntersector {
	inline bool operator()(const vec3_t<T> triangle[3], const vec4_t<T> frustum[6]) {
		// TODO
		return false;
	}
	inline bool operator()(const vec3_t<T> triangle[3], const Ray<T>& ray, T* t, bool any) {
		vec3_t<T> tuv;
		bool hit = ray.Intersect(triangle[0], triangle[1], triangle[2], &tuv);
		if (t) *t = tuv.x;
		return hit;
	}
};

using TriangleBvh2 = bvh_t<float, float3[3], TriangleIntersector<float>>;

struct VertexAttributeId {
	VertexAttributeType mType : 24;
	unsigned int mTypeIndex : 8;
	VertexAttributeId()=default;
	VertexAttributeId(const VertexAttributeId&)=default;
	VertexAttributeId(VertexAttributeType type, uint8_t typeIndex):mType(type), mTypeIndex(typeIndex) {}
	inline bool operator==(const VertexAttributeId& rhs) const = default;
};
struct vertex_attribute_id_hash { inline size_t operator()(const VertexAttributeId& id) const { return *(const uint32_t*)&id; } };

struct VertexAttributeData {
	ArrayBufferView mBufferView;
	uint32_t mElementOffset;
	vk::VertexInputRate mInputRate;
	inline VertexAttributeData(ArrayBufferView bufferView, uint32_t elementOffset = 0, vk::VertexInputRate inputRate = vk::VertexInputRate::eVertex) : mBufferView(bufferView), mElementOffset(elementOffset), mInputRate(inputRate) {}
	inline bool operator==(const VertexAttributeData& rhs) const = default;
};

class Mesh {
public:
	using VertexAttributeMap = unordered_map<VertexAttributeId, VertexAttributeData, vertex_attribute_id_hash>;
	
	class Submesh {
	public:
		uint32_t mVertexCount = 0;
		uint32_t mBaseVertex = 0;
		uint32_t mIndexCount = 0;
		uint32_t mBaseIndex = 0;
		TriangleBvh2* mBvh = nullptr;

		Submesh() = default;
		Submesh(const Submesh&) = default;
		inline Submesh(uint32_t vertexCount, uint32_t baseVertex, uint32_t indexCount, uint32_t baseIndex, TriangleBvh2* bvh = nullptr)
			: mVertexCount(vertexCount), mBaseVertex(baseVertex), mIndexCount(indexCount), mBaseIndex(baseIndex), mBvh(bvh) {}
		inline ~Submesh() { if (mBvh) delete mBvh; };

		STRATUM_API void Draw(CommandBuffer& commandBuffer, uint32_t instanceCount = 1, uint32_t firstInstance = 0);
	};

	const string mName;

	inline Mesh(const string& name, const VertexAttributeMap& vertexAttributes, const ArrayBufferView& indexBuffer = {}, vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList)
		: mName(name), mVertexAttributes(vertexAttributes), mIndexBuffer(indexBuffer), mTopology(topology) {}

	inline uint32_t SubmeshCount() const { return (uint32_t)mSubmeshes.size(); }
	inline const Submesh& GetSubmesh(uint32_t index) const { return mSubmeshes[index]; }
	inline void AddSubmesh(const Submesh& submesh) { mSubmeshes.push_back(submesh); }

	inline VertexAttributeData* Attribute(VertexAttributeId id) {
		if (mVertexAttributes.count(id))
			return &mVertexAttributes.at(id);
		return nullptr;
	}
	inline vk::PrimitiveTopology Topology() const { return mTopology; }
	inline const ArrayBufferView& IndexBuffer() const { return mIndexBuffer; }

	struct VertexStageInput {
		vector<vk::VertexInputAttributeDescription> mAttributes;
		// indexed by mAttributes[...].binding
		vector<VertexAttributeData> mBindings;
	};
	STRATUM_API VertexStageInput CreateInput(const SpirvModule& vertexStage);

private:
	VertexAttributeMap mVertexAttributes;
	ArrayBufferView mIndexBuffer;
	vector<Submesh> mSubmeshes;
	vk::PrimitiveTopology mTopology = vk::PrimitiveTopology::eTriangleList;
};

}