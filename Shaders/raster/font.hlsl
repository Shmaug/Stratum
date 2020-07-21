#pragma vertex vsmain
#pragma fragment fsmain

#pragma multi_compile SCREEN_SPACE

#pragma render_queue 4000
#pragma cull false
#pragma blend alpha

#pragma static_sampler Sampler

#include <include/shadercompat.h>

//[[vk::binding(BINDING_START + 0, PER_OBJECT)]] Texture2D<float4> SrcTexture : register(t0);
[[vk::binding(BINDING_START + 1, PER_OBJECT)]] StructuredBuffer<TextGlyph> Glyphs : register(t0);
[[vk::binding(BINDING_START + 2, PER_OBJECT)]] StructuredBuffer<GlyphVertex> GlyphVertices : register(t1);
[[vk::binding(BINDING_START + 3, PER_OBJECT)]] StructuredBuffer<float4x4> Transforms : register(t2);
[[vk::binding(BINDING_START + 4, PER_OBJECT)]] SamplerState Sampler : register(s1);

[[vk::push_constant]] cbuffer PushConstants : register(b1) {
	STRATUM_PUSH_CONSTANTS
	float4 Color;
	float4 ClipBounds;
	float2 ScreenSize;
	uint GlyphOffset;
	float Depth;
}

#include <include/util.hlsli>

inline float sqr(float x) { return x*x; }

struct v2f {
	float4 position : SV_Position;
	float4 worldPos : TEXCOORD0;
	float2 shapeCoord :TEXCOORD1;
	float2 referencePoint :TEXCOORD2;
	float2 canvasPos : TEXCOORD3;
	uint startVertex : TEXCOORD4;
	uint lastVertex : TEXCOORD5;
};

float Winding(float2 p1, float2 p2, float2 p3) { return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y); }
uint InTriangle(float2 p, float2 p0, float2 p1, float2 p2) {
	float d1 = Winding(p, p0, p1);
	float d2 = Winding(p, p1, p2);
	float d3 = Winding(p, p2, p0);
	return ((d1 > 0) & (d2 > 0) & (d3 > 0)) | ((d1 < 0) & (d2 < 0) & (d3 < 0));
}

uint InCurve(float2 p, float2 p0, float2 p1, float2 cp) {
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
	return (u >= 0) & (v >= 0) & (w >= 0) & (coord.x*coord.x - coord.y <= 0);
}
float Fill(float2 coord, float2 ref, uint startVertex, uint lastVertex) {
	float2 p0 = ref;
	uint winding = 0;
	for (uint j = startVertex; j < lastVertex; j++) {
		GlyphVertex v = GlyphVertices[j];
		float2 p1 = v.Endpoint;
		uint w = 0;
		if (v.Degree > 0) {
			w += InTriangle(coord, p0, p1, ref);
			if (v.Degree == 2) w += InTriangle(coord, p0, p1, v.ControlPoint) & InCurve(coord, p0, p1, v.ControlPoint);
		}
		winding += w;
		p0 = p1;
	}
	return winding % 2;
}

v2f vsmain(uint id : SV_VertexId, uint instance : SV_InstanceID) {
	static const float2 offsets[6] = {
		float2(0, 0),
		float2(1, 0),
		float2(0, 1),
		float2(1, 0),
		float2(1, 1),
		float2(0, 1)
	};

	TextGlyph glyph = Glyphs[GlyphOffset + id / 6];
	float2 p = glyph.Offset + glyph.Extent * offsets[id % 6];

	v2f o;
	o.startVertex = glyph.StartVertex;
	o.lastVertex = glyph.StartVertex + glyph.VertexCount;
	o.referencePoint = glyph.ShapeOffset - 3;
	o.shapeCoord = glyph.ShapeOffset + glyph.ShapeExtent*offsets[id % 6] - 0.5;
	#ifdef SCREEN_SPACE
		o.position = float4((round(p) / ScreenSize) * 2 - 1, Depth, 1);
		o.position.y = -o.position.y;
		o.worldPos = 0;
	#else
		float4x4 o2w = Transforms[instance];
		o2w[0][3] += -STRATUM_CAMERA_POSITION.x * o2w[3][3];
		o2w[1][3] += -STRATUM_CAMERA_POSITION.y * o2w[3][3];
		o2w[2][3] += -STRATUM_CAMERA_POSITION.z * o2w[3][3];
		float4 worldPos = mul(o2w, float4(p, 0, 1));
		o.position = mul(STRATUM_MATRIX_VP, worldPos);
		o.worldPos = float4(worldPos.xyz, o.position.z);
	#endif
	o.canvasPos = (p - ClipBounds.xy) / ClipBounds.zw;
	return o;
}

void fsmain(v2f i,
	out float4 color : SV_Target0,
	out float4 depthNormal : SV_Target1 ) {
	
	#ifdef SCREEN_SPACE
	depthNormal = 0;
	#else
	depthNormal = float4(normalize(cross(ddx(i.worldPos.xyz), ddy(i.worldPos.xyz))) * i.worldPos.w, 1);
	#endif

	float2 dx = ddx(i.shapeCoord.xy);
	float2 dy = ddy(i.shapeCoord.xy);

	clip(i.canvasPos);
	clip(1 - i.canvasPos);

	#ifdef SCREEN_SPACE
		float2 coord = i.shapeCoord - dy/2;
		dy /= 4;
		dx /= 3;
		color = 0;
		for (uint j = 0; j < 4; j++) {
			color.r += Fill(coord - dx, i.referencePoint, i.startVertex, i.lastVertex);
			color.g += Fill(coord     , i.referencePoint, i.startVertex, i.lastVertex);
			color.b += Fill(coord + dx, i.referencePoint, i.startVertex, i.lastVertex);
			coord += dy;
		}
		color.rgb /= 4;
		color.a = dot(color.rgb, 1.0/3.0);
		color.rgb = lerp(color.rgb, 1, color.a); // hack: desaturate based off overall contribution
	#else
		float4 oxy = float4(dx, dy) * float4(0.125, 0.375, 0.125, 0.375);
		float4 oyx = float4(dy, dx) * float4(0.125, 0.375, 0.125, 0.375);
		float2 offsets[4] = {
			 oxy.xy,
			-oxy.xy - oxy.zw,
			 oyx.zw - oyx.xy,
			-oyx.zw + oyx.xy
		};

		color.rgb = 1;
		color.a = 0;
		color.a += Fill(i.shapeCoord + offsets[0], i.referencePoint, i.startVertex, i.lastVertex);
		color.a += Fill(i.shapeCoord + offsets[1], i.referencePoint, i.startVertex, i.lastVertex);
		color.a += Fill(i.shapeCoord + offsets[2], i.referencePoint, i.startVertex, i.lastVertex);
		color.a += Fill(i.shapeCoord + offsets[3], i.referencePoint, i.startVertex, i.lastVertex);
		color.a /= 4;
	#endif
	
	color *= Color;
	depthNormal.a *= color.a;
}