#pragma compile vertex vs_ui
#pragma compile fragment fs_ui

#include <stratum.hlsli>

SamplerState gFontsSampler : register(s0, space2);
Texture2D<float4> gFontsTexture: register(t3, space2);

[[vk::push_constant]] struct {
	uint gScale;
	float2 gTranslate;
} gPushConstants;

struct v2f {
	float4 color : COLOR;
	float2 uv : TEXCOORD0;
};

v2f vs_ui(uint instance : SV_InstanceID, float2 aPos : POSITION, float2 aUV : TEXCOORD0, float4 aColor : COLOR) {
	v2f o;
	o.color = aColor;
	o.uv = aUV;
	return o;
}

float4 fs_ui(uint instance : SV_InstanceID, v2f i) : SV_Target0 {
	float4 color = i.color * gFontsTexture.Sample(gFontsSampler, i.uv);
	return color;
}