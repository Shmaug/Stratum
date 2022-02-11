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
	BF_SET(v.data[0].x, instance_map[v.instance_index()], 0, 16);
	return v;
}

#endif // __HLSL_VERSION

#endif