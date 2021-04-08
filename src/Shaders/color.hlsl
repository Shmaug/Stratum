#pragma compile vertex vs_segment vertex vs_axis fragment fs_color

#include "include/transform.hlsli"

struct PushConstants {
	TransformData WorldToCamera;
	ProjectionData Projection;
	float4 Color;
	float3 P0;
	float Width;
	float3 Direction;
	float Distance;
};
[[vk::push_constant]] const PushConstants gPushConstants = { TRANSFORM_I, PROJECTION_I, float4(1,1,1,1), float3(0,0,0), .01, float3(0,0,1), 1 };

void vs_segment(uint index : SV_VertexID, uint instance : SV_InstanceID, out float4 vertex : SV_Position, out float4 color : COLOR) {
	static const uint2 quad[6] = {
		uint2(0, 0), uint2(1, 0), uint2(1, 1),
		uint2(0, 1), uint2(0, 0), uint2(1, 1)
	};
	const uint2 q = quad[index%6];
	float3 p = transform_point(gPushConstants.WorldToCamera, gPushConstants.P0 + q.y ? gPushConstants.Direction*gPushConstants.Distance : 0);
	float3 d = transform_vector(gPushConstants.WorldToCamera, gPushConstants.Direction);
	d.xy = normalize(float2(d.y,-d.x)) * gPushConstants.Width/2;
	p.xy += q.x ? d.xy : -d.xy;
	vertex = project_point(gPushConstants.Projection, p);
	color = gPushConstants.Color;
}

void vs_axis(uint sgn : SV_VertexID, uint axis : SV_InstanceID, out float4 vertex : SV_Position, out float4 color : COLOR) {
	float3 origin = gPushConstants.WorldToCamera.Translation;
	float3 direction = 0;
	direction[axis] = 1;
	direction = transform_vector(gPushConstants.WorldToCamera, direction);
	vertex = project_point(gPushConstants.Projection, origin + direction*ray_box(origin, 1/direction, -10, 10)[sgn]);
	color = 0.25;
	color[axis] = sgn ? 0.75 : 1;
}

float4 fs_color(float4 color : COLOR) : SV_Target0 {
  return color;
}