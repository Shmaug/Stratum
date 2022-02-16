#ifndef VISBUFFER_H
#define VISBUFFER_H

#define VISIBILITY_BUFFER_COUNT 3

#ifdef __HLSL_VERSION

RWTexture2D<uint4> gVisibility[VISIBILITY_BUFFER_COUNT];
RWTexture2D<uint4> gPrevVisibility[VISIBILITY_BUFFER_COUNT];

struct VisibilityInfo {
	uint4 data[VISIBILITY_BUFFER_COUNT];

	inline uint4 rng_seed() { return data[0]; }
	inline uint instance_index() { return BF_GET(data[1].x, 0, 16); }
	inline uint primitive_index() { return BF_GET(data[1].x, 16, 16); }
	inline float2 bary()    { return asfloat(data[1].yz); }
	inline min16float3 normal()  { return unpack_normal_octahedron(data[1].w); }
	inline float z()        { return f16tof32(BF_GET(data[2].x, 0, 16)); }
	inline float prev_z()   { return f16tof32(BF_GET(data[2].x, 16, 16)); }
	inline float dz_dx()    { return f16tof32(BF_GET(data[2].y, 0, 16)); }
	inline float dz_dy()    { return f16tof32(BF_GET(data[2].y, 16, 16)); }
	inline float2 prev_uv() { return asfloat(data[2].zw); }
};
inline void store_visibility(const uint2 index,
														 const uint4 rng_seed,
														 const uint instance_index,
														 const uint primitive_index,
														 const float2 bary,
														 const float3 normal,
														 const float z,
														 const float prev_z,
														 const differential dz,
														 const float2 prev_uv) {
	uint4 data[VISIBILITY_BUFFER_COUNT];
	data[0] = rng_seed;
	BF_SET(data[1].x, instance_index, 0, 16);
	BF_SET(data[1].x, primitive_index, 16, 16);
	data[1].yz = asuint(bary);
	data[1].w = pack_normal_octahedron(normal);
	BF_SET(data[2].x, f32tof16(z), 0, 16);
	BF_SET(data[2].x, f32tof16(prev_z), 16, 16);
	BF_SET(data[2].y, f32tof16(dz.dx), 0, 16);
	BF_SET(data[2].y, f32tof16(dz.dy), 16, 16);
	data[2].zw = asuint(prev_uv);

	for (uint i = 0; i < VISIBILITY_BUFFER_COUNT; i++)
		gVisibility[i][index] = data[i];
}
inline VisibilityInfo load_visibility(const uint2 index) {
	VisibilityInfo v;
	for (uint i = 0; i < VISIBILITY_BUFFER_COUNT; i++)
		v.data[i] = gVisibility[i][index];
	return v;
}
inline VisibilityInfo load_prev_visibility(const uint2 prev_index, StructuredBuffer<uint> instance_map) {
	VisibilityInfo v;
	for (uint i = 0; i < VISIBILITY_BUFFER_COUNT; i++)
		v.data[i] = gPrevVisibility[i][prev_index];
	const uint mapped_instance = instance_map[v.instance_index()];
	BF_SET(v.data[1].x, mapped_instance, 0, 16);
	return v;
}


struct PathBounceState {
	uint4 rng;
	float3 ray_origin;
	uint packed_ray_direction;
	uint3 packed_dP;
	uint instance_primitive_index;
	uint3 packed_dD;
	uint pad;
	float2 bary_or_z;
	uint2 packed_throughput;

	inline uint instance_index() { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }
	inline float3 throughput() { return float3(unpack_f16_2(packed_throughput[0]), f16tof32(packed_throughput[1])); }
	inline float eta_scale() { return f16tof32(packed_throughput[1]>>16); }
	inline RayDifferential ray() {
		RayDifferential r;
		r.origin    = ray_origin;
		r.direction = unpack_normal_octahedron(packed_ray_direction);
		r.t_min = 0;
		r.t_max = 1.#INF;
		r.dP.dx = float3(unpack_f16_2(packed_dP[0]), f16tof32(packed_dP[2]));
		r.dP.dy = float3(unpack_f16_2(packed_dP[1]), f16tof32(packed_dP[2]>>16));
		r.dD.dx = float3(unpack_f16_2(packed_dD[0]), f16tof32(packed_dD[2]));
		r.dD.dy = float3(unpack_f16_2(packed_dD[1]), f16tof32(packed_dD[2]>>16));
		return r;
	}
};
inline void store_path_bounce_state(out PathBounceState p, const uint4 rng, const float3 throughput, const float eta_scale, const float2 bary_or_z, const RayDifferential ray, const uint instance_primitive_index) {
	p.rng = rng;
	p.ray_origin = ray.origin;
	p.packed_ray_direction = pack_normal_octahedron(ray.direction);
	p.packed_dP[0] = pack_f16_2(ray.dP.dx.xy);
	p.packed_dP[1] = pack_f16_2(ray.dP.dy.xy);
	p.packed_dP[2] = pack_f16_2(float2(ray.dP.dx.z, ray.dP.dy.z));
	p.instance_primitive_index = instance_primitive_index;
	p.packed_dD[0] = pack_f16_2(ray.dD.dx.xy);
	p.packed_dD[1] = pack_f16_2(ray.dD.dy.xy);
	p.packed_dD[2] = pack_f16_2(float2(ray.dD.dx.z, ray.dD.dy.z));
	p.bary_or_z = bary_or_z;
	p.packed_throughput[0] = pack_f16_2(throughput.xy);
	p.packed_throughput[1] = pack_f16_2(float2(throughput.z, eta_scale));
}

RWStructuredBuffer<PathBounceState> gPathStates;

#endif // __HLSL_VERSION

#endif