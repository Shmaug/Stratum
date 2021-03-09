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

	#define DECLARE_FIXED_SIZE_TYPE(TypeName, TypeSuffix, SizeSuffix) \
		using TypeName##SizeSuffix = Vector##SizeSuffix##TypeSuffix; \
		using TypeName##SizeSuffix##x##SizeSuffix = Matrix##SizeSuffix##TypeSuffix;

	#define DECLARE_FIXED_SIZE_TYPES(TypeName, TypeSuffix) \
		DECLARE_FIXED_SIZE_TYPE(TypeName, TypeSuffix, 2) \
		DECLARE_FIXED_SIZE_TYPE(TypeName, TypeSuffix, 3) \
		DECLARE_FIXED_SIZE_TYPE(TypeName, TypeSuffix, 4)
	
	DECLARE_FIXED_SIZE_TYPES(float,f)
	DECLARE_FIXED_SIZE_TYPES(double,d)
	DECLARE_FIXED_SIZE_TYPES(int,i)
	DECLARE_FIXED_SIZE_TYPES(uint,i)
	
	#undef DECLARE_FIXED_SIZE_TYPES
	#undef DECLARE_FIXED_SIZE_TYPE
	
	#include "../Shaders/include/stratum.hlsli"
}

}