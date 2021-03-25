#ifndef DTYPES_H
#define DTYPES_H

#define LIGHT_ATTEN_DIRECTIONAL 0
#define LIGHT_ATTEN_DISTANCE 1
#define LIGHT_ATTEN_ANGULAR 2
#define LIGHT_USE_SHADOWMAP 4

struct CameraData {
	float4x4 View;
	float4x4 Projection;
	float4x4 ViewProjection;
	float4x4 InvProjection;
	float3 Position;
	uint LightCount;
};

struct InstanceData {
	float4x4 Transform;
	float4x4 InverseTransform;
};

struct GlyphRect {
	float2 Offset;
	float2 Extent;
	float4 TextureST;
};

struct LightData {
	float4x4 ToLight;
	float3 Emission;
	uint Flags;
	float3 Position;
	uint pad;
	float SpotAngleScale;
	float SpotAngleOffset;
	float ShadowBias;
	float2 ShadowCoordScale;
	float2 ShadowCoordOffset;
};

struct MaterialData {
	float4 gTextureST;
	float4 gBaseColor;
	float3 gEmission;
	float gMetallic;
	float gRoughness;
	float gBumpStrength;
	float gAlphaCutoff;
	uint pad;
};

#endif