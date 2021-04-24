#include "include/transform.hlsli"

#define LIGHT_ATTEN_DIRECTIONAL 0
#define LIGHT_ATTEN_DISTANCE 1
#define LIGHT_ATTEN_ANGULAR 2
#define LIGHT_USE_SHADOWMAP 4

struct LightData {
	float3 Emission;
	uint Flags;
	TransformData LightToWorld;
	ProjectionData ShadowProjection;
	float4 ShadowST;
	float SpotAngleScale;  // 1/(cos(InnerAngle) - cos(OuterAngle))
	float SpotAngleOffset; // -cos(OuterAngle) * SpotAngleScale;
	float ShadowBias;
	uint pad;
};
struct MaterialData {
	float4 gTextureST;
	float4 gBaseColor;
	float3 gEmission;
	float gMetallic;
	float gRoughness;
	float gBumpStrength;
	uint pad[2];
};

#ifndef __cplusplus

#pragma compile vertex vs_pbr fragment fs_pbr

#include "include/bsdf.hlsli"

StructuredBuffer<LightData> gLights					: register(t0, space0);
Texture2D<float> gShadowAtlas 							: register(t1, space0);
SamplerComparisonState gShadowSampler 			: register(s2, space0);

StructuredBuffer<MaterialData> gMaterials		: register(t0, space1);
Texture2D<float4> gBaseColorTexture 				: register(t1, space1);
Texture2D<float4> gNormalTexture 						: register(t2, space1);
Texture2D<float2> gMetallicRoughnessTexture : register(t3, space1);
SamplerState gSampler 											: register(s4, space1);

StructuredBuffer<TransformData> gInstanceTransforms : register(t0, space2);

[[vk::constant_id(0)]] const float gAlphaClip = -1; // set below 0 to disable

struct push_constants {
	TransformData WorldToCamera;
	ProjectionData Projection;
	uint LightCount;
};
[[vk::push_constant]] const push_constants gPushConstants = { TRANSFORM_I, PROJECTION_I, 0 };

struct v2f {
	float4 position : SV_Position;
	float4 pack0 : TEXCOORD0; // normal (xyz) texcoord.x (w)
	float4 pack1 : TEXCOORD1; // tangent (xyz) texcoord.y (w)
	float3 cameraPos : TEXCOORD2;
};

v2f vs_pbr(inout uint instanceId : SV_InstanceID, float3 vertex : POSITION, float3 normal : NORMAL, float2 texcoord : TEXCOORD0, float3 tangent : TANGENT) {
	v2f o;
	TransformData objectToCamera = tmul(gPushConstants.WorldToCamera, gInstanceTransforms[instanceId]);
	o.cameraPos = transform_point(objectToCamera, vertex);
	o.position = project_point(gPushConstants.Projection, o.cameraPos);
	o.pack0 = float4(transform_vector(objectToCamera, normal ), texcoord.x);
	o.pack1 = float4(transform_vector(objectToCamera, tangent), texcoord.y);
	return o;
}

float4 SampleLight(LightData light, float3 cameraPos) {
	TransformData lightToCamera = tmul(gPushConstants.WorldToCamera, light.LightToWorld);
	float3 lightCoord = transform_point(inverse(lightToCamera), cameraPos);

	float4 r = float4(0,0,0,1);
	
	if (light.Flags & LIGHT_ATTEN_DISTANCE) {
		r.xyz = normalize(lightToCamera.Translation - cameraPos);
		r.w *= 1/dot(lightCoord,lightCoord);
		if (light.Flags & LIGHT_ATTEN_ANGULAR)
			r.w *= pow2(saturate(lightCoord.z/length(lightCoord)) * light.SpotAngleScale + light.SpotAngleOffset);
	} else
		r.xyz = rotate_vector(lightToCamera.Rotation, float3(0,0,-1));

	if ((light.Flags & LIGHT_USE_SHADOWMAP) && r.w > 0) {
		float3 shadowCoord = hnormalized(project_point(light.ShadowProjection, lightCoord));
		if (all(abs(shadowCoord.xy) < 1)) {
			shadowCoord.y = -shadowCoord.y;
			r.w *= gShadowAtlas.SampleCmp(gShadowSampler, saturate(shadowCoord.xy*.5 + .5)*light.ShadowST.xy + light.ShadowST.zw, shadowCoord.z);
		}
	}
	
	return r;
}

float4 fs_pbr(v2f i, uint instanceId : SV_InstanceID) : SV_Target0 {
	MaterialData material = gMaterials[instanceId];

	float2 uv = float2(i.pack0.w, i.pack1.w)*material.gTextureST.xy + material.gTextureST.zw;
	float4 color = material.gBaseColor * gBaseColorTexture.Sample(gSampler, uv);

	if (gAlphaClip >= 0 && color.a < gAlphaClip) discard;

	float4 bump = gNormalTexture.Sample(gSampler, uv);
	
	float3 view 	 = normalize(-i.cameraPos);
	float3 normal  = normalize(i.pack0.xyz);
	float3 tangent = normalize(i.pack1.xyz);
	normal = normalize(mul(float3(1,-1,material.gBumpStrength)*(bump.xyz*2-1), float3x3(tangent, normalize(cross(normal, tangent)), normal)));

	float2 metallicRoughness = gMetallicRoughnessTexture.Sample(gSampler, uv);
	BSDF bsdf = make_BSDF(color.rgb, metallicRoughness.x*material.gMetallic, metallicRoughness.y*material.gRoughness, material.gEmission);

	float3 eval = bsdf.emission;
	for (uint l = 0; l < gPushConstants.LightCount; l++) {
		float4 Li = SampleLight(gLights[l], i.cameraPos);
		float3 sfc = ShadePoint(bsdf, Li.xyz, view, normal);
		eval += gLights[l].Emission * Li.w * sfc;
	}
	return float4(eval, color.a);
}

#endif