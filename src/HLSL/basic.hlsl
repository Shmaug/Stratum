#pragma compile glslc -fshader-stage=vert -fentry-point=color_vs
#pragma compile glslc -fshader-stage=frag -fentry-point=color_fs
#pragma compile glslc -fshader-stage=vert -fentry-point=color_texture_vs
#pragma compile glslc -fshader-stage=frag -fentry-point=color_texture_fs
#pragma compile glslc -fshader-stage=vert -fentry-point=skybox_vs
#pragma compile glslc -fshader-stage=frag -fentry-point=skybox_fs

#include "transform.hlsli"

[[vk::constant_id(0)]] const uint gTextureCount = 1;

[[vk::binding(0)]] Texture2D<float4> gTextures[gTextureCount];
[[vk::binding(1)]] SamplerState gSampler;

[[vk::push_constant]] cbuffer {
	TransformData gWorldToCamera;
	ProjectionData gProjection;
	float4 gColor;
	float4 gTextureST;
	uint gTextureIndex;
	float gEnvironmentGamma;
};

float4 color_vs(in float3 vertex : POSITION, inout float4 color : COLOR) : SV_Position {
	color *= gColor;
	return project_point(gProjection, transform_point(gWorldToCamera, vertex));
}
float4 color_fs(float4 color : COLOR) : SV_Target0 { return color; }

float4 color_texture_vs(in float3 vertex : POSITION, inout float2 uv : TEXCOORD0, inout float4 color : COLOR) : SV_Position {
	color *= gColor;
	uv = gTextureST.xy*uv + gTextureST.zw;
	return project_point(gProjection, transform_point(gWorldToCamera, vertex));
}
float4 color_texture_fs(float2 uv : TEXCOORD0, float4 color : COLOR) : SV_Target0 { return color * gTextures[(gTextureCount>1) ? gTextureIndex : 0].Sample(gSampler, uv); }

float4 skybox_vs(in uint vertexId : SV_VertexID, out float2 clipPos : TEXCOORD0) : SV_Position {
	static const float2 quad[] = {
		float2(-1,-1),
		float2( 1,-1),
		float2( 1, 1),
		float2(-1,-1),
		float2( 1, 1),
		float2(-1, 1)
	};
	clipPos = quad[vertexId];
	return float4(clipPos, 1, 1);
}
float4 skybox_fs(float2 clipPos : TEXCOORD0) : SV_Target0 {
	float3 ray = normalize( transform_vector(inverse(gWorldToCamera), back_project(gProjection, clipPos)) );
	float4 color = gTextures[(gTextureCount>1) ? gTextureIndex : 0].SampleLevel(gSampler, float2(atan2(ray.z, ray.x)*M_1_PI*.5 + .5, acos(ray.y)*M_1_PI), 0);
	color.rgb = pow(color.rgb, 1/gEnvironmentGamma);
	return color;
}