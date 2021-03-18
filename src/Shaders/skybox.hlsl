#pragma compile vertex vs_skybox fragment fs_skybox

[[vk::constant_id(0)]] const bool gTonemap = true;

#include <stratum.hlsli>
#include <lighting.hlsli>

SamplerState gSampler : register(s0, space2);

void vs_skybox(
	float3 vertex : POSITION,
	out float4 position : SV_Position,
	out float3 viewRay : TEXCOORD0) {
	position = mul(gCamera.Projection, float4(vertex, 1));
	viewRay = mul(vertex, (float3x3)gCamera.View);
}

float4 fs_skybox(in float3 viewRay : TEXCOORD0) : SV_Target0 {
	float3 ray = normalize(viewRay);
	float4 color = 1;
	float2 envuv = float2(atan2(ray.z, ray.x) * M_1_PI * .5 + .5, acos(ray.y) * M_1_PI);
	color.rgb = gEnvironmentTexture.SampleLevel(gSampler, envuv, 0).rgb;
	if (gTonemap) color.rgb = pow(color.rgb, 1 / 2.2);
	color.rgb *= gEnvironment.Ambient;
	return color;
}