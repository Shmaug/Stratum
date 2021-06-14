#pragma compile -D -S vert -e axis_vs
#pragma compile -D -S vert -e skybox_vs
#pragma compile -D -S vert -e texture_vs
#pragma compile -D -S frag -e color_fs
#pragma compile -D -S frag -e skybox_fs
#pragma compile -D -S frag -e texture_fs

#include "include/transform.hlsli"

[[vk::binding(0)]] Texture2D<float4> gTexture;
[[vk::binding(1)]] SamplerState gSampler;

[[vk::push_constant]] cbuffer {
	TransformData gWorldToCamera;
	ProjectionData gProjection;
	float gEnvironmentGamma;
	float4 gScaleTranslate;
};

float4 axis_vs(in uint axis : SV_InstanceID, in uint sgn : SV_VertexID, out float4 color : COLOR) : SV_Position {
	float3 direction = 0;
	direction[axis] = 1;
	direction = transform_vector(gWorldToCamera, direction);
	color = 0.25;
	color[axis] = sgn ? 0.75 : 1;
	return project_point(gProjection, gWorldToCamera.Translation + direction*ray_box(gWorldToCamera.Translation, 1/direction, -10, 10)[sgn]);
}
float4 skybox_vs(in float3 vertex : POSITION, out float3 viewRay : TEXCOORD0) : SV_Position {
	viewRay = rotate_vector(inverse(gWorldToCamera.Rotation), vertex);
	return project_point(gProjection, vertex);
}

struct vertex_gui {
	float2 vertex : POSITION;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
};
float4 texture_vs(in vertex_gui v, out float2 uv : TEXCOORD0, out float4 color : COLOR) : SV_Position {
	uv = v.uv;
	color = v.color;
	return float4(v.vertex * gScaleTranslate.xy + gScaleTranslate.zw, 0, 1);
}

float4 color_fs(float4 color : COLOR) : SV_Target0 { return color; }
float4 skybox_fs(float3 viewRay : TEXCOORD0) : SV_Target0 {
	float3 ray = normalize(viewRay);
	float4 color = gTexture.SampleLevel(gSampler, float2(atan2(ray.z, ray.x)*M_1_PI*.5 + .5, acos(ray.y)*M_1_PI), 0);
	color.rgb = pow(color.rgb, 1/gEnvironmentGamma);
	return color;
}
float4 texture_fs(float2 uv : TEXCOORD0, float4 color : COLOR) : SV_Target0 { return color * gTexture.Sample(gSampler, uv); }