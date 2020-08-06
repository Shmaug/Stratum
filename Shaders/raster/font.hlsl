#pragma pass forward/transparent vsmain fsmain

#pragma multi_compile SCREEN_SPACE
#pragma render_queue 4000
#pragma cull false
#pragma sample_shading true
#pragma blend 0 add srcAlpha oneMinusSrcAlpha add srcAlpha oneMinusSrcAlpha
#pragma static_sampler Sampler

#include <include/shadercompat.h>

[[vk::binding(BINDING_START + 0, PER_OBJECT)]] StructuredBuffer<GlyphRect> Glyphs : register(t0);
[[vk::binding(BINDING_START + 1, PER_OBJECT)]] StructuredBuffer<float4x4> Transforms : register(t1);
[[vk::binding(BINDING_START + 2, PER_OBJECT)]] SamplerState Sampler : register(s1);
[[vk::binding(BINDING_START + 3, PER_OBJECT)]] Texture2D<float4> SDFs[8] : register(t2);

[[vk::push_constant]] cbuffer PushConstants : register(b0) {
	STM_PUSH_CONSTANTS
	float4 Color;
	float4 ClipBounds;
	float2 ScreenSize;
	float Depth;
	float Smoothing;
	uint SdfIndex;
};

#include <include/util.hlsli>

float Winding(float2 p1, float2 p2, float2 p3) { return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y); }
bool InTriangle(float2 p, float2 p0, float2 p1, float2 p2) {
	float d1 = Winding(p, p0, p1);
	float d2 = Winding(p, p1, p2);
	float d3 = Winding(p, p2, p0);
	return ((d1 > 0) && (d2 > 0) && (d3 > 0)) || ((d1 < 0) && (d2 < 0) && (d3 < 0));
}
bool InCurve(float2 p, float2 p0, float2 p1, float2 cp) {
	float2 v0 = p1 - p0;
	float2 v1 = cp - p0;
	float2 v2 = p - p0;

	float dot00 = dot(v0, v0);
	float dot01 = dot(v0, v1);
	float dot02 = dot(v0, v2);
	float dot11 = dot(v1, v1);
	float dot12 = dot(v1, v2);
	
	float invDenom = 1.0 / (dot00 * dot11 - dot01 * dot01);
	float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
	float v = (dot00 * dot12 - dot01 * dot02) * invDenom;
	float w = 1 - u - v;
	float2 coord = float2(v/2 + w, w);
	return (u > 0) && (v > 0) && (w > 0) && (coord.x*coord.x - coord.y < 0);
}
float2 LineClosestPoint(float2 a, float2 b) {
  float2 ba = b - a;
  return a + ba*saturate(-dot(a, ba)/dot(ba, ba));
}
float det2x2(float2 a, float2 b) { return a.x*b.y - b.x*a.y; }
float2 CurveClosestPoint(float2 b0, float2 b1, float2 b2) {
  float a = det2x2(b0, b2);
	float b = 2*det2x2(b1, b0);
	float d = 2*det2x2(b2, b1);
  float f = b*d-a*a;
  float2 d21 = b2 - b1;
	float2 d10 = b1 - b0;
	float2 d20 = b2 - b0;
  float2 gf = 2.0*(b*d21 + d*d10 + a*d20);
  gf = float2(gf.y, -gf.x);
  float2 pp = -f*gf/dot(gf, gf);
  float2 d0p = b0-pp;
  float ap = det2x2(d0p, d20);
	float bp = 2.0*det2x2(d10, d0p);
  float t = saturate((ap + bp)/(2.0*a + b + d));
  return lerp(lerp(b0, b1, t), lerp(b1, b2, t), t);
}

struct v2f {
	float4 position : SV_Position;
	sample float2 texcoord :TEXCOORD0;
	sample float2 clipPos : TEXCOORD1;
};

v2f vsmain(uint id : SV_VertexId, uint instance : SV_InstanceID) {
	static const float2 offsets[6] = {
		float2(0, 1),
		float2(0, 0),
		float2(1, 0),

		float2(0, 1),
		float2(1, 0),
		float2(1, 1)
	};

	v2f o;
	
	GlyphRect glyph = Glyphs[id/6];
	float2 pos = glyph.Offset + glyph.Extent*offsets[id%6];
	o.texcoord = offsets[id%6];
	o.texcoord = glyph.TextureST.xy*o.texcoord + glyph.TextureST.zw;
	o.clipPos = (pos - ClipBounds.xy) / ClipBounds.zw;

	#ifdef SCREEN_SPACE
		o.position = float4((pos / ScreenSize) * 2 - 1, Depth, 1);
	#else
		float4x4 o2w = Transforms[instance];
		o2w[0][3] += -STRATUM_CAMERA_POSITION.x * o2w[3][3];
		o2w[1][3] += -STRATUM_CAMERA_POSITION.y * o2w[3][3];
		o2w[2][3] += -STRATUM_CAMERA_POSITION.z * o2w[3][3];
		o.position = mul(STRATUM_MATRIX_VP, mul(o2w, float4(pos, 0, 1)));
	#endif
	return o;
}

float median(float3 v) { return max(min(v.r, v.g), min(max(v.r, v.g), v.b)); }

float4 fsmain(v2f i) : SV_Target0 {
	float4 msdf = SDFs[SdfIndex].SampleLevel(Sampler, i.texcoord, 0);
	float d = median(msdf.rgb);
	float w = fwidth(d);
	float4 color = Color;
	color.a *= smoothstep(0.5 - w, 0.5 + w, d);
	if (color.a <= 0 || any(i.clipPos < 0) || any(i.clipPos > 1)) discard;
	return color;
}