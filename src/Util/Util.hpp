#pragma once

#include <vulkan/vulkan.hpp>

#define _USE_MATH_DEFINES
#include <cmath>

#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <string>
#include <cstring>
#include <stdexcept>
#include <variant>
#include <optional>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>
#include <stack>
#include <queue>
#include <map>
#include <unordered_map>
#include <set>
#include <list>
#include <forward_list>

#include "tvec.hpp"
#include "tmat.hpp"
#include "tquat.hpp"

#include "Enums.hpp"
#include "StratumForward.hpp"
#include "StratumPlatform.hpp"

namespace stm {
	
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

#include <Shaders/include/shadercompat.h>

// TODO: consider using numbers here instead?
typedef std::string RenderTargetIdentifier;
typedef std::string ShaderPassIdentifier;

class Sphere {
public:
	float3 mCenter;
	float mRadius = 0;
	inline Sphere() = default;
	inline Sphere(const float3& center, float radius) : mCenter(center), mRadius(radius) {}
};
class AABB {
public:
	float3 mMin;
	float3 mMax;

	AABB() = default;
	inline AABB(const float3& min, const float3& max) : mMin(min), mMax(max) {}
	inline AABB(const AABB& aabb) : mMin(aabb.mMin), mMax(aabb.mMax) {}
	inline AABB(const AABB& aabb, const float4x4& transform) : AABB(aabb) { *this *= transform; }

	inline float3 Center() const { return (mMax + mMin) * .5f; }
	inline float3 HalfSize() const { return (mMax - mMin) * .5f; }
	inline float3 Size() const { return (mMax - mMin); }

	inline bool Intersects(const float3& point) const {
		float3 e = (mMax - mMin) * .5f;
		float3 s = point - (mMax + mMin) * .5f;
		return
			(s.x <= e.x && s.x >= -e.x) &&
			(s.y <= e.y && s.y >= -e.y) &&
			(s.z <= e.z && s.z >= -e.z);
	}
	inline bool Intersects(const Sphere& sphere) const {
		float3 e = (mMax - mMin) * .5f;
		float3 s = sphere.mCenter - (mMax + mMin) * .5f;
		float3 delta = e - s;
		float sqDist = 0.0f;
		for (int i = 0; i < 3; i++) {
			if (s[i] < -e[i]) sqDist += delta[i];
			if (s[i] >  e[i]) sqDist += delta[i];
		}
		return sqDist <= sphere.mRadius * sphere.mRadius;
	}
	inline bool Intersects(const AABB& aabb) const {
		// for each i in (x, y, z) if a_min(i) > b_max(i) or b_min(i) > a_max(i) then return false
		bool dx = (mMin.x > aabb.mMax.x) || (aabb.mMin.x > mMax.x);
		bool dy = (mMin.y > aabb.mMax.y) || (aabb.mMin.y > mMax.y);
		bool dz = (mMin.z > aabb.mMax.z) || (aabb.mMin.z > mMax.z);
		return !(dx || dy || dz);
	}

	inline bool Intersects(const float4 frustum[6]) const {
		float3 center = Center();
		float3 size = HalfSize();
		for (uint32_t i = 0; i < 6; i++) {
			float r = dot(size, abs(frustum[i].xyz));
			float d = dot(center, frustum[i].xyz) - frustum[i].w;
			if (d <= -r) return false;
		}
		return true;
	}

	inline void Encapsulate(const float3& p) {
		mMin = min(mMin, p);
		mMax = max(mMax, p);
	}
	inline void Encapsulate(const AABB& aabb) {
		mMin = min(aabb.mMin, mMin);
		mMax = max(aabb.mMax, mMax);
	}

	inline AABB operator *(const float4x4& transform) {
		return AABB(*this, transform);
	}
	inline AABB operator *=(const float4x4& transform) {
		float3 corners[8]{
			mMax,							// 1,1,1
			float3(mMin.x, mMax.y, mMax.z),	// 0,1,1
			float3(mMax.x, mMax.y, mMin.z),	// 1,1,0
			float3(mMin.x, mMax.y, mMin.z),	// 0,1,0
			float3(mMax.x, mMin.y, mMax.z),	// 1,0,1
			float3(mMin.x, mMin.y, mMax.z),	// 0,0,1
			float3(mMax.x, mMin.y, mMin.z),	// 1,0,0
			mMin,							// 0,0,0
		};
		for (uint32_t i = 0; i < 8; i++)
			corners[i] = (transform * float4(corners[i], 1)).xyz;
		mMin = corners[0];
		mMax = corners[0];
		for (uint32_t i = 1; i < 8; i++) {
			mMin = min(mMin, corners[i]);
			mMax = max(mMax, corners[i]);
		}
		return *this;
	}
};
class Ray {
public:
	float3 mOrigin;
	float3 mDirection;

	inline Ray() = default;
	inline Ray(const float3& ro, const float3& rd) : mOrigin(ro), mDirection(rd) {};

	inline float Intersect(const float4& plane) const {
		return -(dot(mOrigin, plane.xyz) + plane.w) / dot(mDirection, plane.xyz);
	}
	inline float Intersect(const float3& planeNormal, const float3& planePoint) const {
		return -dot(mOrigin - planePoint, planeNormal) / dot(mDirection, planeNormal);
	}

	inline bool Intersect(const AABB& aabb, float2& t) const {
		float3 id = 1.f / mDirection;

		float3 pmin = (aabb.mMin - mOrigin) * id;
		float3 pmax = (aabb.mMax - mOrigin) * id;

		float3 mn, mx;
		mn.x = id.x >= 0.f ? pmin.x : pmax.x;
		mn.y = id.y >= 0.f ? pmin.y : pmax.y;
		mn.z = id.z >= 0.f ? pmin.z : pmax.z;
		
		mx.x = id.x >= 0.f ? pmax.x : pmin.x;
		mx.y = id.y >= 0.f ? pmax.y : pmin.y;
		mx.z = id.z >= 0.f ? pmax.z : pmin.z;

		t = float2(fmaxf(fmaxf(mn.x, mn.y), mn.z), fminf(fminf(mx.x, mx.y), mx.z));
		return t.y > t.x;
	}
	inline bool Intersect(const Sphere& sphere, float2& t) const {
		float3 pq = mOrigin - sphere.mCenter;
		float a = dot(mDirection, mDirection);
		float b = 2 * dot(pq, mDirection);
		float c = dot(pq, pq) - sphere.mRadius * sphere.mRadius;
		float d = b * b - 4 * a * c;
		if (d < 0.f) return false;
		d = sqrt(d);
		t = -.5f * float2(b + d, b - d) / a;
		return true;
	}

	inline bool Intersect(float3 v0, float3 v1, float3 v2, float3* tuv) const {
		// Algorithm from http://jcgt.org/published/0002/01/05/paper.pdf

		v0 -= mOrigin;
		v1 -= mOrigin;
		v2 -= mOrigin;

		float3 rd = mDirection;
		float3 ad = abs(mDirection);

		uint32_t largesti = 0;
		if (ad[largesti] < ad[1]) largesti = 1;
		if (ad[largesti] < ad[2]) largesti = 2;
		 
		float idz;
		float2 rdz;

		if (largesti == 0) {
			v0 = float3(v0.y, v0.z, v0.x);
			v1 = float3(v1.y, v1.z, v1.x);
			v2 = float3(v2.y, v2.z, v2.x);
			idz = 1.f / rd.x;
			rdz = float2(rd.y, rd.z) * idz;
		} else if (largesti == 1) {
			v0 = float3(v0.z, v0.x, v0.y);
			v1 = float3(v1.z, v1.x, v1.y);
			v2 = float3(v2.z, v2.x, v2.y);
			idz = 1.f / rd.y;
			rdz = float2(rd.z, rd.x) * idz;
		} else {
			idz = 1.f / rd.z;
			rdz = float2(rd.x, rd.y) * idz;
		}

		v0 = float3(v0.x - v0.z * rdz.x, v0.y - v0.z * rdz.y, v0.z * idz);
		v1 = float3(v1.x - v1.z * rdz.x, v1.y - v1.z * rdz.y, v1.z * idz);
		v2 = float3(v2.x - v2.z * rdz.x, v2.y - v2.z * rdz.y, v2.z * idz);

		float u = v2.x * v1.y - v2.y * v1.x;
		float v = v0.x * v2.y - v0.y * v2.x;
		float w = v1.x * v0.y - v1.y * v0.x;

		if ((u < 0 || v < 0 || w < 0) && (u > 0 || v > 0 || w > 0)) return false;

		float det = u + v + w;
		if (det == 0) return false; // co-planar

		float t = u * v0.z + v * v1.z + w * v2.z;
		if (tuv) *tuv = float3(t, u, v) / det;
		return true;
	}
};
class fRect2D {
public:
	union {
		float v[4];
		float4 xyzw;
		struct {
			float2 mOffset;
			// full size of rectangle
			float2 mSize;
		};
	};

	inline fRect2D() : mOffset(0), mSize(0) {};
	inline fRect2D(const fRect2D& r) : mOffset(r.mOffset), mSize(r.mSize) {};
	inline fRect2D(const float2& offset, const float2& size) : mOffset(offset), mSize(size) {};
	inline fRect2D(float ox, float oy, float sx, float sy) : mOffset(float2(ox, oy)), mSize(sx, sy) {};

	inline fRect2D& operator=(const fRect2D & rhs) {
		mOffset = rhs.mOffset;
		mSize = rhs.mSize;
		return *this;
	}

	inline bool Intersects(const fRect2D& p) const {
		return !(
			mOffset.x + mSize.x < p.mOffset.x ||
			mOffset.y + mSize.y < p.mOffset.y ||
			mOffset.x > p.mOffset.x + p.mSize.x ||
			mOffset.y > p.mOffset.y + p.mSize.y);
	}
	inline bool Contains(const float2& p) const {
		return 
			p.x > mOffset.x && p.y > mOffset.y &&
			p.x < mOffset.x + mSize.x && p.y < mOffset.y + mSize.y;
	}
};

inline constexpr vk::DeviceSize operator"" _kB(vk::DeviceSize x) { return x*1024; }
inline constexpr vk::DeviceSize operator"" _mB(vk::DeviceSize x) { return x*1024*1024; }
inline constexpr vk::DeviceSize operator"" _gB(vk::DeviceSize x) { return x*1024*1024*1024; }

template<typename T>
inline void safe_delete(T*& x) {if (x != nullptr) { delete x; x = nullptr; } }
template<typename T>
inline void safe_delete_array(T*& x) { if (x != nullptr) { delete[] x; x = nullptr; } }

template <typename T>
inline T AlignUpWithMask(T value, size_t mask) { return (T)(((size_t)value + mask) & ~mask); }
template <typename T>
inline T AlignDownWithMask(T value, size_t mask) { return (T)((size_t)value & ~mask); }
template <typename T>
inline T AlignUp(T value, size_t alignment) { return AlignUpWithMask(value, alignment - 1); }
template <typename T>
inline T AlignDown(T value, size_t alignment) { return AlignDownWithMask(value, alignment - 1); }
template <typename T>
inline bool IsPowerOfTwo(T value) { return 0 == (value & (value - 1)); }

template<typename... Args>
#ifdef WINDOWS
inline void fprintf_color(ConsoleColor color, FILE* str, const char* format, Args&&... a) {
#else
inline void fprintf_color(ConsoleColor color, _IO_FILE* str, const char* format, Args&&... a) {
#endif
#ifdef WINDOWS
	int c = 0;
	if (color & ConsoleColorBits::eRed) 	c |= FOREGROUND_RED;
	if (color & ConsoleColorBits::eGreen) c |= FOREGROUND_GREEN;
	if (color & ConsoleColorBits::eBlue) 	c |= FOREGROUND_BLUE;
	if (color & ConsoleColorBits::eBold) 	c |= FOREGROUND_INTENSITY;
	if (str == stdin) SetConsoleTextAttribute(GetStdHandle(STD_INPUT_HANDLE), c);
	if (str == stdout) SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), c);
	if (str == stderr) SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE), c);
#else
	switch(color) {
		case ConsoleColorBits::eRed:
		fprintf(str, "\x1B[0;31m");
		break;
		case ConsoleColorBits::eGreen:
		fprintf(str, "\x1B[0;32m");
		break;
		case ConsoleColorBits::eBlue:
		fprintf(str, "\x1B[0;34m");
		break;
		case ConsoleColorBits::eYellow:
		fprintf(str, "\x1B[0;33m");
		break;
		case ConsoleColorBits::eCyan:
		fprintf(str, "\x1B[0;36m");
		break;
		case ConsoleColorBits::eMagenta:
		fprintf(str, "\x1B[0;35m");
		break;
	}
#endif

	fprintf(str, format, std::forward<Args>(a)...);

	#ifdef WINDOWS
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
	#else
	fprintf(str, "\x1B[0m");
	#endif
}
template<typename... Args>
inline void printf_color(ConsoleColor color, const char* format, Args&&... a) { fprintf_color(color, stdout, format, std::forward<Args>(a)...); }


inline bool ReadFile(const fs::path& filename, std::string& dest) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) return false;
	size_t fileSize = (size_t)file.tellg();
	dest.resize(fileSize);
	file.seekg(0);
	file.read(const_cast<char*>(dest.data()), fileSize);
	file.close();
	return true;
}
inline bool ReadFile(const fs::path& filename, std::vector<uint8_t>& dest) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) return false;
	size_t fileSize = (size_t)file.tellg();
	dest.resize(fileSize);
	file.seekg(0);
	file.clear();
	file.read(reinterpret_cast<char*>(dest.data()), fileSize);
	file.close();
	return true;
}
inline char* ReadFile(const fs::path& filename, size_t& fileSize) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) return nullptr;
	fileSize = (size_t)file.tellg();
	if (fileSize == 0) return nullptr;
	char* data = new char[fileSize];
	file.seekg(0);
	file.clear();
	file.read(data, fileSize);
	file.close();
	return data;
}

template<typename T>
inline void ReadValue(std::istream& stream, T& value) {
	stream.read(reinterpret_cast<char*>(&value), sizeof(T));
}
inline void ReadString(std::istream& stream, std::string& dest) {
	uint64_t sz;
	ReadValue<uint64_t>(stream, sz);
	if (sz == 0) dest = "";
	else {
		dest.resize(sz);
		stream.read(dest.data(), sz);
	}
}
template<typename Tx>
inline void ReadVector(std::istream& stream, std::vector<Tx>& value) {
	uint64_t size;
	ReadValue<uint64_t>(stream, size);
	if (size) {
		value.resize(size);
		stream.read(reinterpret_cast<char*>(value.data()), sizeof(Tx)*size);
	}
}

template<typename T>
inline void WriteValue(std::ostream& stream, const T& value) {
	stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
}
inline void WriteString(std::ostream& stream, const std::string& value) {
	WriteValue<uint64_t>(stream, value.size());
	if (!value.empty()) stream.write(value.data(), value.size());
}
template<typename Tx>
inline void WriteVector(std::ostream& stream, const std::vector<Tx>& value) {
	WriteValue<uint64_t>(stream, value.size());
	if (!value.empty()) stream.write(reinterpret_cast<const char*>(value.data()), sizeof(Tx)*value.size());
}



inline vk::AccessFlags GuessAccessMask(vk::ImageLayout layout) {
	switch (layout) {
    case vk::ImageLayout::eUndefined:
    case vk::ImageLayout::ePresentSrcKHR:
    case vk::ImageLayout::eColorAttachmentOptimal:
			return {};

    case vk::ImageLayout::eGeneral:
			return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

    case vk::ImageLayout::eDepthAttachmentOptimal:
    case vk::ImageLayout::eStencilAttachmentOptimal:
    case vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal:
    case vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal:
    case vk::ImageLayout::eDepthStencilAttachmentOptimal:
			return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    case vk::ImageLayout::eDepthReadOnlyOptimal:
    case vk::ImageLayout::eStencilReadOnlyOptimal:
    case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
			return vk::AccessFlagBits::eDepthStencilAttachmentRead;
		
    case vk::ImageLayout::eShaderReadOnlyOptimal:
			return vk::AccessFlagBits::eShaderRead;
    case vk::ImageLayout::eTransferSrcOptimal:
			return vk::AccessFlagBits::eTransferRead;
    case vk::ImageLayout::eTransferDstOptimal:
			return vk::AccessFlagBits::eTransferWrite;
	}
	return vk::AccessFlagBits::eShaderRead;
}
inline vk::PipelineStageFlags GuessStage(vk::ImageLayout layout) {
	switch (layout) {
		case vk::ImageLayout::eGeneral:
			return vk::PipelineStageFlagBits::eComputeShader;

		case vk::ImageLayout::eColorAttachmentOptimal:
			return vk::PipelineStageFlagBits::eColorAttachmentOutput;
		
		case vk::ImageLayout::eShaderReadOnlyOptimal:
		case vk::ImageLayout::eDepthReadOnlyOptimal:
		case vk::ImageLayout::eStencilReadOnlyOptimal:
		case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
			return vk::PipelineStageFlagBits::eFragmentShader;

		case vk::ImageLayout::eTransferSrcOptimal:
		case vk::ImageLayout::eTransferDstOptimal:
			return vk::PipelineStageFlagBits::eTransfer;

		case vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal:
		case vk::ImageLayout::eDepthStencilAttachmentOptimal:
		case vk::ImageLayout::eStencilAttachmentOptimal:
		case vk::ImageLayout::eDepthAttachmentOptimal:
		case vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal:
			return vk::PipelineStageFlagBits::eLateFragmentTests;

		case vk::ImageLayout::ePresentSrcKHR:
		case vk::ImageLayout::eSharedPresentKHR:
			return vk::PipelineStageFlagBits::eBottomOfPipe;

		default:
			return vk::PipelineStageFlagBits::eTopOfPipe;
	}
}


// Size of an element of format, in bytes
inline const vk::DeviceSize ElementSize(vk::Format format) {
	switch (format) {
	case vk::Format::eR4G4UnormPack8:
	case vk::Format::eR8Unorm:
	case vk::Format::eR8Snorm:
	case vk::Format::eR8Uscaled:
	case vk::Format::eR8Sscaled:
	case vk::Format::eR8Uint:
	case vk::Format::eR8Sint:
	case vk::Format::eR8Srgb:
	case vk::Format::eS8Uint:
		return 1;

	case vk::Format::eR4G4B4A4UnormPack16:
	case vk::Format::eB4G4R4A4UnormPack16:
	case vk::Format::eR5G6B5UnormPack16:
	case vk::Format::eB5G6R5UnormPack16:
	case vk::Format::eR5G5B5A1UnormPack16:
	case vk::Format::eB5G5R5A1UnormPack16:
	case vk::Format::eA1R5G5B5UnormPack16:
	case vk::Format::eR8G8Unorm:
	case vk::Format::eR8G8Snorm:
	case vk::Format::eR8G8Uscaled:
	case vk::Format::eR8G8Sscaled:
	case vk::Format::eR8G8Uint:
	case vk::Format::eR8G8Sint:
	case vk::Format::eR8G8Srgb:
	case vk::Format::eR16Unorm:
	case vk::Format::eR16Snorm:
	case vk::Format::eR16Uscaled:
	case vk::Format::eR16Sscaled:
	case vk::Format::eR16Uint:
	case vk::Format::eR16Sint:
	case vk::Format::eR16Sfloat:
	case vk::Format::eD16Unorm:
		return 2;

	case vk::Format::eR8G8B8Unorm:
	case vk::Format::eR8G8B8Snorm:
	case vk::Format::eR8G8B8Uscaled:
	case vk::Format::eR8G8B8Sscaled:
	case vk::Format::eR8G8B8Uint:
	case vk::Format::eR8G8B8Sint:
	case vk::Format::eR8G8B8Srgb:
	case vk::Format::eB8G8R8Unorm:
	case vk::Format::eB8G8R8Snorm:
	case vk::Format::eB8G8R8Uscaled:
	case vk::Format::eB8G8R8Sscaled:
	case vk::Format::eB8G8R8Uint:
	case vk::Format::eB8G8R8Sint:
	case vk::Format::eB8G8R8Srgb:
	case vk::Format::eD16UnormS8Uint:
		return 3;

	case vk::Format::eR8G8B8A8Unorm:
	case vk::Format::eR8G8B8A8Snorm:
	case vk::Format::eR8G8B8A8Uscaled:
	case vk::Format::eR8G8B8A8Sscaled:
	case vk::Format::eR8G8B8A8Uint:
	case vk::Format::eR8G8B8A8Sint:
	case vk::Format::eR8G8B8A8Srgb:
	case vk::Format::eB8G8R8A8Unorm:
	case vk::Format::eB8G8R8A8Snorm:
	case vk::Format::eB8G8R8A8Uscaled:
	case vk::Format::eB8G8R8A8Sscaled:
	case vk::Format::eB8G8R8A8Uint:
	case vk::Format::eB8G8R8A8Sint:
	case vk::Format::eB8G8R8A8Srgb:
	case vk::Format::eA8B8G8R8UnormPack32:
	case vk::Format::eA8B8G8R8SnormPack32:
	case vk::Format::eA8B8G8R8UscaledPack32:
	case vk::Format::eA8B8G8R8SscaledPack32:
	case vk::Format::eA8B8G8R8UintPack32:
	case vk::Format::eA8B8G8R8SintPack32:
	case vk::Format::eA8B8G8R8SrgbPack32:
	case vk::Format::eA2R10G10B10UnormPack32:
	case vk::Format::eA2R10G10B10SnormPack32:
	case vk::Format::eA2R10G10B10UscaledPack32:
	case vk::Format::eA2R10G10B10SscaledPack32:
	case vk::Format::eA2R10G10B10UintPack32:
	case vk::Format::eA2R10G10B10SintPack32:
	case vk::Format::eA2B10G10R10UnormPack32:
	case vk::Format::eA2B10G10R10SnormPack32:
	case vk::Format::eA2B10G10R10UscaledPack32:
	case vk::Format::eA2B10G10R10SscaledPack32:
	case vk::Format::eA2B10G10R10UintPack32:
	case vk::Format::eA2B10G10R10SintPack32:
	case vk::Format::eR16G16Unorm:
	case vk::Format::eR16G16Snorm:
	case vk::Format::eR16G16Uscaled:
	case vk::Format::eR16G16Sscaled:
	case vk::Format::eR16G16Uint:
	case vk::Format::eR16G16Sint:
	case vk::Format::eR16G16Sfloat:
	case vk::Format::eR32Uint:
	case vk::Format::eR32Sint:
	case vk::Format::eR32Sfloat:
	case vk::Format::eD24UnormS8Uint:
	case vk::Format::eD32Sfloat:
		return 4;

	case vk::Format::eD32SfloatS8Uint:
		return 5;
		
	case vk::Format::eR16G16B16Unorm:
	case vk::Format::eR16G16B16Snorm:
	case vk::Format::eR16G16B16Uscaled:
	case vk::Format::eR16G16B16Sscaled:
	case vk::Format::eR16G16B16Uint:
	case vk::Format::eR16G16B16Sint:
	case vk::Format::eR16G16B16Sfloat:
		return 6;

	case vk::Format::eR16G16B16A16Unorm:
	case vk::Format::eR16G16B16A16Snorm:
	case vk::Format::eR16G16B16A16Uscaled:
	case vk::Format::eR16G16B16A16Sscaled:
	case vk::Format::eR16G16B16A16Uint:
	case vk::Format::eR16G16B16A16Sint:
	case vk::Format::eR16G16B16A16Sfloat:
	case vk::Format::eR32G32Uint:
	case vk::Format::eR32G32Sint:
	case vk::Format::eR32G32Sfloat:
	case vk::Format::eR64Uint:
	case vk::Format::eR64Sint:
	case vk::Format::eR64Sfloat:
		return 8;

	case vk::Format::eR32G32B32Uint:
	case vk::Format::eR32G32B32Sint:
	case vk::Format::eR32G32B32Sfloat:
		return 12;

	case vk::Format::eR32G32B32A32Uint:
	case vk::Format::eR32G32B32A32Sint:
	case vk::Format::eR32G32B32A32Sfloat:
	case vk::Format::eR64G64Uint:
	case vk::Format::eR64G64Sint:
	case vk::Format::eR64G64Sfloat:
		return 16;

	case vk::Format::eR64G64B64Uint:
	case vk::Format::eR64G64B64Sint:
	case vk::Format::eR64G64B64Sfloat:
		return 24;

	case vk::Format::eR64G64B64A64Uint:
	case vk::Format::eR64G64B64A64Sint:
	case vk::Format::eR64G64B64A64Sfloat:
		return 32;

	}
	return 0;
}
inline uint32_t ChannelCount(vk::Format format) {
	switch (format) {
		case vk::Format::eR8Unorm:
		case vk::Format::eR8Snorm:
		case vk::Format::eR8Uscaled:
		case vk::Format::eR8Sscaled:
		case vk::Format::eR8Uint:
		case vk::Format::eR8Sint:
		case vk::Format::eR8Srgb:
		case vk::Format::eR16Unorm:
		case vk::Format::eR16Snorm:
		case vk::Format::eR16Uscaled:
		case vk::Format::eR16Sscaled:
		case vk::Format::eR16Uint:
		case vk::Format::eR16Sint:
		case vk::Format::eR16Sfloat:
		case vk::Format::eR32Uint:
		case vk::Format::eR32Sint:
		case vk::Format::eR32Sfloat:
		case vk::Format::eR64Uint:
		case vk::Format::eR64Sint:
		case vk::Format::eR64Sfloat:
		case vk::Format::eD16Unorm:
		case vk::Format::eD32Sfloat:
		case vk::Format::eD16UnormS8Uint:
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eX8D24UnormPack32:
		case vk::Format::eS8Uint:
		case vk::Format::eD32SfloatS8Uint:
			return 1;
		case vk::Format::eR4G4UnormPack8:
		case vk::Format::eR8G8Unorm:
		case vk::Format::eR8G8Snorm:
		case vk::Format::eR8G8Uscaled:
		case vk::Format::eR8G8Sscaled:
		case vk::Format::eR8G8Uint:
		case vk::Format::eR8G8Sint:
		case vk::Format::eR8G8Srgb:
		case vk::Format::eR16G16Unorm:
		case vk::Format::eR16G16Snorm:
		case vk::Format::eR16G16Uscaled:
		case vk::Format::eR16G16Sscaled:
		case vk::Format::eR16G16Uint:
		case vk::Format::eR16G16Sint:
		case vk::Format::eR16G16Sfloat:
		case vk::Format::eR32G32Uint:
		case vk::Format::eR32G32Sint:
		case vk::Format::eR32G32Sfloat:
		case vk::Format::eR64G64Uint:
		case vk::Format::eR64G64Sint:
		case vk::Format::eR64G64Sfloat:
			return 2;
		case vk::Format::eR4G4B4A4UnormPack16:
		case vk::Format::eB4G4R4A4UnormPack16:
		case vk::Format::eR5G6B5UnormPack16:
		case vk::Format::eB5G6R5UnormPack16:
		case vk::Format::eR8G8B8Unorm:
		case vk::Format::eR8G8B8Snorm:
		case vk::Format::eR8G8B8Uscaled:
		case vk::Format::eR8G8B8Sscaled:
		case vk::Format::eR8G8B8Uint:
		case vk::Format::eR8G8B8Sint:
		case vk::Format::eR8G8B8Srgb:
		case vk::Format::eB8G8R8Unorm:
		case vk::Format::eB8G8R8Snorm:
		case vk::Format::eB8G8R8Uscaled:
		case vk::Format::eB8G8R8Sscaled:
		case vk::Format::eB8G8R8Uint:
		case vk::Format::eB8G8R8Sint:
		case vk::Format::eB8G8R8Srgb:
		case vk::Format::eR16G16B16Unorm:
		case vk::Format::eR16G16B16Snorm:
		case vk::Format::eR16G16B16Uscaled:
		case vk::Format::eR16G16B16Sscaled:
		case vk::Format::eR16G16B16Uint:
		case vk::Format::eR16G16B16Sint:
		case vk::Format::eR16G16B16Sfloat:
		case vk::Format::eR32G32B32Uint:
		case vk::Format::eR32G32B32Sint:
		case vk::Format::eR32G32B32Sfloat:
		case vk::Format::eR64G64B64Uint:
		case vk::Format::eR64G64B64Sint:
		case vk::Format::eR64G64B64Sfloat:
		case vk::Format::eB10G11R11UfloatPack32:
			return 3;
		case vk::Format::eR5G5B5A1UnormPack16:
		case vk::Format::eB5G5R5A1UnormPack16:
		case vk::Format::eA1R5G5B5UnormPack16:
		case vk::Format::eR8G8B8A8Unorm:
		case vk::Format::eR8G8B8A8Snorm:
		case vk::Format::eR8G8B8A8Uscaled:
		case vk::Format::eR8G8B8A8Sscaled:
		case vk::Format::eR8G8B8A8Uint:
		case vk::Format::eR8G8B8A8Sint:
		case vk::Format::eR8G8B8A8Srgb:
		case vk::Format::eB8G8R8A8Unorm:
		case vk::Format::eB8G8R8A8Snorm:
		case vk::Format::eB8G8R8A8Uscaled:
		case vk::Format::eB8G8R8A8Sscaled:
		case vk::Format::eB8G8R8A8Uint:
		case vk::Format::eB8G8R8A8Sint:
		case vk::Format::eB8G8R8A8Srgb:
		case vk::Format::eA8B8G8R8UnormPack32:
		case vk::Format::eA8B8G8R8SnormPack32:
		case vk::Format::eA8B8G8R8UscaledPack32:
		case vk::Format::eA8B8G8R8SscaledPack32:
		case vk::Format::eA8B8G8R8UintPack32:
		case vk::Format::eA8B8G8R8SintPack32:
		case vk::Format::eA8B8G8R8SrgbPack32:
		case vk::Format::eA2R10G10B10UnormPack32:
		case vk::Format::eA2R10G10B10SnormPack32:
		case vk::Format::eA2R10G10B10UscaledPack32:
		case vk::Format::eA2R10G10B10SscaledPack32:
		case vk::Format::eA2R10G10B10UintPack32:
		case vk::Format::eA2R10G10B10SintPack32:
		case vk::Format::eA2B10G10R10UnormPack32:
		case vk::Format::eA2B10G10R10SnormPack32:
		case vk::Format::eA2B10G10R10UscaledPack32:
		case vk::Format::eA2B10G10R10SscaledPack32:
		case vk::Format::eA2B10G10R10UintPack32:
		case vk::Format::eA2B10G10R10SintPack32:
		case vk::Format::eR16G16B16A16Unorm:
		case vk::Format::eR16G16B16A16Snorm:
		case vk::Format::eR16G16B16A16Uscaled:
		case vk::Format::eR16G16B16A16Sscaled:
		case vk::Format::eR16G16B16A16Uint:
		case vk::Format::eR16G16B16A16Sint:
		case vk::Format::eR16G16B16A16Sfloat:
		case vk::Format::eR32G32B32A32Uint:
		case vk::Format::eR32G32B32A32Sint:
		case vk::Format::eR32G32B32A32Sfloat:
		case vk::Format::eR64G64B64A64Uint:
		case vk::Format::eR64G64B64A64Sint:
		case vk::Format::eR64G64B64A64Sfloat:
		case vk::Format::eE5B9G9R9UfloatPack32:
		case vk::Format::eBc1RgbaUnormBlock:
		case vk::Format::eBc1RgbaSrgbBlock:
			return 4;

		// TODO: compressed formats
		
	}
	return 0;
}
inline bool HasDepthComponent(vk::Format format) {
	return
		format == vk::Format::eD16Unorm ||
		format == vk::Format::eX8D24UnormPack32 ||
		format == vk::Format::eD32Sfloat ||
		format == vk::Format::eD16UnormS8Uint ||
		format == vk::Format::eD24UnormS8Uint ||
		format == vk::Format::eD32SfloatS8Uint;
}
inline bool HasStencilComponent(vk::Format format) {
	return
		format == vk::Format::eS8Uint ||
		format == vk::Format::eD16UnormS8Uint ||
		format == vk::Format::eD24UnormS8Uint ||
		format == vk::Format::eD32SfloatS8Uint;
}

template<typename T>
std::string PrintKeys(const std::map<std::string, T>& map) {
	std::string str = "";
	for (const auto& kp : map) str += + "\"" + kp.first + "\", ";
	if (map.size()) str = str.substr(0, str.length()-1); // remove trailing space
	return str;
}
inline vk::CompareOp atocmp(const std::string& str) {
	static const std::map<std::string, vk::CompareOp> map {
		{ "less",	vk::CompareOp::eLess },
		{ "greater",	vk::CompareOp::eGreater },
		{ "lequal",	vk::CompareOp::eLessOrEqual },
		{ "gequal",	vk::CompareOp::eGreaterOrEqual },
		{ "equal",	vk::CompareOp::eEqual },
		{ "nequal",	vk::CompareOp::eNotEqual },
		{ "never",	vk::CompareOp::eNever },
		{ "always",	vk::CompareOp::eAlways }
	};
	if (!map.count(str)) fprintf_color(ConsoleColorBits::eYellow, stderr, "Error: Unknown comparison: %s (expected one of: '%s')\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}
inline vk::ColorComponentFlags atocolormask(const std::string& str) {
	vk::ColorComponentFlags mask;
	for (uint32_t i = 0; i < str.length(); i++) {
		if      (str[i] == 'r') mask |= vk::ColorComponentFlagBits::eR;
		else if (str[i] == 'g') mask |= vk::ColorComponentFlagBits::eG;
		else if (str[i] == 'b') mask |= vk::ColorComponentFlagBits::eB;
		else if (str[i] == 'a') mask |= vk::ColorComponentFlagBits::eA;
		else fprintf_color(ConsoleColorBits::eYellow, stderr, "Error: Unknown color channel: %c (expected a concatenation of: 'r' 'g' 'b' 'a')\n", str[i]);
	}
	return mask;
}
inline vk::BlendOp atoblendop(const std::string& str) {
	static const std::map<std::string, vk::BlendOp> map {
		 { "add", vk::BlendOp::eAdd },
		 { "subtract", vk::BlendOp::eSubtract },
		 { "reverseSubtract", vk::BlendOp::eReverseSubtract },
		 { "min", vk::BlendOp::eMin },
		 { "max", vk::BlendOp::eMax }
	};
	if (!map.count(str)) fprintf_color(ConsoleColorBits::eYellow, stderr, "Error: Unknown blend op: %s (expected one of: %s)\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}
inline vk::BlendFactor atoblendfactor(const std::string& str) {
	static const std::map<std::string, vk::BlendFactor> map {
		{ "zero", vk::BlendFactor::eZero },
		{ "one", vk::BlendFactor::eOne },
		{ "srcColor", vk::BlendFactor::eSrcColor },
		{ "dstColor", vk::BlendFactor::eDstColor },
  	{ "src1Color", vk::BlendFactor::eSrc1Color },
		{ "oneMinusSrcColor", vk::BlendFactor::eOneMinusSrcColor },
		{ "oneMinusDstColor", vk::BlendFactor::eOneMinusDstColor },
  	{ "oneMinusSrc1Color", vk::BlendFactor::eOneMinusSrc1Color },
		{ "srcAlpha", vk::BlendFactor::eSrcAlpha },
		{ "dstAlpha", vk::BlendFactor::eDstAlpha },
  	{ "src1Alpha", vk::BlendFactor::eSrc1Alpha },
		{ "oneMinusSrcAlpha", vk::BlendFactor::eOneMinusSrcAlpha },
		{ "oneMinusDstAlpha", vk::BlendFactor::eOneMinusDstAlpha },
  	{ "oneMinusSrc1Alpha", vk::BlendFactor::eOneMinusSrc1Alpha },
		{ "srcAlphaSaturate", vk::BlendFactor::eSrcAlphaSaturate }
	};
	if (!map.count(str)) fprintf_color(ConsoleColorBits::eYellow, stderr, "Error: Unknown blend factor: %s (expected one of: %s)\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}
inline vk::Filter atofilter(const std::string& str) {
	static const std::map<std::string, vk::Filter> map {
		{ "nearest", vk::Filter::eNearest },
		{ "linear", vk::Filter::eLinear },
		{ "cubic", vk::Filter::eCubicIMG }
	};
	if (!map.count(str)) fprintf_color(ConsoleColorBits::eYellow, stderr, "Error: Unknown filter: %s (expected one of: %s)\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}
inline vk::SamplerAddressMode atoaddressmode(const std::string& str) {
	static const std::map<std::string, vk::SamplerAddressMode> map {
		{ "repeat", vk::SamplerAddressMode::eRepeat },
		{ "mirroredRepeat", vk::SamplerAddressMode::eRepeat },
		{ "clampEdge", vk::SamplerAddressMode::eClampToEdge },
		{ "clampBorder", vk::SamplerAddressMode::eClampToBorder },
		{ "mirrorClampEdge", vk::SamplerAddressMode::eMirrorClampToEdge },
	};
	if (!map.count(str)) fprintf_color(ConsoleColorBits::eYellow, stderr, "Error: Unknown sampler address mode: %s (expected one of: %s)\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}
inline vk::BorderColor atobordercolor(const std::string& str) {
	static const std::map<std::string, vk::BorderColor> map {
		{ "floatTransparentBlack", vk::BorderColor::eFloatTransparentBlack },
		{ "intTransparentBlack", vk::BorderColor::eIntTransparentBlack },
		{ "floatOpaqueBlack", vk::BorderColor::eFloatOpaqueBlack },
		{ "intOpaqueBlack", vk::BorderColor::eIntOpaqueBlack },
		{ "floatOpaqueWhite", vk::BorderColor::eFloatOpaqueWhite },
		{ "intOpaqueWhite", vk::BorderColor::eIntOpaqueWhite }
	};
	if (!map.count(str)) fprintf_color(ConsoleColorBits::eYellow, stderr, "Error: Unknown border color: %s (expected one of: %s)\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}
inline vk::SamplerMipmapMode atomipmapmode(const std::string& str) {
	static const std::map<std::string, vk::SamplerMipmapMode> map {
		{ "nearest", vk::SamplerMipmapMode::eNearest },
		{ "linear", vk::SamplerMipmapMode::eLinear }
	};
	if (!map.count(str)) fprintf_color(ConsoleColorBits::eYellow, stderr, "Error: Unknown mipmap mode: %s (expected one of: '%s')\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}

template <typename T> inline size_t hash_combine(const T& v) { return std::hash<T>().operator()(v); }
template <typename T, typename... Args>
inline size_t hash_combine(const T& s0, const Args&... rest) {
		size_t seed = ((std::hash<T>())).operator()(s0);
		return seed ^ (hash_combine(rest...) + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

}

namespace std {
	
template<typename BitType>
struct hash<vk::Flags<BitType>> {
	inline size_t operator()(const ::vk::Flags<BitType>& v) const {
		return stm::hash_combine((::vk::Flags<BitType>::MaskType)v);
	}
};

template<>
struct hash<vk::Extent2D> {
	inline size_t operator()(const vk::Extent2D& v) const {
		return stm::hash_combine(v.width, v.height);
	}
};
template<>
struct hash<vk::Extent3D> {
	inline size_t operator()(const vk::Extent3D& v) const {
		return stm::hash_combine(v.width, v.height, v.depth);
	}
};

}