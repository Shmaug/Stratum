#ifndef STRATUM_H
#define STRATUM_H

struct InstanceData {
	float4x4 Transform;
	float4x4 InverseTransform;
};

struct CameraData {
	float4x4 View;
	float4x4 Projection;
	float4x4 ViewProjection;
	float4x4 InvProjection;
	float4 Position;
};

struct GlyphRect {
	float2 Offset;
	float2 Extent;
	float4 TextureST;
};

#ifndef __cplusplus
ConstantBuffer<CameraData> gCamera : register(b0, space0);

#define STRATUM_MATRIX_V gCamera.View
#define STRATUM_MATRIX_P gCamera.Projection
#define STRATUM_MATRIX_VP gCamera.ViewProjection
#define STRATUM_CAMERA_POSITION gCamera.Position.xyz
#define STRATUM_CAMERA_NEAR 
#define STRATUM_CAMERA_FAR gCamera.Position.w
#endif

#endif // STRATUM_H