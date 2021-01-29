#pragma compile vertex vs_pbr fragment fs_pbr_depth
#pragma compile vertex vs_pbr fragment fs_pbr

[[vk::constant_id(0)]] const bool gAlphaClip = true;
[[vk::constant_id(1)]] const uint gTextureCount = 64;

#include <stratum.hlsli>
#include <lighting.hlsli>

StructuredBuffer<InstanceData> gInstances : register(t0, space1);

Texture2D<float4> gBaseColorTexture 					: register(t0, space2);
Texture2D<float4> gNormalTexture 							: register(t1, space2);
Texture2D<float2> gMetallicRoughnessTexture 	: register(t2, space2);
SamplerState gSampler 												: register(s0, space2);
cbuffer gMaterial : register(b0, space2) {
	float4 gTextureST;
	float4 gBaseColor;
	float3 gEmission;
	float gMetallic;
	float gRoughness;
	float gBumpStrength;
	float gAlphaCutoff;
	uint pad;
};

[[vk::push_constant]] struct {
	uint gStereoEye;
} gPushConstants;


class DisneyBSDF : BSDF {
	float3 diffuse;
	float3 specular;
	float3 emission;
	float perceptualRoughness;
	float roughness;
	float occlusion;

	float3 Evaluate(float3 Li, float3 Lo, float3 position, float3 normal) {
		return saturate(dot(Li, normal));
	}
};

struct v2f {
	float4 position : SV_Position;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float2 texcoord : TEXCOORD0;
	float4 cameraPos : TEXCOORD1;
};

v2f vs_pbr(
	float3 vertex : POSITION,
	float3 normal : NORMAL,
	float3 tangent : TANGENT,
	float2 texcoord : TEXCOORD0,
	uint instanceId : SV_InstanceID ) {
	
	InstanceData instance = gInstances[instanceId];
	float4 cameraPos = mul(instance.Transform, float4(vertex, 1.0));

	v2f o;
	o.position = mul(gCamera.View[gPushConstants.gStereoEye], cameraPos);
	o.normal = mul(float4(normal, 1), instance.InverseTransform).xyz;
	o.tangent = mul(float4(tangent, 1), instance.InverseTransform).xyz;
	o.texcoord = texcoord*gTextureST.xy + gTextureST.zw;
	o.cameraPos = float4(cameraPos.xyz, o.position.z);
	return o;
}

float4 fs_pbr(v2f i) : SV_Target0 {
	float3 view = normalize(-i.cameraPos.xyz);

	float4 color = gBaseColor * gBaseColorTexture.Sample(gSampler, i.texcoord);
	float4 bump = gNormalTexture.Sample(gSampler, i.texcoord);
	float2 metallicRoughness = gMetallicRoughnessTexture.Sample(gSampler, i.texcoord);

	float occlusion = 1.0;

	float3 normal = normalize(i.normal);
	if (dot(i.normal, view) < 0) normal = -normal;

	float3 tangent = normalize(i.tangent);
	normal = mul(bump.xyz*2-1, float3x3(tangent, normalize(cross(normal, tangent)), normal));

	DisneyBSDF bsdf;
	bsdf.diffuse = color.rgb;
	bsdf.specular = color.rgb * metallicRoughness.x;
	bsdf.emission = gEmission;
	bsdf.perceptualRoughness = metallicRoughness.y;
	bsdf.roughness = max(.002, bsdf.perceptualRoughness * bsdf.perceptualRoughness);
	bsdf.occlusion = occlusion;

	float3 eval = Shade(bsdf, i.cameraPos.xyz, normal, view);

	return float4(eval, color.a);
}

float fs_pbr_depth(float4 position : SV_Position, in float2 texcoord : TEXCOORD2) : SV_Target0 {
	if (gAlphaClip && gBaseColorTexture.Sample(gSampler, texcoord).a * gBaseColor.a < gAlphaCutoff) discard;
	return position.z;
}