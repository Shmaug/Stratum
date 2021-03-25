#pragma compile vertex vs_pbr fragment fs_pbr_depth
#pragma compile vertex vs_pbr fragment fs_pbr

[[vk::constant_id(0)]] const bool gAlphaClip = true;

#include <bsdf.hlsli>

ConstantBuffer<MaterialData> gMaterial 			: register(b0, space1);
Texture2D<float4> gBaseColorTexture 				: register(t1, space1);
Texture2D<float4> gNormalTexture 						: register(t2, space1);
Texture2D<float2> gMetallicRoughnessTexture : register(t3, space1);
SamplerState gSampler 											: register(s4, space1);

StructuredBuffer<InstanceData> gInstances 	: register(t0, space2);

struct v2f {
	float4 position : SV_Position;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float2 texcoord : TEXCOORD0;
	float4 cameraPos : TEXCOORD1; // camera-space
};

v2f vs_pbr(
	float3 vertex : POSITION,
	float3 normal : NORMAL,
	float3 tangent : TANGENT,
	float2 texcoord : TEXCOORD0,
	uint instanceId : SV_InstanceID ) {
	
	InstanceData instance = gInstances[instanceId];
	float3 cameraPos = hnormalize(mul(instance.Transform, homogeneous(vertex))) - gCamera.Position;

	v2f o;
	o.position = mul(gCamera.ViewProjection, homogeneous(cameraPos));
	o.normal = mul(homogeneous(normal), instance.InverseTransform).xyz;
	o.tangent = mul(homogeneous(tangent), instance.InverseTransform).xyz;
	o.texcoord = texcoord*gMaterial.gTextureST.xy + gMaterial.gTextureST.zw;
	o.cameraPos = float4(cameraPos, o.position.z / o.position.w);
	return o;
}

float4 fs_pbr(v2f i) : SV_Target0 {
	float3 view = normalize(-i.cameraPos.xyz);

	float4 color = gMaterial.gBaseColor * gBaseColorTexture.Sample(gSampler, i.texcoord);
	float2 metallicRoughness = gMetallicRoughnessTexture.Sample(gSampler, i.texcoord);
	float4 bump = gNormalTexture.Sample(gSampler, i.texcoord);
	BSDF bsdf = make_BSDF(color.rgb, metallicRoughness.x*gMaterial.gMetallic, metallicRoughness.y*gMaterial.gRoughness, gMaterial.gEmission);

	float3 normal = normalize(i.normal);
	float3 tangent = normalize(i.tangent);
	normal = normalize(mul(float3(1,1,gMaterial.gBumpStrength)*bump.xyz*2-1, float3x3(tangent, normalize(cross(normal, tangent)), normal)));

	float3 eval = bsdf.emission;
	float3 Le,Li;
	for (uint l = 0; l < gCamera.LightCount; l++)
		if (SampleLight(gLights[l], i.cameraPos.xyz, Le, Li))
			eval += Le * Evaluate(bsdf, Li, view, normal, i.cameraPos.xyz);
	return float4(eval, color.a);
}

float fs_pbr_depth(float4 position : SV_Position, in float2 texcoord : TEXCOORD2) : SV_Target0 {
	if (gAlphaClip && gBaseColorTexture.Sample(gSampler, texcoord).a * gMaterial.gBaseColor.a < gMaterial.gAlphaCutoff) discard;
	return position.z;
}