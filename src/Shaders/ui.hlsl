#pragma compile vertex vs_ui fragment fs_ui

#define gTextureCount 64

[[vk::constant_id(0)]] const uint gVertexStride = 12; // stride in bytes of gVertices. Set to 0 to generate a unit quad
[[vk::constant_id(1)]] const uint gTransformStride = 64; // stride in bytes of gTransforms. Set to 0 for screen-space transform (in pixels)
[[vk::constant_id(2)]] const float gAlphaClip = -1; // set below 0 to disable

#include <stratum.hlsli>

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

StructuredBuffer<GuiElement> gElements : register(t0, space2);
ByteAddressBuffer gVertices : register(t1, space2);
ByteAddressBuffer gTransforms : register(t2, space2); // column-major
SamplerState gSampler : register(s4, space2);
Texture2D<float4> gTextures[gTextureCount] : register(t3, space2);

[[vk::push_constant]] struct {
	float2 gScreenTexelSize;
} gPushConstants;

struct v2f {
	float4 position : SV_Position;
	float2 clipPos : TEXCOORD0;
	sample float2 texcoord : TEXCOORD1;
};

static const float2 gUnitQuad[6] = {
	float2(0,0),
	float2(1,0),
	float2(1,1),
	float2(0,1),
	float2(0,0),
	float2(1,1)
};

v2f vs_ui(uint instance : SV_InstanceID, uint index : SV_VertexID) {
	GuiElement elem = gElements[instance];

	float3 p;
	if (gVertexStride)
		p = asfloat(gVertices.Load3(index * gVertexStride));
	else
		p = float3(gUnitQuad[index%6], 0);

	// fit to elem.Rect, elem.RectZ
	p = float3(elem.Rect.xy + p.xy*elem.Rect.zw, p.z + elem.RectZ);

	v2f o;
	o.clipPos = (p.xy - elem.ClipBounds.xy) / elem.ClipBounds.zw;
	o.texcoord = elem.TextureRect.xy + p.xy*elem.TextureRect.zw;
	if (gTransformStride) {
		uint4 addrs = instance*gTransformStride + uint4(0,16,32,48);
		float4x4 transform = float4x4(gTransforms.Load4(addrs[0]), gTransforms.Load4(addrs[1]), gTransforms.Load4(addrs[2]), gTransforms.Load4(addrs[3])); 
		o.position = mul(gCamera.ViewProjection, mul(float4(p, 1.0), transform));
	} else
		o.position = float4(p.xy*gPushConstants.gScreenTexelSize*2 - 1, p.z, 1);
	return o;
}

float4 fs_ui(uint instance : SV_InstanceID, v2f i) : SV_Target0 {
	GuiElement elem = gElements[instance];
	float4 color = elem.Color;
	if (gTextureCount && elem.TextureIndex < gTextureCount)
		color *= gTextures[elem.TextureIndex].Sample(gSampler, i.texcoord);
	if (gTextureCount && elem.SdfIndex < gTextureCount) {
		float4 msdf = gTextures[elem.SdfIndex].SampleLevel(gSampler, i.texcoord, 0);
		float d = max(min(msdf.r, msdf.g), min(max(msdf.r, msdf.g), msdf.b)); // median(msdf.rgb)
		float w = fwidth(d);
		color.a *= max(0, smoothstep(0.5 - w, 0.5 + w, d));
	}
	color.a *= all(i.clipPos >= 0) * all(i.clipPos <= 1);
	if (gAlphaClip >= 0 && color.a < gAlphaClip) discard;
	return color;
}