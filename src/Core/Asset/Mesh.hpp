#pragma once

#include "../GeometryData.hpp"
#include "../CommandBuffer.hpp"
#include "../Pipeline.hpp"

namespace stm {

class Mesh {
public:
	struct Submesh {
		uint32_t mPrimitiveCount;
		uint32_t mFirstVertex;
		uint32_t mFirstIndex;
	};
	
	inline Mesh(const string& name) : mName(name) {}
	inline const GeometryData& Geometry() { return mGeometry; }
	inline const vector<Submesh>& Submeshes() const { return mSubmeshes; }

	inline VertexAttributeData& operator[](const VertexAttributeId& id) { return mTopology.mAttributes[id]; }
	struct VertexAttributeArrayView {
		// intermediate object to allow Mesh[attrib][idx] to work
		Mesh& mMesh;
		VertexAttributeType mAttributeType;
		inline VertexAttribute& operator[](const VertexAttributeId& id) { return mMesh.mAttributeBuffers[id]; }
		inline Mesh::VertexAttribute& operator[](uint32_t idx) { return mMesh.mAttributeBuffers[VertexAttributeId(mAttributeType,idx)]; }
	};
	inline VertexAttributeArrayView operator[](const VertexAttributeType& type) { return VertexAttributeArrayView(*this, type); }
	inline VertexAttributeArrayView at(const VertexAttributeType& type, uint32_t idx = 0) { return mAttributeBuffers.at({type, idx}); }
	inline VertexAttributeArrayView at(const VertexAttributeId& id) { return mAttributeBuffers.at(id); }

	inline uint32_t Index(uint32_t i, uint32_t baseIndex = 0, uint32_t baseVertex = 0) const {
		if (mIndexBuffer)
			switch (mIndexBuffer.stride()) {
				case sizeof(uint16_t): return baseVertex + mIndexBuffer.at<uint16_t>(baseIndex + i);
				default:
				case sizeof(uint32_t): return baseVertex + mIndexBuffer.at<uint32_t>(baseIndex + i);
				case sizeof(uint8_t):  return baseVertex + mIndexBuffer.at<uint8_t>(baseIndex + i);
			}
		else
			return baseVertex + i;
	}

	inline void AddSubmesh(uint32_t vertexCount, uint32_t baseIndex=0, uint32_t baseVertex=0) {
		mSubmeshes.emplace_back(*this, vertexCount, baseIndex, baseVertex);
		vector<pair<VertexAttributeArrayView, size_t>> prims;
		ranges::for_each(mSubmeshes, [&](Submesh& s) {
			for (uint32_t i = 0; i < s.mPrimitiveCount; i++)
				prims.push_back(make_pair(s, i));
		});
		mBVH = bvh_t(prims);
	}

	inline void Draw(CommandBuffer& commandBuffer, uint32_t instanceCount, uint32_t firstInstance) {
		auto pipeline = dynamic_pointer_cast<GraphicsPipeline>(commandBuffer.BoundPipeline());
		if (!pipeline) throw logic_error("cannot draw a mesh without a bound graphics pipeline\n");

		for (uint32_t i = 0; i < mGeometry.mBindings.size(); i++)
			commandBuffer.BindVertexBuffer(mGeometry.mBindings[i].mBuffer, i);
		if (mIndexBuffer) commandBuffer.BindIndexBuffer(mIndexBuffer);

		uint32_t primCount = 0;
		for (const Submesh& s : mSubmeshes) {
			if (mIndexBuffer)
				commandBuffer->drawIndexed(s.mPrimitiveCount*PrimitiveDegree(), instanceCount, s.mFirstIndex, s.mFirstVertex, firstInstance);
			 else
				commandBuffer->draw(s.mPrimitiveCount*PrimitiveDegree(), instanceCount, s.mFirstVertex, firstInstance);
			primCount += s.mPrimitiveCount;
		}
		commandBuffer.mPrimitiveCount += instanceCount * primCount;
	}

private:
	GeometryData mGeometry;
	Buffer::ArrayView<> mIndices;
	vector<Submesh> mSubmeshes;
	string mName;
};

}