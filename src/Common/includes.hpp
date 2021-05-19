#pragma once

#include <bit>
#include <chrono>
#include <functional>
#include <locale>
#include <mutex>
#include <numeric>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <stdlib.h>
#include <thread>
#include <typeindex>
#include <variant>

#include <iostream>
#include <fstream>

#include <bitset>
#include <string>
#include <forward_list>
#include <filesystem>
#include <list>
#include <map>
#include <vector>
#include <deque>
#include <queue>
#include <set>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <ranges>
#include <span>

#include <math.h>

#ifdef WIN32
#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <WS2tcpip.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif
#undef near
#undef far
#undef free

#ifdef STRATUM_CORE
#define STRATUM_API __declspec(dllexport)
#define PLUGIN_EXPORT
#else
#define STRATUM_API __declspec(dllimport)
#define PLUGIN_EXPORT __declspec(dllexport)
#endif
#endif
#ifdef __linux
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <dlfcn.h>
#define STRATUM_API
#define PLUGIN_EXPORT
#endif

#define EIGEN_HAS_STD_RESULT_OF 0
#include <Eigen/Geometry>
#include <Eigen/LU>
#include <unsupported/Eigen/BVH>
#include <unsupported/Eigen/CXX11/Tensor>

#include <vulkan/vulkan.hpp>

namespace stm {

using namespace std;
using namespace Eigen;
namespace fs = std::filesystem;

template<typename T>
struct remove_const_tuple { using type = remove_const_t<T>; };
template<typename Tx, typename Ty>
struct remove_const_tuple<pair<Tx, Ty>> { using type = pair<remove_const_t<Tx>, remove_const_t<Ty>>; };
template<typename... Types>
struct remove_const_tuple<tuple<Types...>> { using type = tuple<remove_const_t<Types>...>; };
template<typename T> using remove_const_tuple_t = typename remove_const_tuple<T>::type;

template<typename _Type, template<typename...> typename _Template>
constexpr bool is_specialization_v = false;
template<template<class...> typename _Template, typename...Args>
constexpr bool is_specialization_v<_Template<Args...>, _Template> = true;

template<typename T> concept is_pair = is_specialization_v<T, pair>;
template<typename T> concept is_tuple = is_specialization_v<T, tuple>;

template<typename T> constexpr bool is_dynamic_span_v = false;
template<typename T> constexpr bool is_dynamic_span_v<span<T,dynamic_extent>> = true;

template<typename R> concept fixed_sized_range = is_specialization_v<R, std::array> || (is_specialization_v<R, span> && !is_dynamic_span_v<R>);
template<typename R> concept resizable_range = ranges::sized_range<R> && !fixed_sized_range<R> && requires(R r, size_t n) { r.resize(n); };

template<typename R> concept associative_range = ranges::range<R> &&
	requires { typename R::key_type; typename R::value_type; } &&
	requires(R r, ranges::range_value_t<R> v) { { r.emplace(v) } -> is_pair; };

}