#pragma once

#include "byte_stream.hpp"
#include "hash_combine.hpp"
#include "Platform.hpp"

namespace stm {

template<typename T> inline void safe_delete(T*& x)       { if (x) { delete   x; x = nullptr; } }
template<typename T> inline void safe_delete_array(T*& x) { if (x) { delete[] x; x = nullptr; } }

#pragma region misc math expressions

template<unsigned_integral T>
constexpr T floorlog2i(T n) { return sizeof(T)*8 - countl_zero<T>(n) - 1; }

template<floating_point T> constexpr T degrees(const T& r) { return r * (T)180/numbers::pi_v<float>; }
template<floating_point T> constexpr T radians(const T& d) { return d * numbers::pi_v<float>/(T)180; }

template<integral T> constexpr T AlignUpWithMask(T value, size_t mask) { return (T)(((size_t)value + mask) & ~mask); }
template<integral T> constexpr T AlignDownWithMask(T value, size_t mask) { return (T)((size_t)value & ~mask); }
template<integral T> constexpr T AlignUp(T value, size_t alignment) { return AlignUpWithMask(value, alignment - 1); }
template<integral T> constexpr T AlignDown(T value, size_t alignment) { return AlignDownWithMask(value, alignment - 1); }

template<typename T>
inline T signedDistance(const Hyperplane<T,3>& plane, const AlignedBox<T,3>& box) {
	Vector3f normal = plane.normal();
	Hyperplane<float,3> dilatatedPlane(normal, plane.offset() - abs(box.sizes().dot(normal)));
	Vector3f n = lerp(box.max(), box.min(), max<Array3f>(normal.array().sign(), Array3f::Zero()));
	return dilatatedPlane.signedDistance(n);
}

#pragma endregion

constexpr size_t operator"" _kB(size_t x) { return x*1024; }
constexpr size_t operator"" _mB(size_t x) { return x*1024*1024; }
constexpr size_t operator"" _gB(size_t x) { return x*1024*1024*1024; }

enum class ConsoleColorBits {
	eBlack	= 0,
	eRed		= 1,
	eGreen	= 2,
	eBlue		= 4,
	eBold 	= 8,
	eYellow   = eRed | eGreen,
	eCyan			= eGreen | eBlue,
	eMagenta  = eRed | eBlue,
	eWhite 		= eRed | eGreen | eBlue,
};
using ConsoleColor = vk::Flags<ConsoleColorBits>;
template<typename... Args>
inline void fprintf_color(ConsoleColor color, FILE* str, const char* format, Args&&... a) {
	#ifdef WIN32
	int c = 0;
	if (color & ConsoleColorBits::eRed) 	c |= FOREGROUND_RED;
	if (color & ConsoleColorBits::eGreen) c |= FOREGROUND_GREEN;
	if (color & ConsoleColorBits::eBlue) 	c |= FOREGROUND_BLUE;
	if (color & ConsoleColorBits::eBold) 	c |= FOREGROUND_INTENSITY;
	if      (str == stdin)  SetConsoleTextAttribute(GetStdHandle(STD_INPUT_HANDLE) , c);
	else if (str == stdout) SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), c);
	else if (str == stderr) SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE) , c);
	#else
	static const unordered_map<ConsoleColorBits, const char*> colorMap {
		{ ConsoleColorBits::eBlack,   	"\x1B[0;30m" }
		{ ConsoleColorBits::eRed,  			"\x1B[0;31m" }
		{ ConsoleColorBits::eGreen,  		"\x1B[0;32m" }
		{ ConsoleColorBits::eYellow,  	"\x1B[0;33m" }
		{ ConsoleColorBits::eBlue,  		"\x1B[0;34m" }
		{ ConsoleColorBits::eMagenta,  	"\x1B[0;35m" }
		{ ConsoleColorBits::eCyan,  		"\x1B[0;36m" }
		{ ConsoleColorBits::eWhite,  		"\x1B[0m" }
	};
	fprintf(str, colorMap.at(color));
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
template<typename... Args> inline void printf_color(ConsoleColor color, const char* format, Args&&... a) { fprintf_color(color, stdout, format, forward<Args>(a)...); }

inline wstring s2ws(const string &str) {
    if (str.empty()) return wstring();
		#ifdef WIN32
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    #endif
		// TODO: linux
		return wstrTo;
}
inline string ws2s(const wstring &wstr) {
    if (wstr.empty()) return string();
		#ifdef WIN32
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    #endif
		// TODO: linux
		return strTo;
}

template<ranges::contiguous_range R>
inline R ReadFile(const fs::path& filename) {
	ifstream file(filename, ios::ate | ios::binary);
	if (!file.is_open()) return {};
	R dst;
	dst.resize((size_t)file.tellg()/sizeof(ranges::range_value_t<R>), '\0');
	if (dst.empty()) return dst;
	file.seekg(0);
	file.clear();
	file.read(reinterpret_cast<char*>(dst.data()), dst.size()*sizeof(ranges::range_value_t<R>));
	return dst;
}

constexpr vk::MemoryPropertyFlags host_visible_coherent = vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent;

// Size of an element of format, in bytes
inline constexpr vk::DeviceSize ElementSize(vk::Format format) {
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
inline constexpr uint32_t ChannelCount(vk::Format format) {
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
inline constexpr bool HasDepthComponent(vk::Format format) {
	return
		format == vk::Format::eD16Unorm ||
		format == vk::Format::eX8D24UnormPack32 ||
		format == vk::Format::eD32Sfloat ||
		format == vk::Format::eD16UnormS8Uint ||
		format == vk::Format::eD24UnormS8Uint ||
		format == vk::Format::eD32SfloatS8Uint;
}
inline constexpr bool HasStencilComponent(vk::Format format) {
	return
		format == vk::Format::eS8Uint ||
		format == vk::Format::eD16UnormS8Uint ||
		format == vk::Format::eD24UnormS8Uint ||
		format == vk::Format::eD32SfloatS8Uint;
}

inline constexpr uint32_t PrimitiveDegree(vk::PrimitiveTopology topo) {
	switch (topo) {
		default:
		case vk::PrimitiveTopology::ePatchList:
		case vk::PrimitiveTopology::ePointList:
			return 1;
		case vk::PrimitiveTopology::eLineList:
		case vk::PrimitiveTopology::eLineStrip:
		case vk::PrimitiveTopology::eLineListWithAdjacency:
		case vk::PrimitiveTopology::eLineStripWithAdjacency:
			return 2;
		case vk::PrimitiveTopology::eTriangleList:
		case vk::PrimitiveTopology::eTriangleStrip:
		case vk::PrimitiveTopology::eTriangleFan:
		case vk::PrimitiveTopology::eTriangleListWithAdjacency:
		case vk::PrimitiveTopology::eTriangleStripWithAdjacency:
			return 3;
	}
}

}