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
	uint mImageIndices;
};
struct ImageIndices {
	uint mAlbedo;
	uint mNormal;
	uint mEmission;
	uint mMetallic;
	uint mRoughness;
	uint mOcclusion;
	uint mMetallicChannel; 
	uint mRoughnessChannel; 
	uint mOcclusionChannel; 
};

inline uint pack_image_indices(ImageIndices v) {
	uint r = 0;
	r |= (v.mAlbedo   &0xF) << 0;
	r |= (v.mNormal   &0xF) << 4;
	r |= (v.mEmission &0xF) << 8;
	r |= (v.mMetallic &0xF) << 12;
	r |= (v.mRoughness&0xF) << 16;
	r |= (v.mOcclusion&0xF) << 20;
	r |= (v.mMetallicChannel &0x3) << 24;
	r |= (v.mRoughnessChannel&0x3) << 26;
	r |= (v.mOcclusionChannel&0x3) << 28;
	return r;
}
inline ImageIndices unpack_image_indices(uint v) {
	ImageIndices r;
	r.mAlbedo           = (v >>  0) & 0xF;
	r.mNormal           = (v >>  4) & 0xF;
	r.mEmission         = (v >>  8) & 0xF;
	r.mMetallic         = (v >> 12) & 0xF;
	r.mRoughness        = (v >> 16) & 0xF;
	r.mOcclusion        = (v >> 20) & 0xF;
	r.mMetallicChannel  = (v >> 24) & 0x3;
	r.mRoughnessChannel = (v >> 26) & 0x3;
	r.mOcclusionChannel = (v >> 28) & 0x3;
	return r;
}

#endif