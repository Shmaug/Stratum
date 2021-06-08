#pragma compile -D -S vert -e vs_skybox
#pragma compile -D -S vert -e vs_axis
#pragma compile -D -S frag -e fs_skybox
#pragma compile -D -S frag -e fs_color
#pragma compile -D -S frag -e fs_texture

#include "include/transform.hlsli"

[[vk::binding(0)]] Texture2D<float4> gTexture;
[[vk::binding(1)]] SamplerState gSampler;

[[vk::push_constant]] cbuffer {
	TransformData gWorldToCamera;
	ProjectionData gProjection;
	float gEnvironmentGamma;
};

void vs_skybox(float3 vertex : POSITION, out float4 position : SV_Position, out float3 viewRay : TEXCOORD0) {
	position = project_point(gProjection, vertex);
	viewRay = rotate_vector(inverse(gWorldToCamera.Rotation), vertex);
}
float4 fs_skybox(float3 viewRay : TEXCOORD0) : SV_Target0 {
	float3 ray = normalize(viewRay);
	float4 color = gTexture.SampleLevel(gSampler, float2(atan2(ray.z, ray.x)*M_1_PI*.5 + .5, acos(ray.y)*M_1_PI), 0);
	color.rgb = pow(color.rgb, 1/gEnvironmentGamma);
	return color;
}

void vs_axis(uint sgn : SV_VertexID, uint axis : SV_InstanceID, out float4 vertex : SV_Position, out float4 color : COLOR) {
	float3 direction = 0;
	direction[axis] = 1;
	direction = transform_vector(gWorldToCamera, direction);
	vertex = project_point(gProjection, gWorldToCamera.Translation + direction*ray_box(gWorldToCamera.Translation, 1/direction, -10, 10)[sgn]);
	color = 0.25;
	color[axis] = sgn ? 0.75 : 1;
}

float4 fs_color(float4 color : COLOR) : SV_Target0 { return color; }
float4 fs_texture(float4 color : COLOR, float2 uv : TEXCOORD0) : SV_Target0 { return color * gTexture.Sample(gSampler, uv); }