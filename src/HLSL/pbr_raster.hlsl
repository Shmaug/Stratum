#pragma compile glslc -fshader-stage=vert -fentry-point=vs
#pragma compile glslc -fshader-stage=frag -fentry-point=fs

#include "scene.hlsli"

[[vk::constant_id(0)]] const uint gImageCount = 16;
[[vk::constant_id(1)]] const float gAlphaClip = -1; // set below 0 to disable

[[vk::binding(0)]] StructuredBuffer<TransformData> gTransforms;
[[vk::binding(1)]] StructuredBuffer<MaterialData> gMaterials;
[[vk::binding(2)]] StructuredBuffer<LightData> gLights;
[[vk::binding(3)]] Texture2DArray<float> gShadowMaps;
[[vk::binding(4)]] SamplerState gSampler;
[[vk::binding(5)]] SamplerComparisonState gShadowSampler;
[[vk::binding(6)]] Texture2D<float4> gImages[gImageCount];

[[vk::push_constant]] cbuffer {
	TransformData gWorldToCamera;
	ProjectionData gProjection;
	uint gLightCount;
	uint gMaterialIndex;
	uint gEnvironmentMap;
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
	quatf q = qmul(gWorldToCamera.mRotation, gTransforms[instanceId].mRotation);
	o.normal.xyz    = rotate_vector(q, normal);
	o.tangent.xyz   = rotate_vector(q, tangent);
	float3 bitangent = cross(o.tangent.xyz, o.normal.xyz);
	o.normal.w = bitangent.x;
	o.tangent.w = bitangent.y;
	o.posCamera.w = bitangent.z;
	o.texcoord = texcoord;
	return o;
}

static const float gDielectricSpecular = 0.04;

inline float3 fresnel(float3 specular, float VdotH) {
  return lerp(specular, 50*max3(specular), pow5(1 - VdotH));
}
inline float visibility_occlusion(float NdotL, float NdotV, float alphaRoughnessSq) {
	float GGXV = NdotL * sqrt(NdotV * NdotV * (1 - alphaRoughnessSq) + alphaRoughnessSq);
	float GGXL = NdotV * sqrt(NdotL * NdotL * (1 - alphaRoughnessSq) + alphaRoughnessSq);
	float ggx = GGXV + GGXL;
	return ggx > 0 ? 0.5 / ggx : 0;
}
inline float microfacet_distribution(float NdotH, float alphaRoughnessSq) {
	return alphaRoughnessSq / (M_PI * pow2(1 + NdotH*(NdotH*alphaRoughnessSq - NdotH)) + 0.000001);
}
class BSDF {
	float3 diffuse;
	float3 specular;
	float3 emission;
	float roughness;
		
	inline float3 shade_point(float3 Li, float3 Lo, float3 normal) {
		float alphaRoughness = pow2(roughness);
		float alphaRoughnessSq = pow2(alphaRoughness);
		float NdotL = saturate(dot(normal, Li));
		float NdotV = saturate(dot(normal, Lo));
		if (NdotL <= 0 || NdotV <= 0) return 0;
		float3 H = normalize(Li + Lo);
		float3 F = lerp(specular, 50*max3(specular), pow5(1 - saturate(dot(Lo, H))));
		float Vis = visibility_occlusion(NdotL, NdotV, alphaRoughnessSq);
		float D = microfacet_distribution(saturate(dot(normal, H)), alphaRoughnessSq);
		float3 diffuseContrib = (1 - F) * (diffuse/M_PI);
		float3 specContrib = F * Vis * D;
		return NdotL*(diffuseContrib + specContrib);
	}
};
inline BSDF make_bsdf(float3 baseColor, float metallic, float roughness, float3 emission = 0) {
	BSDF bsdf;
	bsdf.specular = lerp(gDielectricSpecular, baseColor, metallic);
	bsdf.diffuse = baseColor * (1 - gDielectricSpecular) * (1 - metallic) / max(1 - max3(bsdf.specular), 1e-5f);
	bsdf.roughness = max(.002f, roughness);
	bsdf.emission = emission;
	return bsdf;
}

float4 light_attenuation(LightData light, TransformData lightToCamera, float3 posCamera, float3 posLight) {
	float3 dir = 0;
	float atten = 0;
	float zdir = sign(light.mShadowProjection.mNear);
	if (light.mType == LIGHT_TYPE_DISTANT) {
		dir = rotate_vector(lightToCamera.mRotation, float3(0,0,-zdir));
		atten = 1;
	} else {
		float inv_len2 = 1/dot(posLight,posLight);
		dir = normalize(lightToCamera.mTranslation - posCamera);
		atten = inv_len2;
		if (light.mType & LIGHT_TYPE_SPOT) {
			PackedLightData p;
			p.v = light.mPackedData;
			atten *= smoothstep(p.cos_outer_angle(), p.cos_inner_angle(), (zdir*posLight.z)*inv_len2);
		}
	}
	return float4(dir, atten);
}

float sample_shadow(LightData light, float3 posLight) {
	float3 shadowCoord = hnormalized(project_point(light.mShadowProjection, posLight));
	shadowCoord.y = -shadowCoord.y;
	float3 ac = abs(shadowCoord);
	if (any(ac.xy < 0) || any(ac.xy > 1)) return 1;
	PackedLightData p;
	p.v = light.mPackedData;
	uint shadowIndex = p.shadow_index();
	if (shadowIndex == -1) return 1;
	if (light.mType == LIGHT_TYPE_POINT) {
		float m = max3(ac);
		if (m == ac.x) shadowIndex += (ac.x < 0) ? 1 : 0;
		if (m == ac.y) shadowIndex += (ac.y < 0) ? 3 : 2;
		if (m == ac.z) shadowIndex += (ac.z < 0) ? 5 : 4;
	}
	return gShadowMaps.SampleCmp(gShadowSampler, float3(saturate(shadowCoord.xy*.5 + .5), shadowIndex), shadowCoord.z + p.shadow_bias());
}

float4 fs(v2f i) : SV_Target0 {
	MaterialData material = gMaterials[gMaterialIndex];
	ImageIndices inds;
	inds.v = material.mImageIndices;
	float4 baseColor = float4(material.mAlbedo, 1 - material.mTransmission);
	float2 metallicRoughness = float2(material.mMetallic, material.mRoughness);
	float3 emission = material.mEmission;
	
	uint ti = inds.albedo();
	if (ti < gImageCount) baseColor *= gImages[ti].Sample(gSampler, i.texcoord);
	
	if (gAlphaClip >= 0 && baseColor.a < gAlphaClip) discard;
	
	ti = inds.metallic();
	if (ti < gImageCount)  metallicRoughness.x = saturate(metallicRoughness.x*gImages[ti].Sample(gSampler, i.texcoord)[inds.metallic_channel()]);
	ti = inds.roughness();
	if (ti < gImageCount) metallicRoughness.y = saturate(metallicRoughness.y*gImages[ti].Sample(gSampler, i.texcoord)[inds.roughness_channel()]);
	ti = inds.emission();
	if (ti < gImageCount)  emission *= gImages[ti].Sample(gSampler, i.texcoord).rgb;

	BSDF bsdf = make_bsdf(baseColor.rgb, metallicRoughness.x, metallicRoughness.y, emission);

	float3 normal = i.normal.xyz;
	ti = inds.normal();
	if (ti < gImageCount) {
		float3 bump = gImages[ti].Sample(gSampler, i.texcoord).xyz*2 - 1;
		bump.xy *= material.mNormalScale;
		normal = bump.x*i.tangent.xyz + bump.y*float3(i.normal.w, i.tangent.w, i.posCamera.w) + bump.z*i.normal.xyz;
	}
	normal = normalize(normal);
	
	float3 view = normalize(-i.posCamera.xyz);

	float3 eval = bsdf.emission;
	for (uint l = 0; l < gLightCount; l++) {
		LightData light = gLights[l];
		TransformData lightToCamera = tmul(gWorldToCamera, light.mLightToWorld);
		float3 posLight = transform_point(inverse(lightToCamera), i.posCamera);
		float4 Li = light_attenuation(light, lightToCamera, i.posCamera.xyz, posLight);
		if (Li.w > 0) {
			Li.w *= sample_shadow(light, posLight);
			eval += gLights[l].mEmission * Li.w * bsdf.shade_point(Li.xyz, view, normal);
		}
	}
	
	float occlusion = 1;
	ti = inds.occlusion();
	if (ti < gImageCount) occlusion = lerp(1, gImages[ti].Sample(gSampler, i.texcoord)[inds.occlusion_channel()].r, material.mOcclusionScale);
	
	return float4(eval, baseColor.a);
}
