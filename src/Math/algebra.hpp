#pragma once

#include "valrange.hpp"

namespace stm {

template<typename T> concept has_inner_product = requires(T a, T b) { inner_product(ranges::begin(a), ranges::end(a), ranges::begin(b), ranges::range_value_t<T>()); };
template<has_inner_product T> inline auto dot(const T& a, const T& b) { return inner_product(a.begin(), a.end(), b.begin(), {}); }
template<has_inner_product T> inline auto length(const T& v) { return sqrt(dot(v, v)); }
template<has_inner_product T> inline T normalize(const T& v) { return v/length(v); }

template<typename T, typename Tt> inline T lerp(const T& x, const T& y, const Tt& t) { return x*(1-t) + y*t; }
template<has_inner_product T, typename Tt> inline T slerp(const T& a, const T& b, const Tt& t) {
	auto d = dot(a, b);
	auto ad = abs(a, b);
	if (ad >= 1) return normalize(lerp(a, b, t));
	auto theta0 = acos(ad);
	auto theta = theta0*t;
	auto s1 = sin(theta)/sin(theta0);
	auto s0 = cos(theta) - ad*s1;
	return d < 0 ?  normalize(a*s0 - b*s1) : normalize(a*s0 + b*s1);
}


template<typename T> class vec2_t : public valrange<array<T,2>> {
public:
	inline T& x() { return array<T,2>::at(0); }
	inline T& y() { return array<T,2>::at(1); }
	inline const T& x() const { return array<T,2>::at(0); }
	inline const T& y() const { return array<T,2>::at(1); }
};
template<typename T> class vec3_t : public valrange<array<T,3>> {
public:
	inline T& x() { return array<T,3>::at(0); }
	inline T& y() { return array<T,3>::at(1); }
	inline T& z() { return array<T,3>::at(2); }
	inline vec2_t<T>& xy() { return *(vec2_t<T>*)&array<T,3>::at(0); }
	inline vec2_t<T>& yz() { return *(vec2_t<T>*)&array<T,3>::at(1); }
	inline const T& x() const { return array<T,3>::at(0); }
	inline const T& y() const { return array<T,3>::at(1); }
	inline const T& z() const { return array<T,3>::at(2); }
	inline const vec2_t<T>& xy() const { return *(vec2_t<T>*)&array<T,3>::at(0); }
	inline const vec2_t<T>& yz() const { return *(vec2_t<T>*)&array<T,3>::at(1); }
		
	inline vec3_t cross(const vec3_t& lhs, const vec3_t& rhs) {
		vec3_t m1(lhs[1], lhs[2], lhs[0]);
		vec3_t m2(rhs[2], rhs[0], rhs[1]);
		vec3_t m3(rhs[1], rhs[2], rhs[0]);
		vec3_t m4(lhs[2], lhs[0], lhs[1]);
		return m1*m2 - m3*m4;
	}
};
template<typename T> class vec4_t : public valrange<array<T,3>> {
public:
	inline T& x() { return array<T,4>::at(0); }
	inline T& y() { return array<T,4>::at(1); }
	inline T& z() { return array<T,4>::at(2); }
	inline T& w() { return array<T,4>::at(3); }
	inline vec2_t<T>& xy() { return *(vec2_t<T>*)&array<T,4>::at(0); }
	inline vec2_t<T>& yz() { return *(vec2_t<T>*)&array<T,4>::at(1); }
	inline vec2_t<T>& zw() { return *(vec2_t<T>*)&array<T,4>::at(2); }
	inline vec3_t<T>& xyz() { return *(vec3_t<T>*)&array<T,4>::at(0); }
	inline vec3_t<T>& yzw() { return *(vec3_t<T>*)&array<T,4>::at(1); }
	inline const T& x() const { return array<T,4>::at(0); }
	inline const T& y() const { return array<T,4>::at(1); }
	inline const T& z() const { return array<T,4>::at(2); }
	inline const T& w() const { return array<T,4>::at(3); }
	inline const vec2_t<T>& xy() const { return *(vec2_t<T>*)&array<T,4>::at(0); }
	inline const vec2_t<T>& yz() const { return *(vec2_t<T>*)&array<T,4>::at(1); }
	inline const vec2_t<T>& zw() const { return *(vec2_t<T>*)&array<T,4>::at(2); }
	inline const vec3_t<T>& xyz() const { return *(vec3_t<T>*)&array<T,4>::at(0); }
	inline const vec3_t<T>& yzw() const { return *(vec3_t<T>*)&array<T,4>::at(1); }
};

template<typename T> class quaternion : public array<T,4> {
public:
	inline T& i() { return array<T,4>::at(0); }
	inline T& j() { return array<T,4>::at(1); }
	inline T& k() { return array<T,4>::at(2); }
	inline T& a() { return array<T,4>::at(3); }
	inline vec3_t<T>& ijk() { return *(vec3_t<T>*)&array<T,4>::at(0); }
	inline const T& x() const { return array<T,4>::at(0); }
	inline const T& y() const { return array<T,4>::at(1); }
	inline const T& z() const { return array<T,4>::at(2); }
	inline const T& a() const { return array<T,4>::at(3); }

	inline quaternion operator-() const { quaternion q; ranges::transform(*this, ranges::begin(q), negate<T>()); return q; }
	inline quaternion operator+(const quaternion& rhs) const { quaternion q; ranges::transform(*this, rhs, ranges::begin(q), plus<T>()); return q; }
	inline quaternion operator-(const quaternion& rhs) const { quaternion q; ranges::transform(*this, rhs, ranges::begin(q), minus<T>()); return q; }
	inline quaternion operator*(const quaternion& rhs) const {
		return quaternion(
			a()*rhs.i() + rhs.a()*i() + j()*rhs.k() - rhs.j()*k(),
			a()*rhs.j() + rhs.a()*j() + k()*rhs.i() - rhs.k()*i(),
			a()*rhs.k() + rhs.a()*k() + i()*rhs.j() - rhs.i()*j(),
			a()*rhs.a() - i()*rhs.i() - j()*rhs.j() - k()*rhs.k() );
	}
	inline quaternion& operator+=(const quaternion& rhs) { ranges::transform(*this, rhs, ranges::begin(*this), plus<T>()); return *this; }
	inline quaternion& operator-=(const quaternion& rhs) { ranges::transform(*this, rhs, ranges::begin(*this), minus<T>()); return *this; }
	inline quaternion& operator*=(const quaternion& rhs) { return *this = operator*(rhs); }

	inline quaternion conjugate() const { return quaternion(-i(), -j(), -k(), a()); }
	inline quaternion inverse() const { return normalize(conjugate()); }

	inline vec3_t<T> operator*(const vec3_t<T>& v) const {
		return 2*dot(ijk(), v)*ijk() + (a()*a() - dot(ijk(), ijk()))*v + 2*a()*cross(ijk(), v);
	}

	inline vec3_t<T> euler() const {
		return vec3_t<T>(
			std::atan2(2*(i()*a() - j()*k()) , -i()*i() + j()*j() - k()*k() + a()*a()),
			std::atan2(2*(j()*a() - i()*k()) ,  i()*i() - j()*j() - k()*k() + a()*a()),
			std::asin (2*(k()*a() + i()*j()) / (i()*i() + j()*j() + k()*k() + a()*a())) );
	}
	
	// Expects normalized vectors
	inline static quaternion FromTo(const vec3_t<T>& from, const vec3_t<T>& to) {
		T d = dot(from, to);
		if (d <= -1) return quaternion(0,0,0,-1);
		else if (d >= 1) return quaternion(0,0,0,1);
		else {
			quaternion r(cross(from, to), 1+d);
			r /= length(r.xyzw);
			return r;
		}
	}
	inline static quaternion Look(const vec3_t<T>& fwd, const vec3_t<T>& up) {
		vec3_t<T> right = normalize(cross(up, fwd));
		vec3_t<T> up_cross = cross(fwd, right);
		T m00 = right[0];
		T m01 = right[1];
		T m02 = right[2];
		T m10 = up_cross[0];
		T m11 = up_cross[1];
		T m12 = up_cross[2];
		T m20 = fwd[0];
		T m21 = fwd[1];
		T m22 = fwd[2];

		T num8 = (m00 + m11) + m22;
		quaternion q;
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
	inline static quaternion Euler(vec3_t<T> euler) {
		euler /= 2;
		vec3_t<T> c = cos(euler);
		vec3_t<T> s = sin(euler);
		vec4_t<T> m0( s.x(), c.x(),  c.x(), c.x());
		vec4_t<T> m1( c.y(), s.y(),  c.y(), c.y());
		vec4_t<T> m2( c.z(), c.z(),  s.z(), c.z());
		vec4_t<T> m3(-c.x(), s.x(), -s.x(), s.x());
		vec4_t<T> m4( s.y(), c.y(),  s.y(), s.y());
		vec4_t<T> m5( s.z(), s.z(),  c.z(), s.z());
		return m0*m1*m2 + m3*m4*m5;
	}
	inline static quaternion AxisAngle(const vec3_t<T>& axis, T angle) {
		angle /= 2;
		return quaternion(axis * std::sin(angle), std::cos(angle));
	}
};


// M rows x N columns
template<typename T, size_t M, size_t N> requires(M >= 1 && N >= 1)
class matrix : public valrange< valrange<array< valrange<array<T,N>>, M> > > {
public:
	using column = valrange<array<T,M>>;

	template<typename T> inline matrix<T,N,M> T(const matrix<T>& m) {
		return matrix<T,M,N>(
			(at(0).T(), ...)
		);
	}

	template<typename Ty, size_t C>
	inline matrix operator*(const matrix<Ty,N,C>& m) const {

	}
};

template<typename T> inline matrix<T,4,4> inverse(const matrix<T,4,4>& m) {
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

	vec4_t<T> f0(c00, c00, c02, c03);
	vec4_t<T> f1(c04, c04, c06, c07);
	vec4_t<T> f2(c08, c08, c10, c11);
	vec4_t<T> f3(c12, c12, c14, c15);
	vec4_t<T> f4(c16, c16, c18, c19);
	vec4_t<T> f5(c20, c20, c22, c23);

	vec4_t<T> v0(m[1][0], m[0][0], m[0][0], m[0][0]);
	vec4_t<T> v1(m[1][1], m[0][1], m[0][1], m[0][1]);
	vec4_t<T> v2(m[1][2], m[0][2], m[0][2], m[0][2]);
	vec4_t<T> v3(m[1][3], m[0][3], m[0][3], m[0][3]);

	vec4_t<T> i0(v1 * f0 - v2 * f1 + v3 * f2);
	vec4_t<T> i1(v0 * f0 - v2 * f3 + v3 * f4);
	vec4_t<T> i2(v0 * f1 - v1 * f3 + v3 * f5);
	vec4_t<T> i3(v0 * f2 - v1 * f4 + v2 * f5);

	vec4_t<T> sa(+1, -1, +1, -1);
	vec4_t<T> sb(-1, +1, -1, +1);
	matrix<T,4,4> inv(i0 * sa, i1 * sb, i2 * sa, i3 * sb);
	vec4_t<T> r0(inv[0][0], inv[1][0], inv[2][0], inv[3][0]);
	vec4_t<T> d0(m[0] * r0);
	return inv / ((d0.x() + d0.y()) + (d0.z() + d0.w()));
}

template<typename T> inline tuple<vec3_t<T>, quaternion<T>, vec3_t<T>> decompose(const matrix<T,4,4>& m) {
	matrix<T,4,4> tmp = m;

	vec3_t<T> scale;

	// Normalization
	if (tmp[3][3] == 0) return;
	for (uint32_t i = 0; i < 4; i++)
		for (uint32_t j = 0; j < 4; j++)
			tmp[i][j] /= tmp[3][3];

	if (tmp[0][3] || tmp[1][3] || tmp[2][3]) { // Clear perspective
		tmp[0][3] = tmp[1][3] = tmp[2][3] = 0;
		tmp[3][3] = 1;
	}


	vec3_t<T> position = tmp[3].xyz();
	tmp[3].xyz() = 0;

	// scale/shear
	vec3_t<T> rows[3];
	for (uint32_t i = 0; i < 3; ++i)
		for (uint32_t j = 0; j < 3; ++j)
			rows[i][j] = tmp[i][j];

	scale.x() = length(rows[0]);

	rows[0] = normalize(rows[0]);

	vec3_t<T> skew;
	skew.z() = dot(rows[0], rows[1]);
	rows[1] += -skew.z() * rows[0];

	scale.y() = length(rows[1]);
	rows[1] = normalize(rows[1]);
	skew.z() /= scale.y();

	skew.y() = dot(rows[0], rows[2]);
	rows[2] += rows[0] * -skew.y();
	skew.x() = dot(rows[1], rows[2]);
	rows[2] += rows[1] * -skew.x();

	scale.z() = length(rows[2]);
	rows[2] = normalize(rows[2]);
	skew.y() /= scale.z();
	skew.x() /= scale.z();

	if (dot(rows[0], cross(rows[1], rows[2])) < 0)
		for (uint32_t i = 0; i < 3; i++) {
			scale[i] = -scale[i];
			rows[i] = -rows[i];
		}

	quaternion<T> quat;

	T root, trace = rows[0].x() + rows[1].y() + rows[2].z();
	if (trace > 0) {
		root = sqrt(trace + 1);
		quat.a() = root/2;
		root = 1 / (2*root);
		quat.i() = root * (rows[1].z() - rows[2].y());
		quat.j() = root * (rows[2].x() - rows[0].z());
		quat.k() = root * (rows[0].y() - rows[1].x());
	} else {
		static const size_t next[3] { 1, 2, 0 };
		size_t i, j, k = 0;
		i = 0;
		if (rows[1][1] > rows[0][0]) i = 1;
		if (rows[2][2] > rows[i][i]) i = 2;
		size_t j = next[i];
		size_t k = next[j];

		root = sqrt(rows[i][i] - rows[j][j] - rows[k][k] + 1);

		quat[i] = root/2;
		root = 1 / (2*root);
		quat[j] = root * (rows[i][j] + rows[j][i]);
		quat[k] = root * (rows[i][k] + rows[k][i]);
		quat.a() = root * (rows[j][k] - rows[k][j]);
	}

	return { position, quat, scale };
}
template<typename T> inline matrix<T,4,4> Look(const vec3_t<T>& p, const vec3_t<T>& fwd, const vec3_t<T>& up) {
	vec3_t<T> f[3];
	f[0] = normalize(cross(up, fwd));
	f[1] = cross(fwd, f[0]);
	f[2] = fwd;
	matrix<T,4,4> r(1);
	rpt(i,3) r[i][0] = f[0][i];
	rpt(i,3) r[i][1] = f[1][i];
	rpt(i,3) r[i][2] = f[2][i];
	rpt(i,3) r[3][i] = -dot(f[i], p);
	return r;
}
template<typename T> inline matrix<T,4,4> PerspectiveFov(T fovy, T aspect, T zNear, T zFar) {
	T df = 1 / (zFar - zNear);
	T sy = 1 / std::tan(fovy / 2);
	matrix r(0);
	r[0][0] = sy / aspect;
	r[1][1] = sy;
	r[2][2] = zFar * df;
	r[3][2] = -zFar * zNear * df;
	r[2][3] = 1;
	return r;
}
template<typename T> inline matrix<T,4,4> Perspective(T width, T height, T zNear, T zFar) {
	T df = 1 / (zFar - zNear);
	matrix r(0);
	r[0][0] = 2 * zNear / width;
	r[1][1] = 2 * zNear / height;
	r[2][2] = zFar * df;
	r[3][2] = -zFar * zNear * df;
	r[2][3] = 1;
	return r;
}
template<typename T> inline matrix<T,4,4> Perspective(T left, T right, T top, T bottom, T zNear, T zFar) {
	T df = 1 / (zFar - zNear);
	matrix r(0);
	r[0][0] = 2 * zNear / (right - left);
	r[1][1] = 2 * zNear / (top - bottom);
	r[2][0] = (right + left) / (right - left);
	r[2][1] = (top + bottom) / (top - bottom);
	r[2][2] = zFar * df;
	r[3][2] = -zFar * zNear * df;
	r[2][3] = 1;
	return r;
}
template<typename T> inline matrix<T,4,4> Orthographic(T width, T height, T zNear, T zFar) {
	T df = 1 / (zFar - zNear);
	matrix r(1);
	r[0][0] = 2 / width;
	r[1][1] = 2 / height;
	r[2][2] = df;
	r[3][2] = -zNear * df;
	return r;
}
template<typename T> inline matrix<T,4,4> Translate(const vec3_t<T>& t) {
	matrix m(1);
	m[3].xyz = t;
	return m;
}
template<typename T> inline matrix<T,4,4> Scale(const vec3_t<T>& t) {
	matrix m(1);
	rpt(i,3) m[i] *= t[i];
	return m;
}
template<typename T> inline matrix<T,4,4> RotateX(T r) {
	T c = std::cos(r);
	T s = std::sin(r);
	return matrix(
		1, 0, 0, 0,
		0, c, -s, 0,
		0, s, c, 0,
		0, 0, 0, 1 );
}
template<typename T> inline matrix<T,4,4> RotateY(T r) {
	T c = std::cos(r);
	T s = std::sin(r);
	return matrix(
		c, 0, s, 0,
		0, 0, 0, 0,
		-s, 0, c, 0,
		0, 0, 0, 1 );
}
template<typename T> inline matrix<T,4,4> RotateZ(T r) {
	T c = std::cos(r);
	T s = std::sin(r);
	return matrix(
		c, -s, 0, 0,
		s, c, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 1 );
}
template<typename T> inline matrix<T,4,4> Rotate(const quaternion<T>& quat) {
	vec3_t q2 = quat.xyz * quat.xyz;
	vec3_t qw = quat.xyz * quat.w;
	vec3_t c = vec3_t(quat.x, quat.x, quat.y) * vec3_t(quat.z, quat.y, quat.z);
	
	matrix;
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
template<typename T> inline matrix<T,4,4> TRS(const vec3_t<T>& translation, const quaternion<T>& quat, const vec3_t<T>& scale) {
	matrix rm = Rotate(quat);
	rpt(i,3) rm[i] *= scale[i];
	(float3)rm[3] = translation; // TODO: is this right?
	return rm;
}

}