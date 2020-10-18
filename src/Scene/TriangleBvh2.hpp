#pragma once

#include "../Stratum.hpp"

namespace stm { 

class TriangleBvh2 {
public:
	struct Primitive {
		uint3 mTriangle;
	};
	struct Node {
		AABB mBounds;
		// index of the first primitive inside this node
		uint32_t mStartIndex;
		// number of primitives inside this node
		uint32_t mCount;
		uint32_t mRightOffset; // 1st child is at node[index + 1], 2nd child is at node[index + mRightOffset]
	};

	STRATUM_API TriangleBvh2(const void* vertices, uint32_t baseVertex, uint32_t vertexCount, size_t vertexStride, const void* indices, uint32_t indexCount, vk::IndexType indexType, uint32_t leafSize = 4);

	STRATUM_API bool Intersect(const Ray& ray, float* t, bool any);

	inline float3 GetVertex(uint32_t index) const { return mVertices[index]; }
	inline uint3 GetTriangle(uint32_t index) const { return mTriangles[index]; }
	inline uint32_t TriangleCount() const { return (uint32_t)mTriangles.size(); }
	inline AABB Bounds() { return mNodes.size() ? mNodes[0].mBounds : AABB(); }	

private:
	std::vector<Node> mNodes;
	std::vector<uint3> mTriangles;
	std::vector<float3> mVertices;
	uint32_t mLeafSize = 4;
};

}