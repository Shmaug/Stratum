#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "math.hlsli"

struct TransformData {
	float3 Translation;
	float Scale;
	quatf Rotation;
};
struct ProjectionData {
	float2 Scale;
	float2 Offset;
	float Near;
	uint Mode;
};

#define ProjectionMode_Orthographic  1

inline float3 transform_vector(TransformData t, float3 v) {
	return rotate_vector(t.Rotation, v*t.Scale);
}
inline float3 transform_point(TransformData t, float3 v) {
	return t.Translation + transform_vector(t, v);
}
inline TransformData inverse(TransformData t) {
	TransformData r;
	r.Rotation = inverse(t.Rotation);
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

inline ProjectionData make_orthographic(float2 size, float2 offset, float znear) {
	ProjectionData r;
	r.Scale = 2/size;
	r.Offset = offset;
	r.Near = znear;
	r.Mode = ProjectionMode_Orthographic;
	return r;
}
inline ProjectionData make_perspective(float fovy, float aspect /* width/height */, float2 offset, float znear) {
	ProjectionData r;
	r.Scale[1] = 1/tan(fovy/2);
	r.Scale[0] = aspect*r.Scale[1];
	r.Offset = offset;
	r.Near = znear;
	r.Mode = 0;
	return r;
}

inline float4 project_point(ProjectionData t, float3 v) {
	float n = t.Near;
	float4 r;
	r[0] = v[0]*t.Scale[0] + t.Offset[0];
	r[1] = v[1]*t.Scale[1] + t.Offset[1];
	if (t.Near < 0) {
		// right handed
		r[2] = -t.Near;
		r[3] = -v[2];
	} else {
		r[2] = t.Near;
		r[3] = v[2];
	}
	if (t.Mode & ProjectionMode_Orthographic) {
		r[0] *= r[3];
		r[1] *= r[3];
	}
	return r;
}
inline float3 back_project(ProjectionData t, float2 v) {
	float x = (v[0] - t.Offset[0])/t.Scale[0];
	float y = (v[1] - t.Offset[0])/t.Scale[1];
	return float3(x, y, (t.Near < 0) ? -1 : 1);
}

#endif