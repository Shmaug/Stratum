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
	float Near;
	float Far;
	float AspectRatio;
	float OrthographicSize;
};

struct GPULight {
	float4 CascadeSplits;
	float3 WorldPosition;
	float InvSqrRange;
	float3 Direction;
	float SpotAngleScale;
	float3 Color;
	float SpotAngleOffset;
	uint Type;
	int ShadowIndex;
	int2 pad;
};

struct ShadowData {
	float4x4 WorldToShadow; // ViewProjection matrix for the shadow render
	float4 ShadowST;
	float3 CameraPosition;
	float InvProj22;
};

struct VertexWeight {
	float4 Weights;
	uint4 Indices;
};
struct TextGlyph {
	float2 Offset;
	float2 Extent;
	float2 TexOffset;
	float2 TexExtent;
};

#ifdef __cplusplus
#undef uint
#else

#define STRATUM_PUSH_CONSTANTS \
uint StereoEye; \
float3 AmbientLight; \
uint LightCount; \
float2 ShadowTexelSize;

#define STRATUM_MATRIX_V Camera.View[StereoEye]
#define STRATUM_MATRIX_P Camera.Projection[StereoEye]
#define STRATUM_MATRIX_VP Camera.ViewProjection[StereoEye]
#define STRATUM_CAMERA_POSITION Camera.Position[StereoEye].xyz

[[vk::binding(LIGHT_BUFFER_BINDING, 				PER_MATERIAL)]] StructuredBuffer<GPULight> Lights : register(t2);
[[vk::binding(SHADOW_BUFFER_BINDING, 				PER_MATERIAL)]] StructuredBuffer<ShadowData> Shadows : register(t4);
[[vk::binding(SHADOW_ATLAS_BINDING, 				PER_MATERIAL)]] Texture2D<float> ShadowAtlas : register(t3);
[[vk::binding(ENVIRONMENT_TEXTURE_BINDING, 	PER_MATERIAL)]] Texture2D<float4> EnvironmentTexture	: register(t5);
[[vk::binding(SHADOW_SAMPLER_BINDING, 			PER_MATERIAL)]] SamplerComparisonState ShadowSampler : register(s6);

[[vk::binding(CAMERA_BUFFER_BINDING, PER_CAMERA)]] ConstantBuffer<CameraBuffer> Camera : register(b0);

#endif

#endif