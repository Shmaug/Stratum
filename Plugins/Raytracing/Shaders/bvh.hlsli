#include "rtcommon.h"

struct Ray {
	float3 Origin;
	float TMin;
	float3 Direction;
	float TMax;

	float3 InvDirection;
	uint LargestComponent;
	float2 RayTransform;
	uint pad[2];
};
struct Intersection {
	float3 Normal;
	float HitT;
	float2 Barycentrics;
	uint PrimitiveIndex;
	uint MaterialIndex;
	float Area;
	uint pad[3];
};

Ray CreateRay(float3 origin, float3 direction, float tmin, float tmax) {
	Ray ray = {};
	ray.Origin = origin;
	ray.Direction = direction;
	ray.TMin = tmin;
	ray.TMax = tmax;
	ray.InvDirection = 1.0 / direction;

	float3 ad = abs(ray.Direction);

	ray.LargestComponent = 0;
	if (ad[1] > ad[ray.LargestComponent]) ray.LargestComponent = 1;
	if (ad[2] > ad[ray.LargestComponent]) ray.LargestComponent = 2;

	if (ray.LargestComponent == 0)
		ray.RayTransform = ray.Direction.yz * ray.InvDirection.x;
	else if (ray.LargestComponent == 1)
		ray.RayTransform = ray.Direction.zx * ray.InvDirection.y;
	else
		ray.RayTransform = ray.Direction.xy * ray.InvDirection.z;
	
	return ray;
}

bool RayTriangle(Ray ray, float3 v0, float3 v1, float3 v2, out float t, out float2 bary) {
	// http://jcgt.org/published/0002/01/05/paper.pdf

		v0 -= ray.Origin;
		v1 -= ray.Origin;
		v2 -= ray.Origin;

		if (ray.LargestComponent == 0) {
			v0.xyz = v0.yzx;
			v1.xyz = v1.yzx;
			v2.xyz = v2.yzx;
		} else if (ray.LargestComponent == 1) {
			v0.xyz = v0.zxy;
			v1.xyz = v1.zxy;
			v2.xyz = v2.zxy;
		}

		float idz = ray.InvDirection[ray.LargestComponent];
		v0 = float3(v0.x - v0.z * ray.RayTransform.x, v0.y - v0.z * ray.RayTransform.y, v0.z * idz);
		v1 = float3(v1.x - v1.z * ray.RayTransform.x, v1.y - v1.z * ray.RayTransform.y, v1.z * idz);
		v2 = float3(v2.x - v2.z * ray.RayTransform.x, v2.y - v2.z * ray.RayTransform.y, v2.z * idz);

		float u = v2.x * v1.y - v2.y * v1.x;
		float v = v0.x * v2.y - v0.y * v2.x;
		float w = v1.x * v0.y - v1.y * v0.x;

		if ((u < 0 || v < 0 || w < 0) && (u > 0 || v > 0 || w > 0))
			return false; // outside triangle

		float det = u + v + w;
		if (det == 0) return false; // ray co-planar with triangle
		float inv_det = 1 / det;
		t = (u * v0.z + v * v1.z + w * v2.z) * inv_det;
		bary = float2(u, v) * inv_det;
		return t >= ray.TMin && t <= ray.TMax;
}
bool RaySphere(Ray ray, float3 p, float r, out float2 t) {
	float3 f = ray.Origin - p;
	float a = dot(ray.Direction, ray.Direction);
	float b = dot(f, ray.Direction);

	float3 l = a * f - ray.Direction * b;
	float det = a * a * r * r - dot(l, l);

	if (det < 0) {
		t = 1.#INF;
		return false;
	}

	float ra = 1 / a;
	det = sqrt(det * ra);
	t = float2(-b - det, -b + det) * ra;
	return t.y >= ray.TMin && t.x <= ray.TMax;
}
bool RayBox(Ray ray, float3 mn, float3 mx, out float2 t) {
	float3 t0 = (mn - ray.Origin) * ray.InvDirection;
	float3 t1 = (mx - ray.Origin) * ray.InvDirection;
	float3 tmin = min(t0, t1);
	float3 tmax = max(t0, t1);
	t.x = max(max(tmin.x, tmin.y), tmin.z);
	t.y = min(min(tmax.x, tmax.y), tmax.z);
	return t.x <= t.y && t.y >= ray.TMin && t.x <= ray.TMax;
}

bool IntersectScene(Ray ray, bool any, out Intersection intersection) {
	uint2 todo[32];
	int stackptr = 0;
	bool hit = false;

	todo[stackptr] = uint2(0, asuint(ray.TMax));

	BvhNode node;
	while (stackptr >= 0) {
		uint2 ni = todo[stackptr--];

		float closest = asfloat(ni.y);
		if (!isnan(closest) && !isinf(closest) && closest > ray.TMax) continue;

		node = SceneBvh[ni.x];
		if (node.RightOffset == 0) {
			for (uint o = 0; o < node.PrimitiveCount; o++) {
				uint primIndex = node.StartIndex + o;
				uint3 addr = VertexStride * Triangles.Load3(4 * 3 * primIndex);
				float3 v0 = asfloat(Vertices.Load3(addr.x));
				float3 v1 = asfloat(Vertices.Load3(addr.y));
				float3 v2 = asfloat(Vertices.Load3(addr.z));

				float2 bary;
				float ht;
				if (RayTriangle(ray, v0, v1, v2, ht, bary)) {
					ray.TMax = ht;

					intersection.HitT = ht;
					intersection.Barycentrics = bary;
					intersection.PrimitiveIndex = primIndex;
					intersection.MaterialIndex = PrimitiveMaterials[primIndex];
					intersection.Normal = cross(v2 - v0, v1 - v0);
					intersection.Area = length(intersection.Normal);
					intersection.Normal /= intersection.Area;
					intersection.Area *= 0.5;

					hit = true;
					if (any) return true;
				}
			}
		} else {
			uint n0 = ni.x + 1;
			uint n1 = ni.x + node.RightOffset;
			float3 min0 = SceneBvh[n0].Min;
			float3 max0 = SceneBvh[n0].Max;
			float3 min1 = SceneBvh[n1].Min;
			float3 max1 = SceneBvh[n1].Max;

			float2 t0, t1;
			bool h0 = RayBox(ray, min0, max0, t0);
			bool h1 = RayBox(ray, min1, max1, t1);

			if (h0) todo[++stackptr] = uint2(n0, asuint(t0.x));
			if (h1) todo[++stackptr] = uint2(n1, asuint(t1.x));
		}
	}

	return hit;
}