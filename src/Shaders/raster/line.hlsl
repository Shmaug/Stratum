#pragma pass forward/depth vsmain fsdepth
#pragma pass forward/opaque vsmain fsmain

#pragma render_queue 4000
#pragma cull false
#pragma blend 0 add srcAlpha oneMinusSrcAlpha

#pragma multi_compile SCREEN_SPACE

#include <include/shadercompat.h>

[[vk::binding(INSTANCE_BUFFER_BINDING, PER_OBJECT)]] ByteAddressBuffer Vertices : register(t0);
[[vk::binding(BINDING_START, PER_OBJECT)]] StructuredBuffer<float4x4> Transforms : register(t1);

[[vk::push_constant]] cbuffer PushConstants : register(b1) {
	STM_PUSH_CONSTANTS
	uint TransformIndex;
	float4 Color;
	float4 ClipBounds;
	float2 ScreenSize;
	float Depth;
}

#include <include/util.hlsli>

struct v2f {
	float4 position : SV_Position;
	float2 clipPos : TEXCOORD0;
};

v2f vsmain(uint index : SV_VertexID, uint instance : SV_InstanceID) {
	float3 p = asfloat(Vertices.Load3(index * 12));

	v2f o;
	#ifdef SCREEN_SPACE
	p = mul(Transforms[TransformIndex], float4(p, 1.0)).xyz;
	o.position = float4((p.xy / ScreenSize) * 2 - 1, p.z, 1);
	#else
	float4x4 o2w = Transforms[TransformIndex];
	o2w[0][3] += -STRATUM_CAMERA_POSITION.x * o2w[3][3];
	o2w[1][3] += -STRATUM_CAMERA_POSITION.y * o2w[3][3];
	o2w[2][3] += -STRATUM_CAMERA_POSITION.z * o2w[3][3];
	o.position = mul(STRATUM_MATRIX_VP, mul(o2w, float4(p, 1.0)));
	#endif
	o.clipPos = (p - ClipBounds.xy) / ClipBounds.zw;

	return o;
}

float4 fsmain(v2f i) : SV_Target0 {
	clip(Color.a);
	clip(i.clipPos);
	clip(1 - i.clipPos);
	return Color;
}

float fsdepth(v2f i) : SV_Target0 {
	if (Color.a <= 0 || any(i.clipPos < 0) || any(i.clipPos > 1)) discard;
	return i.position.z;
}