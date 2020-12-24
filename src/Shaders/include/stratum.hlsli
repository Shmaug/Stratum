#ifndef STRATUM_H
#define STRATUM_H

struct InstanceData {
	float4x4 Transform;
	float4x4 InverseTransform;
};

struct CameraData {
	float4x4 View[2];
	float4x4 Projection[2];
	float4x4 ViewProjection[2];
	float4x4 InvProjection[2];
	float4 Position[2];
};

struct GlyphRect {
	float2 Offset;
	float2 Extent;
	float4 TextureST;
};

#ifndef __cplusplus
ConstantBuffer<CameraData> gCamera : register(b0, space0);

#define STRATUM_MATRIX_V gCamera.View[gPushConstants.gStereoEye]
#define STRATUM_MATRIX_P gCamera.Projection[gPushConstants.gStereoEye]
#define STRATUM_MATRIX_VP gCamera.ViewProjection[gPushConstants.gStereoEye]
#define STRATUM_CAMERA_POSITION gCamera.Position[gPushConstants.gStereoEye].xyz
#define STRATUM_CAMERA_NEAR gCamera.Position[0].w
#define STRATUM_CAMERA_FAR gCamera.Position[1].w
#endif

#endif // STRATUM_H