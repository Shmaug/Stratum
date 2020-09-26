#pragma once

#include "tvec.hpp"

#define rpt(i,n) for (uint32_t i = 0; i < n; ++i)

namespace stm {

template<typename T>
class tquat : public tvec<4, T> {
public:
	inline tquat() : tvec(0, 0, 0, 1) {};
	inline tquat(const T& x, const T& y, const T& z, const T& w) : tvec(x,y,z,w) {}
	inline tquat(const tvec<3, T>& xyz, const T& w) : tvec(xyz, w) {}
	inline tquat(const tvec<4, T>& xyzw) : tvec(xyzw) {}

	inline tquat operator-() const {
		tquat r;
		rpt(i,N) r.v[i] = -v[i];
		return r;
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

	inline tvec<3, T> euler() const {
		tvec<3,T> sq = xyzw * xyzw;
		T unit = sq.x + sq.y + sq.z + sq.w;
		T test = x * y + z * w;
		if (test > unit/2) 
			return tvec<3,T>(0, 2 * std::atan2(x, w), M_PI/2);
		if (test < -unit/2) 
			return tvec<3,T>(0, -2 * std::atan2(x, w), -M_PI/2);
		return tvec<3,T>(
			std::atan2(2 * x * w - 2 * y * z, -sq.x + sq.y - sq.z + sq.w),
			std::atan2(2 * y * w - 2 * x * z, sq.x - sq.y - sq.z + sq.w),
			std::asin(2 * test / unit) );
	}
	
	inline static tquat FromTo(const tvec<3, T>& v1, const tvec<3, T>& v2){
		T d = dot(v1, v2);
		if (d < -1) {
			tvec<3,T> tmp = cross(tvec<3,T>(1, 0, 0), v1);
			if (dot(tmp, tmp) < std::numeric_limits<T>::epsilon()) tmp = cross(tvec<3,T>(0, 1, 0), v1);
			tquat r;
			r.xyz = normalize(tmp) * std::sin((T)M_PI / 2);
			r.w = std::cos((T)M_PI / 2);
			return r;
		} else if (d > 1) {
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
	inline static tquat Look(const tvec<3, T>& fwd, const tvec<3, T>& up) {
		tvec3<T> right = normalize(cross(up, fwd));
		tvec3<T> up_cross = cross(fwd, right);
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
		tquat q;
		if (num8 > 0) {
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
	inline static tquat Euler(tvec<3, T> euler) {
		euler /= 2;
		tvec<3,T> c = cos(euler);
		tvec<3,T> s = sin(euler);
		
		tvec<4,T> m0(s.x, c.x, c.x, c.x);
		tvec<4,T> m1(c.y, s.y, c.y, c.y);
		tvec<4,T> m2(c.z, c.z, s.z, c.z);

		tvec<4,T> m3(-c.x, s.x, -s.x, s.x);
		tvec<4,T> m4(s.y, c.y, s.y, s.y);
		tvec<4,T> m5(s.z, s.z, c.z, s.z);

		return tquat(m0*m1*m2 + m3*m4*m5);
	}
	inline static tquat Euler(const T& x, const T& y, const T& z) { return Euler(tvec<3,T>(x,y,z)); }
	inline static tquat AxisAngle(const tvec<3, T>& axis, T angle) {
		angle /= 2;
		return tquat(axis * std::sin(angle), std::cos(angle));
	}
};

template<typename T> inline tquat<T> inverse(const tquat<T>& q) {
	T s = dot(q, q);
	return tquat<T>(-q.x, -q.y, -q.z, q.w) / s;
}

template<typename T> inline tquat<T> normalize(const tquat<T>& q){
	float l = length(q);
	return l == 0 ? q : q / l;
}

template<typename T> inline tquat<T> lerp(const tquat<T>& a, const tquat<T>& b, const T& t) { return a + (b - a) * t; }
template<typename T> inline tquat<T> slerp(const tquat<T>& v0, tquat<T> v1, const T& t){
	T d = dot(v0, v1);
	if (d < 0) {
		v1 = -v1;
		d = -d;
	}

	if (d > 1 - std::numeric_limits<T>::epsilon()) return normalize(lerp(v0, v1, t));

	T theta_0 = std::acos(d);
	T theta = theta_0*t;
	T sin_theta = std::sin(theta);
	T sin_theta_0 = std::sin(theta_0);

	T s0 = std::cos(theta) - d * sin_theta / sin_theta_0;
	T s1 = sin_theta / sin_theta_0;
	return v0 * s0 + v1 * s1;
}

typedef tquat<float> quaternion;

}

namespace std {
	
template<typename T>
struct hash<stm::tquat<T>> {
	inline size_t operator()(const stm::tquat<T>& v) const {
		size_t h = 0;
		rpt(i,4) stm::hash_combine(h, v[i]);
		return h;
	}
};

}

#undef rpt