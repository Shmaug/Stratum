#include "include/transform.hlsli"

#define LIGHT_ATTEN_DIRECTIONAL 0
#define LIGHT_ATTEN_DISTANCE 1
#define LIGHT_ATTEN_ANGULAR 2
#define LIGHT_USE_SHADOWMAP 4

struct LightData {
	TransformData mLightToWorld;
	ProjectionData mShadowProjection;
	float4 mShadowST;
	float3 mEmission;
	uint mFlags;
	float mSpotAngleScale;  // 1/(cos(InnerAngle) - cos(OuterAngle))
	float mSpotAngleOffset; // -cos(OuterAngle) * mSpotAngleScale;
	float mShadowBias;
	uint pad;
};
struct TextureIndices {
	uint mBaseColor;
	uint mNormal;
	uint mEmission;
	uint mMetallic;
	uint mMetallicChannel; 
	uint mRoughness;
	uint mRoughnessChannel; 
	uint mOcclusion;
	uint mOcclusionChannel; 
};
struct MaterialData {
  float4 mBaseColor;
  float3 mEmission;
	float mMetallic;
  float mRoughness;
	float mNormalScale; // scaledNormal = normalize((<sampled normal texture value> * 2.0 - 1.0) * vec3(<normal scale>, <normal scale>, 1.0))
	float mOcclusionScale; // lerp(color, color * <sampled occlusion texture value>, <occlusion strength>)
	uint pad;
	uint4 mTextureIndices;
};

inline uint4 pack_texture_indices(TextureIndices v) {
	uint4 r;
	r[0] = (v.mMetallic  << 24) | (v.mEmission         << 16) | (v.mNormal    << 8) | v.mBaseColor;
	r[1] = (v.mOcclusion << 24) | (v.mRoughnessChannel << 16) | (v.mRoughness << 8) | v.mMetallicChannel;
	r[2] = v.mOcclusionChannel;
	return r;
}
inline TextureIndices unpack_texture_indices(uint4 v) {
	TextureIndices r;
	r.mBaseColor = v[0] & 0xFF;
	r.mNormal    = (v[0] >> 8) & 0xFF;
	r.mEmission  = (v[0] >> 16) & 0xFF;
	r.mMetallic  = (v[0] >> 24) & 0xFF;
	r.mMetallicChannel  = v[1] & 0xFF;
	r.mRoughness        = (v[1] >> 8) & 0xFF;
	r.mRoughnessChannel = (v[1] >> 16) & 0xFF;
	r.mOcclusion        = (v[1] >> 24) & 0xFF;
	r.mOcclusionChannel = v[2];
	return r;
}

#ifndef __cplusplus

#pragma compile -D -S vert -e vs
#pragma compile -D -S frag -e fs

#include "include/bsdf.hlsli"

[[vk::constant_id(0)]] const uint gTextureCount = 16;
[[vk::constant_id(1)]] const float gAlphaClip = -1; // set below 0 to disable

[[vk::binding(0)]] StructuredBuffer<TransformData> gTransforms;
[[vk::binding(1)]] StructuredBuffer<MaterialData> gMaterials;
[[vk::binding(2)]] StructuredBuffer<LightData> gLights;
[[vk::binding(3)]] Texture2D<float> gShadowMap;
[[vk::binding(4)]] SamplerState gSampler;
[[vk::binding(5)]] SamplerComparisonState gShadowSampler;
[[vk::binding(6)]] Texture2D<float4> gTextures[gTextureCount];

[[vk::push_constant]] cbuffer {
	TransformData gWorldToCamera;
	ProjectionData gProjection;
	uint gLightCount;
	uint gMaterialIndex;
};

struct v2f {
	float4 position : SV_Position;
	float4 normal : NORMAL;
	float4 tangent : TANGENT;
	float4 posCamera : TEXCOORD0; // vertex position in camera space
	float2 texcoord : TEXCOORD1;
};

v2f vs(
	float3 vertex : POSITION,
	float3 normal : NORMAL,
	float3 tangent : TANGENT,
	float2 texcoord : TEXCOORD,
	uint instanceId : SV_InstanceID) {
	v2f o;
	TransformData objectToCamera = tmul(gWorldToCamera, gTransforms[instanceId]);
	o.posCamera.xyz = transform_point(objectToCamera, vertex);
	o.position = project_point(gProjection, o.posCamera.xyz);
	o.normal.xyz = transform_vector(objectToCamera, normal);
	o.tangent.xyz = transform_vector(objectToCamera, tangent);
	float3 bitangent = cross(o.normal.xyz, o.tangent.xyz);
	o.normal.w = bitangent.x;
	o.tangent.w = bitangent.y;
	o.posCamera.w = bitangent.z;
	o.texcoord = texcoord;
	return o;
}

float4 SampleLight(LightData light, float3 posCamera) {
	TransformData lightToCamera = tmul(gWorldToCamera, light.mLightToWorld);
	float3 lightCoord = transform_point(inverse(lightToCamera), posCamera);

	float4 r = float4(0,0,0,1);
	
	if (light.mFlags & LIGHT_ATTEN_DISTANCE) {
		r.xyz = normalize(lightToCamera.Translation - posCamera);
		r.w *= 1/dot(lightCoord,lightCoord);
		if (light.mFlags & LIGHT_ATTEN_ANGULAR)
			r.w *= pow2(saturate(lightCoord.z/length(lightCoord)) * light.mSpotAngleScale + light.mSpotAngleOffset);
	} else
		r.xyz = rotate_vector(lightToCamera.Rotation, float3(0,0,-1));

	if ((light.mFlags & LIGHT_USE_SHADOWMAP) && r.w > 0) {
		float3 shadowCoord = hnormalized(project_point(light.mShadowProjection, lightCoord));
		if (all(abs(shadowCoord.xy) < 1)) {
			shadowCoord.y = -shadowCoord.y;
			r.w *= gShadowMap.SampleCmp(gShadowSampler, saturate(shadowCoord.xy*.5 + .5)*light.mShadowST.xy + light.mShadowST.zw, shadowCoord.z);
		}
	}
	
	return r;
}

float4 fs(v2f i) : SV_Target0 {
	MaterialData material = gMaterials[gMaterialIndex];
	TextureIndices inds = unpack_texture_indices(material.mTextureIndices);
	float4 baseColor = material.mBaseColor;
	float2 metallicRoughness = float2(material.mMetallic, material.mRoughness);
	float3 emission = material.mEmission;
	
	if (inds.mBaseColor < gTextureCount) baseColor *= gTextures[inds.mBaseColor].Sample(gSampler, i.texcoord);
	
	if (gAlphaClip >= 0 && baseColor.a < gAlphaClip) discard;
	
	if (inds.mMetallic < gTextureCount)  metallicRoughness.x = saturate(metallicRoughness.x*gTextures[inds.mMetallic].Sample(gSampler, i.texcoord)[inds.mMetallicChannel]);
	if (inds.mRoughness < gTextureCount) metallicRoughness.x = saturate(metallicRoughness.x*gTextures[inds.mRoughness].Sample(gSampler, i.texcoord)[inds.mRoughnessChannel]);
	if (inds.mEmission < gTextureCount)  emission *= gTextures[inds.mEmission].Sample(gSampler, i.texcoord).rgb;

	BSDF bsdf = make_BSDF(baseColor.rgb, metallicRoughness.x, metallicRoughness.y, emission);

	float3 normal = i.normal.xyz;
	if (inds.mNormal < gTextureCount) {
		float3 bump = gTextures[inds.mNormal].Sample(gSampler, i.texcoord).xyz*2 - 1;
		bump.xy *= material.mNormalScale;
		normal = bump.x*i.tangent.xyz + bump.y*float3(i.normal.w, i.tangent.w, i.posCamera.w) + bump.z*i.normal.xyz;
	}
	normal = normalize(normal);
	
	float3 view = normalize(-i.posCamera.xyz);

	float3 eval = bsdf.emission;
	for (uint l = 0; l < gLightCount; l++) {
		float4 Li = SampleLight(gLights[l], i.posCamera.xyz);
		if (Li.w > 0) eval += gLights[l].mEmission * Li.w * ShadePoint(bsdf, Li.xyz, view, normal);
	}

	if (inds.mOcclusion < gTextureCount)
		eval = lerp(eval, eval * gTextures[inds.mOcclusion].Sample(gSampler, i.texcoord)[inds.mOcclusionChannel].r, material.mOcclusionScale);
	
	return float4(eval, baseColor.a);
}

#endif