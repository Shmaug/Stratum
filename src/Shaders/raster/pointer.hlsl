#pragma compile vertex vsmain
#pragma compile fragment fsmain

#include <stratum.hlsli>

cbuffer gPointerData : register(b0, space2) {
	float4 gColor;
	float3 gP0;
	float gWidth;
	float3 gP1;
};

[[vk::push_constant]] struct {
	uint gStereoEye;
} gPushConstants;

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

	float3 dir = gP1 - gP0;
	float3 view = STRATUM_CAMERA_POSITION - gP0;
	float3 right = normalize(cross(normalize(dir), normalize(view)));

	float3 worldPos = lerp(gP0, gP1, p.y) + .5 * gWidth * right * p.x;

	v2f o;
	o.position = mul(STRATUM_MATRIX_VP, float4(worldPos, 1));
	o.fade = p.x;
	return o;
}

float4 fsmain(v2f i) : SV_Target0 {
	float4 color = gColor;
	color.a *= (1 - abs(i.fade)) * (1 - abs(i.fade));
	return color;
}