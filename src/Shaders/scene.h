#ifndef SCENE_H
#define SCENE_H

#include "common.h"
#include "bitfield.h"
#include "transform.h"

#ifdef __HLSL__
#define PNANOVDB_HLSL
#include "../extern/nanovdb/PNanoVDB.h"
#endif

#define INSTANCE_TYPE_TRIANGLES 0
#define INSTANCE_TYPE_SPHERE 1
#define INSTANCE_TYPE_VOLUME 2

#define BVH_FLAG_NONE 0
#define BVH_FLAG_TRIANGLES BIT(0)
#define BVH_FLAG_SPHERES BIT(1)
#define BVH_FLAG_VOLUME BIT(2)
#define BVH_FLAG_EMITTER BIT(3)

#define INVALID_INSTANCE 0xFFFF
#define INVALID_PRIMITIVE 0xFFFF

#define gImageCount 4096
#define gVolumeCount 8

struct InstanceData {
	uint4 packed;

	inline uint type() CONST_CPP { return BF_GET(packed[0], 0, 4); }
	inline uint material_address() CONST_CPP { return BF_GET(packed[0], 4, 28); }
	inline uint light_index() CONST_CPP { return BF_GET(packed[1], 0, 12); }

	// mesh
	inline uint prim_count() CONST_CPP { return BF_GET(packed[1], 12, 16); }
	inline uint index_stride() CONST_CPP { return BF_GET(packed[1], 28, 4); }
	inline uint first_vertex() CONST_CPP { return packed[2]; }
	inline uint indices_byte_offset() CONST_CPP { return packed[3]; }

	// sphere
	inline float radius() CONST_CPP { return asfloat(packed[2]); }

	// volume
	inline uint volume_index() CONST_CPP { return packed[2]; }
};

inline TransformData make_instance_motion_transform(const TransformData instance_inv_transform, const TransformData prevObjectToWorld) { return tmul(prevObjectToWorld, instance_inv_transform); }
inline InstanceData make_instance_triangles(const uint materialAddress, const uint primCount, const uint firstVertex, const uint indexByteOffset, const uint indexStride) {
	InstanceData r;
	r.packed = 0;
	BF_SET(r.packed[0], INSTANCE_TYPE_TRIANGLES, 0, 4);
	BF_SET(r.packed[0], materialAddress, 4, 28);
	BF_SET(r.packed[1], -1, 0, 12);
	BF_SET(r.packed[1], primCount, 12, 16);
	BF_SET(r.packed[1], indexStride, 28, 4);
	r.packed[2] = firstVertex;
	r.packed[3] = indexByteOffset;
	return r;
}
inline InstanceData make_instance_sphere(const uint materialAddress, const float radius) {
	InstanceData r;
	r.packed = 0;
	BF_SET(r.packed[0], INSTANCE_TYPE_SPHERE, 0, 4);
	BF_SET(r.packed[0], materialAddress, 4, 28);
	BF_SET(r.packed[1], -1, 0, 12);
	r.packed[2] = asuint(radius);
	return r;
}
inline InstanceData make_instance_volume(const uint materialAddress, const uint volume_index) {
	InstanceData r;
	r.packed = 0;
	BF_SET(r.packed[0], INSTANCE_TYPE_VOLUME, 0, 4);
	BF_SET(r.packed[0], materialAddress, 4, 28);
	BF_SET(r.packed[1], -1, 0, 12);
	r.packed[2] = volume_index;
	return r;
}

struct PackedVertexData {
	float3 position;
	float u;
	float3 normal;
	float v;
	float4 tangent;

	inline float2 uv() { return float2(u, v); }
};

struct RayDifferentialFull {
	float3 origin_dx, origin_dy;
	float3 direction_dx, direction_dy;
#ifdef __HLSL__
	SLANG_MUTATING
	inline void transfer(const float hit_t) {
		origin_dx += hit_t;
		origin_dy += hit_t;
	}
	SLANG_MUTATING
	inline void transfer(const float3 center, const float radius, const float hit_t) {
		const float2 tx = ray_sphere(origin_dx, direction_dx, center, radius);
		origin_dx += direction_dx * (tx.x > 0 ? tx.x : tx.y > 0 ? tx.y : hit_t);
		const float2 ty = ray_sphere(origin_dy, direction_dy, center, radius);
		origin_dy += direction_dy * (ty.x > 0 ? ty.x : ty.y > 0 ? ty.y : hit_t);
	}
	SLANG_MUTATING
	inline void transfer(const float3 center, const float3 normal) {
		origin_dx += ray_plane(origin_dx - center, direction_dx, normal);
		origin_dy += ray_plane(origin_dy - center, direction_dy, normal);
	}
	SLANG_MUTATING
	inline void reflect_(const float3 n) {
		direction_dx = reflect(direction_dx, n);
		direction_dy = reflect(direction_dy, n);
	}
	SLANG_MUTATING
	inline void refract_(const float3 n, const float eta) {
		direction_dx = refract(direction_dx, n, eta);
		direction_dy = refract(direction_dy, n, eta);
	}
#endif
};
struct RayDifferential {
	float radius;
	float spread;
#ifdef __HLSL__
	SLANG_MUTATING
	inline void transfer(const float t) {
		radius += spread*t;
	}
	SLANG_MUTATING
	inline void reflect(const float mean_curvature, const float roughness) {
		const float spec_spread = spread + 2 * mean_curvature * radius;
		const float diff_spread = 0.2;
		spread = max(0, lerp(spec_spread, diff_spread, roughness));
	}
	SLANG_MUTATING
	inline void refract(const float mean_curvature, const float roughness, const float eta) {
		const float spec_spread = (spread + 2 * mean_curvature * radius) / eta;
		const float diff_spread = 0.2;
		spread = max(0, lerp(spec_spread, diff_spread, roughness));
	}
#endif
};

struct ViewData {
	ProjectionData projection;
	int2 image_min;
	int2 image_max;
	inline int2 extent() CONST_CPP { return image_max - image_min; }
#ifdef __cplusplus
	inline bool test_inside(const int2 p) const { return (p >= image_min).all() && (p < image_max).all(); }
#endif
#ifdef __HLSL__
	inline bool test_inside(const int2 p) { return all(p >= image_min) && all(p < image_max); }
	inline float image_plane_dist() { return abs(image_max.y - image_min.y) / (2 * tan(projection.vertical_fov/2)); }
	inline float sensor_pdfW(const float cos_theta) {
		//return 1 / (cos_theta / pow2(image_plane_dist() / cos_theta));
		return pow2(image_plane_dist()) / pow3(cos_theta);
	}
#endif
};

struct VisibilityInfo {
	uint instance_primitive_index;
	uint packed_normal;
	uint packed_z;
	uint packed_dz;

	inline uint instance_index()  { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }

#ifdef __HLSL__
	inline float3 normal()   { return unpack_normal_octahedron(packed_normal); }
	inline float z()         { return f16tof32(packed_z); }
	inline float prev_z()    { return f16tof32(packed_z>>16); }
	inline float2 dz_dxy()   { return unpack_f16_2(packed_dz); }
#endif
};

#ifdef __HLSL__

inline uint get_view_index(const uint2 index, StructuredBuffer<ViewData> views, const uint viewCount) {
	for (uint i = 0; i < viewCount; i++)
		if (all(index >= views[i].image_min) && all(index < views[i].image_max))
			return i;
	return -1;
}

inline uint3 load_tri_(ByteAddressBuffer indices, uint indexByteOffset, uint indexStride, uint primitiveIndex) {
	const uint offsetBytes = indexByteOffset + primitiveIndex*3*indexStride;
	uint3 tri;
	if (indexStride == 2) {
		// https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingSimpleLighting/Raytracing.hlsl
		const uint dwordAlignedOffset = offsetBytes & ~3;
		const uint2 four16BitIndices = indices.Load2(dwordAlignedOffset);
		if (dwordAlignedOffset == offsetBytes) {
				tri.x = four16BitIndices.x & 0xffff;
				tri.y = (four16BitIndices.x >> 16) & 0xffff;
				tri.z = four16BitIndices.y & 0xffff;
		} else {
				tri.x = (four16BitIndices.x >> 16) & 0xffff;
				tri.y = four16BitIndices.y & 0xffff;
				tri.z = (four16BitIndices.y >> 16) & 0xffff;
		}
	} else
		tri = indices.Load3(offsetBytes);
	return tri;
}
inline uint3 load_tri(ByteAddressBuffer indices, const InstanceData instance, uint primitiveIndex) {
	return instance.first_vertex() + load_tri_(indices, instance.indices_byte_offset(), instance.index_stride(), primitiveIndex);
}

#endif // __HLSL__

#endif