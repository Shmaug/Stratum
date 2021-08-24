#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "math.hlsli"

struct TransformData {
	float3 Translation;
	float Scale;
	quatf Rotation;
};
struct ProjectionData {
	float3 Scale;
	uint Mode;
	float3 Offset;
	uint pad;
};

#define ProjectionMode_Perspective  1
#define ProjectionMode_RightHanded  2

inline float3 transform_vector(TransformData t, float3 v) {
	return rotate_vector(t.Rotation, v*t.Scale);
}
inline float3 transform_point(TransformData t, float3 v) {
	return t.Translation + transform_vector(t, v);
}
inline TransformData inverse(TransformData t) {
	TransformData r;
	r.Rotation = conj(t.Rotation);
	r.Scale = 1/t.Scale;
	r.Translation = rotate_vector(r.Rotation, -t.Translation * r.Scale);
	return r;
}
inline TransformData tmul(TransformData lhs, TransformData rhs) {
	TransformData r;
	r.Translation = transform_point(lhs, rhs.Translation);
	r.Rotation = qmul(lhs.Rotation, rhs.Rotation);
	r.Scale = lhs.Scale * rhs.Scale;
	return r;
}

inline ProjectionData make_orthographic(float width, float height, float ox, float oy, float zfar, bool rightHanded) {
	ProjectionData r;
	r.Scale[0] = 2/width;
	r.Scale[1] = 2/height;
	r.Scale[2] = 1/zfar;
	r.Mode = rightHanded ? ProjectionMode_RightHanded : 0;
	r.Offset[0] = ox;
	r.Offset[1] = oy;
	return r;
}
inline ProjectionData make_perspective(float fovy, float aspect /* width/height */, float ox, float oy, float zfar, bool rightHanded) {
	ProjectionData r;
	r.Scale[1] = 1/tan(fovy/2);
	r.Scale[0] = aspect*r.Scale[1];
	r.Scale[2] = 1/zfar;
	r.Offset[0] = ox;
	r.Offset[1] = oy;
	r.Mode = ProjectionMode_Perspective;
	if (rightHanded) r.Mode |= ProjectionMode_RightHanded;
	return r;
}

inline float4 project_point(ProjectionData t, float3 v) {
	if (t.Mode & ProjectionMode_RightHanded) v[2] = -v[2];
	float w = (t.Mode & ProjectionMode_Perspective) ? v[2] : 1;
	v = v*t.Scale + t.Offset;
	return float4(v[0], v[1], v[2]*abs(w), w);
}
inline float3 back_project(ProjectionData t, float2 v) {
	return float3(v[0] / t.Scale[0], v[1] / t.Scale[1], (t.Mode & ProjectionMode_RightHanded) ? -1 : 1);
}

#endif