#pragma once

#include "hash.hpp"

namespace stm {

using fRay = ParametrizedLine<float,3>;
using dRay = ParametrizedLine<double,3>;

namespace hlsl {

using uint = uint32_t;

template<typename T, int M, int N = 1> using ArrayType  = Eigen::Array<T, M, N, Eigen::ColMajor, M, N>;

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

#define QUATF_I Quaternionf::Identity()

template<typename T, int M, int N, int K>
inline ArrayType<T,M,K> mul(const ArrayType<T,M,N>& a, const ArrayType<T,N,K>& b) {
	return (a.matrix()*b.matrix()).array();
}

template<typename T> inline const T& min(const T& a, const T& b) { return std::min(a,b); }
template<typename T> inline const T& max(const T& a, const T& b) { return std::max(a,b); }
template<typename T, int M, int N> inline ArrayType<T,M,N> max(const ArrayType<T,M,N>& a, const ArrayType<T,M,N>& b) { return a.max(b); }
template<typename T, int M, int N> inline ArrayType<T,M,N> min(const ArrayType<T,M,N>& a, const ArrayType<T,M,N>& b) { return a.min(b); }

template<typename T, int M, int N>
inline ArrayType<T,M,N> saturate(const ArrayType<T,M,N>& v) { return v.max(ArrayType<T,M,N>::Zero()).min(ArrayType<T,M,N>::Ones()); }

template<typename T, int M, int N>
inline T dot(const ArrayType<T,M,N>& a, const ArrayType<T,M,N>& b) { return a.matrix().dot(b.matrix()); }
template<typename T, int M, int N>
inline T length(const ArrayType<T,M,N>& a) { return a.matrix().norm(); }
template<typename T, int M, int N>
inline ArrayType<T,M,N> normalize(const ArrayType<T,M,N>& a) { return a.matrix().normalized().array(); }
template<typename T>
inline ArrayType<T,3> cross(const T& a, const T& b) { return a.matrix().cross(b.matrix()).array(); }

template<typename T> inline T max3(const ArrayType<T,3,1>& x) { return max(max(x[0], x[1]), x[2]); }
template<typename T> inline T max4(const ArrayType<T,4,1>& x) { return max(max(x[0], x[1]), max(x[2], x[3])); }
template<typename T> inline T min3(const ArrayType<T,3,1>& x) { return min(min(x[0], x[1]), x[2]); }
template<typename T> inline T min4(const ArrayType<T,4,1>& x) { return min(min(x[0], x[1]), min(x[2], x[3])); }

template<typename T> inline T pow2(const T& x) { return x*x; }
template<typename T> inline T pow3(const T& x) { return pow2(x)*x; }
template<typename T> inline T pow4(const T& x) { return pow2(x)*pow2(x); }
template<typename T> inline T pow5(const T& x) { return pow4(x)*x; }

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

}

#pragma region misc math expressions

template<unsigned_integral T>
constexpr T floorlog2i(T n) { return sizeof(T)*8 - countl_zero<T>(n) - 1; }

template<floating_point T> constexpr T degrees(const T& r) { return r * (T)180/numbers::pi_v<float>; }
template<floating_point T> constexpr T radians(const T& d) { return d * numbers::pi_v<float>/(T)180; }

template<integral T> constexpr T align_up_mask(T value, size_t mask) { return (T)(((size_t)value + mask) & ~mask); }
template<integral T> constexpr T align_down_mask(T value, size_t mask) { return (T)((size_t)value & ~mask); }
template<integral T> constexpr T align_up(T value, size_t alignment) { return align_up_mask(value, alignment - 1); }
template<integral T> constexpr T align_down(T value, size_t alignment) { return align_down_mask(value, alignment - 1); }

template<typename T>
inline T signed_distance(const Hyperplane<T,3>& plane, const AlignedBox<T,3>& box) {
	Vector3f normal = plane.normal();
	Hyperplane<float,3> dilatatedPlane(normal, plane.offset() - abs(box.sizes().dot(normal)));
	Vector3f n = lerp(box.max(), box.min(), max<Array3f>(normal.array().sign(), Array3f::Zero()));
	return dilatatedPlane.signedDistance(n);
}

#pragma endregion

constexpr unsigned long long operator"" _kB(unsigned long long x) { return x*1024; }
constexpr unsigned long long operator"" _mB(unsigned long long x) { return x*1024*1024; }
constexpr unsigned long long operator"" _gB(unsigned long long x) { return x*1024*1024*1024; }

enum class ConsoleColor {
	eBlack	= 0,
	eRed		= 1,
	eGreen	= 2,
	eBlue		= 4,
	eBold 	= 8,
	eYellow   = eRed | eGreen,
	eCyan		  = eGreen | eBlue,
	eMagenta  = eRed | eBlue,
	eWhite 		= eRed | eGreen | eBlue,
};
template<typename... Args>
inline void fprintf_color(ConsoleColor color, FILE* str, const char* format, Args&&... a) {
	#ifdef WIN32
	int c = 0;
	if ((int)color & (int)ConsoleColor::eRed) 	c |= FOREGROUND_RED;
	if ((int)color & (int)ConsoleColor::eGreen) c |= FOREGROUND_GREEN;
	if ((int)color & (int)ConsoleColor::eBlue) 	c |= FOREGROUND_BLUE;
	if ((int)color & (int)ConsoleColor::eBold) 	c |= FOREGROUND_INTENSITY;
	if      (str == stdin)  SetConsoleTextAttribute(GetStdHandle(STD_INPUT_HANDLE) , c);
	else if (str == stdout) SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), c);
	else if (str == stderr) SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE) , c);
	#else
	switch (color) {
		case ConsoleColor::eBlack:   	fprintf(str, "\x1B[0;30m"); break;
		case ConsoleColor::eRed:  		fprintf(str, "\x1B[0;31m"); break;
		case ConsoleColor::eGreen:  	fprintf(str, "\x1B[0;32m"); break;
		case ConsoleColor::eYellow:  	fprintf(str, "\x1B[0;33m"); break;
		case ConsoleColor::eBlue:  		fprintf(str, "\x1B[0;34m"); break;
		case ConsoleColor::eMagenta:  	fprintf(str, "\x1B[0;35m"); break;
		case ConsoleColor::eCyan:  		fprintf(str, "\x1B[0;36m"); break;
		case ConsoleColor::eWhite:  	fprintf(str, "\x1B[0m"); break;
	}
	#endif
	
	fprintf(str, format, forward<Args>(a)...);

	#ifdef WIN32
	if      (str == stdin)  SetConsoleTextAttribute(GetStdHandle(STD_INPUT_HANDLE) , FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
	else if (str == stdout) SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
	else if (str == stderr) SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE) , FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
	#else
	fprintf(str, "\x1B[0m");
	#endif
}
template<typename... Args> inline void printf_color(ConsoleColor color, const char* format, Args&&... a) { 
	fprintf_color(color, stdout, format, forward<Args>(a)...);
}

inline wstring s2ws(const string &str) {
	 if (str.empty()) return wstring();
	#ifdef WIN32
	 int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
	 wstring wstr(size_needed, 0);
	 MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
	return wstr;
	 #endif
	#ifdef __linux
	 wstring_convert<codecvt_utf8<wchar_t>> wstr;
	 return wstr.from_bytes(str);
	#endif
}
inline string ws2s(const wstring &wstr) {
	if (wstr.empty()) return string();
	#ifdef WIN32
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
	string str(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size_needed, NULL, NULL);
	return str;
	#endif
	#ifdef __linux
	 wstring_convert<codecvt_utf8<wchar_t>> str;
	 return str.to_bytes(wstr);
	#endif
}

template<ranges::contiguous_range R>
inline R read_file(const fs::path& filename) {
	ifstream file(filename, ios::ate | ios::binary);
	if (!file.is_open()) return {};
	R dst;
	dst.resize((size_t)file.tellg()/sizeof(ranges::range_value_t<R>));
	if (dst.empty()) return dst;
	file.seekg(0);
	file.clear();
	file.read(reinterpret_cast<char*>(dst.data()), dst.size()*sizeof(ranges::range_value_t<R>));
	return dst;
}
template<ranges::contiguous_range R>
inline void write_file(const fs::path& filename, const R& r) {
	ofstream file(filename, ios::ate | ios::binary);
	file.write(reinterpret_cast<char*>(r.data()), r.size()*sizeof(ranges::range_value_t<R>));
}

inline constexpr bool is_depth_stencil(vk::Format format) {
	return
		format == vk::Format::eS8Uint ||
		format == vk::Format::eD16Unorm ||
		format == vk::Format::eD16UnormS8Uint ||
		format == vk::Format::eX8D24UnormPack32 ||
		format == vk::Format::eD24UnormS8Uint ||
		format == vk::Format::eD32Sfloat ||
		format == vk::Format::eD32SfloatS8Uint;
}

// Size of an element of format, in bytes
template<typename T = uint32_t> requires(is_arithmetic_v<T>)
inline constexpr T texel_size(vk::Format format) {
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

template<typename T = uint32_t> requires(is_arithmetic_v<T>)
inline constexpr T channel_count(vk::Format format) {
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

}