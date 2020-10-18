#pragma once

#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdint>

#pragma pack(push)
#pragma pack(1)

#define rpt(i,n) for (uint32_t i = 0; i < n; ++i)

namespace stm {

template<uint32_t N, typename T> class tvec {
public:
	T v[N];
#include "tvec_impl.hpp"
};
template<typename T> class tvec<2,T> {
public:
	static constexpr uint32_t N = 2;
	union {
		T v[N];
		struct { T x, y; };
		struct { T r, g; };
	};
	inline tvec(const T& x, const T& y) : x(x), y(y) {}
#include "tvec_impl.hpp"
};
template<typename T> struct tvec<3,T> {
public:
	static constexpr uint32_t N = 3;
	union {
		T v[N];
		struct { T x, y, z; };
		tvec<2,T> xy;
		
		struct { T r, g, b; };
		tvec<2,T> rg;
	};
	inline tvec(const T& x, const T& y, const T& z) : x(x), y(y), z(z) {}
	inline tvec(const tvec<2, T>& xy, const T& z) : xy(xy), z(z) {}
#include "tvec_impl.hpp"
};
template<typename T> class tvec<4,T> {
public:
	static constexpr uint32_t N = 4;
	union {
		T v[N];
		struct { T x, y, z, w; };
		struct { tvec<2,T> xy, zw; };
		tvec<3,T> xyz;
		
		struct { T r, g, b, a; };
		struct { tvec<2,T> rg, ba; };
		tvec<3,T> rgb;
	};
	inline tvec(const T& x, const T& y, const T& z, const T& w) : x(x), y(y), z(z), w(w) {}
	inline tvec(const tvec<2, T>& xy, const T& z, const T& w) : xy(xy), z(z), w(w) {}
	inline tvec(const tvec<3, T>& xyz, const T& w) : xyz(xyz), w(w) {}
	inline tvec(const tvec<2, T>& xy, const tvec<2, T>& zw) : xy(xy), zw(zw) {}
#include "tvec_impl.hpp"
};



template<uint32_t N, typename T> inline tvec<N,T> pow(const tvec<N,T>& a, const tvec<N,T>& b) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::pow(a.v[i], b.v[i]);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> cos(const tvec<N,T>& a) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::cos(a.v[i]);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> sin(const tvec<N,T>& a) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::sin(a.v[i]);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> tan(const tvec<N,T>& a) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::tan(a.v[i]);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> acos(const tvec<N,T>& a) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::acos(a.v[i]);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> asin(const tvec<N,T>& a) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::asin(a.v[i]);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> atan(const tvec<N,T>& a) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::atan(a.v[i]);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> atan2(const tvec<N,T>& y, const tvec<N,T>& x) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::atan2(y.v[i], x.v[i]);
	return r;
}

template<typename T> inline T degrees(const T& r) { return r * (T)(180.0 / M_PI); }
template<typename T> inline T radians(const T& d) { return d * (T)(M_PI / 180.0); }

template<uint32_t N, typename T> inline float dot(const tvec<N,T>& a, const tvec<N,T>& b) {
	T r = 0;
	tvec<N,T> m = a * b;
	rpt(i,N) r += m.v[i];
	return r;
}
template<uint32_t N, typename T> inline float length(const tvec<N,T>& v) { return sqrtf(dot(v, v)); }
template<uint32_t N, typename T> inline tvec<N,T> normalize(const tvec<N,T>& v) { return v / length(v); }

template<typename T> inline tvec<3,T> cross(const tvec<3,T>& a, const tvec<3,T>& b) {
	tvec<3,T> m1(a[1], a[2], a[0]);
	tvec<3,T> m2(b[2], b[0], b[1]);
	tvec<3,T> m3(b[1], b[2], b[0]);
	tvec<3,T> m4(a[2], a[0], a[1]);
	return m1 * m2 - m3 * m4;
}

template<uint32_t N, typename T> inline tvec<N,T> min(const tvec<N,T>& a, const tvec<N,T>& b) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::min(a.v[i], b.v[i]);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> max(const tvec<N,T>& a, const tvec<N,T>& b) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::max(a.v[i], b.v[i]);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> min(const tvec<N,T>& a, const T& b) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::min(a.v[i], b);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> max(const tvec<N,T>& a, const T& b) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::max(a.v[i], b);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> min(const T& a, const tvec<N,T>& b) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::min(a, b.v[i]);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> max(const T& a, const tvec<N,T>& b) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::max(a, b.v[i]);
	return r;
}

template<uint32_t N, typename T> inline tvec<N,T> clamp(const tvec<N,T>& a, const tvec<N,T>& l, const tvec<N,T>& h) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::min(std::max(a.v[i], l.v[i]), h.v[i]);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> clamp(const tvec<N,T>& a, const tvec<N,T>& l, const T& h) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::min(std::max(a.v[i], l.v[i]), h);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> clamp(const tvec<N,T>& a, const T& l, const tvec<N,T>& h) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::min(std::max(a.v[i], l), h.v[i]);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> clamp(const tvec<N,T>& a, const T& l, const T& h) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::min(std::max(a.v[i], l), h);
	return r;
}

template<uint32_t N, typename T> inline tvec<N,T> abs(const tvec<N,T>& a) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::abs(a.v[i]);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> ceil(const tvec<N,T>& a) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::ceil(a.v[i]);
	return r;
}
template<uint32_t N, typename T> inline tvec<N,T> floor(const tvec<N,T>& a) {
	tvec<N,T> r;
	rpt(i,N) r.v[i] = std::floor(a.v[i]);
	return r;
}

template<uint32_t N, typename T> inline tvec<N,T> frac(const tvec<N,T>& a) { return a - stm::floor(a); }
template<uint32_t N, typename T> inline tvec<N,T> lerp(const tvec<N,T>& a, const tvec<N,T>& b, const float t) { return a + (b - a)*t; }


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


}

namespace std {

template<uint32_t N, typename T>
struct hash<stm::tvec<N,T>> {
	inline size_t operator()(const stm::tvec<N,T>& v) const {
		size_t h = 0;
		rpt(i,N) stm::hash_combine(h, v[i]);
		return h;
	}
};

}

#undef rpt
#pragma pack(pop)