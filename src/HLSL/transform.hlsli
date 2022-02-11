#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "quatf.hlsli"

//#define TRANSFORM_UNIFORM_SCALING

struct TransformData {
#ifdef TRANSFORM_UNIFORM_SCALING
	float3 mTranslation;
	float mScale;
	quatf mRotation;
#else
 float4x4 m;
#endif

	inline float3 transform_vector(float3 v) CONST_CPP {
#ifdef TRANSFORM_UNIFORM_SCALING
		return rotate_vector(mRotation, v*mScale);
#else
#ifdef __cplusplus
		return m.block<3,3>(0,0).matrix() * v.matrix();
#else
		return mul((float3x3)m, v).xyz;
#endif
#endif
	}

	inline float3 transform_point(float3 v) CONST_CPP {
#ifdef TRANSFORM_UNIFORM_SCALING
		return mTranslation + transform_vector(v);
#else
#ifdef __cplusplus
		return (m.matrix() * v.matrix().homogeneous()).col(3).head<3>();
#else
		return mul(m, float4(v, 1)).xyz;
#endif
#endif
	}

	inline TransformData inverse() CONST_CPP {
		TransformData r;
#ifdef TRANSFORM_UNIFORM_SCALING
		r.mRotation = inverse(mRotation);
		r.mScale = 1/t.mScale;
		r.mTranslation = rotate_vector(r.mRotation, -mTranslation * r.mScale);
#else
#ifdef __cplusplus
		r.m = m.matrix().inverse();
#else
		float a00 = m[0][0], a01 = m[0][1], a02 = m[0][2];
		float a10 = m[1][0], a11 = m[1][1], a12 = m[1][2];
		float a20 = m[2][0], a21 = m[2][1], a22 = m[2][2];
		float b01 =  a22 * a11 - a12 * a21;
		float b11 = -a22 * a10 + a12 * a20;
		float b21 =  a21 * a10 - a11 * a20;
		float det = 1/(a00 * b01 + a01 * b11 + a02 * b21);
		r.m[0] = float4(b01*det, (-a22 * a01 + a02 * a21)*det, ( a12 * a01 - a02 * a11)*det, -m[0][3]);
		r.m[1] = float4(b11*det, ( a22 * a00 - a02 * a20)*det, (-a12 * a00 + a02 * a10)*det, -m[1][3]);
		r.m[2] = float4(b21*det, (-a21 * a00 + a01 * a20)*det, ( a11 * a00 - a01 * a10)*det, -m[2][3]);
#endif
#endif
		return r;
	}
};

inline TransformData make_transform(float3 t, quatf r, float3 s) {
	TransformData result;
#ifdef TRANSFORM_UNIFORM_SCALING
	result.mTranslation = t;
	result.mRotation = r;
	result.mScale = pow(s[0]*s[1]*s[2], 1.f/3.f);
#else
#ifdef __cplusplus
	result.m = Affine3f(Translation3f(t)).matrix();
	result.m.block<3,3>(0,0) = (Quaternionf(r.w, r.xyz[0], r.xyz[1], r.xyz[2]) * Scaling(s.matrix())).matrix();
#else
	// https://www.euclideanspace.com/maths/geometry/rotations/conversions/quaternionToMatrix/index.htm
	float sqw = r.w*r.w;
	float sqx = r.xyz[0]*r.xyz[0];
	float sqy = r.xyz[1]*r.xyz[1];
	float sqz = r.xyz[2]*r.xyz[2];

	// invs (inverse square length) is only required if quaternion is not already normalised
	float invs = 1 / (sqx + sqy + sqz + sqw);
	result.m[0][0] = ( sqx - sqy - sqz + sqw)*invs; // since sqw + sqx + sqy + sqz =1/invs*invs
	result.m[1][1] = (-sqx + sqy - sqz + sqw)*invs;
	result.m[2][2] = (-sqx - sqy + sqz + sqw)*invs;
	
	float tmp1 = r.xyz[0]*r.xyz[2];
	float tmp2 = r.xyz[1]*r.w;
	result.m[2][0] = 2 * (tmp1 + tmp2)*invs;
	result.m[0][2] = 2 * (tmp1 - tmp2)*invs;

	tmp1 = r.xyz[0]*r.xyz[1];
	tmp2 = r.xyz[2]*r.w;
	result.m[1][0] = 2 * (tmp1 - tmp2)*invs;
	result.m[0][1] = 2 * (tmp1 + tmp2)*invs;

	tmp1 = r.xyz[2]*r.xyz[1];
	tmp2 = r.xyz[0]*r.w;
	result.m[1][2] = 2 * (tmp1 + tmp2)*invs;
	result.m[2][1] = 2 * (tmp1 - tmp2)*invs;

	result.m[0][3] = t[0];
	result.m[1][3] = t[1];
	result.m[2][3] = t[2];
#endif
#endif
	return result;
}

inline TransformData tmul(TransformData lhs, TransformData rhs) {
	TransformData r;
#ifdef TRANSFORM_UNIFORM_SCALING
	r.mTranslation = transform_point(lhs, rhs.mTranslation);
	r.mRotation = qmul(lhs.mRotation, rhs.mRotation);
	r.mScale = lhs.mScale * rhs.mScale;
#else
#ifdef __cplusplus
	r.m = (lhs.m.matrix() * rhs.m.matrix()).array();
#else
	r.m = mul(lhs.m, rhs.m); // https://github.com/Microsoft/DirectXShaderCompiler/blob/master/docs/SPIR-V.rst#vectors-and-matrices
#endif
#endif
	return r;
}

inline float3x4 to_float3x4(TransformData t) {
#ifdef TRANSFORM_UNIFORM_SCALING
#ifdef __cplusplus
	return (Eigen::Translation3f(t.mTranslation) * Eigen::Quaternionf(t.mRotation.w, t.mRotation.xyz[0], t.mRotation.xyz[1], t.mRotation.xyz[2]) * Eigen::Scaling(t.mScale)).matrix().array().block<3,4>(0,0);
#else
	float3x4 m;
	float3 r0 = rotate_vector(t.mRotation, float3(1,0,0));
	float3 r1 = rotate_vector(t.mRotation, float3(0,1,0));
	float3 r2 = rotate_vector(t.mRotation, float3(0,0,1));
	m[0] = float4(r0[0], r1[0], r2[0], t.mTranslation[0]);
	m[1] = float4(r0[1], r1[1], r2[1], t.mTranslation[1]);
	m[2] = float4(r0[2], r1[2], r2[2], t.mTranslation[2]);
	return m;
#endif
#else
#ifdef __cplusplus
	return t.m.block<3,4>(0,0);
#else
	return (float3x4)t.m;
#endif
#endif
}

inline TransformData from_float3x4(const float3x4 m) {
	TransformData t;
#ifdef TRANSFORM_UNIFORM_SCALING
#ifdef __cplusplus
	t.mTranslation = m.col(3);
	Matrix3f m3x3 = m.block<3,3>(0,0);
	t.mScale = pow(m3x3.determinant(), 1.f/3.f);
	m3x3 /= t.mScale;
	Quaternionf q(m3x3);
	t.mRotation = make_quatf(q.x(), q.y(), q.z(), q.w());
#else
	for (uint i = 0; i < 3; i++) t.mTranslation[i] = m[i][3];
	float3x3 m3x3 = (float3x3)m;
	t.mScale = pow(determinant(m3x3), 1.f/3.f);

	float tr = m3x3[0][0] + m3x3[1][1] + m3x3[2][2];
	if (tr > 0) { 
		float S = sqrt(tr+1.0) * 2; // S=4*qw 
		t.mRotation.w = 0.25 * S;
		t.mRotation.xyz[0] = (m3x3[2][1] - m3x3[1][2]) / S;
		t.mRotation.xyz[1] = (m3x3[0][2] - m3x3[2][0]) / S; 
		t.mRotation.xyz[2] = (m3x3[1][0] - m3x3[0][1]) / S; 
	} else if ((m3x3[0][0] > m3x3[1][1])&(m3x3[0][0] > m3x3[2][2])) { 
		float S = sqrt(1.0 + m3x3[0][0] - m3x3[1][1] - m3x3[2][2]) * 2; // S=4*qx 
		t.mRotation.w = (m3x3[2][1] - m3x3[1][2]) / S;
		t.mRotation.xyz[0] = 0.25 * S;
		t.mRotation.xyz[1] = (m3x3[0][1] + m3x3[1][0]) / S; 
		t.mRotation.xyz[2] = (m3x3[0][2] + m3x3[2][0]) / S; 
	} else if (m3x3[1][1] > m3x3[2][2]) { 
		float S = sqrt(1.0 + m3x3[1][1] - m3x3[0][0] - m3x3[2][2]) * 2; // S=4*qy
		t.mRotation.w = (m3x3[0][2] - m3x3[2][0]) / S;
		t.mRotation.xyz[0] = (m3x3[0][1] + m3x3[1][0]) / S; 
		t.mRotation.xyz[1] = 0.25 * S;
		t.mRotation.xyz[2] = (m3x3[1][2] + m3x3[2][1]) / S; 
	} else { 
		float S = sqrt(1.0 + m3x3[2][2] - m3x3[0][0] - m3x3[1][1]) * 2; // S=4*qz
		t.mRotation.w = (m3x3[1][0] - m3x3[0][1]) / S;
		t.mRotation.xyz[0] = (m3x3[0][2] + m3x3[2][0]) / S;
		t.mRotation.xyz[1] = (m3x3[1][2] + m3x3[2][1]) / S;
		t.mRotation.xyz[2] = 0.25 * S;
	}
#endif
#else
#ifdef __cplusplus
	t.m.block<3,4>(0,0) = m;
	t.m.row(3) = float4(0,0,0,1);
#else
	t.m[0] = m[0];
	t.m[1] = m[1];
	t.m[2] = m[2];
	t.m[3] = float4(0,0,0,1);
#endif
#endif
	return t;
}

struct ProjectionData {
	float2 mScale;
	float2 mOffset;
	float mNear;
	float mFar;
	uint mOrthographic;
	uint pad;

	// uses reversed z (1 at near plane -> 0 at far plane)
	inline float4 project_point(float3 v) CONST_CPP {
		float4 r;
		if (mOrthographic) {
			// orthographic
			r[0] = v[0]*mScale[0] + mOffset[0];
			r[1] = v[1]*mScale[1] + mOffset[1];
			r[2] = (v[2] - mFar)/(mNear - mFar);
			r[3] = 1;
		} else {
			// perspective, infinite far plane
			r[0] = v[0]*mScale[0] + v[2]*mOffset[0];
			r[1] = v[1]*mScale[1] + v[2]*mOffset[1];
			r[2] = abs(mNear);
			r[3] = v[2]*sign(mNear);
		}
		return r;
	}
	inline float3 back_project(float2 v) CONST_CPP {
		float3 r;
		if (mOrthographic) {
			r[0] = (v[0] - mOffset[0])/mScale[0];
			r[1] = (v[1] - mOffset[1])/mScale[1];
		} else {
			r[0] = mNear*(v[0]*sign(mNear) - mOffset[0])/mScale[0];
			r[1] = mNear*(v[1]*sign(mNear) - mOffset[1])/mScale[1];
		}
		r[2] = mNear;
		return r;
	}
};

inline ProjectionData make_orthographic(float2 size, float2 offset, float znear, float zfar) {
	ProjectionData r;
	r.mScale = 2/size;
	r.mOffset = offset;
	r.mNear = znear;
	r.mFar = zfar;
	r.mOrthographic = 1;
	return r;
}
inline ProjectionData make_perspective(float fovy, float aspect, float2 offset, float znear) {
	ProjectionData r;
	r.mScale[1] = 1/tan(fovy/2);
	r.mScale[0] = aspect*r.mScale[1];
	r.mOffset = offset;
	r.mNear = znear;
	r.mFar = 0;
	r.mOrthographic = 0;
	return r;
}

#endif
