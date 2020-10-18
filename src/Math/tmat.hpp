#pragma once

#include "tvec.hpp"
#include "tquat.hpp"

#define rpt(i,n) for (uint32_t i = 0; i < n; ++i)

namespace stm {

template<typename T>
class tmat4x4 {
public:
	// Column-major
	tvec<4,T> v[4];

	inline tmat4x4(
		T m11, T m21, T m31, T m41,
		T m12, T m22, T m32, T m42,
		T m13, T m23, T m33, T m43,
		T m14, T m24, T m34, T m44) {
			v[0] = tvec<4,T>(m11,m12,m13,m14);
			v[1] = tvec<4,T>(m21,m22,m23,m24);
			v[2] = tvec<4,T>(m31,m32,m33,m34);
			v[3] = tvec<4,T>(m41,m42,m43,m44);
		};
	inline tmat4x4(tvec<4,T> c1, tvec<4,T> c2, tvec<4,T> c3, tvec<4,T> c4) { v[0] = c1; v[1] = c2; v[2] = c3; v[3] = c4; };
	inline tmat4x4(T s) : tmat4x4(
		s, 0, 0, 0,
		0, s, 0, 0,
		0, 0, s, 0,
		0, 0, 0, s) {};
	inline tmat4x4() : tmat4x4(1) {};
	inline tmat4x4(tquat<T> quat) : tmat4x4(1) {
		tvec<3,T> q2 = quat.xyz * quat.xyz;
		tvec<3,T> qw = quat.xyz * quat.w;
		tvec<3,T> c = tvec<3,T>(quat.x, quat.x, quat.y) * tvec<3,T>(quat.z, quat.y, quat.z);

		v[0][0] = 1 - 2 * (q2.y + q2.z);
		v[0][1] = 2 * (c.y + qw.z);
		v[0][2] = 2 * (c.x - qw.y);
		v[1][0] = 2 * (c.y - qw.z);
		v[1][1] = 1 - 2 * (q2.x + q2.z);
		v[1][2] = 2 * (c.z + qw.x);
		v[2][0] = 2 * (c.x + qw.y);
		v[2][1] = 2 * (c.z - qw.x);
		v[2][2] = 1 - 2 * (q2.x + q2.y);
	}

	inline void Decompose(tvec<3,T>* position, tquat<T>* quat, tvec<3,T>* scale) {
		tmat4x4 tmp = *this;

		// tmp scale (if scale is not provided)
		tvec<3,T> sc;
		if (!scale) scale = &sc;

		// Normalization
		if (tmp[3][3] == 0) return;
		for (uint32_t i = 0; i < 4; i++)
			for (uint32_t j = 0; j < 4; j++)
				tmp.v[i][j] /= tmp.v[3][3];

		if (tmp[0][3] != 0.f ||
			tmp[1][3] != 0.f ||
			tmp[2][3] != 0.f) { // Clear perspective
			tmp[0][3] = tmp[1][3] = tmp[2][3] = 0;
			tmp[3][3] = 1.f;
		}


		if (position) *position = tmp.v[3].xyz;
		tmp.v[3].xyz = 0;

		// scale/shear
		tvec<3,T> rows[3];
		for (uint32_t i = 0; i < 3; ++i)
			for (uint32_t j = 0; j < 3; ++j)
				rows[i][j] = tmp.v[i][j];

		scale->x = length(rows[0]);

		rows[0] = normalize(rows[0]);

		tvec<3,T> skew;
		skew.z = dot(rows[0], rows[1]);
		rows[1] += -skew.z * rows[0];

		scale->y = length(rows[1]);
		rows[1] = normalize(rows[1]);
		skew.z /= scale->y;

		skew.y = dot(rows[0], rows[2]);
		rows[2] += rows[0] * -skew.y;
		skew.x = dot(rows[1], rows[2]);
		rows[2] += rows[1] * -skew.x;

		scale->z = length(rows[2]);
		rows[2] = normalize(rows[2]);
		skew.y /= scale->z;
		skew.x /= scale->z;

		if (dot(rows[0], cross(rows[1], rows[2])) < 0) {
			for (uint32_t i = 0; i < 3; i++) {
				scale->v[i] = -scale->v[i];
				rows[i] = -rows[i];
			}
		}

		if (!quat) return;

		uint32_t i, j, k = 0;
		T root, trace = rows[0].x + rows[1].y + rows[2].z;
		if (trace > 0) {
			root = sqrt(trace + 1.f);
			quat->w = .5f * root;
			root = .5f / root;
			quat->x = root * (rows[1].z - rows[2].y);
			quat->y = root * (rows[2].x - rows[0].z);
			quat->z = root * (rows[0].y - rows[1].x);
		} else {
			static const uint32_t next[3] { 1, 2, 0 };
			i = 0;
			if (rows[1].y > rows[0].x) i = 1;
			if (rows[2].z > rows[i][i]) i = 2;
			j = next[i];
			k = next[j];

			root = sqrtf(rows[i][i] - rows[j][j] - rows[k][k] + 1.f);

			quat->v[i] = .5f * root;
			root = .5f / root;
			quat->v[j] = root * (rows[i][j] + rows[j][i]);
			quat->v[k] = root * (rows[i][k] + rows[k][i]);
			quat->w = root * (rows[j][k] - rows[k][j]);
		}
	}

	inline tvec<4,T>& operator[](uint32_t i) { return v[i]; }
	inline tvec<4,T> operator[](uint32_t i) const { return v[i]; }

	inline tmat4x4 operator=(const tmat4x4& m) { rpt(i,4) v[i] = m.v[i]; return *this; }
	inline tmat4x4 operator+=(const tmat4x4& m) { rpt(i,4) v[i] += m.v[i]; return *this; }
	inline tmat4x4 operator+=(const T& s) { rpt(i,4) v[i] += s; return *this; }

	inline tmat4x4 operator+(const T& s) const {
		tmat4x4 r;
		rpt(i,4) r.v[i] = v[i] + s;
		return r;
	}
	inline tmat4x4 operator*(const T& s) const {
		tmat4x4 r;
		rpt(i,4) r.v[i] = v[i] * s;
		return r;
	}
	inline tmat4x4 operator*=(const T& s) { rpt(i,4) v[i] *= s; return *this; }
	inline tmat4x4 operator/(const T& s) const { return operator *(1.f / s); }
	inline tmat4x4 operator/=(const T& s) { return operator *=(1.f / s); }

	inline tmat4x4 operator+(const tmat4x4& s) const {
		tmat4x4 r(0);
		rpt(i,4) r.v[i] = v[i] + s.v[i];
		return r;
	}
	inline tmat4x4 operator-(const tmat4x4& s) const {
		tmat4x4 r(0);
		rpt(i,4) r.v[i] = v[i] - s.v[i];
		return r;
	}
	inline tvec<4,T> operator*(const tvec<4,T>& s) const {
		tvec<4,T> r = 0;
		rpt(i,4) r += v[i] * s.v[i];
		return r;
	}
	inline tmat4x4 operator*(const tmat4x4& m) const {
		tmat4x4 r;
		rpt(i,4) r.v[i] = (*this) * m.v[i];
		return r;
	}
	inline tmat4x4 operator*=(const tmat4x4& m) { *this = operator*(m); return *this; }

	inline bool operator ==(const tmat4x4& a) const { rpt(i,4) if (v[i] != a.v[i]) return false; return true; }
	inline bool operator !=(const tmat4x4& a) const { return !operator ==(a); }
	
	inline friend tmat4x4 operator*(const float& s, const tmat4x4& m) {
		tmat4x4 r;
		rpt(i,4) r.v[i] = m.v[i] * s;
		return r;
	}
	
	inline static tmat4x4 Look(const tvec<3,T>& p, const tvec<3,T>& fwd, const tvec<3,T>& up) {
		tvec<3,T> f[3];
		f[0] = normalize(cross(up, fwd));
		f[1] = cross(fwd, f[0]);
		f[2] = fwd;
		tmat4x4 r(1);
		rpt(i,3) r.v[i][0] = f[0].v[i];
		rpt(i,3) r.v[i][1] = f[1].v[i];
		rpt(i,3) r.v[i][2] = f[2].v[i];
		rpt(i,3) r.v[3][i] = -dot(f[i], p);
		return r;
	}
	inline static tmat4x4 PerspectiveFov(T fovy, T aspect, T zNear, T zFar) {
		T df = 1 / (zFar - zNear);
		T sy = 1 / std::tan(fovy / 2);
		tmat4x4 r(0);
		r[0][0] = sy / aspect;
		r[1][1] = sy;
		r[2][2] = zFar * df;
		r[3][2] = -zFar * zNear * df;
		r[2][3] = 1;
		return r;
	}
	inline static tmat4x4 Perspective(T width, T height, T zNear, T zFar) {
		T df = 1 / (zFar - zNear);
		tmat4x4 r(0);
		r[0][0] = 2 * zNear / width;
		r[1][1] = 2 * zNear / height;
		r[2][2] = zFar * df;
		r[3][2] = -zFar * zNear * df;
		r[2][3] = 1;
		return r;
	}
	inline static tmat4x4 Perspective(T left, T right, T top, T bottom, T zNear, T zFar) {
		T df = 1 / (zFar - zNear);
		tmat4x4 r(0);
		r[0][0] = 2 * zNear / (right - left);
		r[1][1] = 2 * zNear / (top - bottom);
		r[2][0] = (right + left) / (right - left);
		r[2][1] = (top + bottom) / (top - bottom);
		r[2][2] = zFar * df;
		r[3][2] = -zFar * zNear * df;
		r[2][3] = 1;
		return r;
	}
	inline static tmat4x4 Orthographic(T width, T height, T zNear, T zFar) {
		T df = 1 / (zFar - zNear);
		tmat4x4 r(1);
		r[0][0] = 2 / width;
		r[1][1] = 2 / height;
		r[2][2] = df;
		r[3][2] = -zNear * df;
		return r;
	}

	inline static tmat4x4 Translate(const tvec<3,T>& t) {
		tmat4x4 m(1);
		m.v[3].xyz = t;
		return m;
	}
	inline static tmat4x4 Scale(const tvec<3,T>& t) {
		tmat4x4 m(1);
		rpt(i,3) m.v[i] *= t.v[i];
		return m;
	}
	inline static tmat4x4 RotateX(T r) {
		T c = std::cos(r);
		T s = std::sin(r);
		return tmat4x4(
			1, 0, 0, 0,
			0, c, -s, 0,
			0, s, c, 0,
			0, 0, 0, 1 );
	}
	inline static tmat4x4 RotateY(T r) {
		T c = std::cos(r);
		T s = std::sin(r);
		return tmat4x4(
			c, 0, s, 0,
			0, 0, 0, 0,
			-s, 0, c, 0,
			0, 0, 0, 1 );
	}
	inline static tmat4x4 RotateZ(T r) {
		T c = std::cos(r);
		T s = std::sin(r);
		return tmat4x4(
			c, -s, 0, 0,
			s, c, 0, 0,
			0, 0, 0, 0,
			0, 0, 0, 1 );
	}

	inline static tmat4x4 TRS(const tvec<3,T>& translation, const tquat<T>& quat, const tvec<3,T>& scale) {
		tmat4x4 rm(quat);
		rpt(i,3) rm.v[i] *= scale.v[i];
		rm.v[3].xyz = translation;
		return rm;
	}
};

template<typename T> inline tmat4x4<T> inverse(const tmat4x4<T>& m) {
	T c00 = m[2][2] * m[3][3] - m[3][2] * m[2][3];
	T c02 = m[1][2] * m[3][3] - m[3][2] * m[1][3];
	T c03 = m[1][2] * m[2][3] - m[2][2] * m[1][3];

	T c04 = m[2][1] * m[3][3] - m[3][1] * m[2][3];
	T c06 = m[1][1] * m[3][3] - m[3][1] * m[1][3];
	T c07 = m[1][1] * m[2][3] - m[2][1] * m[1][3];

	T c08 = m[2][1] * m[3][2] - m[3][1] * m[2][2];
	T c10 = m[1][1] * m[3][2] - m[3][1] * m[1][2];
	T c11 = m[1][1] * m[2][2] - m[2][1] * m[1][2];

	T c12 = m[2][0] * m[3][3] - m[3][0] * m[2][3];
	T c14 = m[1][0] * m[3][3] - m[3][0] * m[1][3];
	T c15 = m[1][0] * m[2][3] - m[2][0] * m[1][3];

	T c16 = m[2][0] * m[3][2] - m[3][0] * m[2][2];
	T c18 = m[1][0] * m[3][2] - m[3][0] * m[1][2];
	T c19 = m[1][0] * m[2][2] - m[2][0] * m[1][2];

	T c20 = m[2][0] * m[3][1] - m[3][0] * m[2][1];
	T c22 = m[1][0] * m[3][1] - m[3][0] * m[1][1];
	T c23 = m[1][0] * m[2][1] - m[2][0] * m[1][1];

	tvec<4,T> f0(c00, c00, c02, c03);
	tvec<4,T> f1(c04, c04, c06, c07);
	tvec<4,T> f2(c08, c08, c10, c11);
	tvec<4,T> f3(c12, c12, c14, c15);
	tvec<4,T> f4(c16, c16, c18, c19);
	tvec<4,T> f5(c20, c20, c22, c23);

	tvec<4,T> v0(m[1][0], m[0][0], m[0][0], m[0][0]);
	tvec<4,T> v1(m[1][1], m[0][1], m[0][1], m[0][1]);
	tvec<4,T> v2(m[1][2], m[0][2], m[0][2], m[0][2]);
	tvec<4,T> v3(m[1][3], m[0][3], m[0][3], m[0][3]);

	tvec<4,T> i0(v1 * f0 - v2 * f1 + v3 * f2);
	tvec<4,T> i1(v0 * f0 - v2 * f3 + v3 * f4);
	tvec<4,T> i2(v0 * f1 - v1 * f3 + v3 * f5);
	tvec<4,T> i3(v0 * f2 - v1 * f4 + v2 * f5);

	tvec<4,T> sa(+1, -1, +1, -1);
	tvec<4,T> sb(-1, +1, -1, +1);
	tmat4x4<T> inv(i0 * sa, i1 * sb, i2 * sa, i3 * sb);

	tvec<4,T> r0(inv[0][0], inv[1][0], inv[2][0], inv[3][0]);

	tvec<4,T> d0(m[0] * r0);
	return inv / ((d0.x + d0.y) + (d0.z + d0.w));
}
template<typename T> inline tmat4x4<T> transpose(const tmat4x4<T>& m) {
	return tmat4x4<T>(
		m[0][0], m[0][1], m[0][2], m[0][3],
		m[1][0], m[1][1], m[1][2], m[1][3],
		m[2][0], m[2][1], m[2][2], m[2][3],
		m[3][0], m[3][1], m[3][2], m[3][3] );
}

typedef tmat4x4<float> float4x4;

}

namespace std {

template<typename T>
struct hash<stm::tmat4x4<T>> {
	inline size_t operator()(const stm::tmat4x4<T>& v) const {
		size_t h = 0;
		rpt(i,4) stm::hash_combine(h, v[i]);
		return h;
	}
};

}

#undef rpt