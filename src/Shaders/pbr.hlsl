#include "include/transform.hlsli"

#define LIGHT_ATTEN_DIRECTIONAL 0
#define LIGHT_ATTEN_DISTANCE 1
#define LIGHT_ATTEN_ANGULAR 2
#define LIGHT_USE_SHADOWMAP 4

#define gTextureCount 16

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

struct MaterialData {
  float4 mBaseColor;
  float3 mEmission;
	float mMetallic;
  float mRoughness;
	float mNormalScale; // scaledNormal = normalize((<sampled normal texture value> * 2.0 - 1.0) * vec3(<normal scale>, <normal scale>, 1.0))
	float mOcclusionScale; // lerp(color, color * <sampled occlusion texture value>, <occlusion strength>)
	uint pad;
};

#ifndef __cplusplus

#pragma compile vs_6_6 vs
#pragma compile ps_6_6 fs

#include "include/bsdf.hlsli"

StructuredBuffer<TransformData> gTransforms;
StructuredBuffer<MaterialData> gMaterials;
StructuredBuffer<LightData> gLights;
Texture2D<float> gShadowAtlas;
SamplerState gSampler;
SamplerComparisonState gShadowSampler;
Texture2D<float4> gTextures[gTextureCount];

[[vk::constant_id(0)]] const float gAlphaClip = -1; // set below 0 to disable

struct push_constants_t {
	TransformData WorldToCamera;
	ProjectionData Projection;
	uint LightCount;

	uint MaterialIndex;
	uint BaseColorTexture;
	uint NormalTexture;
	uint MetallicRoughnessTexture;
	uint OcclusionTexture;
	uint EmissionTexture;
};
[[vk::push_constant]] const push_constants_t gPushConstants = { TRANSFORM_I, PROJECTION_I, 0, ~0, ~0, ~0, ~0, ~0, ~0 };

struct v2f {
	float4 position : SV_Position;
	float4 normal : NORMAL;
	float4 tangent : TANGENT;
	float4 cameraPos : TEXCOORD0; // vertex position in camera space (camera at 0,0,0)
	float2 texcoord : TEXCOORD1;
};

v2f vs(
	float3 vertex : POSITION,
	float3 normal : NORMAL,
	float3 tangent : TANGENT,
	float2 texcoord : TEXCOORD,
	uint instanceId : SV_InstanceID) {
	v2f o;
	TransformData objectToCamera = tmul(gPushConstants.WorldToCamera, gTransforms[instanceId]);
	o.cameraPos.xyz = transform_point(objectToCamera, vertex);
	o.position = project_point(gPushConstants.Projection, o.cameraPos.xyz);
	o.normal.xyz = transform_vector(objectToCamera, normal);
	o.tangent.xyz = transform_vector(objectToCamera, tangent);
	float3 bitangent = cross(o.normal.xyz, o.tangent.xyz);
	o.cameraPos.w = bitangent.x;
	o.normal.w = bitangent.y;
	o.tangent.w = bitangent.z;
	o.texcoord = texcoord;
	return o;
}

float4 SampleLight(LightData light, float3 cameraPos) {
	TransformData lightToCamera = tmul(gPushConstants.WorldToCamera, light.mLightToWorld);
	float3 lightCoord = transform_point(inverse(lightToCamera), cameraPos);

	float4 r = float4(0,0,0,1);
	
	if (light.mFlags & LIGHT_ATTEN_DISTANCE) {
		r.xyz = normalize(lightToCamera.Translation - cameraPos);
		r.w *= 1/dot(lightCoord,lightCoord);
		if (light.mFlags & LIGHT_ATTEN_ANGULAR)
			r.w *= pow2(saturate(lightCoord.z/length(lightCoord)) * light.mSpotAngleScale + light.mSpotAngleOffset);
	} else
		r.xyz = rotate_vector(lightToCamera.Rotation, float3(0,0,-1));

	if ((light.mFlags & LIGHT_USE_SHADOWMAP) && r.w > 0) {
		float3 shadowCoord = hnormalized(project_point(light.mShadowProjection, lightCoord));
		if (all(abs(shadowCoord.xy) < 1)) {
			shadowCoord.y = -shadowCoord.y;
			r.w *= gShadowAtlas.SampleCmp(gShadowSampler, saturate(shadowCoord.xy*.5 + .5)*light.mShadowST.xy + light.mShadowST.zw, shadowCoord.z);
		}
	}
	
	return r;
}

float4 fs(v2f i) : SV_Target0 {
	MaterialData material = gMaterials[gPushConstants.MaterialIndex];
	float4 baseColor = material.mBaseColor;
	float3 view = normalize(-i.cameraPos.xyz);
	float3 normal = i.normal.xyz;
	float2 metallicRoughness = float2(material.mMetallic, material.mRoughness);
	float3 emission = material.mEmission;
	
	if (gPushConstants.BaseColorTexture < gTextureCount)
		baseColor *= gTextures[gPushConstants.BaseColorTexture].Sample(gSampler, i.texcoord);
	
	if (gAlphaClip >= 0 && baseColor.a < gAlphaClip) discard;
	
	if (gPushConstants.NormalTexture < gTextureCount) {
		float3 bitangent = float3(i.cameraPos.w, i.normal.w, i.tangent.w);
		normal = mul((gTextures[gPushConstants.NormalTexture].Sample(gSampler, i.texcoord).xyz*2-1) * float3(material.mNormalScale.xx, 1), float3x3(i.tangent.xyz, bitangent, i.normal.xyz));
	}

	if (gPushConstants.MetallicRoughnessTexture < gTextureCount)
		metallicRoughness *= gTextures[gPushConstants.MetallicRoughnessTexture].Sample(gSampler, i.texcoord).rg;
	if (gPushConstants.EmissionTexture < gTextureCount)
		emission *= gTextures[gPushConstants.EmissionTexture].Sample(gSampler, i.texcoord).rgb;

	BSDF bsdf = make_BSDF(baseColor.rgb, metallicRoughness.x, metallicRoughness.y, emission);

	normal = normalize(normal);

	float3 eval = bsdf.emission;
	for (uint l = 0; l < gPushConstants.LightCount; l++) {
		float4 Li = SampleLight(gLights[l], i.cameraPos.xyz);
		float3 sfc = ShadePoint(bsdf, Li.xyz, view, normal);
		eval += gLights[l].mEmission * Li.w * sfc;
	}
	if (gPushConstants.OcclusionTexture < gTextureCount)
		eval = lerp(eval, eval * gTextures[gPushConstants.OcclusionTexture].Sample(gSampler, i.texcoord).r, material.mOcclusionScale);
	return float4(eval, baseColor.a);
}

#endif