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

#include <math.h>
#include <numeric>

#define EIGEN_HAS_STD_RESULT_OF 0
#include <Eigen/Geometry>
#include <Eigen/LU>
#include <unsupported/Eigen/BVH>
#include <unsupported/Eigen/CXX11/Tensor>

#include <vulkan/vulkan.hpp>

namespace stm {

namespace fs = std::filesystem;

using namespace std;
using namespace Eigen;

template<typename T> inline Transform<T,3,Projective> Perspective(T width, T height, T zNear, T zFar) {
	Matrix<T,4,4> r = Matrix<T,4,4>::Zero();
	r(0,0) = 2*zNear/width;
	r(1,1) = 2*zNear/height;
	r(2,2) = zFar / (zFar - zNear);
	r(2,3) = zNear * -r(2,2);
	r(2,3) = 1;
	return Transform<T,3,Projective>(r);
}
template<typename T> inline Transform<T,3,Projective> Perspective(T left, T right, T top, T bottom, T zNear, T zFar) {
	Matrix<T,4,4> r = Matrix<T,4,4>::Zero();
	r(0,0) = 2*zNear / (right - left);
	r(1,1) = 2*zNear / (top - bottom);
	r(2,0) = (left + right) / (left - right);
	r(1,2) = (top + bottom) / (bottom - top);
	r(2,2) = zFar / (zFar - zNear);
	r(2,3) = zNear * -r(2,2);
	r(2,3) = 1;
	return Transform<T,3,Projective>(r);
}
template<typename T> inline Transform<T,3,Projective> PerspectiveFov(T fovy, T aspect, T zNear, T zFar) {
	T sy = 1 / tan(fovy / 2);
	Matrix<T,4,4> r = Matrix<T,4,4>::Zero();
	r(0,0) = sy/aspect;
	r(1,1) = sy;
	r(2,2) = zFar / (zFar - zNear);
	r(3,2) = zNear * -r(2,2);
	r(2,3) = 1;
	return Transform<T,3,Projective>(r);
}
template<typename T> inline Transform<T,3,Projective> Orthographic(T width, T height, T zNear, T zFar) {
	Matrix<T,4,4> r = Matrix<T,4,4>::Zero();
	r(0,0) = 2/width;
	r(1,1) = 2/height;
	r(2,2) = 1/(zFar - zNear);
	r(3,2) = -zNear * r(2,2);
	r(3,3) = 1;
	return Transform<T,3,Projective>(r);
}
template<typename T> inline Transform<T,3,Projective> Orthographic(T left, T right, T bottom, T top, T zNear, T zFar) {
	Transform<T,3,Projective> r;
	r(0,0) = 2 / (right - left);
	r(1,1) = 2 / (top - bottom);
	r(2,2) = 1 / (zFar - zNear);
	r(3,0) = (left + right) / (left - right);
	r(3,1) = (top + bottom) / (bottom - top);
	r(3,2) = zNear / (zNear - zFar);
	r(3,3) = 1;
	return r;
}

using fRay = ParametrizedLine<float,3>;
using dRay = ParametrizedLine<double,3>;

template<class _Type, template<class...> class _Template> constexpr bool is_specialization_v = false;
template<template<class...> class _Template, class... _Types> constexpr bool is_specialization_v<_Template<_Types...>, _Template> = true;


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


template<typename R> concept fixed_size_range = ranges::sized_range<R> && requires(R r) { tuple_size<R>::value; };
template<typename R> concept resizable_range = ranges::sized_range<R> && !fixed_size_range<R> && requires(R r, size_t n) { r.resize(n); };

template<typename S, typename T> concept stream_extractable = requires(S s, T t) { s >> t; };
template<typename S, typename T> concept stream_insertable = requires(S s, T t) { s << t; };

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

namespace shader_interop {
	using uint = uint32_t;

	#define DECLARE_FIXED_SIZE_TYPES(HLSLName, CppName) \
		using HLSLName##2   = std::array<CppName, 2>; \
		using HLSLName##3   = std::array<CppName, 3>; \
		using HLSLName##4   = std::array<CppName, 4>; \
		using HLSLName##2x2 = std::array<CppName, 4>; \
		using HLSLName##2x3 = std::array<CppName, 6>; \
		using HLSLName##3x2 = std::array<CppName, 6>; \
		using HLSLName##3x3 = std::array<CppName, 9>; \
		using HLSLName##4x3 = std::array<CppName, 12>; \
		using HLSLName##3x4 = std::array<CppName, 12>; \
		using HLSLName##4x4 = std::array<CppName, 16>;
	DECLARE_FIXED_SIZE_TYPES(float,float)
	DECLARE_FIXED_SIZE_TYPES(double,double)
	DECLARE_FIXED_SIZE_TYPES(int,int32_t)
	DECLARE_FIXED_SIZE_TYPES(uint,uint32_t)
	#undef DECLARE_FIXED_SIZE_TYPES
	
	#pragma pack(push)
	#pragma pack(1)
	#include "../Shaders/include/stratum.hlsli"
	#include "../Shaders/include/lighting.hlsli"
	#pragma pack(pop)
}

}