#include "transform.hlsli"

#define LightType_Distant 0
#define LightType_Point   1
#define LightType_Spot    2

#define LightFlags_None      0
#define LightFlags_Shadowmap 1

struct LightData {
	TransformData mLightToWorld;
	ProjectionData mShadowProjection;
	float4 mShadowST;
	float3 mEmission;
	uint mType;
	uint mFlags;
	float mSpotAngleScale;  // 1/(cos(InnerAngle) - cos(OuterAngle))
	float mSpotAngleOffset; // -cos(OuterAngle) * mSpotAngleScale;
	float mShadowBias;
};
struct TextureIndices {
	uint mBaseColor;
	uint mNormal;
	uint mEmission;
	uint mMetallic;
	uint mRoughness;
	uint mOcclusion;
	uint mMetallicChannel; 
	uint mRoughnessChannel; 
	uint mOcclusionChannel; 
};
struct MaterialData {
  float4 mBaseColor;
  float3 mEmission;
	float mMetallic;
  float mRoughness;
	float mNormalScale; // scaledNormal = normalize((<sampled normal texture value> * 2.0 - 1.0) * vec3(<normal scale>, <normal scale>, 1.0))
	float mOcclusionScale; // lerp(color, color * <sampled occlusion texture value>, <occlusion strength>)
	uint mTextureIndices;
};

inline uint pack_texture_indices(TextureIndices v) {
	uint r = 0;
	r |= (v.mBaseColor&0xF) << 0;
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
inline TextureIndices unpack_texture_indices(uint v) {
	TextureIndices r;
	r.mBaseColor        = (v >>  0) & 0xF;
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
