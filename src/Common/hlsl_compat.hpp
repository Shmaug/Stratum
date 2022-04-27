#pragma once

// this file is meant to be included by hlsl files

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace stm {

template<typename T, int M, int N> using MatrixType = Eigen::Array<T, M, N, Eigen::ColMajor, M, N>;

using char2    	= MatrixType<int8_t , 2, 1>;
using char3    	= MatrixType<int8_t , 3, 1>;
using char4    	= MatrixType<int8_t , 4, 1>;
using uchar2   	= MatrixType<int8_t , 2, 1>;
using uchar3   	= MatrixType<int8_t , 3, 1>;
using uchar4   	= MatrixType<int8_t , 4, 1>;
using int2    	= MatrixType<int32_t, 2, 1>;
using int3    	= MatrixType<int32_t, 3, 1>;
using int4    	= MatrixType<int32_t, 4, 1>;
using uint2   	= MatrixType<int32_t, 2, 1>;
using uint3   	= MatrixType<int32_t, 3, 1>;
using uint4   	= MatrixType<int32_t, 4, 1>;
using long2    	= MatrixType<int64_t, 2, 1>;
using long3    	= MatrixType<int64_t, 3, 1>;
using long4    	= MatrixType<int64_t, 4, 1>;
using ulong2   	= MatrixType<int64_t, 2, 1>;
using ulong3   	= MatrixType<int64_t, 3, 1>;
using ulong4   	= MatrixType<int64_t, 4, 1>;
using float2  	= MatrixType<float  , 2, 1>;
using float3  	= MatrixType<float  , 3, 1>;
using float4  	= MatrixType<float  , 4, 1>;
using int2x2    = MatrixType<int32_t, 2, 2>;
using int3x2    = MatrixType<int32_t, 3, 2>;
using int4x2    = MatrixType<int32_t, 4, 2>;
using uint2x2   = MatrixType<int32_t, 2, 2>;
using uint3x2   = MatrixType<int32_t, 3, 2>;
using uint4x2   = MatrixType<int32_t, 4, 2>;
using float2x2  = MatrixType<float  , 2, 2>;
using float3x2  = MatrixType<float  , 3, 2>;
using float4x2  = MatrixType<float  , 4, 2>;
using int2x3    = MatrixType<int32_t, 2, 3>;
using int3x3    = MatrixType<int32_t, 3, 3>;
using int4x3    = MatrixType<int32_t, 4, 3>;
using uint2x3   = MatrixType<int32_t, 2, 3>;
using uint3x3   = MatrixType<int32_t, 3, 3>;
using uint4x3   = MatrixType<int32_t, 4, 3>;
using float2x3  = MatrixType<float  , 2, 3>;
using float3x3  = MatrixType<float  , 3, 3>;
using float4x3  = MatrixType<float  , 4, 3>;
using int2x4    = MatrixType<int32_t, 2, 4>;
using int3x4    = MatrixType<int32_t, 3, 4>;
using int4x4    = MatrixType<int32_t, 4, 4>;
using uint2x4   = MatrixType<int32_t, 2, 4>;
using uint3x4   = MatrixType<int32_t, 3, 4>;
using uint4x4   = MatrixType<int32_t, 4, 4>;
using float2x4  = MatrixType<float  , 2, 4>;
using float3x4  = MatrixType<float  , 3, 4>;
using float4x4  = MatrixType<float  , 4, 4>;

using uint = uint32_t;

using std::min;
using std::max;
using std::abs;
template<typename T,int M, int N> inline MatrixType<T,M,N> max(const MatrixType<T,M,N>& a, const MatrixType<T,M,N>& b) { return a.max(b); }
template<typename T,int M, int N> inline MatrixType<T,M,N> min(const MatrixType<T,M,N>& a, const MatrixType<T,M,N>& b) { return a.min(b); }
template<typename T,int M, int N> inline MatrixType<T,M,N> abs(const MatrixType<T,M,N>& a) { return a.abs(); }
template<typename T,int M, int N> inline bool all(const MatrixType<T,M,N>& a) { return a.all(); }
template<typename T,int M, int N> inline bool any(const MatrixType<T,M,N>& a) { return a.any(); }

template<floating_point T> inline T saturate(const T& a) { return min(max(a, 0), 1); }
template<typename T,int M, int N> inline MatrixType<T,M,N> saturate(const MatrixType<T,M,N>& a) { return a.max(MatrixType<T,M,N>::Zero()).min(MatrixType<T,M,N>::Ones()); }

template<typename T, int M, int N> inline T dot(const MatrixType<T,M,N>& a, const MatrixType<T,M,N>& b) { return a.matrix().dot(b.matrix()); }
template<typename T, int M, int N> inline T length(const MatrixType<T,M,N>& a) { return a.matrix().norm(); }

template<typename T, int M, int N> inline MatrixType<T,M,N> normalize(const MatrixType<T,M,N>& a) { return a.matrix().normalized(); }
template<typename T> inline MatrixType<T,3,1> cross(const MatrixType<T,3,1>& a, const MatrixType<T,3,1>& b) { return a.matrix().cross(b.matrix()); }

inline float asfloat(uint32_t v) { return *reinterpret_cast<float*>(&v); }
inline uint32_t asuint(float v) { return *reinterpret_cast<uint32_t*>(&v); }
template<int M, int N> inline MatrixType<float, M, N> asfloat(const MatrixType<uint32_t, M, N>& v) { return MatrixType<float, M, N>::Map(reinterpret_cast<float*>(v.data())); }
template<int M, int N> inline MatrixType<uint,  M, N> asuint (const MatrixType<float, M, N>& v)    { return MatrixType<uint,  M, N>::Map(reinterpret_cast<uint32_t*>(v.data())); }

}