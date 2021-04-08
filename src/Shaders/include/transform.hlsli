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
	float OffsetZ;
};
bool is_perspective(ProjectionData t) { return t.Scale[2] > 0; }

#define PROJECTION_I { float3(.1,.1,1/128.f), 0 }
#define TRANSFORM_I { float3(0,0,0), 1, QUATF_I }

float3 transform_vector(TransformData t, float3 v) {
	return rotate_vector(t.Rotation, v)*t.Scale;
}
float3 transform_point(TransformData t, float3 v) {
	return t.Translation + transform_vector(t, v);
}
TransformData inverse(TransformData t) {
	TransformData r;
	r.Rotation = inverse(t.Rotation);
	r.Scale = 1/t.Scale;
	r.Translation = -rotate_vector(r.Rotation, t.Translation) * r.Scale;
	return r;
}
TransformData tmul(TransformData lhs, TransformData rhs) {
	TransformData r;
	r.Translation = transform_point(lhs, rhs.Translation);
	r.Rotation = qmul(lhs.Rotation, rhs.Rotation);
	r.Scale = lhs.Scale * rhs.Scale;
	return r;
}

TransformData make_transform(float3 translation, quatf rotation, float scale) {
	TransformData r;
	r.Translation = translation;
	r.Scale = scale;
	r.Rotation = rotation;
	return r;
}
TransformData make_transform(float3 translation, quatf rotation) {
	return make_transform(translation, rotation, 1);
}
TransformData make_transform(float3 translation) {
	quatf i = QUATF_I;
	return make_transform(translation, i, 1);
}
TransformData make_transform(quatf rotation) {
	return make_transform(float3(0,0,0), rotation, 1);
}

ProjectionData make_orthographic(float width, float height, float znear, float zfar) {
	ProjectionData r;
	r.Scale[0] = 2/width;
	r.Scale[1] = 2/height;
	r.Scale[2] = -1/(zfar - znear);
	r.OffsetZ = -znear;
	return r;
}
ProjectionData make_perspective(float fovy, float aspect /* width/height */, float znear, float zfar) {
	ProjectionData r;
	r.Scale[1] = 1/tan(fovy/2);
	r.Scale[0] = aspect*r.Scale[1];
	r.Scale[2] = 1/(zfar - znear);
	r.OffsetZ = -znear;
	return r;
}

float4 project_point(ProjectionData t, float3 v) {
	float4 r;
	r[0] = v[0]*t.Scale[0];
	r[1] = v[1]*t.Scale[1];
	r[2] = (v[2] + t.OffsetZ) * t.Scale[2];
	if (is_perspective(t)) {
		r[2] *= v[2];
		r[3] = v[2];
	} else {
		r[2] = -r[2];
		r[3] = 1;
	}
	return r;
}
float3 back_project(ProjectionData t, float2 v) {
	return float3(v[0]/t.Scale[0], v[1]/t.Scale[1], 1);
}

#endif