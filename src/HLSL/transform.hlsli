#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "math.hlsli"

struct TransformData {
	float3 mTranslation;
	float mScale;
	quatf mRotation;
};
struct ProjectionData {
	float2 mScale;
	float2 Offset;
	float mNear;
	float mFar;
	uint mMode;
};

#define ProjectionMode_Orthographic 1

inline float3 transform_vector(TransformData t, float3 v) {
	return rotate_vector(t.mRotation, v*t.mScale);
}
inline float3 transform_point(TransformData t, float3 v) {
	return t.mTranslation + transform_vector(t, v);
}
inline TransformData inverse(TransformData t) {
	TransformData r;
	r.mRotation = inverse(t.mRotation);
	r.mScale = 1/t.mScale;
	r.mTranslation = rotate_vector(r.mRotation, -t.mTranslation * r.mScale);
	return r;
}
inline TransformData tmul(TransformData lhs, TransformData rhs) {
	TransformData r;
	r.mTranslation = transform_point(lhs, rhs.mTranslation);
	r.mRotation = qmul(lhs.mRotation, rhs.mRotation);
	r.mScale = lhs.mScale * rhs.mScale;
	return r;
}

inline ProjectionData make_orthographic(float2 size, float2 offset, float znear, float zfar) {
	ProjectionData r;
	r.mScale = 2/size;
	r.Offset = offset;
	r.mNear = znear;
	r.mFar = zfar;
	r.mMode = ProjectionMode_Orthographic;
	return r;
}
inline ProjectionData make_perspective(float fovy, float aspect /* width/height */, float2 offset, float znear, float zfar) {
	ProjectionData r;
	r.mScale[1] = 1/tan(fovy/2);
	r.mScale[0] = aspect*r.mScale[1];
	r.Offset = offset;
	r.mNear = znear;
	r.mFar = zfar;
	r.mMode = 0;
	return r;
}

inline float4 project_point(ProjectionData t, float3 v) {
	float4 r;
	r[0] = v[0]*t.mScale[0] + t.Offset[0];
	r[1] = v[1]*t.mScale[1] + t.Offset[1];
	if (t.mMode & ProjectionMode_Orthographic) {
		// reversed z
		r[2] = (v[2] - t.mFar)/(t.mNear - t.mFar);
		r[3] = 1;
	} else {
		// infinite far plane, reversed z
		r[2] = abs(t.mNear);
		r[3] = v[2]*sign(t.mNear);
	}
	return r;
}
inline float3 back_project(ProjectionData t, float3 v) {
	float3 r;
	r[0] = (v[0] - t.Offset[0])/t.mScale[0];
	r[1] = (v[1] - t.Offset[0])/t.mScale[1];
	if (t.mMode & ProjectionMode_Orthographic) {
		r[2] = t.mFar + r[2]*(t.mNear - t.mFar);
	} else
		r[2] = t.mNear/v[2];
	return r;
}

#endif