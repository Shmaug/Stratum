#pragma compile glslc -fshader-stage=vert -fentry-point=color_vs
#pragma compile glslc -fshader-stage=frag -fentry-point=color_fs
#pragma compile glslc -fshader-stage=vert -fentry-point=color_image_vs
#pragma compile glslc -fshader-stage=frag -fentry-point=color_image_fs
#pragma compile glslc -fshader-stage=vert -fentry-point=skybox_vs
#pragma compile glslc -fshader-stage=frag -fentry-point=skybox_fs

#include "transform.hlsli"

[[vk::constant_id(0)]] const uint gImageCount = 16;
[[vk::constant_id(1)]] const float gAlphaClip = -1; // set below 0 to disable

[[vk::binding(0)]] SamplerState gSampler;
[[vk::binding(1)]] Texture2D<float4> gImages[gImageCount];

[[vk::push_constant]] cbuffer {
	TransformData gWorldToCamera;
	ProjectionData gProjection;
	float4 gColor;
	float4 gImageST;
	uint gImageIndex;
	float gEnvironmentGamma;
};

float4 color_vs(in float3 vertex : POSITION, inout float4 color : COLOR) : SV_Position {
	color *= gColor;
	return project_point(gProjection, transform_point(gWorldToCamera, vertex));
}
float4 color_fs(float4 color : COLOR) : SV_Target0 { return color; }

float4 color_image_vs(in float3 vertex : POSITION, inout float4 color : COLOR, inout float2 uv : TEXCOORD0) : SV_Position {
	color *= gColor;
	uv = gImageST.xy*uv + gImageST.zw;
	return project_point(gProjection, transform_point(gWorldToCamera, vertex));
}
float4 color_image_fs(float4 color : COLOR, float2 uv : TEXCOORD0) : SV_Target0 {
	float4 c = color * gImages[(gImageCount>1) ? gImageIndex : 0].Sample(gSampler, uv);
	if (gAlphaClip >= 0 && c.a < gAlphaClip) discard;
	return c;
}


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
	return float4(clipPos, 0, 1);
}
float4 skybox_fs(float2 clipPos : TEXCOORD0) : SV_Target0 {
	float3 ray = rotate_vector(inverse(gWorldToCamera.mRotation), normalize(back_project(gProjection, float3(clipPos,abs(gProjection.mNear)))));
	float4 color = gImages[(gImageCount>1) ? gImageIndex : 0].SampleLevel(gSampler, cartesian_to_spherical_uv(ray), 0);
	color.rgb = pow(color.rgb, 1/gEnvironmentGamma);
	return color;
}