#pragma pass forward/opaque vsmain fsmain

#pragma render_queue 5000
#pragma cull false
#pragma blend 0 add srcAlpha oneMinusSrcAlpha

#include <include/shadercompat.h>

[[vk::push_constant]] cbuffer PushConstants : register(b1) {
	STM_PUSH_CONSTANTS
	float4 Color;
	float3 P0;
	float Width;
	float3 P1;
}

#include <include/util.hlsli>

struct v2f {
	float4 position : SV_Position;
	float fade : TEXCOORD0;
};

v2f vsmain(uint index : SV_VertexID, uint instance : SV_InstanceID) {
	static const float2 positions[6] = {
		float2(-1, 0),
		float2( 1, 0),
		float2( 1, 1),
		
		float2(-1, 1),
		float2(-1, 0),
		float2( 1, 1)
	};
	float2 p = positions[index];

	float3 dir = P1 - P0;
	float3 view = STRATUM_CAMERA_POSITION - P0;
	float3 right = normalize(cross(normalize(dir), normalize(view)));

	float3 worldPos = lerp(P0, P1, p.y) + .5 * Width * right * p.x;

	v2f o;
	o.position = mul(STRATUM_MATRIX_VP, float4(worldPos - STRATUM_CAMERA_POSITION, 1));
	o.fade = p.x;
	return o;
}

float4 fsmain(v2f i) : SV_Target0 {
	float4 color = Color;
	color.a *= (1 - abs(i.fade)) * (1 - abs(i.fade));
	return color;
}