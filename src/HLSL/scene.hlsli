#ifndef SCENE_H
#define SCENE_H

#include "transform.hlsli"

#define LIGHT_TYPE_DISTANT   0
#define LIGHT_TYPE_POINT     1
#define LIGHT_TYPE_SPOT      2
#define LIGHT_TYPE_EMISSIVE_MATERIAL  4

struct LightData {
	TransformData mLightToWorld;
	float3 mEmission;
	uint mType;
	float mCosInnerAngle; // also area firstIndex
	float mCosOuterAngle; // also area triangleCount
	float mShadowBias;    // also area emissionImage
	int mShadowIndex;     // also area instanceIndex; point light radius
	ProjectionData mShadowProjection;
};

class ImageIndices {
#ifdef __cplusplus
public:
#endif
	uint4 v;

	inline uint albedo() 						{ return (v[0] >> 0 ) & 0xFF; }
	inline uint normal() 						{ return (v[0] >> 8 ) & 0xFF; }
	inline uint emission() 					{ return (v[0] >> 16) & 0xFF; }
	inline uint metallic() 					{ return (v[0] >> 24) & 0xFF; }
	inline uint roughness() 				{ return (v[1] >> 0 ) & 0xFF; }
	inline uint occlusion() 				{ return (v[1] >> 16) & 0xFF; }
	inline uint metallic_channel() 	{ return v[0] >> 30; }
	inline uint roughness_channel() { return v[1] >> 30; }
	inline uint occlusion_channel() { return v[2] >> 30; }
	
	inline void albedo(uint i) 						{ v[0] |= (i&0xFF) << 0 ; }
	inline void normal(uint i) 						{ v[0] |= (i&0xFF) << 8 ; }
	inline void emission(uint i) 					{ v[0] |= (i&0xFF) << 16; }
	inline void metallic(uint i) 					{ v[0] |= (i&0xFF) << 24; }
	inline void roughness(uint i) 				{ v[1] |= (i&0xFF) << 0 ; }
	inline void occlusion(uint i) 				{ v[1] |= (i&0xFF) << 16; }
	inline void metallic_channel(uint i) 	{ v[0] |= i << 30; }
	inline void roughness_channel(uint i) { v[1] |= i << 30; }
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