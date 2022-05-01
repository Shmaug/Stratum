#pragma compile dxc -Zpr -spirv -T vs_6_7 -E color_vs
#pragma compile dxc -Zpr -spirv -T ps_6_7 -E color_fs
#pragma compile dxc -Zpr -spirv -T vs_6_7 -E color_image_vs
#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing  -T ps_6_7 -E color_image_fs
#pragma compile dxc -Zpr -spirv -T vs_6_7 -E skybox_vs
#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing  -T ps_6_7 -E skybox_fs

#include <transform.h>
#include <scene.h>

[[vk::constant_id(1)]] const float gAlphaClip = -1; // set below 0 to disable

SamplerState gSampler;
StructuredBuffer<ViewData> gViews;
Texture2D<float4> gImages[gImageCount];

[[vk::push_constant]] const struct {
	float4 gColor;
	float4 gImageST;
	uint gImageIndex;
	float gEnvironmentGamma;
	uint gViewIndex;
} gPushConstants;

struct appdata {
	float3 pos : POSITION;
	float4 color : COLOR;
};
struct v2f {
	float4 pos : SV_POSITION;
	float4 color : COLOR;
	float2 uv : TEXCOORD0;
};
struct appdata_image {
	float3 pos : POSITION;
	float4 color : COLOR;
	float2 uv : TEXCOORD0;
};
struct v2f_image {
	float4 pos : SV_POSITION;
	float4 color : COLOR;
	float2 uv : TEXCOORD0;
};

v2f color_vs(appdata v) {
	v2f o;
	o.pos = gViews[gPushConstants.gViewIndex].projection.project_point(gViews[gPushConstants.gViewIndex].world_to_camera.transform_point(v.pos));
	o.color = v.color*gPushConstants.gColor;
	return o;
}
float4 color_fs(float4 color : COLOR) : SV_Target0 { return color; }

v2f_image color_image_vs(appdata_image v) {
	v2f_image o;
	o.pos = gViews[gPushConstants.gViewIndex].projection.project_point(gViews[gPushConstants.gViewIndex].world_to_camera.transform_point(v.pos));
	o.color = v.color*gPushConstants.gColor;
	o.uv = gPushConstants.gImageST.xy*v.uv + gPushConstants.gImageST.zw;
	return o;
}
float4 color_image_fs(float4 color : COLOR, float2 uv : TEXCOORD0) : SV_Target0 {
	const float4 c = color * gImages[(gImageCount>1) ? gPushConstants.gImageIndex : 0].Sample(gSampler, uv);
	if (gAlphaClip >= 0 && c.a < gAlphaClip) discard;
	return c;
}

float4 skybox_vs(uint vertexId : SV_VertexID, out float2 clipPos : TEXCOORD0) : SV_Position {
	static const float2 quad[] = {
		float2(-1,-1), float2( 1,-1), float2( 1, 1),
		float2(-1,-1), float2( 1, 1), float2(-1, 1) };
	clipPos = quad[vertexId];
	return float4(clipPos, 0, 1);
}
float4 skybox_fs(float2 clipPos : TEXCOORD0) : SV_Target0 {
	const float3 ray = normalize(gViews[gPushConstants.gViewIndex].camera_to_world.transform_vector(gViews[gPushConstants.gViewIndex].projection.back_project(clipPos)));
	float4 color = gImages[gPushConstants.gImageIndex].SampleLevel(gSampler, cartesian_to_spherical_uv(ray), 0);
	color.rgb = pow(color.rgb, 1/gPushConstants.gEnvironmentGamma);
	return color;
}