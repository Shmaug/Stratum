#pragma once

#ifndef MATH_HPP
#define MATH_HPP

#ifdef WINDOWS
#pragma warning(push)
#pragma warning(disable:26495)
#pragma warning(disable:4244)
#endif

#define _USE_MATH_DEFINES
#include <cmath>

#ifdef far
#undef far
#endif
#ifdef near
#undef near
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef abs
#undef abs
#endif

#define rpt(i,n) for (int i = 0; i < n; ++i)
#define TVEC_TEMPLATE template<uint32_t N, typename T>

#pragma pack(push)
#pragma pack(1)

#pragma region tvec body
#define TVEC_BODY(N) \
	static_assert(std::is_arithmetic<T>::value, "Vector must be of arithmetic type.");					\
																																															\
	inline tvec() { rpt(i,N) v[i] = 0; };																												\
	inline tvec(const T& s) { rpt(i,N) v[i] = s; };																							\
																																															\
	inline tvec operator=(const T s) {																													\
		rpt(i,N) v[i] = s;																																				\
		return *this;																																							\
	}																																														\
	inline tvec operator=(const tvec& s) {																											\
		rpt(i,N) v[i] = s.v[i];																																		\
		return *this;																																							\
	}																																														\
																																															\
	inline tvec operator-() const {																															\
		tvec r;																																										\
		rpt(i,N) r.v[i] = -v[i];																																	\
		return r;																																									\
	}																																														\
	inline tvec operator-(const T s) const {																										\
		tvec r;																																										\
		rpt(i,N) r.v[i] = v[i] - s;																																\
		return r;																																									\
	}																																														\
	inline tvec operator -(const tvec& s) const {																								\
		tvec r;																																										\
		rpt(i,N) r.v[i] = v[i] - s.v[i];																													\
		return r;																																									\
	}																																														\
	inline tvec operator -=(const T s) {																												\
		rpt(i,N) v[i] -= s;																																				\
		return *this;																																							\
	}																																														\
	inline tvec operator -=(const tvec& s) {																										\
		rpt(i,N) v[i] -= s.v[i];																																	\
		return *this;																																							\
	}																																														\
	inline friend tvec operator -(const T a, const tvec& s) {																		\
		tvec r;																																										\
		rpt(i,N) r.v[i] = a - s.v[i];																															\
		return r;																																									\
	}																																														\
																																															\
	inline tvec operator +(const T s) const {																										\
		tvec r;																																										\
		rpt(i,3) r.v[i] = v[i] + s;																																\
		return r;																																									\
	}																																														\
	inline tvec operator +(const tvec& s) const {																								\
		tvec r;																																										\
		rpt(i,N) r.v[i] = v[i] + s.v[i];																													\
		return r;																																									\
	}																																														\
	inline tvec operator +=(const T s) {																												\
		rpt(i,N) v[i] += s;																																				\
		return *this;																																							\
	}																																														\
	inline tvec operator +=(const tvec& s) {																										\
		rpt(i,N) v[i] += s.v[i];																																	\
		return *this;																																							\
	}																																														\
	inline friend tvec operator +(const T a, const tvec& s) { return s + a; }										\
																																															\
	inline tvec operator *(const T s) const {																										\
		tvec r;																																										\
		rpt(i,N) r.v[i] = v[i] * s;																																\
		return r;																																									\
	}																																														\
	inline tvec operator *(const tvec& s) const {																								\
		tvec r;																																										\
		rpt(i,N) r.v[i] = v[i] * s.v[i];																													\
		return r;																																									\
	}																																														\
	inline tvec operator *=(const T s) {																												\
		rpt(i,N) v[i] *= s;																																				\
		return *this;																																							\
	}																																														\
	inline tvec operator *=(const tvec& s) {																										\
		rpt(i,N) v[i] *= s.v[i];																																	\
		return *this;																																							\
	}																																														\
	inline friend tvec operator *(const T a, const tvec& s) { return s * a; }										\
																																															\
	inline friend tvec operator /(const T a, const tvec& s) {																		\
		tvec r;																																										\
		rpt(i,N) r.v[i] = a / s.v[i];																															\
		return r;																																									\
	}																																														\
	inline tvec operator /(const T s) const { return operator*(1 / s); }												\
	inline tvec operator /(const tvec& s) const { return operator*(1 / s); }										\
	inline tvec operator /=(const T s) { return operator*=(1 / s); }														\
	inline tvec operator /=(const tvec& v) { return operator*=(1 / v); }												\
																																															\
	inline T& operator[](uint32_t i) {																													\
		return v[i];																																							\
	}																																														\
	inline T operator[](uint32_t i) const {																											\
		return v[i];																																							\
	}																																														\
																																															\
	inline bool operator ==(const tvec& a) const {																							\
		rpt(i,N) if (v[i] != a.v[i]) return false;																								\
		return true;																																							\
	}																																														\
	inline bool operator !=(const tvec& a) const { return !operator ==(a); }										\
																																															\
	template<typename Y>																																				\
	inline operator tvec<N,Y>() const {																													\
		tvec<N,Y> r;																																							\
		rpt(i,N) r.v[i] = (Y)v[i];																																\
		return r;																																									\
	}																																														
#pragma endregion

TVEC_TEMPLATE struct tvec;

template<typename T> struct tvec<2, T> {
	union {
		T v[2];
		struct { T x, y; };
		struct { T r, g; };
	};

	inline tvec(const T& x, const T& y) : x(x), y(y) {};

	TVEC_BODY(2)
};
template<typename T> struct tvec<3, T> {
	union {
		T v[3];

		struct { T x, y, z; };
		tvec<2, T> xy;
		
		struct { T r, g, b; };
		tvec<2, T> rg;
	};
	
	inline tvec(const T& x, const T& y, const T& z) : x(x), y(y), z(z) {};
	inline tvec(const tvec<2, T>& xy, const T& z) : xy(xy), z(z) {};

	TVEC_BODY(3)
};
template<typename T> struct tvec<4, T> {
	union {
		T v[4];

		struct { T x, y, z, w; };
		struct { tvec<2, T> xy, zw; };
		tvec<3, T> xyz;

		struct { T r, g, b, a; };
		struct { tvec<2, T> rg, ba; };
		tvec<3, T> rgb;
	};
	
	inline tvec(const T& x, const T& y, const T& z, const T& w) : x(x), y(y), z(z), w(w) {};
	inline tvec(const tvec<2, T>& xy, const T& z, const T& w) : xy(xy), z(z), w(w) {};
	inline tvec(const T& x, const T& y, const tvec<2, T>& zw) : x(x), y(y), zw(zw) {};
	inline tvec(const tvec<2, T>& xy, const tvec<2, T>& zw) : xy(xy), zw(zw) {};
	inline tvec(const tvec<3, T>& xyz, const T& w) : xyz(xyz), w(w) {};

	TVEC_BODY(4)
};

#undef TVEC_BODY

TVEC_TEMPLATE inline tvec<N,T> pow(const tvec<N,T>& a, const tvec<N,T>& b) {
	tvec<N,T> r;
	if (std::is_same<T, double>())
		rpt(i,N) r.v[i] = (T)pow(a.v[i], b.v[i]);
	else
		rpt(i,N) r.v[i] = (T)powf(a.v[i], b.v[i]);
	return r;
}
TVEC_TEMPLATE inline tvec<N,T> pow(const tvec<N,T>& a, const T b) {
	tvec<N,T> r;
	if (std::is_same<T, double>())
		rpt(i,N) r.v[i] = (T)pow(a.v[i], b);
	else
		rpt(i,N) r.v[i] = (T)powf(a.v[i], b);
	return r;
}

TVEC_TEMPLATE inline tvec<N,T> sin(const tvec<N,T>& s) {
	tvec<N,T> r;
	if (std::is_same<T, double>())
		rpt(i,N) r.v[i] = (T)sin(s.v[i]);
	else
		rpt(i,N) r.v[i] = (T)sinf(s.v[i]);
	return r;
}
TVEC_TEMPLATE inline tvec<N,T> cos(const tvec<N,T>& s) {
	tvec<N,T> r;
	if (std::is_same<T, double>())
		rpt(i,N) r.v[i] = (T)cos(s.v[i]);
	else
		rpt(i,N) r.v[i] = (T)cosf(s.v[i]);
	return r;
}
TVEC_TEMPLATE inline tvec<N,T> tan(const tvec<N,T>& s) {
	tvec<N,T> r;
	if (std::is_same<T, double>())
		rpt(i,N) r.v[i] = (T)tan(s.v[i]);
	else
		rpt(i,N) r.v[i] = (T)tanf(s.v[i]);
	return r;
}

template<typename T> inline T degrees(const T& r) { return r * (T)(180.0 / M_PI); }
template<typename T> inline T radians(const T& d) { return d * (T)(M_PI / 180.0); }
TVEC_TEMPLATE inline tvec<N,T> degrees(const tvec<N,T>& r) { return r * (T)(180.0 / M_PI); }
TVEC_TEMPLATE inline tvec<N,T> radians(const tvec<N,T>& d) { return d * (T)(M_PI / 180.0); }

TVEC_TEMPLATE inline float dot(const tvec<N,T>& a, const tvec<N,T>& b) {
	T r = 0;
	tvec<N,T> m = a * b;
	rpt(i,N) r += m.v[i];
	return r;
}
TVEC_TEMPLATE inline float length(const tvec<N,T>& v) { return sqrtf(dot(v, v)); }
TVEC_TEMPLATE inline tvec<N,T> normalize(const tvec<N,T>& v) { return v / length(v); }

template<typename T> inline tvec<3,T> cross(const tvec<3,T>& a, const tvec<3,T>& b) {
	tvec<3,T> m1(a[1], a[2], a[0]);
	tvec<3,T> m2(b[2], b[0], b[1]);
	tvec<3,T> m3(b[1], b[2], b[0]);
	tvec<3,T> m4(a[2], a[0], a[1]);
	return m1 * m2 - m3 * m4;
}

template<typename T> struct tquat {
	static_assert(std::is_floating_point<T>::value, "Quaternion must be of floating-point type.");

	union {
		T v[4];
		struct { T x, y, z, w; };
		tvec<3,T> xyz;
		tvec<4,T> xyzw;
	};

	inline tquat(T x, T y, T z, T w) : x(x), y(y), z(z), w(w) {};
	inline tquat(tvec<3,T> euler) {
		euler /= 2;
		tvec<3,T> c = cos(euler);
		tvec<3,T> s = sin(euler);
		
		tvec<4,T> m0(s.x, c.x, c.x, c.x);
		tvec<4,T> m1(c.y, s.y, c.y, c.y);
		tvec<4,T> m2(c.z, c.z, s.z, c.z);

		tvec<4,T> m3(-c.x, s.x, -s.x, s.x);
		tvec<4,T> m4(s.y, c.y, s.y, s.y);
		tvec<4,T> m5(s.z, s.z, c.z, s.z);

		xyzw = m0*m1*m2 + m3*m4*m5;
	};
	inline tquat(T angle, const tvec<3,T>& axis) {
		angle /= 2;
		xyz = axis * sinf(angle);
		w = cosf(angle);
	};
	inline tquat() : tquat(0, 0, 0, 1) {};

	inline static tquat FromTo(const tvec<3,T>& v1, const tvec<3,T>& v2){
		float d = dot(v1, v2);
		if (d < -0.999999f) {
			tvec<3,T> tmp = cross(tvec<3,T>(1, 0, 0), v1);
			if (dot(tmp, tmp) < 1e-5f) tmp = cross(tvec<3,T>(0, 1, 0), v1);
			tquat r;
			r.xyz = normalize(tmp) * sinf(M_PI / 2);
			r.w = cosf(M_PI / 2);
			return r;
		} else if (d > 0.999999f) {
			return tquat(0,0,0,1);
		} else {
			tquat r;
			r.xyz = cross(v1, v2);
			r.w = 1 + d;
			r.xyzw /= length(r.xyzw);
			return r;
		}
	}
	// Expects normalized vectors
	inline static tquat Look(const tvec<3,T>& fwd, const tvec<3,T>& up) {
		tvec<3,T> right = normalize(cross(up, fwd));
		tvec<3,T> up_cross = cross(fwd, right);
		T m00 = right.x;
		T m01 = right.y;
		T m02 = right.z;
		T m10 = up_cross.x;
		T m11 = up_cross.y;
		T m12 = up_cross.z;
		T m20 = fwd.x;
		T m21 = fwd.y;
		T m22 = fwd.z;

		T num8 = (m00 + m11) + m22;
		tquat q(0);
		if (num8 > 0.0) {
			T num = sqrtf(num8 + 1);
			q.w = num / 2;
			num = 1 / (num / 2);
			q.x = (m12 - m21) * num;
			q.y = (m20 - m02) * num;
			q.z = (m01 - m10) * num;
			return q;
		}
		if ((m00 >= m11) && (m00 >= m22)) {
			T num7 = sqrtf(((1 + m00) - m11) - m22);
			T num4 = 1 / (num7 / 2);
			q.x = num7 / 2;
			q.y = (m01 + m10) * num4;
			q.z = (m02 + m20) * num4;
			q.w = (m12 - m21) * num4;
			return q;
		}
		if (m11 > m22) {
			T num6 = sqrtf(((1 + m11) - m00) - m22);
			T num3 = 1 / (num6 / 2);
			q.x = (m10 + m01) * num3;
			q.y = num6 / 2;
			q.z = (m21 + m12) * num3;
			q.w = (m20 - m02) * num3;
			return q;
		}
		T num5 = sqrtf(((1 + m22) - m00) - m11);
		T num2 = 1 / (num5 / 2);
		q.x = (m20 + m02) * num2;
		q.y = (m21 + m12) * num2;
		q.z = num5 / 2;
		q.w = (m01 - m10) * num2;
		return q;
	}
	
	inline tvec<3,T> euler() const {
		tvec<3,T> sq = xyzw * xyzw;
		T unit = sq.x + sq.y + sq.z + sq.w;
		T test = x * y + z * w;
		if (test > unit/2) 
			return tvec<3,T>(0, 2 * atan2f(x, w), M_PI/2);
		if (test < -unit/2) 
			return tvec<3,T>(0, -2 * atan2f(x, w), -M_PI/2);
		return tvec<3,T>(
			atan2f(2 * x * w - 2 * y * z, -sq.x + sq.y - sq.z + sq.w),
			atan2f(2 * y * w - 2 * x * z, sq.x - sq.y - sq.z + sq.w),
			asinf(2 * test / unit) );
	}

	inline tquat operator =(const tquat& q) {
		rpt(i,4) v[i] = q.v[i];
		return *this;
	}

	inline tquat operator +(const tquat& s) const {
		tquat r;
		rpt(i,4) r.v[i] = v[i] + s.v[i];
		return r;
	}
	inline tquat operator -(const tquat& s) const {
		tquat r;
		rpt(i,4) r.v[i] = v[i] - s.v[i];
		return r;
	}

	inline tquat operator *(const tquat& s) const {
		return tquat(
			w * s.x + s.w * x + y * s.z - s.y * z,
			w * s.y + s.w * y + z * s.x - s.z * x,
			w * s.z + s.w * z + x * s.y - s.x * y,
			w * s.w - x * s.x - y * s.y - z * s.z);
	}
	inline tquat operator *=(const tquat& s) {
		*this = *this * s;
		return *this;
	}

	inline tquat operator *(const T s) const {
		tquat r;
		rpt(i,4) r.v[i] = v[i] * s;
		return r;
	}
	inline tquat operator *=(const T s) {
		rpt(i,4) v[i] *= s;
		return *this;
	}

	inline tquat operator /(const T s) const { return operator*(1 / s); }
	inline tquat operator /=(const T s) { return operator*=(1 / s); }

	inline tvec<3,T> operator *(const tvec<3,T>& vec) const {
		return 2 * dot(xyz, vec) * xyz + (w * w - dot(xyz, xyz)) * vec + 2 * w * cross(xyz, vec);
	}

	inline bool operator ==(const tquat& a) const {
		rpt(i,4) if (v[i] != a.v[i]) return false;
		return true;
	}
	inline bool operator !=(const tquat& a) const { return !operator ==(a); }
};

// Column-major 4x4 matrix
template<typename T> struct tmat4x4 {
	static_assert(std::is_floating_point<T>::value, "Matrix must be of floating-point type.");

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
	inline tmat4x4(const tvec<4,T>& c1, const tvec<4,T>& c2, const tvec<4,T>& c3, const tvec<4,T>& c4) { v[0] = c1; v[1] = c2; v[2] = c3; v[3] = c4; };
	inline tmat4x4(const T& s) : tmat4x4(
		s, 0, 0, 0,
		0, s, 0, 0,
		0, 0, s, 0,
		0, 0, 0, s) {};
	inline tmat4x4() : tmat4x4(1) {};
	inline tmat4x4(const tquat<T>& q) : tmat4x4(1) {
		tvec<3,T> q2 = q.xyz * q.xyz;
		tvec<3,T> qw = q.xyz * q.w;
		tvec<3,T> c = tvec<3,T>(q.x, q.x, q.y) * tvec<3,T>(q.z, q.y, q.z);

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

	inline void Decompose(tvec<3,T>* position, tquat<T>* rotation, tvec<3,T>* scale) {
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

		if (!rotation) return;

		uint32_t i, j, k = 0;
		T root, trace = rows[0].x + rows[1].y + rows[2].z;
		if (trace > 0) {
			root = sqrt(trace + 1.f);
			rotation->w = .5f * root;
			root = .5f / root;
			rotation->x = root * (rows[1].z - rows[2].y);
			rotation->y = root * (rows[2].x - rows[0].z);
			rotation->z = root * (rows[0].y - rows[1].x);
		} else {
			static const uint32_t next[3] { 1, 2, 0 };
			i = 0;
			if (rows[1].y > rows[0].x) i = 1;
			if (rows[2].z > rows[i][i]) i = 2;
			j = next[i];
			k = next[j];

			root = sqrtf(rows[i][i] - rows[j][j] - rows[k][k] + 1.f);

			rotation->v[i] = .5f * root;
			root = .5f / root;
			rotation->v[j] = root * (rows[i][j] + rows[j][i]);
			rotation->v[k] = root * (rows[i][k] + rows[k][i]);
			rotation->w = root * (rows[j][k] - rows[k][j]);
		}
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
	inline static tmat4x4 PerspectiveFov(T fovy, T aspect, T near, T far) {
		T df = 1 / (far - near);
		T sy = 1 / tan(fovy / 2);
		tmat4x4 r(0);
		r[0][0] = sy / aspect;
		r[1][1] = sy;
		r[2][2] = far * df;
		r[3][2] = -far * near * df;
		r[2][3] = 1;
		return r;
	}
	inline static tmat4x4 Perspective(T width, T height, T near, T far) {
		T df = 1 / (far - near);
		tmat4x4 r(0);
		r[0][0] = 2 * near / width;
		r[1][1] = 2 * near / height;
		r[2][2] = far * df;
		r[3][2] = -far * near * df;
		r[2][3] = 1;
		return r;
	}
	inline static tmat4x4 Perspective(T left, T right, T top, T bottom, T near, T far) {
		T df = 1 / (far - near);
		tmat4x4 r(0);
		r[0][0] = 2 * near / (right - left);
		r[1][1] = 2 * near / (top - bottom);
		r[2][0] = (right + left) / (right - left);
		r[2][1] = (top + bottom) / (top - bottom);
		r[2][2] = far * df;
		r[3][2] = -far * near * df;
		r[2][3] = 1;
		return r;
	}
	inline static tmat4x4 Orthographic(T width, T height, T near, T far) {
		T df = 1 / (far - near);
		tmat4x4 r(1);
		r[0][0] = 2 / width;
		r[1][1] = 2 / height;
		r[2][2] = df;
		r[3][2] = -near * df;
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
		T c = cosf(r);
		T s = sinf(r);
		return tmat4x4(
			1, 0, 0, 0,
			0, c, -s, 0,
			0, s, c, 0,
			0, 0, 0, 1 );
	}
	inline static tmat4x4 RotateY(T r) {
		T c = cosf(r);
		T s = sinf(r);
		return tmat4x4(
			c, 0, s, 0,
			0, 0, 0, 0,
			-s, 0, c, 0,
			0, 0, 0, 1 );
	}
	inline static tmat4x4 RotateZ(T r) {
		T c = cosf(r);
		T s = sinf(r);
		return tmat4x4(
			c, -s, 0, 0,
			s, c, 0, 0,
			0, 0, 0, 0,
			0, 0, 0, 1 );
	}

	inline static tmat4x4 TRS(const tvec<3,T>& t, const tquat<T>& r, const tvec<3,T>& s) {
		tmat4x4 rm(r);
		rpt(i,3) rm.v[i] *= s.v[i];
		rm.v[3].xyz = t;
		return rm;
	}

	inline tvec<4,T>& operator[](uint32_t i) { return v[i]; }
	inline tvec<4,T> operator[](uint32_t i) const { return v[i]; }

	inline tmat4x4 operator=(const tmat4x4& m) {
		rpt(i,4) v[i] = m.v[i];
		return *this;
	}

	inline tmat4x4 operator+=(const tmat4x4& m) {
		rpt(i,4) v[i] += m.v[i];
		return *this;
	}

	inline tmat4x4 operator+=(const T& s) {
		rpt(i,4) v[i] += s;
		return *this;
	}

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
	inline tmat4x4 operator*=(const T& s) {
		rpt(i,4) v[i] *= s;
		return *this;
	}
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
	inline tmat4x4 operator*=(const tmat4x4& m) {
		*this = operator*(m);
		return *this;
	}

	inline bool operator ==(const tmat4x4& a) const {
		rpt(i,4) if (v[i] != a.v[i]) return false;
		return true;
	}
	inline bool operator !=(const tmat4x4& a) const { return !operator ==(a); }
	
	inline friend tmat4x4 operator*(const float& s, const tmat4x4& m) {
		tmat4x4 r;
		rpt(i,4) r.v[i] = m.v[i] * s;
		return r;
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

template<typename T> inline tquat<T> inverse(const tquat<T>& q) {
	T s = 1 / dot(q.xyzw, q.xyzw);
	return tquat<T>(-q.x, -q.y, -q.z, q.w) * s;
}

template<typename T> inline tquat<T> normalize(const tquat<T>& q){
	float l = length(q.xyzw);
	return l == 0 ? q : q / l;
}

template<typename T> inline tmat4x4<T> transpose(const tmat4x4<T>& m) {
	return tmat4x4<T>(
		m[0][0], m[0][1], m[0][2], m[0][3],
		m[1][0], m[1][1], m[1][2], m[1][3],
		m[2][0], m[2][1], m[2][2], m[2][3],
		m[3][0], m[3][1], m[3][2], m[3][3] );
}

TVEC_TEMPLATE inline tvec<N,T> min(const tvec<N,T>& a, const tvec<N,T>& b) {
	tvec<N,T> r;
	if (std::is_integral<T>())
		rpt(i,N) r.v[i] = std::min(a.v[i], b.v[i]);
	else if (std::is_same<T, double>())
		rpt(i,N) r.v[i] = fmin(a.v[i], b.v[i]);
	else
		rpt(i,N) r.v[i] = fminf(a.v[i], b.v[i]);
	return r;
}
TVEC_TEMPLATE inline tvec<N,T> max(const tvec<N,T>& a, const tvec<N,T>& b) {
	tvec<N,T> r;
	if (std::is_integral<T>())
		rpt(i,N) r.v[i] = std::max(a.v[i], b.v[i]);
	else if (std::is_same<T, double>())
		rpt(i,N) r.v[i] = fmax(a.v[i], b.v[i]);
	else
		rpt(i,N) r.v[i] = fmaxf(a.v[i], b.v[i]);
	return r;
}
TVEC_TEMPLATE inline tvec<N,T> min(const tvec<N,T>& a, const T& b) {
	tvec<N,T> r;
	if (std::is_integral<T>())
		rpt(i,N) r.v[i] = std::min(a.v[i], b);
	else if (std::is_same<T, double>())
		rpt(i,N) r.v[i] = fmin(a.v[i], b);
	else
		rpt(i,N) r.v[i] = fminf(a.v[i], b);
	return r;
}
TVEC_TEMPLATE inline tvec<N,T> max(const tvec<N,T>& a, const T& b) {
	tvec<N,T> r;
	if (std::is_integral<T>())
		rpt(i,N) r.v[i] = std::max(a.v[i], b);
	else if (std::is_same<T, double>())
		rpt(i,N) r.v[i] = fmax(a.v[i], b);
	else
		rpt(i,N) r.v[i] = fmaxf(a.v[i], b);
	return r;
}
TVEC_TEMPLATE inline tvec<N,T> min(const T& a, const tvec<N,T>& b) { return ::min(b, a); }
TVEC_TEMPLATE inline tvec<N,T> max(const T& a, const tvec<N,T>& b) { return ::min(b, a); }

TVEC_TEMPLATE inline tvec<N,T> clamp(const tvec<N,T>& a, const tvec<N,T>& l, const tvec<N,T>& h) {
	tvec<N,T> r;
	if (std::is_integral<T>())
		rpt(i,N) r.v[i] = std::min(std::max(a.v[i], l.v[i]), h.v[i]);
	else if (std::is_same<T, double>())
		rpt(i,N) r.v[i] = fmin(fmax(a.v[i], l.v[i]), h.v[i]);
	else
		rpt(i,N) r.v[i] = fminf(fmax(a.v[i], l.v[i]), h.v[i]);
	return r;
}
TVEC_TEMPLATE inline tvec<N,T> clamp(const tvec<N,T>& a, const tvec<N,T>& l, const T& h) {
	tvec<N,T> r;
	if (std::is_integral<T>())
		rpt(i,N) r.v[i] = std::min(std::max(a.v[i], l.v[i]), h);
	else if (std::is_same<T, double>())
		rpt(i,N) r.v[i] = fmin(fmax(a.v[i], l.v[i]), h);
	else
		rpt(i,N) r.v[i] = fminf(fmax(a.v[i], l.v[i]), h);
	return r;
}
TVEC_TEMPLATE inline tvec<N,T> clamp(const tvec<N,T>& a, const T& l, const tvec<N,T>& h) {
	tvec<N,T> r;
	if (std::is_integral<T>())
		rpt(i,N) r.v[i] = std::min(std::max(a.v[i], l), h.v[i]);
	else if (std::is_same<T, double>())
		rpt(i,N) r.v[i] = fmin(fmax(a.v[i], l), h.v[i]);
	else
		rpt(i,N) r.v[i] = fminf(fmax(a.v[i], l), h.v[i]);
	return r;
}
TVEC_TEMPLATE inline tvec<N,T> clamp(const tvec<N,T>& a, const T& l, const T& h) {
	tvec<N,T> r;
	if (std::is_integral<T>())
		rpt(i,N) r.v[i] = std::min(std::max(a.v[i], l), h);
	else if (std::is_same<T, double>())
		rpt(i,N) r.v[i] = fmin(fmax(a.v[i], l), h);
	else
		rpt(i,N) r.v[i] = fminf(fmax(a.v[i], l), h);
	return r;
}

TVEC_TEMPLATE inline tvec<N,T> abs(const tvec<N,T>& a) {
	tvec<N,T> r;
	if (std::is_integral<T>())
		rpt(i,N) r.v[i] = abs(a.v[i]);
	else if (sizeof(T) == sizeof(double))
		rpt(i,N) r.v[i] = fabs(a.v[i]);
	else
		rpt(i,N) r.v[i] = fabsf(a.v[i]);
	return r;
}
TVEC_TEMPLATE inline tvec<N,T> floor(const tvec<N,T>& a) {
	if (std::is_integral<T>()) return a;
	tvec<N,T> r;
	if (sizeof(T) == sizeof(double))
		rpt(i,N) r.v[i] = ::floor(a.v[i]);
	else
		rpt(i,N) r.v[i] = floorf(a.v[i]);
	return r;
}
TVEC_TEMPLATE inline tvec<N,T> ceil(const tvec<N,T>& a) {
	if (std::is_integral<T>()) return a;
	tvec<N,T> r;
	if (sizeof(T) == sizeof(double))
		rpt(i,N) r.v[i] = ::ceil(a.v[i]);
	else
		rpt(i,N) r.v[i] = ceilf(a.v[i]);
	return r;
}

TVEC_TEMPLATE inline tvec<N,T> frac(const tvec<N,T>& a) { return a - floor(a); }
TVEC_TEMPLATE inline tvec<N,T> lerp(const tvec<N,T>& a, const tvec<N,T>& b, const float t) { return a + (b - a)*t; }
template<typename T> inline tquat<T> lerp(const tquat<T>& a, const tquat<T>& b, const T t) { return a + (b - a) * t; }

template<typename T> inline tquat<T> slerp(const tquat<T>& v0, tquat<T> v1, const T t){
	T d = dot(v0.xyzw, v1.xyzw);
	if (d < 0) {
		v1.xyzw = -v1.xyzw;
		d = -d;
	}

	if (d > .9995f) return normalize(lerp(v0, v1, t));

	T theta_0 = acosf(d);
	T theta = theta_0*t;
	T sin_theta = sin(theta);
	T sin_theta_0 = sin(theta_0);

	T s0 = cosf(theta) - d * sin_theta / sin_theta_0;
	T s1 = sin_theta / sin_theta_0;
	return v0 * s0 + v1 * s1;
}

template <class T> inline void hash_combine(std::size_t& seed, const T& v) {
	std::hash<T> hasher;
	seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

namespace std {
	template<uint32_t N, typename T>
	struct hash<tvec<N,T>> {
		inline std::size_t operator()(const tvec<N,T>& v) const {
			std::size_t h = 0;
			rpt(i,N) hash_combine(h, v[i]);
			return h;
		}
	};
	
	template<typename T>
	struct hash<tquat<T>> {
		inline std::size_t operator()(const tquat<T>& v) const {
			std::size_t h = 0;
			rpt(i,4) hash_combine(h, v[i]);
			return h;
		}
	};
	
	template<typename T>
	struct hash<tmat4x4<T>> {
		inline std::size_t operator()(const tmat4x4<T>& v) const {
			std::size_t h = 0;
			rpt(i,4) hash_combine(h, v[i]);
			return h;
		}
	};
}


typedef tvec<2, int32_t> int2;
typedef tvec<3, int32_t> int3;
typedef tvec<4, int32_t> int4;
typedef tvec<2, uint32_t> uint2;
typedef tvec<3, uint32_t> uint3;
typedef tvec<4, uint32_t> uint4;
typedef tvec<2, double> double2;
typedef tvec<3, double> double3;
typedef tvec<4, double> double4;
typedef tvec<2, float> float2;
typedef tvec<3, float> float3;
typedef tvec<4, float> float4;
typedef tmat4x4<float> float4x4;
typedef tquat<float> quaternion;


#undef rpt
#ifdef WINDOWS
#pragma warning(pop)
#endif
#pragma pack(pop)
#endif