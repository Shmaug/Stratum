#pragma compile vertex vs_pointer fragment fs_pointer

#include <stratum.hlsli>

cbuffer gPointerData : register(b0, space2) {
	float4 gColor;
	float3 gP0;
	float gWidth;
	float3 gP1;
};

struct v2f {
	float4 position : SV_Position;
	float fade : TEXCOORD0;
};

v2f vs_pointer(uint index : SV_VertexID, uint instance : SV_InstanceID) {
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
	float3 view = gCamera.Position.xyz - gP0;
	float3 right = normalize(cross(normalize(dir), normalize(view)));

	float3 worldPos = lerp(gP0, gP1, p.y) + .5 * gWidth * right * p.x;

	v2f o;
	o.position = mul(gCamera.ViewProjection, float4(worldPos, 1));
	o.fade = p.x;
	return o;
}

float4 fs_pointer(float fade : TEXCOORD0) : SV_Target0 {
	float4 color = gColor;
	float s = 1 - abs(fade);
	color.a *= s*s;
	return color;
}