#pragma once

#include "algebra.hpp"

namespace stm {

template<typename T> class Sphere {
public:
	vec3_t<T> mCenter;
	T mRadius;
	inline Sphere() = default;
	inline Sphere(const vec3_t<T>& center, const T& radius) : mCenter(center), mRadius(radius) {}
};
template<typename T> class AABB {
public:
	vec3_t<T> mMin;
	vec3_t<T> mMax;

	AABB() = default;
	template<typename iterator_t> inline AABB(iterator_t begin, const iterator_t& end) : mMin(*begin), mMax(*begin) { Encapsulate(++begin, end); }
	inline AABB(const vec3_t<T>& min, const vec3_t<T>& max) : mMin(min), mMax(max) {}
	inline AABB(const AABB& aabb) : mMin(aabb.mMin), mMax(aabb.mMax) {}
	inline AABB(const AABB& aabb, const matrix<T,4,4>& transform) : AABB(aabb) { operator*=(transform); }

	inline vec3_t<T> Center() const { return (mMax + mMin)/2; }
	inline vec3_t<T> HalfSize() const { return (mMax - mMin)/2; }
	inline vec3_t<T> Size() const { return (mMax - mMin); }
	
	inline bool Intersects(const AABB& aabb) const {
		// for each i in (x, y, z) if a_min(i) > b_max(i) or b_min(i) > a_max(i) then return false
		bool dx = (mMin.x > aabb.mMax.x) || (aabb.mMin.x > mMax.x);
		bool dy = (mMin.y > aabb.mMax.y) || (aabb.mMin.y > mMax.y);
		bool dz = (mMin.z > aabb.mMax.z) || (aabb.mMin.z > mMax.z);
		return !(dx || dy || dz);
	}
	inline bool Intersects(const vec3_t<T>& point) const {
		vec3_t<T> e = (mMax - mMin) * .5f;
		vec3_t<T> s = point - (mMax + mMin) * .5f;
		return
			(s.x <= e.x && s.x >= -e.x) &&
			(s.y <= e.y && s.y >= -e.y) &&
			(s.z <= e.z && s.z >= -e.z);
	}
	inline bool Intersects(const Sphere<T>& sphere) const {
		vec3_t<T> e = HalfSize();
		vec3_t<T> s = sphere.mCenter - Center();
		vec3_t<T> delta = e - s;
		T sqDist = 0;
		for (int i = 0; i < 3; i++) {
			if (s[i] < -e[i]) sqDist += delta[i];
			if (s[i] >  e[i]) sqDist += delta[i];
		}
		return sqDist <= sphere.mRadius * sphere.mRadius;
	}
	inline bool Intersects(const vec4_t<T> frustum[6]) const {
		vec3_t<T> center = Center();
		vec3_t<T> size = HalfSize();
		for (uint32_t i = 0; i < 6; i++) {
			T r = dot(size, abs(frustum[i].xyz));
			T d = dot(center, frustum[i].xyz) - frustum[i].w;
			if (d <= -r) return false;
		}
		return true;
	}

	inline void Encapsulate(const vec3_t<T>& p) {
		mMin = min(mMin, p);
		mMax = max(mMax, p);
	}
	template<typename iterator_t> inline void Encapsulate(const iterator_t& begin, const iterator_t& end) {
		for (auto it = begin; it != end; ++it) Encapsulate(*it);
	}
	inline void Encapsulate(const AABB& aabb) {
		mMin = min(aabb.mMin, mMin);
		mMax = max(aabb.mMax, mMax);
	}

	inline AABB operator *(const matrix<T,4,4>& transform) const { return AABB(*this, transform); }
	inline AABB operator *(const quaternion<T>& q) const { return AABB(*this, q); }
	inline AABB operator *=(const matrix<T,4,4>& transform) {
		const array<vec3_t<T>,8> corners {
			(transform * vec4_t<T>(mMax, 1)).xyz,										// 1,1,1
			(transform * vec4_t<T>(mMin.x, mMax.y, mMax.z, 1)).xyz,	// 0,1,1
			(transform * vec4_t<T>(mMax.x, mMax.y, mMin.z, 1)).xyz,	// 1,1,0
			(transform * vec4_t<T>(mMin.x, mMax.y, mMin.z, 1)).xyz,	// 0,1,0
			(transform * vec4_t<T>(mMax.x, mMin.y, mMax.z, 1)).xyz,	// 1,0,1
			(transform * vec4_t<T>(mMin.x, mMin.y, mMax.z, 1)).xyz,	// 0,0,1
			(transform * vec4_t<T>(mMax.x, mMin.y, mMin.z, 1)).xyz,	// 1,0,0
			(transform * vec4_t<T>(mMin, 1)).xyz 										// 0,0,0
		};
		mMin = mMax = corners[0];
		Encapsulate(++corners.begin(), corners.end());
		return *this;
	}
	inline AABB operator *=(const quaternion<T>& q) {
		const array<vec3_t<T>,8> corners {
			q*mMax,																// 1,1,1
			q*vec3_t<T>(mMin.x, mMax.y, mMax.z),	// 0,1,1
			q*vec3_t<T>(mMax.x, mMax.y, mMin.z),	// 1,1,0
			q*vec3_t<T>(mMin.x, mMax.y, mMin.z),	// 0,1,0
			q*vec3_t<T>(mMax.x, mMin.y, mMax.z),	// 1,0,1
			q*vec3_t<T>(mMin.x, mMin.y, mMax.z),	// 0,0,1
			q*vec3_t<T>(mMax.x, mMin.y, mMin.z),	// 1,0,0
			q*mMin 																// 0,0,0
		};
		mMin = mMax = corners[0];
		Encapsulate(++corners.begin(), corners.end());
		return *this;
	}
};
template<typename T> class Ray {
public:
	vec3_t<T> mOrigin;
	vec3_t<T> mDirection;

	inline Ray() = default;
	inline Ray(const vec3_t<T>& ro, const vec3_t<T>& rd) : mOrigin(ro), mDirection(rd) {};

	inline T Intersect(const vec4_t<T>& plane) const {
		return -(dot(mOrigin, plane.xyz) + plane.w) / dot(mDirection, plane.xyz);
	}
	inline T Intersect(const vec3_t<T>& planeNormal, const vec3_t<T>& planePoint) const {
		return -dot(mOrigin - planePoint, planeNormal) / dot(mDirection, planeNormal);
	}

	inline bool Intersect(const AABB<T>& aabb, vec2_t<T>& t) const {
		vec3_t<T> id = 1/mDirection;
		vec3_t<T> pmin = (aabb.mMin - mOrigin) * id;
		vec3_t<T> pmax = (aabb.mMax - mOrigin) * id;
		vec3_t<T> mn, mx;
		mn.x = id.x >= 0 ? pmin.x : pmax.x;
		mn.y = id.y >= 0 ? pmin.y : pmax.y;
		mn.z = id.z >= 0 ? pmin.z : pmax.z;
		mx.x = id.x >= 0 ? pmax.x : pmin.x;
		mx.y = id.y >= 0 ? pmax.y : pmin.y;
		mx.z = id.z >= 0 ? pmax.z : pmin.z;
		t = vec2_t<T>(std::max(std::max(mn.x, mn.y), mn.z), std::min(std::min(mx.x, mx.y), mx.z));
		return t.y > t.x;
	}
	inline bool Intersect(const Sphere<T>& sphere, vec2_t<T>& t) const {
		vec3_t<T> pq = mOrigin - sphere.mCenter;
		T a = dot(mDirection, mDirection);
		T b = 2 * dot(pq, mDirection);
		T c = dot(pq, pq) - sphere.mRadius * sphere.mRadius;
		T d = b * b - 4 * a * c;
		if (d < 0.f) return false;
		d = sqrt(d);
		t = -vec2_t<T>(b + d, b - d) / (2*a);
		return true;
	}

	inline bool Intersect(vec3_t<T> v0, vec3_t<T> v1, vec3_t<T> v2, vec3_t<T>* tuv) const {
		// Algorithm from http://jcgt.org/published/0002/01/05/paper.pdf

		v0 -= mOrigin;
		v1 -= mOrigin;
		v2 -= mOrigin;

		vec3_t<T> rd = mDirection;
		vec3_t<T> ad = abs(mDirection);

		uint32_t largesti = 0;
		if (ad[largesti] < ad[1]) largesti = 1;
		if (ad[largesti] < ad[2]) largesti = 2;
		 
		T idz;
		vec2_t<T> rdz;

		if (largesti == 0) {
			v0 = vec3_t<T>(v0.y, v0.z, v0.x);
			v1 = vec3_t<T>(v1.y, v1.z, v1.x);
			v2 = vec3_t<T>(v2.y, v2.z, v2.x);
			idz = 1/rd.x;
			rdz = vec2_t<T>(rd.y, rd.z) * idz;
		} else if (largesti == 1) {
			v0 = vec3_t<T>(v0.z, v0.x, v0.y);
			v1 = vec3_t<T>(v1.z, v1.x, v1.y);
			v2 = vec3_t<T>(v2.z, v2.x, v2.y);
			idz = 1/rd.y;
			rdz = vec2_t<T>(rd.z, rd.x) * idz;
		} else {
			idz = 1/rd.z;
			rdz = vec2_t<T>(rd.x, rd.y) * idz;
		}

		v0 = vec3_t<T>(v0.x - v0.z * rdz.x, v0.y - v0.z * rdz.y, v0.z * idz);
		v1 = vec3_t<T>(v1.x - v1.z * rdz.x, v1.y - v1.z * rdz.y, v1.z * idz);
		v2 = vec3_t<T>(v2.x - v2.z * rdz.x, v2.y - v2.z * rdz.y, v2.z * idz);

		T u = v2.x * v1.y - v2.y * v1.x;
		T v = v0.x * v2.y - v0.y * v2.x;
		T w = v1.x * v0.y - v1.y * v0.x;

		if ((u < 0 || v < 0 || w < 0) && (u > 0 || v > 0 || w > 0)) return false;

		T det = u + v + w;
		if (det == 0) return false; // co-planar

		T t = u * v0.z + v * v1.z + w * v2.z;
		if (tuv) *tuv = vec3_t<T>(t, u, v) / det;
		return true;
	}
};
template<typename T> class Rect2D {
public:
	vec2_t<T> mOffset;
	// full size of rectangle
	vec2_t<T> mSize;

	inline Rect2D() : mOffset(0), mSize(0) {};
	inline Rect2D(const Rect2D& r) : mOffset(r.mOffset), mSize(r.mSize) {};
	inline Rect2D(const vec2_t<T>& offset, const vec2_t<T>& size) : mOffset(offset), mSize(size) {};
	inline Rect2D(const T& ox, const T& oy, const T& sx, const T& sy) : mOffset(vec2_t<T>(ox, oy)), mSize(sx, sy) {};

	inline Rect2D& operator=(const Rect2D & rhs) {
		mOffset = rhs.mOffset;
		mSize = rhs.mSize;
		return *this;
	}

	inline bool Intersects(const Rect2D& p) const {
		return !(
			mOffset.x + mSize.x < p.mOffset.x ||
			mOffset.y + mSize.y < p.mOffset.y ||
			mOffset.x > p.mOffset.x + p.mSize.x ||
			mOffset.y > p.mOffset.y + p.mSize.y);
	}
	inline bool Contains(const vec2_t<T>& p) const {
		return 
			p.x > mOffset.x && p.y > mOffset.y &&
			p.x < mOffset.x + mSize.x && p.y < mOffset.y + mSize.y;
	}
};

template<typename T, typename Primitive, class Intersector>
class bvh_t {
public:
	struct Node {
		AABB<T> mBounds;
		uint32_t mStartIndex;
		uint32_t mCount;
		unsigned int mChildOffset : 31, mIsLeaf : 1; // 1st child is at node[index + 1], 2nd child is at node[index + mChildOffset]
	};
private:
	vector<Node> mNodes;
	vector<Primitive> mPrimitives;

	template<typename AABBContainer> inline void build(const AABBContainer& aabbs, uint32_t leafSize) {
		stack<vec3_t<uint32_t>> todo;
		vector<uint8_t> touchCount;

		todo.push(uint3(0, (uint32_t)mPrimitives.size(), 0));

		while (!todo.empty()) {
			uint32_t start = todo.top().x;
			uint32_t end = todo.top().y;
			uint32_t parentIndex = todo.top().z;
			todo.pop();

			uint32_t count = end - start;

			AABB<T> bb(aabbs[start]);
			AABB<T> bc(aabbs[start].Center(), aabbs[start].Center());
			for (uint32_t p = start + 1; p < end; ++p) {
				bb.Encapsulate(aabbs[p]);
				bc.Encapsulate(aabbs[p].Center());
			}

			if (!mNodes.empty() && ++touchCount[parentIndex] == 2)
				mNodes[parentIndex].mChildOffset = mNodes.size() - 1 - parentIndex;

			mNodes.push_back({ bb, start, count, 0, count <= leafSize });
			touchCount.push_back(0);

			if (mNodes.back().mIsLeaf) continue; // leaf node

			// compute splitDim and splitCoord
			uint32_t splitDim = 0;
			vec3_t<T> extent = bc.HalfSize();
			if (extent[1] > extent[0]) {
				splitDim = 1;
				if (extent[2] > extent[1]) splitDim = 2;
			} else
				if (extent[2] > extent[0]) splitDim = 2;
			T splitCoord = (bc.mMin[splitDim] + bc.mMax[splitDim]) / 2;

			// swizzle primitives
			uint32_t mid = start;
			for (uint32_t i = start; i < end; i++)
				if (aabbs[i].Center()[splitDim] < splitCoord) {
					swap(mPrimitives[i], mPrimitives[mid]);
					swap(aabbs[i], aabbs[mid]);
					mid++;
				}

			// default to middle if the split was bad
			if (mid == start || mid == end)	mid = start + (end - start) / 2;

			todo.push(uint3(start, mid, (uint32_t)mNodes.size() - 1));
			todo.push(uint3(mid, end, (uint32_t)mNodes.size() - 1));
		}
	}
	template<typename U> inline AABB<T> recursive_dereference(const U& obj) {
		if constexpr (is_convertible<U, AABB<T>>::value) return obj;
		return *obj;
	}

public:
	template<typename PrimitiveContainer>
	inline bvh_t(const PrimitiveContainer& primitives, uint32_t leafSize = 4) {
		if (primitives.empty()) return;

		if constexpr (is_same<PrimitiveContainer, vector<Primitive>>::value)
			mPrimitives = primitives;
		else {
			mPrimitives.resize(primitives.size());
			for (uint32_t i = 0; i < primitives.size(); i++)
				mPrimitives[i] = primitives[i];
		}

		vector<AABB<T>> aabbs(mPrimitives.size());
		for (uint32_t i = 0; i < mPrimitives.size(); i++)
			if constexpr (is_pointer<Primitive>::value)
				aabbs[i] = recursive_dereference(mPrimitives[i]->Bounds());
			else
				aabbs[i] = recursive_dereference(mPrimitives[i].Bounds());

		build(aabbs, leafSize);
	}
	
	template<typename PrimitiveContainer, typename AABBContainer>
	inline bvh_t(const PrimitiveContainer& primitives, const AABBContainer& aabbs, uint32_t leafSize = 4) {
		if (primitives.empty()) return;
		
		if constexpr (is_same<AABBContainer, vector<Primitive>>::value)
			mPrimitives = primitives;
		else {
			mPrimitives.resize(primitives.size());
			for (uint32_t i = 0; i < primitives.size(); i++)
				mPrimitives[i] = primitives[i];
		}
		
		build(aabbs, leafSize);
	}

	inline const Node& GetNode(size_t idx) const { return mNodes[idx]; }
	inline Primitive& operator[](size_t idx) const { return mPrimitives[idx]; }

	inline vector<Primitive> Intersect(const vec4_t<T> frustum[6]) const {
		if (mNodes.empty()) return {};
		uint32_t todo[1024];
		int32_t stackptr = 0;
		todo[stackptr] = 0;

		Intersector intersector;

		vector<Primitive> result;
		while (stackptr >= 0) {
			int ni = todo[stackptr];
			stackptr--;
			const Node& node = mNodes[ni];

			if (node.mChildOffset == 0) { // leaf node
				if (intersector(mPrimitives[node.mStartIndex], frustum))
					result.push_back(mPrimitives[node.mStartIndex]);
			} else {
				uint32_t n0 = ni + 1;
				uint32_t n1 = ni + node.mChildOffset;
				if (mNodes[n0].mBounds.Intersects(frustum)) todo[++stackptr] = n0;
				if (mNodes[n1].mBounds.Intersects(frustum)) todo[++stackptr] = n1;
			}
		}
		return result;
	}
	inline const Primitive* Intersect(const Ray<T>& ray, T* t = nullptr, bool any = false) const {
		if (mNodes.empty()) return nullptr;

		uint32_t todo[128];
		int stackptr = 0;
		todo[stackptr] = 0;
		
		T ht = numeric_limits<T>::infinity();
		const Primitive* hit = nullptr;

		Intersector intersector;

		while (stackptr >= 0) {
			uint32_t ni = todo[stackptr];
			stackptr--;
			const auto& node = mNodes[ni];

			if (node.mChildOffset == 0) {
				T ct;
				if (!intersector(mPrimitives[node.mStartIndex], ray, &ct, any)) continue;

				if (ct < ht) {
					ht = ct;
					hit = &mPrimitives[node.mStartIndex];
					if (any) {
						if (t) *t = ht;
						return hit;
					}
				}
			} else {
				uint32_t n0 = ni + 1;
				uint32_t n1 = ni + node.mChildOffset;
				vec2_t<T> t0;
				vec2_t<T> t1;
				bool h0 = ray.Intersect(mNodes[n0].mBounds, t0);
				bool h1 = ray.Intersect(mNodes[n1].mBounds, t1);
				if (h0 && t0.y < ht) todo[++stackptr] = n0;
				if (h1 && t1.y < ht) todo[++stackptr] = n1;
			}
		}
		if (t) *t = ht;
		return hit;
	}
};


}