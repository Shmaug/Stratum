#ifndef SHADER_COMPAT_H
#define SHADER_COMPAT_H

#ifdef __cplusplus
#define uint uint32_t
#endif

#define PER_CAMERA 0
#define PER_MATERIAL 1
#define PER_OBJECT 2

#define CAMERA_BUFFER_BINDING 0
#define INSTANCE_BUFFER_BINDING 1
#define LIGHT_BUFFER_BINDING 2
#define SHADOW_BUFFER_BINDING 3
#define SHADOW_ATLAS_BINDING 4
#define ENVIRONMENT_TEXTURE_BINDING 5
#define SHADOW_SAMPLER_BINDING 6
#define BINDING_START 7

#define LIGHT_SUN 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

struct InstanceBuffer {
	float4x4 ObjectToWorld;
	float4x4 WorldToObject;
};

struct CameraBuffer {
	float4x4 View[2];
	float4x4 Projection[2];
	float4x4 ViewProjection[2];
	float4x4 InvProjection[2];
	float4 Position[2];
};

struct GPULight {
	float3 Color;
	uint Type;
	float3 WorldPosition;
	float InvSqrRange;
	float3 Direction;
	int ShadowDataIndex;
	float4 CascadeSplits;
	float SpotAngleScale;
	float SpotAngleOffset;
	uint ShadowIndex;
	uint pad;
};

struct ShadowData {
	float4x4 WorldToShadow; // ViewProjection matrix for the shadow render
	float3 CameraPosition;
	float InvProj22;
};

struct VertexWeight {
	float4 Weights;
	uint4 Indices;
};

struct GlyphRect {
	float2 Offset;
	float2 Extent;
	float4 TextureST;
};

#ifdef __cplusplus
#undef uint
#else

#pragma static_sampler ShadowSampler 

#define STM_PUSH_CONSTANTS \
float3 AmbientLight; \
uint stmPad0; \
float2 ShadowTexelSize; \
uint StereoEye; \
uint LightCount;

#pragma static_sampler ShadowSampler maxAnisotropy=0 maxLod=0 addressMode=clampBorder borderColor=floatOpaqueWhite compareOp=less

#define STRATUM_MATRIX_V Camera.View[StereoEye]
#define STRATUM_MATRIX_P Camera.Projection[StereoEye]
#define STRATUM_MATRIX_VP Camera.ViewProjection[StereoEye]
#define STRATUM_CAMERA_POSITION Camera.Position[StereoEye].xyz
#define STRATUM_CAMERA_NEAR Camera.Position[0].w
#define STRATUM_CAMERA_FAR Camera.Position[1].w

[[vk::binding(CAMERA_BUFFER_BINDING, 				PER_CAMERA)]] ConstantBuffer<CameraBuffer> Camera : register(b0);
[[vk::binding(LIGHT_BUFFER_BINDING, 				PER_CAMERA)]] StructuredBuffer<GPULight> Lights : register(t0);
[[vk::binding(SHADOW_BUFFER_BINDING, 				PER_CAMERA)]] StructuredBuffer<ShadowData> Shadows : register(t1);
[[vk::binding(SHADOW_ATLAS_BINDING, 				PER_CAMERA)]] Texture2D<float> ShadowAtlas : register(t2);
[[vk::binding(ENVIRONMENT_TEXTURE_BINDING, 	PER_CAMERA)]] Texture2D<float4> EnvironmentTexture	: register(t3);
[[vk::binding(SHADOW_SAMPLER_BINDING, 			PER_CAMERA)]] SamplerComparisonState ShadowSampler : register(s0);

[[vk::binding(INSTANCE_BUFFER_BINDING, PER_OBJECT)]] StructuredBuffer<InstanceBuffer> Instances : register(t0);

#endif

#endif