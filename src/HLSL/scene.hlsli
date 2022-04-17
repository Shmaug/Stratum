#ifndef SCENE_H
#define SCENE_H

#include "common.hlsli"

#include "bitfield.hlsli"
#include "transform.hlsli"

#define SAMPLE_FLAG_DEMODULATE_ALBEDO		BIT(0)
#define SAMPLE_FLAG_SAMPLE_PIXEL_AREA 		BIT(1)
#define SAMPLE_FLAG_SAMPLE_ENVIRONMENT 		BIT(2)
#define SAMPLE_FLAG_SAMPLE_EMISSIVE 		BIT(3)
#define SAMPLE_FLAG_MIS 					BIT(4)
#define SAMPLE_FLAG_STORE_PATH_VERTICES		BIT(5)
#define SAMPLE_FLAG_TRACE_LIGHT_PATHS		BIT(6)
#define SAMPLE_FLAG_UNIFORM_SPHERE_SAMPLING	BIT(7)
#define SAMPLE_FLAG_SAMPLE_RESERVOIRS		BIT(8)
#define SAMPLE_FLAG_RAY_CONE_LOD 			BIT(9)
#define SAMPLE_FLAG_ENABLE_VOLUMES 			BIT(10)
#define SAMPLE_FLAG_DIRECT_ONLY				BIT(11)
#define SAMPLE_FLAG_LIGHT_PATHS_ONLY		BIT(12)
#define SAMPLE_FLAG_CONNECT_TO_VIEWS		BIT(13)

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
#define INVALID_MATERIAL 0xFFFFFFF

#define COUNTER_ADDRESS_RAY_COUNT 0

struct InstanceData {
	// instance -> world
	TransformData transform;
	// world -> instance
	TransformData inv_transform;
	// cur world -> prev world
	TransformData motion_transform;
	// prev world -> cur world
	TransformData inv_motion_transform;

	uint4 packed;

	inline uint type() CONST_CPP { return BF_GET(packed[0], 0, 4); }
	inline uint material_address() CONST_CPP { return BF_GET(packed[0], 4, 28); }

	// mesh
	inline uint prim_count() CONST_CPP { return BF_GET(packed[1], 0, 16); }
	inline uint index_stride() CONST_CPP { return BF_GET(packed[1], 16, 4); }
	inline uint first_vertex() CONST_CPP { return packed[2]; }
	inline uint indices_byte_offset() CONST_CPP { return packed[3]; }

	// sphere
	inline float radius() CONST_CPP { return asfloat(packed[1]); }

	// volume
	inline uint volume_index() CONST_CPP { return packed[1]; }
};
inline InstanceData make_instance_transform(const TransformData objectToWorld, const TransformData prevObjectToWorld) {
	InstanceData r;
	r.transform = objectToWorld;
	r.inv_transform = objectToWorld.inverse();
	r.motion_transform = tmul(prevObjectToWorld, r.inv_transform);
	r.inv_motion_transform = r.motion_transform.inverse();
	return r;
}
inline InstanceData make_instance_triangles(const TransformData objectToWorld, const TransformData prevObjectToWorld, const uint materialAddress, const uint primCount, const uint firstVertex, const uint indexByteOffset, const uint indexStride) {
	InstanceData r = make_instance_transform(objectToWorld, prevObjectToWorld);
	r.packed = 0;
	BF_SET(r.packed[0], INSTANCE_TYPE_TRIANGLES, 0, 4);
	BF_SET(r.packed[0], materialAddress, 4, 28);
	BF_SET(r.packed[1], primCount, 0, 16);
	BF_SET(r.packed[1], indexStride, 16, 4);
	r.packed[2] = firstVertex;
	r.packed[3] = indexByteOffset;
	return r;
}
inline InstanceData make_instance_sphere(const TransformData objectToWorld, const TransformData prevObjectToWorld, const uint materialAddress, const float radius) {
	InstanceData r = make_instance_transform(objectToWorld, prevObjectToWorld);
	r.packed = 0;
	BF_SET(r.packed[0], INSTANCE_TYPE_SPHERE, 0, 4);
	BF_SET(r.packed[0], materialAddress, 4, 28);
	r.packed[1] = asuint(radius);
	return r;
}
inline InstanceData make_instance_volume(const TransformData objectToWorld, const TransformData prevObjectToWorld, const uint materialAddress, const uint volume_index) {
	InstanceData r = make_instance_transform(objectToWorld, prevObjectToWorld);
	r.packed = 0;
	BF_SET(r.packed[0], INSTANCE_TYPE_VOLUME, 0, 4);
	BF_SET(r.packed[0], materialAddress, 4, 28);
	r.packed[1] = volume_index;
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

struct ViewData {
	TransformData camera_to_world;
	TransformData world_to_camera;
	ProjectionData projection;
	uint2 image_min;
	uint2 image_max;
#ifdef __HLSL_VERSION
	inline int2 extent() { return (int2)image_max - (int2)image_min; }
#endif
};

struct VisibilityInfo {
	uint4 rng_state;
	uint3 position;
	uint instance_primitive_index;
	uint4 nz;
	float2 prev_uv;
	uint pad[2];
#ifdef __HLSL_VERSION
	
	inline uint instance_index()  { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }

	inline float3 normal()        { return unpack_normal_octahedron2(asfloat(nz.xy)); }
	inline min16float z()         { return (min16float)f16tof32(nz.z); }
	inline min16float prev_z()    { return (min16float)f16tof32(nz.z>>16); }
	inline min16float2 dz_dxy()   { return unpack_f16_2(nz.w); }

	inline void store_nz(const float3 n, const float z, const float prev_z, const float2 dz) {
		nz = uint4(asuint(pack_normal_octahedron2(n)), pack_f16_2(float2(z,prev_z)), pack_f16_2(dz));
	}
#endif
};

struct PathTracePushConstants {
	uint gViewCount;
	uint gLightCount;
	uint gEnvironmentMaterialAddress;
	float gEnvironmentSampleProbability;	
	uint gRandomSeed;
	uint gReservoirSamples;
	uint gMaxNullCollisions;
	uint gMinDepth;
	uint gMaxEyeDepth;
	uint gMaxLightDepth;
};

#ifdef __HLSL_VERSION

inline uint get_view_index(const uint2 index, StructuredBuffer<ViewData> views, const uint viewCount) {
	for (uint i = 0; i < viewCount; i++)
		if (all(index >= views[i].image_min) && all(index < views[i].image_max))
			return i;
	return -1;
}

inline float3 ray_offset(const float3 P, const float3 Ng) {
	// This function should be used to compute a modified ray start position for
	// rays leaving from a surface. This is from "A Fast and Robust Method for Avoiding
	// Self-Intersection" see https://research.nvidia.com/publication/2019-03_A-Fast-and
  const float int_scale = 256.0f;
  const int3 of_i = int3((int)(int_scale * Ng.x), (int)(int_scale * Ng.y), (int)(int_scale * Ng.z));

  const float origin = 1 / 32.0;
  const float float_scale = 1 / 65536.0;
  return float3(abs(P.x) < origin ? P.x + float_scale * Ng.x : asfloat(asint(P.x) + ((P.x < 0) ? -of_i.x : of_i.x)),
                abs(P.y) < origin ? P.y + float_scale * Ng.y : asfloat(asint(P.y) + ((P.y < 0) ? -of_i.y : of_i.y)),
                abs(P.z) < origin ? P.z + float_scale * Ng.z : asfloat(asint(P.z) + ((P.z < 0) ? -of_i.z : of_i.z)));
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

#endif

#endif