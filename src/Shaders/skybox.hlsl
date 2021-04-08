#pragma compile vertex vs_skybox fragment fs_skybox

#include "include/transform.hlsli"

[[vk::constant_id(0)]] const bool gTonemap = true;

struct PushConstants {
	ProjectionData Projection;
	quatf Rotation;
};
[[vk::push_constant]] const PushConstants gPushConstants = { PROJECTION_I, QUATF_I };

Texture2D<float4> gEnvironmentTexture : register(t0, space2);
SamplerState gSampler : register(s1, space2);

void vs_skybox(
	float3 vertex : POSITION,
	out float4 position : SV_Position,
	out float3 viewRay : TEXCOORD0) {
	position = project_point(gPushConstants.Projection, vertex);
	viewRay = rotate_vector(gPushConstants.Rotation, vertex);
}

float4 fs_skybox(in float3 viewRay : TEXCOORD0) : SV_Target0 {
	float4 color = 1;
	float3 ray = normalize(viewRay);
	float2 envuv = float2(atan2(ray.z, ray.x) * M_1_PI * .5 + .5, acos(ray.y) * M_1_PI);
	color.rgb = gEnvironmentTexture.SampleLevel(gSampler, envuv, 0).rgb;
	if (gTonemap) color.rgb = pow(color.rgb, 1/2.2);
	return color;
}