#include "pbr.hlsli"

#ifndef __cplusplus
#pragma compile glslc -fshader-stage=vert -fentry-point=vs
#pragma compile glslc -fshader-stage=frag -fentry-point=fs

#include "bsdf.hlsli"

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
	float3 vertex   : POSITION,
	float3 normal   : NORMAL,
	float3 tangent  : TANGENT,
	float2 texcoord : TEXCOORD,
	uint instanceId : SV_InstanceID) {
	v2f o;
	o.posCamera.xyz = transform_point(gWorldToCamera, transform_point(gTransforms[instanceId], vertex));
	o.position      = project_point(gProjection, o.posCamera.xyz);
	quatf q = qmul(gWorldToCamera.Rotation, gTransforms[instanceId].Rotation);
	o.normal.xyz    = rotate_vector(q, normal);
	o.tangent.xyz   = rotate_vector(q, tangent);
	float3 bitangent = cross(o.tangent.xyz, o.normal.xyz);
	o.normal.w = bitangent.x;
	o.tangent.w = bitangent.y;
	o.posCamera.w = bitangent.z;
	o.texcoord = texcoord;
	return o;
}

float4 SampleLight(LightData light, float3 posCamera) {
	TransformData lightToCamera = tmul(gWorldToCamera, light.mLightToWorld);
	float3 lightCoord = transform_point(inverse(lightToCamera), posCamera);
	float3 dir = 0;
	float atten = 0;
	float zdir = sign(light.mShadowProjection.Near);
	if (light.mType == LightType_Distant) {
		dir = rotate_vector(lightToCamera.Rotation, float3(0,0,zdir));
		atten = 1;
	} else {
		float len = 1/dot(lightCoord,lightCoord)
		dir = normalize(lightToCamera.Translation - posCamera);
		atten = len;
		if (light.mFlags & LightType_Spot)
			atten *= pow2(light.mSpotAngleScale*saturate(sqrt(len)*zdir*lightCoord.z) + light.mSpotAngleOffset);
	}
	if (atten > 0 && (light.mFlags & LightFlags_Shadowmap) == LightFlags_Shadowmap) {
		float3 shadowCoord = hnormalized(project_point(light.mShadowProjection, lightCoord));
		if (all(abs(shadowCoord.xy) < 1)) {
			shadowCoord.y = -shadowCoord.y;
			atten *= gShadowMap.SampleCmp(gShadowSampler, saturate(shadowCoord.xy*.5 + .5)*light.mShadowST.xy + light.mShadowST.zw, shadowCoord.z);
		}
	}
	return float4(dir, atten);
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