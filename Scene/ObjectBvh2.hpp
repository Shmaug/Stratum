#pragma once

#include <Scene/Object.hpp>

#ifdef GetObject
#undef GetObject
#endif

// Stores a binary bvh of Objects, based off each object's Object::Bounds() 
class ObjectBvh2 {
public:
	struct Node {
		AABB mBounds;
		// index of the first primitive inside this node
		uint32_t mStartIndex;
		// number of primitives inside this node
		uint32_t mRightOffset; // 1st child is at node[index + 1], 2nd child is at node[index + mRightOffset]
		uint32_t mCount;
	};

	inline ObjectBvh2() {};
	inline ~ObjectBvh2() {}

	const std::vector<Node>& Nodes() const { return mNodes; }
	Object* GetObject(uint32_t index) const { return mPrimitives[index].mObject; }

	STRATUM_API void Build(Object** objects, uint32_t objectCount);
	STRATUM_API void FrustumCheck(const float4 frustum[6], std::vector<Object*>& objects, uint32_t mask);
	STRATUM_API Object* Intersect(const Ray& ray, float* t, bool any, uint32_t mask);

private:
	struct Primitive {
		AABB mBounds;
		Object* mObject;
	};
	std::vector<Node> mNodes;
	std::vector<Primitive> mPrimitives;
};