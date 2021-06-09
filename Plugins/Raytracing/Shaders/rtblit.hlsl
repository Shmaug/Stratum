#pragma vertex vsmain
#pragma fragment fsmain

#pragma render_queue 5000
#pragma cull false
#pragma zwrite false
#pragma ztest false

#pragma static_sampler Sampler filter=nearest

[[vk::binding(0, 0)]] Texture2D<float4> Radiance : register(t0);
[[vk::binding(1, 0)]] SamplerState Sampler : register(s0);

[[vk::push_constant]] cbuffer PushConstants : register(b2) {
	float4 ScaleTranslate;
	float4 TextureST;
	float Gamma;
}

struct v2f {
	float4 position : SV_Position;
	float2 texcoord : TEXCOORD1;
};

v2f vsmain(uint index : SV_VertexID) {
	static const float2 positions[6] = {
		float2(0,0),
		float2(1,0),
		float2(1,1),
		float2(0,1),
		float2(0,0),
		float2(1,1)
	};

	float2 p = positions[index] * ScaleTranslate.xy + ScaleTranslate.zw;
	
	v2f o;
	o.position = float4(p * 2 - 1, .01, 1);
	o.position.y = -o.position.y;
	o.texcoord = positions[index] * TextureST.xy + TextureST.zw;

	return o;
}

void fsmain(v2f i,
	out float4 color : SV_Target0,
	out float4 depthNormal : SV_Target1) {
	depthNormal = 0;

	float4 radiance = Radiance.SampleLevel(Sampler, i.texcoord, 0);
	color = float4(pow(radiance.rgb, 1.0 / Gamma), 1);
}