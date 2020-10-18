#pragma pass forward/transparent vsmain fsmain

#pragma render_queue 4000
#pragma cull false
#pragma blend 0 add srcAlpha oneMinusSrcAlpha

#pragma multi_compile TEXTURED
#pragma multi_compile SCREEN_SPACE

#pragma static_sampler Sampler

#include <shadercompat.h>

struct GuiRect {
	float4x4 ObjectToWorld;
	float4 ScaleTranslate;
	float4 ClipBounds;
	float4 Color;

	float4 TextureST;
	uint TextureIndex;
	float Depth;
	uint pad[2];
};

[[vk::binding(BINDING_START + 0, PER_OBJECT)]] Texture2D<float4> Textures[64] : register(t0);
[[vk::binding(BINDING_START + 1, PER_OBJECT)]] StructuredBuffer<GuiRect> Rects : register(t64);
[[vk::binding(BINDING_START + 2, PER_OBJECT)]] SamplerState Sampler : register(s0);

[[vk::push_constant]] cbuffer PushConstants : register(b2) {
	STM_PUSH_CONSTANTS
	float2 ScreenSize;
}

#include <util.hlsli>

struct v2f {
	[[vk::location(0)]] float4 position : SV_Position;
	[[vk::location(1)]] float4 color : COLOR0;
	[[vk::location(2)]] float2 texcoord : TEXCOORD0;
	[[vk::location(3)]] float2 clipPos : TEXCOORD1;
	#ifdef TEXTURED
	[[vk::location(4)]] uint textureIndex : TEXCOORD2;
	#endif
};

v2f vsmain(uint index : SV_VertexID, uint instance : SV_InstanceID) {
	static const float2 positions[6] = {
		float2(0,0),
		float2(1,0),
		float2(1,1),
		float2(0,1),
		float2(0,0),
		float2(1,1)
	};

	GuiRect r = Rects[instance];

	float2 p = positions[index] * r.ScaleTranslate.xy + r.ScaleTranslate.zw;
	
	v2f o;
	#ifdef SCREEN_SPACE
	o.position = float4((p / ScreenSize) * 2 - 1, r.Depth, 1);
	#else
	o.position = mul(STRATUM_MATRIX_VP, mul(ApplyCameraTranslation(r.ObjectToWorld), float4(p, 0, 1.0)));
	#endif

	#ifdef TEXTURED
	o.textureIndex = r.TextureIndex;
	#endif

	#ifdef SCREEN_SPACE
	o.texcoord = float2(positions[index].x, 1 - positions[index].y) * r.TextureST.xy + r.TextureST.zw;
	#else
	o.texcoord = positions[index] * r.TextureST.xy + r.TextureST.zw;
	#endif

	o.clipPos = (p - r.ClipBounds.xy) / r.ClipBounds.zw;
	o.color = r.Color;
	return o;
}

float4 fsmain(v2f i) : SV_Target0 {
	float4 color = i.color;

	#ifdef TEXTURED
	#ifdef SCREEN_SPACE
	color *= Textures[i.textureIndex].SampleLevel(Sampler, i.texcoord, 0);
	#else
	color *= Textures[i.textureIndex].SampleBias(Sampler, i.texcoord, -1);
	#endif
	#endif
	
	if (color.a <= 0 || any(i.clipPos < 0) || any(i.clipPos > 1)) discard;
	return color;
}