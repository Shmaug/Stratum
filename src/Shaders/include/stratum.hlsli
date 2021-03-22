#ifndef STRATUM_H
#define STRATUM_H

struct CameraData {
	float4x4 View;
	float4x4 Projection;
	float4x4 ViewProjection;
	float4x4 InvProjection;
	float4 Position;
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

#ifndef __cplusplus
ConstantBuffer<CameraData> gCamera : register(b0, space0);

inline float3 Dehomogenize(float4 v) {
	return v.xyz/v.w;
}
#endif

#endif // STRATUM_H