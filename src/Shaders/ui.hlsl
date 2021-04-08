#pragma compile fragment fs_ui

#include "include/transform.hlsli"

struct GuiElement {
	float4 Rect;
	float4 ClipBounds;
	float4 Color;
	float RectZ;
	uint pad;
	uint TextureIndex; // optional
	uint SdfIndex; // optional
	float4 TextureRect;
};

#define gTextureCount 64

StructuredBuffer<GuiElement> gElements : register(t0, space2);
SamplerState gSampler : register(s4, space2);
Texture2D<float4> gTextures[gTextureCount] : register(t3, space2);

[[vk::constant_id(0)]] const float gAlphaClip = -1; // set below 0 to disable

float4 fs_ui(uint instance : SV_InstanceID, float2 clipPos : TEXCOORD0, sample float2 texcoord : TEXCOORD1) : SV_Target0 {
	GuiElement elem = gElements[instance];
	float4 color = elem.Color;
	if (gTextureCount && elem.TextureIndex < gTextureCount)
		color *= gTextures[elem.TextureIndex].Sample(gSampler, texcoord);
	if (gTextureCount && elem.SdfIndex < gTextureCount) {
		float4 msdf = gTextures[elem.SdfIndex].SampleLevel(gSampler, texcoord, 0);
		float d = max(min(msdf.r, msdf.g), min(max(msdf.r, msdf.g), msdf.b)); // median(msdf.rgb)
		float w = fwidth(d);
		color.a *= max(0, smoothstep(0.5 - w, 0.5 + w, d));
	}
	color.a *= all(clipPos >= 0) * all(clipPos <= 1);
	if (gAlphaClip >= 0 && color.a < gAlphaClip) discard;
	return color;
}