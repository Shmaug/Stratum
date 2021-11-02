#ifndef SCENE_H
#define SCENE_H

#include "transform.hlsli"

#define LIGHT_TYPE_DISTANT   0
#define LIGHT_TYPE_POINT     1
#define LIGHT_TYPE_SPOT      2
#define LIGHT_TYPE_EMISSIVE_MATERIAL  3

class PackedLightData {
#ifdef __cplusplus
public:
#endif
	float4 v;

	// point/spot/distant light data
	inline float radius() { return v[0]; }
	inline void radius(float s) { v[0] = s; }
	
	// spot light data
	inline float cos_inner_angle() { return (asuint(v[1])>>16)/(float)0xFFFF; }
	inline void cos_inner_angle(float s) { v[1] = asfloat(asuint(v[1]) | ((uint(s*0xFFFF)&0xFFFF)>>16)); }
	inline float cos_outer_angle() { return (asuint(v[1])&0xFFFF)/(float)0xFFFF; }
	inline void cos_outer_angle(float s) { v[1] = asfloat(asuint(v[1]) | (uint(s*0xFFFF)&0xFFFF)); }

	// shadow map (distant/point/spot light)
	inline float shadow_bias() { return v[2]; }
	inline void shadow_bias(float s) { v[2] = s; }
	inline uint shadow_index() { return asuint(v[3]); }
	inline void shadow_index(uint i) { v[3] = asfloat(i); }

	// mesh light data
	inline uint instance_index() { return asuint(v[1]); }
	inline void instance_index(uint i) { v[1] = asfloat(i); }
	inline uint prim_count() { return asuint(v[2]); }
	inline void prim_count(uint i) { v[2] = asfloat(i); }
};

struct LightData {
	TransformData mLightToWorld;
	float3 mEmission;
	uint mType;
	ProjectionData mShadowProjection;
	float4 mPackedData;
};

class ImageIndices {
#ifdef __cplusplus
public:
#endif
	uint4 v;

	inline uint albedo() { return (v[0] >> 0 ) & 0xFF; }
	inline void albedo(uint i) { v[0] |= (i&0xFF) << 0 ; }
	inline uint normal() { return (v[0] >> 8 ) & 0xFF; }
	inline void normal(uint i) { v[0] |= (i&0xFF) << 8 ; }
	inline uint emission() { return (v[0] >> 16) & 0xFF; }
	inline void emission(uint i) { v[0] |= (i&0xFF) << 16; }
	inline uint metallic() { return (v[0] >> 24) & 0xFF; }
	inline void metallic(uint i) { v[0] |= (i&0xFF) << 24; }
	inline uint roughness() { return (v[1] >> 0 ) & 0xFF; }
	inline void roughness(uint i) { v[1] |= (i&0xFF) << 0 ; }
	inline uint occlusion() { return (v[1] >> 16) & 0xFF; }
	inline void occlusion(uint i) { v[1] |= (i&0xFF) << 16; }
	inline uint metallic_channel() 	{ return v[0] >> 30; }
	inline void metallic_channel(uint i) { v[0] |= i << 30; }
	inline uint roughness_channel() { return v[1] >> 30; }
	inline void roughness_channel(uint i) { v[1] |= i << 30; }
	inline uint occlusion_channel() { return v[2] >> 30; }
	inline void occlusion_channel(uint i) { v[2] |= i << 30; }
};
struct MaterialData {
  float3 mAlbedo;
  float mRoughness;
  float3 mEmission;
	float mMetallic;
	float3 mAbsorption;
	float mNormalScale; // scaledNormal = normalize((<sampled normal image value> * 2.0 - 1.0) * vec3(<normal scale>, <normal scale>, 1.0))
	float mOcclusionScale; // lerp(color, color * <sampled occlusion image value>, <occlusion strength>)
	float mIndexOfRefraction;
	float mTransmission;
	float pad;
	uint4 mImageIndices;
};

#endif