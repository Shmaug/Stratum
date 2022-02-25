#ifndef VISBUFFER_H
#define VISBUFFER_H

#define VISIBILITY_BUFFER_COUNT 3

struct PathBounceState {
	uint4 rng;
	float3 ray_origin;
	uint radius_spread;
	float3 position;
	uint instance_primitive_index;
	float2 bary;
	uint2 throughput_eta_scale;

#ifdef __HLSL_VERSION
	inline uint instance_index() { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }
	inline min16float3 throughput() { return min16float3(unpack_f16_2(throughput_eta_scale[0]), f16tof32(throughput_eta_scale[1])); }
	inline min16float eta_scale() { return (min16float)f16tof32(throughput_eta_scale[1]>>16); }
	inline min16float radius() { return (min16float)f16tof32(radius_spread); }
	inline min16float spread() { return (min16float)f16tof32(radius_spread>>16); }
	inline RayDifferential ray() {
		RayDifferential r;
		r.origin    = ray_origin;
		r.direction = normalize(position - ray_origin);
		r.t_min = 0;
		r.t_max = 1.#INF;
		r.radius = radius();
		r.spread = spread();
		return r;
	}
#endif
};

#ifdef __HLSL_VERSION

RWTexture2D<uint4> gVisibility[VISIBILITY_BUFFER_COUNT];
RWTexture2D<uint4> gPrevVisibility[VISIBILITY_BUFFER_COUNT];

RWStructuredBuffer<PathBounceState> gPathStates;
inline void store_path_bounce_state(out PathBounceState p,
																	  const uint4 rng,
																	  const float3 throughput,
																	  const float eta_scale,
																	  const float3 position,
																	  const float2 bary,
																	  const float3 ray_origin,
																	  const float radius,
																	  const float spread,
																	  const uint instance_primitive_index) {
	p.rng = rng;
	p.ray_origin = ray_origin;
	p.radius_spread = pack_f16_2(float2(radius, spread));
	p.position = position;
	p.instance_primitive_index = instance_primitive_index;
	p.bary = bary;
	p.throughput_eta_scale[0] = pack_f16_2(throughput.xy);
	p.throughput_eta_scale[1] = pack_f16_2(float2(throughput.z, eta_scale));
}

struct VisibilityInfo {
	uint4 data[VISIBILITY_BUFFER_COUNT];

	inline uint4 rng_seed()       { return data[0]; }
	inline uint instance_index()  { return BF_GET(data[1].x, 0, 16); }
	inline uint primitive_index() { return BF_GET(data[1].x, 16, 16); }
	inline float2 bary()          { return asfloat(data[1].yz); }
	inline min16float3 normal()   { return unpack_normal_octahedron(data[1].w); }
	inline min16float z()         { return unpack_f16_2(data[2].x).x; }
	inline min16float prev_z()    { return unpack_f16_2(data[2].x).y; }
	inline min16float2 dz_dxy()   { return unpack_f16_2(data[2].y); }
	inline float2 prev_uv()       { return asfloat(data[2].zw); }
};
inline void store_visibility(const uint2 index,
														 const uint4 rng_seed,
														 const uint instance_index,
														 const uint primitive_index,
														 const float2 bary,
														 const float3 normal,
														 const float z,
														 const float prev_z,
														 const float2 dz_dxy,
														 const float2 prev_uv) {
	uint4 data[VISIBILITY_BUFFER_COUNT];
	data[0] = rng_seed;
	BF_SET(data[1].x, instance_index, 0, 16);
	BF_SET(data[1].x, primitive_index, 16, 16);
	data[1].yz = asuint(bary);
	data[1].w = pack_normal_octahedron(normal);
	data[2].x = pack_f16_2(float2(z, prev_z));
	data[2].y = pack_f16_2(dz_dxy);
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

#endif // __HLSL_VERSION

#endif