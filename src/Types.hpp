#pragma once

#include <iostream>
#include <fstream>
#include <stdexcept>

#include <typeindex>

#include <mutex>
#include <thread>
#include <chrono>
#include <functional>
#include <optional>
#include <bit>
#include <string>
#include <variant>
#include <deque>
#include <forward_list>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <ranges>
#include <span>

#include <bitset>
#include <locale>

#include <math.h>
#include <numeric>

#include <vulkan/vulkan.hpp>

#define EIGEN_HAS_STD_RESULT_OF 0
#include <Eigen/Geometry>
#include <Eigen/LU>
#include <unsupported/Eigen/BVH>
#include <unsupported/Eigen/CXX11/Tensor>

namespace stm {

using namespace std;
using namespace Eigen;

namespace fs = std::filesystem;

using fRay = ParametrizedLine<float,3>;
using dRay = ParametrizedLine<double,3>;

template<class _Type, template<class...> class _Template> constexpr bool is_specialization_v = false;
template<template<class...> class _Template, class... _Types> constexpr bool is_specialization_v<_Template<_Types...>, _Template> = true;


template<typename R> concept fixed_size_range = ranges::sized_range<R> && requires(R r) { tuple_size<R>::value; };
template<typename R> concept resizable_range = ranges::sized_range<R> && !fixed_size_range<R> && requires(R r, size_t n) { r.resize(n); };

template<typename S, typename T> concept stream_extractable = requires(S s, T t) { s >> t; };
template<typename S, typename T> concept stream_insertable = requires(S s, T t) { s << t; };


template<typename T>
struct remove_const_tuple {
	using type = remove_const_t<T>;
};
template<typename Tx, typename Ty>
struct remove_const_tuple<pair<Tx, Ty>> {
	using type = pair<remove_const_t<Tx>, remove_const_t<Ty>>;
};
template<typename... Types>
struct remove_const_tuple<tuple<Types...>> {
	using type = tuple<remove_const_t<Types>...>;
};
template<typename T> using remove_const_tuple_t = typename remove_const_tuple<T>::type;


template<typename T>
struct add_const_tuple {
	using type = const T;
};
template<typename Tx, typename Ty>
struct add_const_tuple<pair<Tx, Ty>> {
	using type = pair<const Tx, const Ty>;
};
template<typename... Types>
struct add_const_tuple<tuple<Types...>> {
	using type = tuple<const Types...>;
};
template<typename T> using add_const_tuple_t = typename add_const_tuple<T>::type;


template <typename T>
class locked_object {
private:
	T mObject;
	mutable mutex mMutex;
public:

	class ptr_t {
	private:
		T* mObject;
		scoped_lock<mutex> mLock;
	public:
		inline ptr_t(T* object, mutex& m) : mObject(object), mLock(scoped_lock<mutex>(m)) {};
		inline T& operator*() { return *mObject; }
		inline T* operator->() { return mObject; }
	};
	class cptr_t {
	private:
		const T* mObject;
		scoped_lock<mutex> mLock;
	public:
		inline cptr_t(const T* object, mutex& m) : mObject(object), mLock(scoped_lock<mutex>(m)) {};
		inline const T& operator*() { return *mObject; }
		inline const T* operator->() { return mObject; }
	};

	inline ptr_t lock() { return ptr_t(&mObject, mMutex); }
	inline cptr_t lock() const { return cptr_t(&mObject, mMutex); }
	inline mutex& m() const { return mMutex; }
};


namespace hlsl {

using uint = uint32_t;

template<typename T, int M, int N = 1> using ArrayType = Eigen::Array<T, M, N, Eigen::ColMajor, M, N>;
template<typename T, int M, int N = 1> using MatrixType = Eigen::Matrix<T, M, N, Eigen::ColMajor, M, N>;

using int2    	= ArrayType<int32_t, 2>;
using int3    	= ArrayType<int32_t, 3>;
using int4    	= ArrayType<int32_t, 4>;
using uint2   	= ArrayType<int32_t, 2>;
using uint3   	= ArrayType<int32_t, 3>;
using uint4   	= ArrayType<int32_t, 4>;
using float2  	= ArrayType<float, 2>;
using float3  	= ArrayType<float, 3>;
using float4  	= ArrayType<float, 4>;
using double2 	= ArrayType<double, 2>;
using double3 	= ArrayType<double, 3>;
using double4 	= ArrayType<double, 4>;
using int2x2    = ArrayType<int32_t, 2, 2>;
using int3x2    = ArrayType<int32_t, 3, 2>;
using int4x2    = ArrayType<int32_t, 4, 2>;
using uint2x2   = ArrayType<int32_t, 2, 2>;
using uint3x2   = ArrayType<int32_t, 3, 2>;
using uint4x2   = ArrayType<int32_t, 4, 2>;
using float2x2  = ArrayType<float, 2, 2>;
using float3x2  = ArrayType<float, 3, 2>;
using float4x2  = ArrayType<float, 4, 2>;
using double2x2 = ArrayType<double, 2, 2>;
using double3x2 = ArrayType<double, 3, 2>;
using double4x2 = ArrayType<double, 4, 2>;
using int2x3    = ArrayType<int32_t, 2, 3>;
using int3x3    = ArrayType<int32_t, 3, 3>;
using int4x3    = ArrayType<int32_t, 4, 3>;
using uint2x3   = ArrayType<int32_t, 2, 3>;
using uint3x3   = ArrayType<int32_t, 3, 3>;
using uint4x3   = ArrayType<int32_t, 4, 3>;
using float2x3  = ArrayType<float, 2, 3>;
using float3x3  = ArrayType<float, 3, 3>;
using float4x3  = ArrayType<float, 4, 3>;
using double2x3 = ArrayType<double, 2, 3>;
using double3x3 = ArrayType<double, 3, 3>;
using double4x3 = ArrayType<double, 4, 3>;
using int2x4    = ArrayType<int32_t, 2, 4>;
using int3x4    = ArrayType<int32_t, 3, 4>;
using int4x4    = ArrayType<int32_t, 4, 4>;
using uint2x4   = ArrayType<int32_t, 2, 4>;
using uint3x4   = ArrayType<int32_t, 3, 4>;
using uint4x4   = ArrayType<int32_t, 4, 4>;
using float2x4  = ArrayType<float, 2, 4>;
using float3x4  = ArrayType<float, 3, 4>;
using float4x4  = ArrayType<float, 4, 4>;
using double2x4 = ArrayType<double, 2, 4>;
using double3x4 = ArrayType<double, 3, 4>;
using double4x4 = ArrayType<double, 4, 4>;

using quatf = Quaternion<float>;
using quatd = Quaternion<double>;

template<typename T, int M, int N, int K>
inline ArrayType<T,M,K> mul(const ArrayType<T,M,N>& a, const ArrayType<T,N,K>& b) {
	return (a.matrix()*b.matrix()).array();
}

template<typename T, int M, int N>
inline ArrayType<T,M,N> max(const ArrayType<T,M,N>& a, const ArrayType<T,M,N>& b) { return a.max(b); }
template<typename T, int M, int N>
inline ArrayType<T,M,N> min(const ArrayType<T,M,N>& a, const ArrayType<T,M,N>& b) { return a.min(b); }

template<typename T, int M, int N>
inline T dot(const ArrayType<T,M,N>& a, const ArrayType<T,M,N>& b) { return a.matrix().dot(b.matrix()); }
template<typename T, int M, int N>
inline T length(const ArrayType<T,M,N>& a) { return a.matrix().norm(); }
template<typename T, int M, int N>
inline ArrayType<T,M,N> normalize(const ArrayType<T,M,N>& a) { return a.matrix().normalized().array(); }
template<typename T>
inline ArrayType<T,3> cross(const T& a, const T& b) { return a.matrix().cross(b.matrix()).array(); }

template<typename T, int M, int N = 1>
inline ArrayType<T,M-1,N> hnormalized(const ArrayType<T,M,N>& a) { return a.matrix().hnormalized().array(); }
template<typename T, int M, int N = 1>
inline ArrayType<T,M+1,N> homogeneous(const ArrayType<T,M,N>& a) { return a.matrix().homogeneous().array(); }

template<typename T>
inline Quaternion<T> qmul(const Quaternion<T>& q1, const Quaternion<T>& q2) { return q1*q2; }
template<typename T>
inline Quaternion<T> inverse(const Quaternion<T>& q) { return q.inverse(); }
template<typename T>
inline ArrayType<T,3> rotate_vector(const Quaternion<T>& q, const ArrayType<T,3>& v) { return q*v; }

#define QUATF_I Quaternionf::Identity()

}

}