#pragma vertex vsmain
#pragma fragment fsmain

#pragma render_queue 4000
#pragma cull false
#pragma blend alpha

#pragma static_sampler SamplerNearest filter=nearest
#pragma static_sampler Sampler

#pragma multi_compile SCREEN_SPACE

#include <include/shadercompat.h>

// per-object
[[vk::binding(BINDING_START + 0, PER_OBJECT)]] Texture2D<float> MainTexture : register(t0);
[[vk::binding(BINDING_START + 1, PER_OBJECT)]] StructuredBuffer<float4x4> Transforms : register(t1);
[[vk::binding(BINDING_START + 2, PER_OBJECT)]] StructuredBuffer<TextGlyph> Glyphs : register(t2);
[[vk::binding(BINDING_START + 3, PER_OBJECT)]] SamplerState SamplerNearest : register(s0);
[[vk::binding(BINDING_START + 4, PER_OBJECT)]] SamplerState Sampler : register(s1);

[[vk::push_constant]] cbuffer PushConstants : register(b2) {
	STRATUM_PUSH_CONSTANTS
	float4 Color;
	float4 Bounds;
	float2 ScreenSize;
	float2 Offset;
	float Depth;
	float Scale;
}

#include <include/util.hlsli>

struct v2f {
	float4 position : SV_Position;
	float4 texcoord : TEXCOORD0;
#ifndef SCREEN_SPACE
	float4 worldPos : TEXCOORD1;
#endif
};

v2f vsmain(uint id : SV_VertexId, uint instance : SV_InstanceID) {
	uint g = id / 6;
	uint c = id % 6;
	
	static const float2 offsets[6] = {
		float2(0,0),
		float2(1,0),
		float2(0,1),
		float2(1,0),
		float2(1,1),
		float2(0,1)
	};

	float2 p = Glyphs[g].Offset + Offset + Glyphs[g].Extent * offsets[c];

	v2f o;
#ifdef SCREEN_SPACE
	o.position = float4((round(p) / ScreenSize) * 2 - 1, Depth, 1);
	o.position.y = -o.position.y;
#else
	float4x4 o2w = Transforms[instance];
	o2w[0][3] += -STRATUM_CAMERA_POSITION.x * o2w[3][3];
	o2w[1][3] += -STRATUM_CAMERA_POSITION.y * o2w[3][3];
	o2w[2][3] += -STRATUM_CAMERA_POSITION.z * o2w[3][3];
	float4 worldPos = mul(o2w, float4(p * Scale, 0, 1));
	o.position = mul(STRATUM_MATRIX_VP, worldPos);
	o.worldPos = float4(worldPos.xyz, o.position.z);
#endif
	
	o.texcoord.xy = Glyphs[g].TexOffset + Glyphs[g].TexExtent * offsets[c];
	o.texcoord.zw = (p - Bounds.xy) / Bounds.zw;
	return o;
}

void fsmain(v2f i,
	out float4 color : SV_Target0,
	out float4 depthNormal : SV_Target1 ) {
	
	float2 dx = ddx(i.texcoord.xy);
	float2 dy = ddx(i.texcoord.xy);
	//dx /= 3;
	//float r = MainTexture.SampleGrad(Sampler, i.texcoord.xy - dx, dx, dy);
	//float g = MainTexture.SampleGrad(Sampler, i.texcoord.xy, dx, dy);
	//float b = MainTexture.SampleGrad(Sampler, i.texcoord.xy + dx, dx, dy);
	//color = float4(r, g, b, g);
	color = float4(1, 1, 1, MainTexture.Sample(Sampler, i.texcoord.xy));

	color *= Color;

	#ifdef SCREEN_SPACE
	depthNormal = 0;
	#else
	depthNormal = float4(normalize(cross(ddx(i.worldPos.xyz), ddy(i.worldPos.xyz))) * i.worldPos.w, 1);
	#endif
	clip(i.texcoord.zw);
	clip(1 - i.texcoord.zw);
	depthNormal.a = color.a;
}