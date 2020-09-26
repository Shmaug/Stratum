#pragma pass forward/opaque vsmain fsmain

#pragma render_queue 0
#pragma blend 0 false
#pragma cull false
#pragma zwrite false
#pragma ztest false

#pragma multi_compile ENVIRONMENT_TEXTURE_HDR

#pragma static_sampler Sampler maxAnisotropy=0 addressMode=clampEdge maxLod=0

#include <shadercompat.h>
#include <math.hlsli>

[[vk::binding(BINDING_START, PER_MATERIAL)]] SamplerState Sampler : register(s0);

[[vk::push_constant]] cbuffer PushConstants : register(b3) {
	STM_PUSH_CONSTANTS
}

#include <util.hlsli>

void vsmain(
	float3 vertex : POSITION,
	out float4 position : SV_Position,
	out float3 viewRay : TEXCOORD0) {
	position = mul(STRATUM_MATRIX_P, float4(vertex, 1));
	viewRay = mul(vertex, (float3x3)STRATUM_MATRIX_V);
}

float4 fsmain(in float3 viewRay : TEXCOORD0) : SV_Target0 {
	float3 ray = normalize(viewRay);
	float4 color = 1;
	float2 envuv = float2(atan2(ray.z, ray.x) * INV_PI * .5 + .5, acos(ray.y) * INV_PI);
	color.rgb = EnvironmentTexture.SampleLevel(Sampler, envuv, 0).rgb;
	#ifdef ENVIRONMENT_TEXTURE_HDR
	color.rgb = pow(color.rgb, 1 / 2.2);
	#endif
	color.rgb *= Lighting.AmbientLight;
	return color;
}