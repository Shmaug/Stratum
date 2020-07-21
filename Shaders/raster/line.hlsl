#pragma vertex vsmain
#pragma fragment fsmain

#pragma multi_compile SCREEN_SPACE

#pragma render_queue 4000
#pragma cull false
#pragma blend alpha

#include <include/shadercompat.h>

[[vk::binding(INSTANCE_BUFFER_BINDING, PER_OBJECT)]] ByteAddressBuffer Vertices : register(t0);
[[vk::binding(BINDING_START, PER_OBJECT)]] StructuredBuffer<float4x4> Transforms : register(t1);

[[vk::push_constant]] cbuffer PushConstants : register(b1) {
	STRATUM_PUSH_CONSTANTS
	uint TransformIndex;
	float4 Color;
	float4 ClipBounds;
	float2 ScreenSize;
	float Depth;
}

#include <include/util.hlsli>

struct v2f {
	float4 position : SV_Position;
	float4 worldPos : TEXCOORD0;
	float2 canvasPos : TEXCOORD1;
};

v2f vsmain(uint index : SV_VertexID, uint instance : SV_InstanceID) {
	float3 p = asfloat(Vertices.Load3(index * 12));

	v2f o;
#ifdef SCREEN_SPACE
	p = mul(Transforms[TransformIndex], float4(p, 1.0)).xyz;
	o.position = float4((p.xy / ScreenSize) * 2 - 1, p.z, 1);
	o.position.y = -o.position.y;
#else
	float4x4 o2w = Transforms[TransformIndex];
	o2w[0][3] += -STRATUM_CAMERA_POSITION.x * o2w[3][3];
	o2w[1][3] += -STRATUM_CAMERA_POSITION.y * o2w[3][3];
	o2w[2][3] += -STRATUM_CAMERA_POSITION.z * o2w[3][3];
	float4 worldPos = mul(o2w, float4(p, 1.0));
	o.position = mul(STRATUM_MATRIX_VP, worldPos);
	o.worldPos = float4(worldPos.xyz, o.position.z);
#endif

	o.canvasPos = (p - ClipBounds.xy) / ClipBounds.zw;

	return o;
}

void fsmain(v2f i,
	out float4 color : SV_Target0,
	out float4 depthNormal : SV_Target1) {
	
	color = Color;

	#ifdef SCREEN_SPACE
	depthNormal = 0;
	#else
	depthNormal = float4(normalize(cross(ddx(i.worldPos.xyz), ddy(i.worldPos.xyz))) * i.worldPos.w, color.a);
	#endif
	
	clip(i.canvasPos);
	clip(1 - i.canvasPos);
}