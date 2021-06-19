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
	float2 gOffset;
	float2 gTexelSize;
};

float4 axis_vs(in uint axis : SV_InstanceID, in uint sgn : SV_VertexID, out float4 position : SV_Position, out float4 color : COLOR) {
	float3 direction = 0;
	direction[axis] = 1;
	direction = transform_vector(gWorldToCamera, direction);
	color = 0.25;
	color[axis] = sgn ? 0.75 : 1;
	return project_point(gProjection, gWorldToCamera.Translation + direction*ray_box(gWorldToCamera.Translation, 1/direction, -10, 10)[sgn]);
}
float4 skybox_vs(in float3 vertex : POSITION, out float4 position : SV_Position, out float3 viewRay : TEXCOORD0) {
	viewRay = rotate_vector(inverse(gWorldToCamera.Rotation), vertex);
	return project_point(gProjection, vertex);
}

struct v2f_texture {
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
};
v2f_texture texture_vs(in float2 vertex : POSITION, in float2 uv : TEXCOORD0, in float4 color : COLOR) {
	v2f_texture o;
	o.position = float4((vertex - gOffset) * gTexelSize*2-1, 0, 1);
	o.uv = uv;
	o.color = color;
	return o;
}

float4 color_fs(float4 color : COLOR) : SV_Target0 { return color; }
float4 skybox_fs(float3 viewRay : TEXCOORD0) : SV_Target0 {
	float3 ray = normalize(viewRay);
	float4 color = gTexture.SampleLevel(gSampler, float2(atan2(ray.z, ray.x)*M_1_PI*.5 + .5, acos(ray.y)*M_1_PI), 0);
	color.rgb = pow(color.rgb, 1/gEnvironmentGamma);
	return color;
}
float4 texture_fs(float2 uv : TEXCOORD0, float4 color : COLOR) : SV_Target0 { return color * gTexture.Sample(gSampler, uv); }