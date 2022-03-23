#ifndef VISBUFFER_H
#define VISBUFFER_H

#define VISIBILITY_BUFFER_COUNT 3

#ifdef __HLSL_VERSION

#ifdef DECLARE_VISIBILITY_BUFFERS
DECLARE_VISIBILITY_BUFFERS
#else
RWTexture2D<uint4> gVisibility[VISIBILITY_BUFFER_COUNT];
RWTexture2D<uint4> gPrevVisibility[VISIBILITY_BUFFER_COUNT];
#endif

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