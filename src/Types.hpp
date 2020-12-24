#pragma once

#define _USE_MATH_DEFINES
#include <math.h>
#include <cstdint>
#include <numeric>
#include <algorithm>
#include <ranges>

#include <iostream>
#include <fstream>
#include <stdexcept>

#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#include <functional>
#include <optional>
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
#include <vector>

namespace stm {
  using namespace std;
  
  #ifdef WINDOWS
  namespace fs = std::filesystem;
  #endif
  #ifdef __GNUC__
  namespace fs = std::experimental::filesystem;
  #endif
}

#include "Math/basic_hash.hpp"
#include "Math/geometry.hpp"

namespace stm {

using uint2 = vec2_t<uint32_t>;
using uint3 = vec3_t<uint32_t>;
using uint4 = vec3_t<uint32_t>;
using int2 = vec2_t<int32_t>;
using int3 = vec3_t<int32_t>;
using int4 = vec3_t<int32_t>;
using float2 = vec2_t<float>;
using float3 = vec3_t<float>;
using float4 = vec3_t<float>;
using fquat = quaternion<float>;
using double2 = vec2_t<double>;
using double3 = vec3_t<double>;
using double4 = vec3_t<double>;
using dquat = quaternion<double>;

using float2x2 = matrix<float,2,2>;
using float3x3 = matrix<float,3,3>;
using float4x4 = matrix<float,4,4>;
using double2x2 = matrix<double,2,2>;
using double3x3 = matrix<double,3,3>;
using double4x4 = matrix<double,4,4>;

using fAABB = AABB<float>;
using fRay = Ray<float>;
using fRect2D = Rect2D<float>;
using dAABB = AABB<double>;
using dRay = Ray<double>;
using dRect2D = Rect2D<double>;

#include "Shaders/include/stratum.hlsli"

class Asset;
class Buffer;
class CommandBuffer;
class DescriptorSet;
class Device; class Fence; class Sampler; class Semaphore;
class EnginePlugin;
class Framebuffer;
class Instance;
class RenderPass;
class Window;
class Font;
class Material;
class Mesh;
class Shader;
class Texture;
class Object;
class Camera;
class Renderer;
class GuiContext;
class Light;

template<typename Test, template<typename...> class Ref>
struct is_specialization : false_type {};
template<template<typename...> class Ref, typename... Args>
struct is_specialization<Ref<Args...>, Ref> : true_type {};
template<template<typename...> class Ref, typename... Args>
using is_specialization_v = is_specialization<Ref<Args...>, Ref>::value;

}

#include "binary_stream.hpp"
#include "byte_blob.hpp"