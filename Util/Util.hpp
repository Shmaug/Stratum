#pragma once

#ifdef WINDOWS

#ifdef _DEBUG
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <winsock2.h>
#include <Windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#undef near
#undef far
#undef free
#else
#pragma GCC diagnostic ignored "-Wformat-security"
#endif

#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <string>
#include <cstring>
#include <stdexcept>
#include <variant>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>

#include <vector>
#include <stack>
#include <queue>
#include <unordered_map>
#include <set>
#include <list>
#include <forward_list>

#include <vulkan/vulkan.h>

#include <Util/Enums.hpp>
#include <Util/StratumForward.hpp>
#include <Util/HelperTypes.hpp>

#ifdef __GNUC__
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

#ifdef WINDOWS
#ifdef STRATUM_CORE
#define STRATUM_API __declspec(dllexport)
#define PLUGIN_EXPORT
#else
#define STRATUM_API __declspec(dllimport)
#define PLUGIN_EXPORT __declspec(dllexport)
#endif
#else
#define STRATUM_API
#define PLUGIN_EXPORT
#endif

#define safe_delete(x) if (x != nullptr) { delete x; x = nullptr; }
#define safe_delete_array(x) if (x != nullptr) { delete[] x; x = nullptr; }

template<typename... Args>
inline void printf_color(ConsoleColor color, const char* format, Args&&... a) {
	#ifdef WINDOWS
	int c = 0;
	switch(color) {
		case COLOR_RED:
		case COLOR_RED_BOLD:
		c = FOREGROUND_RED;
		break;
		case COLOR_GREEN:
		case COLOR_GREEN_BOLD:
		c = FOREGROUND_GREEN;
		break;
		case COLOR_BLUE:
		case COLOR_BLUE_BOLD:
		c = FOREGROUND_BLUE;
		break;
		case COLOR_YELLOW:
		case COLOR_YELLOW_BOLD:
		c = FOREGROUND_RED | FOREGROUND_GREEN;
		break;
		case COLOR_CYAN:
		case COLOR_CYAN_BOLD:
		c = FOREGROUND_BLUE | FOREGROUND_GREEN;
		break;
		case COLOR_MAGENTA:
		case COLOR_MAGENTA_BOLD:
		c = FOREGROUND_RED | FOREGROUND_BLUE;
		break;
	}
	if (color >= 6) c |= FOREGROUND_INTENSITY;
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), c);
	#else
	switch(color) {
		case COLOR_RED:
		printf("\x1B[0;31m");
		break;
		case COLOR_GREEN:
		printf("\x1B[0;32m");
		break;
		case COLOR_BLUE:
		printf("\x1B[0;34m");
		break;
		case COLOR_YELLOW:
		printf("\x1B[0;33m");
		break;
		case COLOR_CYAN:
		printf("\x1B[0;36m");
		break;
		case COLOR_MAGENTA:
		printf("\x1B[0;35m");
		break;

		case COLOR_RED_BOLD:
		printf("\x1B[1;31m");
		break;
		case COLOR_GREEN_BOLD:
		printf("\x1B[1;32m");
		break;
		case COLOR_BLUE_BOLD:
		printf("\x1B[1;34m");
		break;
		case COLOR_YELLOW_BOLD:
		printf("\x1B[1;33m");
		break;
		case COLOR_CYAN_BOLD:
		printf("\x1B[1;36m");
		break;
		case COLOR_MAGENTA_BOLD:
		printf("\x1B[1;35m");
		break;
	}
	#endif

	printf(format, std::forward<Args>(a)...);

	#ifdef WINDOWS
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
	#else
	printf("\x1B[0m");
	#endif
}

template<typename... Args>
#ifdef WINDOWS
inline void fprintf_color(ConsoleColor color, FILE* str, const char* format, Args&&... a) {
#else
inline void fprintf_color(ConsoleColor color, _IO_FILE* str, const char* format, Args&&... a) {
#endif
	#ifdef WINDOWS
	int c = 0;
	switch(color) {
		case COLOR_RED:
		case COLOR_RED_BOLD:
		c = FOREGROUND_RED;
		break;
		case COLOR_GREEN:
		case COLOR_GREEN_BOLD:
		c = FOREGROUND_GREEN;
		break;
		case COLOR_BLUE:
		case COLOR_BLUE_BOLD:
		c = FOREGROUND_BLUE;
		break;
		case COLOR_YELLOW:
		case COLOR_YELLOW_BOLD:
		c = FOREGROUND_RED | FOREGROUND_GREEN;
		break;
		case COLOR_CYAN:
		case COLOR_CYAN_BOLD:
		c = FOREGROUND_BLUE | FOREGROUND_GREEN;
		break;
		case COLOR_MAGENTA:
		case COLOR_MAGENTA_BOLD:
		c = FOREGROUND_RED | FOREGROUND_BLUE;
		break;
	}
	if (color >= 6) c |= FOREGROUND_INTENSITY;
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), c);
	#else
	switch(color) {
		case COLOR_RED:
		fprintf(str, "\x1B[0;31m");
		break;
		case COLOR_GREEN:
		fprintf(str, "\x1B[0;32m");
		break;
		case COLOR_BLUE:
		fprintf(str, "\x1B[0;34m");
		break;
		case COLOR_YELLOW:
		fprintf(str, "\x1B[0;33m");
		break;
		case COLOR_CYAN:
		fprintf(str, "\x1B[0;36m");
		break;
		case COLOR_MAGENTA:
		fprintf(str, "\x1B[0;35m");
		break;

		case COLOR_RED_BOLD:
		fprintf(str, "\x1B[1;31m");
		break;
		case COLOR_GREEN_BOLD:
		fprintf(str, "\x1B[1;32m");
		break;
		case COLOR_BLUE_BOLD:
		fprintf(str, "\x1B[1;34m");
		break;
		case COLOR_YELLOW_BOLD:
		fprintf(str, "\x1B[1;33m");
		break;
		case COLOR_CYAN_BOLD:
		fprintf(str, "\x1B[1;36m");
		break;
		case COLOR_MAGENTA_BOLD:
		fprintf(str, "\x1B[1;35m");
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


inline void ThrowIfFailed(VkResult result, const std::string& message) {
	if (result != VK_SUCCESS) {
		const char* code = "<unknown>";
		switch (result) {
			case VK_NOT_READY: code = "VK_NOT_READY"; break;
			case VK_TIMEOUT: code = "VK_TIMEOUT"; break;
			case VK_EVENT_SET: code = "VK_EVENT_SET"; break;
			case VK_EVENT_RESET: code = "VK_EVENT_RESET"; break;
			case VK_INCOMPLETE: code = "VK_INCOMPLETE"; break;
			case VK_ERROR_OUT_OF_HOST_MEMORY: code = "VK_ERROR_OUT_OF_HOST_MEMORY"; break;
			case VK_ERROR_OUT_OF_DEVICE_MEMORY: code = "VK_ERROR_OUT_OF_DEVICE_MEMORY"; break;
			case VK_ERROR_INITIALIZATION_FAILED: code = "VK_ERROR_INITIALIZATION_FAILED"; break;
			case VK_ERROR_DEVICE_LOST: code = "VK_ERROR_DEVICE_LOST"; break;
			case VK_ERROR_MEMORY_MAP_FAILED: code = "VK_ERROR_MEMORY_MAP_FAILED"; break;
			case VK_ERROR_LAYER_NOT_PRESENT: code = "VK_ERROR_LAYER_NOT_PRESENT"; break;
			case VK_ERROR_EXTENSION_NOT_PRESENT: code = "VK_ERROR_EXTENSION_NOT_PRESENT"; break;
			case VK_ERROR_FEATURE_NOT_PRESENT: code = "VK_ERROR_FEATURE_NOT_PRESENT"; break;
			case VK_ERROR_INCOMPATIBLE_DRIVER: code = "VK_ERROR_INCOMPATIBLE_DRIVER"; break;
			case VK_ERROR_TOO_MANY_OBJECTS : code = "VK_ERROR_TOO_MANY_OBJECTS "; break;
			case VK_ERROR_FORMAT_NOT_SUPPORTED : code = "VK_ERROR_FORMAT_NOT_SUPPORTED "; break;
			case VK_ERROR_FRAGMENTED_POOL : code = "VK_ERROR_FRAGMENTED_POOL "; break;
			case VK_ERROR_OUT_OF_POOL_MEMORY: code = "VK_ERROR_OUT_OF_POOL_MEMORY"; break;
			case VK_ERROR_INVALID_EXTERNAL_HANDLE: code = "VK_ERROR_INVALID_EXTERNAL_HANDLE"; break;
			case VK_ERROR_SURFACE_LOST_KHR: code = "VK_ERROR_SURFACE_LOST_KHR"; break;
			case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: code = "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR"; break;
			case VK_ERROR_OUT_OF_DATE_KHR: code = "VK_ERROR_OUT_OF_DATE_KHR"; break;
			case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: code = "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR"; break;
			case VK_ERROR_VALIDATION_FAILED_EXT: code = "VK_ERROR_VALIDATION_FAILED_EXT"; break;
			case VK_ERROR_INVALID_SHADER_NV: code = "VK_ERROR_INVALID_SHADER_NV"; break;
			case VK_ERROR_FRAGMENTATION_EXT: code = "VK_ERROR_FRAGMENTATION_EXT"; break;
			case VK_ERROR_NOT_PERMITTED_EXT: code = "VK_ERROR_NOT_PERMITTED_EXT"; break;
		}
		fprintf_color(COLOR_RED, stderr, "%s: %s\n", message.c_str(), code);
		throw;
	}
}


template <typename T>
inline T AlignUpWithMask(T value, size_t mask) {
	return (T)(((size_t)value + mask) & ~mask);
}
template <typename T>
inline T AlignDownWithMask(T value, size_t mask) {
	return (T)((size_t)value & ~mask);
}
template <typename T>
inline T AlignUp(T value, size_t alignment) {
	return AlignUpWithMask(value, alignment - 1);
}
template <typename T>
inline T AlignDown(T value, size_t alignment) {
	return AlignDownWithMask(value, alignment - 1);
}

template <typename T>
inline bool IsPowerOfTwo(T value) {
	return 0 == (value & (value - 1));
}


inline bool ReadFile(const std::string& filename, std::string& dest) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) return false;
	size_t fileSize = (size_t)file.tellg();
	dest.resize(fileSize);
	file.seekg(0);
	file.read(const_cast<char*>(dest.data()), fileSize);
	file.close();
	return true;
}
inline bool ReadFile(const std::string& filename, std::vector<uint8_t>& dest) {
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
inline char* ReadFile(const std::string& filename, size_t& fileSize) {
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

inline VkAccessFlags GuessAccessMask(VkImageLayout layout) {
	switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			return 0;

    case VK_IMAGE_LAYOUT_GENERAL:
			return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
    case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			return VK_ACCESS_SHADER_READ_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			return VK_ACCESS_TRANSFER_READ_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			return VK_ACCESS_TRANSFER_WRITE_BIT;
	}
	return VK_ACCESS_SHADER_READ_BIT;
}
inline VkPipelineStageFlags GuessStage(VkImageLayout layout) {
	switch (layout) {
		case VK_IMAGE_LAYOUT_GENERAL:
			return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
		case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			return VK_PIPELINE_STAGE_TRANSFER_BIT;

		case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
			return VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
		case VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR:
			return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

		default:
			return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}
}


// Size of an element of format, in bytes
inline const VkDeviceSize FormatSize(VkFormat format) {
	switch (format) {
	case VK_FORMAT_R4G4_UNORM_PACK8:
	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R8_SNORM:
	case VK_FORMAT_R8_USCALED:
	case VK_FORMAT_R8_SSCALED:
	case VK_FORMAT_R8_UINT:
	case VK_FORMAT_R8_SINT:
	case VK_FORMAT_R8_SRGB:
	case VK_FORMAT_S8_UINT:
		return 1;

	case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
	case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
	case VK_FORMAT_R5G6B5_UNORM_PACK16:
	case VK_FORMAT_B5G6R5_UNORM_PACK16:
	case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
	case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
	case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
	case VK_FORMAT_R8G8_UNORM:
	case VK_FORMAT_R8G8_SNORM:
	case VK_FORMAT_R8G8_USCALED:
	case VK_FORMAT_R8G8_SSCALED:
	case VK_FORMAT_R8G8_UINT:
	case VK_FORMAT_R8G8_SINT:
	case VK_FORMAT_R8G8_SRGB:
	case VK_FORMAT_R16_UNORM:
	case VK_FORMAT_R16_SNORM:
	case VK_FORMAT_R16_USCALED:
	case VK_FORMAT_R16_SSCALED:
	case VK_FORMAT_R16_UINT:
	case VK_FORMAT_R16_SINT:
	case VK_FORMAT_R16_SFLOAT:
	case VK_FORMAT_D16_UNORM:
		return 2;

	case VK_FORMAT_R8G8B8_UNORM:
	case VK_FORMAT_R8G8B8_SNORM:
	case VK_FORMAT_R8G8B8_USCALED:
	case VK_FORMAT_R8G8B8_SSCALED:
	case VK_FORMAT_R8G8B8_UINT:
	case VK_FORMAT_R8G8B8_SINT:
	case VK_FORMAT_R8G8B8_SRGB:
	case VK_FORMAT_B8G8R8_UNORM:
	case VK_FORMAT_B8G8R8_SNORM:
	case VK_FORMAT_B8G8R8_USCALED:
	case VK_FORMAT_B8G8R8_SSCALED:
	case VK_FORMAT_B8G8R8_UINT:
	case VK_FORMAT_B8G8R8_SINT:
	case VK_FORMAT_B8G8R8_SRGB:
	case VK_FORMAT_D16_UNORM_S8_UINT:
		return 3;

	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SNORM:
	case VK_FORMAT_R8G8B8A8_USCALED:
	case VK_FORMAT_R8G8B8A8_SSCALED:
	case VK_FORMAT_R8G8B8A8_UINT:
	case VK_FORMAT_R8G8B8A8_SINT:
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_SNORM:
	case VK_FORMAT_B8G8R8A8_USCALED:
	case VK_FORMAT_B8G8R8A8_SSCALED:
	case VK_FORMAT_B8G8R8A8_UINT:
	case VK_FORMAT_B8G8R8A8_SINT:
	case VK_FORMAT_B8G8R8A8_SRGB:
	case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
	case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
	case VK_FORMAT_A8B8G8R8_USCALED_PACK32:
	case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:
	case VK_FORMAT_A8B8G8R8_UINT_PACK32:
	case VK_FORMAT_A8B8G8R8_SINT_PACK32:
	case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
	case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
	case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
	case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
	case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
	case VK_FORMAT_A2R10G10B10_UINT_PACK32:
	case VK_FORMAT_A2R10G10B10_SINT_PACK32:
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
	case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
	case VK_FORMAT_A2B10G10R10_USCALED_PACK32:
	case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
	case VK_FORMAT_A2B10G10R10_UINT_PACK32:
	case VK_FORMAT_A2B10G10R10_SINT_PACK32:
	case VK_FORMAT_R16G16_UNORM:
	case VK_FORMAT_R16G16_SNORM:
	case VK_FORMAT_R16G16_USCALED:
	case VK_FORMAT_R16G16_SSCALED:
	case VK_FORMAT_R16G16_UINT:
	case VK_FORMAT_R16G16_SINT:
	case VK_FORMAT_R16G16_SFLOAT:
	case VK_FORMAT_R32_UINT:
	case VK_FORMAT_R32_SINT:
	case VK_FORMAT_R32_SFLOAT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
	case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
		return 4;

	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return 5;
		
	case VK_FORMAT_R16G16B16_UNORM:
	case VK_FORMAT_R16G16B16_SNORM:
	case VK_FORMAT_R16G16B16_USCALED:
	case VK_FORMAT_R16G16B16_SSCALED:
	case VK_FORMAT_R16G16B16_UINT:
	case VK_FORMAT_R16G16B16_SINT:
	case VK_FORMAT_R16G16B16_SFLOAT:
		return 6;

	case VK_FORMAT_R16G16B16A16_UNORM:
	case VK_FORMAT_R16G16B16A16_SNORM:
	case VK_FORMAT_R16G16B16A16_USCALED:
	case VK_FORMAT_R16G16B16A16_SSCALED:
	case VK_FORMAT_R16G16B16A16_UINT:
	case VK_FORMAT_R16G16B16A16_SINT:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
	case VK_FORMAT_R32G32_UINT:
	case VK_FORMAT_R32G32_SINT:
	case VK_FORMAT_R32G32_SFLOAT:
	case VK_FORMAT_R64_UINT:
	case VK_FORMAT_R64_SINT:
	case VK_FORMAT_R64_SFLOAT:
		return 8;

	case VK_FORMAT_R32G32B32_UINT:
	case VK_FORMAT_R32G32B32_SINT:
	case VK_FORMAT_R32G32B32_SFLOAT:
		return 12;

	case VK_FORMAT_R32G32B32A32_UINT:
	case VK_FORMAT_R32G32B32A32_SINT:
	case VK_FORMAT_R32G32B32A32_SFLOAT:
	case VK_FORMAT_R64G64_UINT:
	case VK_FORMAT_R64G64_SINT:
	case VK_FORMAT_R64G64_SFLOAT:
		return 16;

	case VK_FORMAT_R64G64B64_UINT:
	case VK_FORMAT_R64G64B64_SINT:
	case VK_FORMAT_R64G64B64_SFLOAT:
		return 24;

	case VK_FORMAT_R64G64B64A64_UINT:
	case VK_FORMAT_R64G64B64A64_SINT:
	case VK_FORMAT_R64G64B64A64_SFLOAT:
		return 32;

	}
	return 0;
}

inline bool HasDepthComponent(VkFormat format) {
	return
		format == VK_FORMAT_D16_UNORM ||
		format == VK_FORMAT_X8_D24_UNORM_PACK32 ||
		format == VK_FORMAT_D32_SFLOAT ||
		format == VK_FORMAT_D16_UNORM_S8_UINT ||
		format == VK_FORMAT_D24_UNORM_S8_UINT ||
		format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}
inline bool HasStencilComponent(VkFormat format) {
	return
		format == VK_FORMAT_S8_UINT ||
		format == VK_FORMAT_D16_UNORM_S8_UINT ||
		format == VK_FORMAT_D24_UNORM_S8_UINT ||
		format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

inline const char* FormatToString(VkFormat format) {
	switch (format) {
	case VK_FORMAT_UNDEFINED: return "VK_FORMAT_UNDEFINED";
	case VK_FORMAT_R4G4_UNORM_PACK8: return "VK_FORMAT_R4G4_UNORM_PACK8";
	case VK_FORMAT_R4G4B4A4_UNORM_PACK16: return "VK_FORMAT_R4G4B4A4_UNORM_PACK16";
	case VK_FORMAT_B4G4R4A4_UNORM_PACK16: return "VK_FORMAT_B4G4R4A4_UNORM_PACK16";
	case VK_FORMAT_R5G6B5_UNORM_PACK16: return "VK_FORMAT_R5G6B5_UNORM_PACK16";
	case VK_FORMAT_B5G6R5_UNORM_PACK16: return "VK_FORMAT_B5G6R5_UNORM_PACK16";
	case VK_FORMAT_R5G5B5A1_UNORM_PACK16: return "VK_FORMAT_R5G5B5A1_UNORM_PACK16";
	case VK_FORMAT_B5G5R5A1_UNORM_PACK16: return "VK_FORMAT_B5G5R5A1_UNORM_PACK16";
	case VK_FORMAT_A1R5G5B5_UNORM_PACK16: return "VK_FORMAT_A1R5G5B5_UNORM_PACK16";
	case VK_FORMAT_R8_UNORM: return "VK_FORMAT_R8_UNORM";
	case VK_FORMAT_R8_SNORM: return "VK_FORMAT_R8_SNORM";
	case VK_FORMAT_R8_USCALED: return "VK_FORMAT_R8_USCALED";
	case VK_FORMAT_R8_SSCALED: return "VK_FORMAT_R8_SSCALED";
	case VK_FORMAT_R8_UINT: return "VK_FORMAT_R8_UINT";
	case VK_FORMAT_R8_SINT: return "VK_FORMAT_R8_SINT";
	case VK_FORMAT_R8_SRGB: return "VK_FORMAT_R8_SRGB";
	case VK_FORMAT_R8G8_UNORM: return "VK_FORMAT_R8G8_UNORM";
	case VK_FORMAT_R8G8_SNORM: return "VK_FORMAT_R8G8_SNORM";
	case VK_FORMAT_R8G8_USCALED: return "VK_FORMAT_R8G8_USCALED";
	case VK_FORMAT_R8G8_SSCALED: return "VK_FORMAT_R8G8_SSCALED";
	case VK_FORMAT_R8G8_UINT: return "VK_FORMAT_R8G8_UINT";
	case VK_FORMAT_R8G8_SINT: return "VK_FORMAT_R8G8_SINT";
	case VK_FORMAT_R8G8_SRGB: return "VK_FORMAT_R8G8_SRGB";
	case VK_FORMAT_R8G8B8_UNORM: return "VK_FORMAT_R8G8B8_UNORM";
	case VK_FORMAT_R8G8B8_SNORM: return "VK_FORMAT_R8G8B8_SNORM";
	case VK_FORMAT_R8G8B8_USCALED: return "VK_FORMAT_R8G8B8_USCALED";
	case VK_FORMAT_R8G8B8_SSCALED: return "VK_FORMAT_R8G8B8_SSCALED";
	case VK_FORMAT_R8G8B8_UINT: return "VK_FORMAT_R8G8B8_UINT";
	case VK_FORMAT_R8G8B8_SINT: return "VK_FORMAT_R8G8B8_SINT";
	case VK_FORMAT_R8G8B8_SRGB: return "VK_FORMAT_R8G8B8_SRGB";
	case VK_FORMAT_B8G8R8_UNORM: return "VK_FORMAT_B8G8R8_UNORM";
	case VK_FORMAT_B8G8R8_SNORM: return "VK_FORMAT_B8G8R8_SNORM";
	case VK_FORMAT_B8G8R8_USCALED: return "VK_FORMAT_B8G8R8_USCALED";
	case VK_FORMAT_B8G8R8_SSCALED: return "VK_FORMAT_B8G8R8_SSCALED";
	case VK_FORMAT_B8G8R8_UINT: return "VK_FORMAT_B8G8R8_UINT";
	case VK_FORMAT_B8G8R8_SINT: return "VK_FORMAT_B8G8R8_SINT";
	case VK_FORMAT_B8G8R8_SRGB: return "VK_FORMAT_B8G8R8_SRGB";
	case VK_FORMAT_R8G8B8A8_UNORM: return "VK_FORMAT_R8G8B8A8_UNORM";
	case VK_FORMAT_R8G8B8A8_SNORM: return "VK_FORMAT_R8G8B8A8_SNORM";
	case VK_FORMAT_R8G8B8A8_USCALED: return "VK_FORMAT_R8G8B8A8_USCALED";
	case VK_FORMAT_R8G8B8A8_SSCALED: return "VK_FORMAT_R8G8B8A8_SSCALED";
	case VK_FORMAT_R8G8B8A8_UINT: return "VK_FORMAT_R8G8B8A8_UINT";
	case VK_FORMAT_R8G8B8A8_SINT: return "VK_FORMAT_R8G8B8A8_SINT";
	case VK_FORMAT_R8G8B8A8_SRGB: return "VK_FORMAT_R8G8B8A8_SRGB";
	case VK_FORMAT_B8G8R8A8_UNORM: return "VK_FORMAT_B8G8R8A8_UNORM";
	case VK_FORMAT_B8G8R8A8_SNORM: return "VK_FORMAT_B8G8R8A8_SNORM";
	case VK_FORMAT_B8G8R8A8_USCALED: return "VK_FORMAT_B8G8R8A8_USCALED";
	case VK_FORMAT_B8G8R8A8_SSCALED: return "VK_FORMAT_B8G8R8A8_SSCALED";
	case VK_FORMAT_B8G8R8A8_UINT: return "VK_FORMAT_B8G8R8A8_UINT";
	case VK_FORMAT_B8G8R8A8_SINT: return "VK_FORMAT_B8G8R8A8_SINT";
	case VK_FORMAT_B8G8R8A8_SRGB: return "VK_FORMAT_B8G8R8A8_SRGB";
	case VK_FORMAT_A8B8G8R8_UNORM_PACK32: return "VK_FORMAT_A8B8G8R8_UNORM_PACK32";
	case VK_FORMAT_A8B8G8R8_SNORM_PACK32: return "VK_FORMAT_A8B8G8R8_SNORM_PACK32";
	case VK_FORMAT_A8B8G8R8_USCALED_PACK32: return "VK_FORMAT_A8B8G8R8_USCALED_PACK32";
	case VK_FORMAT_A8B8G8R8_SSCALED_PACK32: return "VK_FORMAT_A8B8G8R8_SSCALED_PACK32";
	case VK_FORMAT_A8B8G8R8_UINT_PACK32: return "VK_FORMAT_A8B8G8R8_UINT_PACK32";
	case VK_FORMAT_A8B8G8R8_SINT_PACK32: return "VK_FORMAT_A8B8G8R8_SINT_PACK32";
	case VK_FORMAT_A8B8G8R8_SRGB_PACK32: return "VK_FORMAT_A8B8G8R8_SRGB_PACK32";
	case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return "VK_FORMAT_A2R10G10B10_UNORM_PACK32";
	case VK_FORMAT_A2R10G10B10_SNORM_PACK32: return "VK_FORMAT_A2R10G10B10_SNORM_PACK32";
	case VK_FORMAT_A2R10G10B10_USCALED_PACK32: return "VK_FORMAT_A2R10G10B10_USCALED_PACK32";
	case VK_FORMAT_A2R10G10B10_SSCALED_PACK32: return "VK_FORMAT_A2R10G10B10_SSCALED_PACK32";
	case VK_FORMAT_A2R10G10B10_UINT_PACK32: return "VK_FORMAT_A2R10G10B10_UINT_PACK32";
	case VK_FORMAT_A2R10G10B10_SINT_PACK32: return "VK_FORMAT_A2R10G10B10_SINT_PACK32";
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
	case VK_FORMAT_A2B10G10R10_SNORM_PACK32: return "VK_FORMAT_A2B10G10R10_SNORM_PACK32";
	case VK_FORMAT_A2B10G10R10_USCALED_PACK32: return "VK_FORMAT_A2B10G10R10_USCALED_PACK32";
	case VK_FORMAT_A2B10G10R10_SSCALED_PACK32: return "VK_FORMAT_A2B10G10R10_SSCALED_PACK32";
	case VK_FORMAT_A2B10G10R10_UINT_PACK32: return "VK_FORMAT_A2B10G10R10_UINT_PACK32";
	case VK_FORMAT_A2B10G10R10_SINT_PACK32: return "VK_FORMAT_A2B10G10R10_SINT_PACK32";
	case VK_FORMAT_R16_UNORM: return "VK_FORMAT_R16_UNORM";
	case VK_FORMAT_R16_SNORM: return "VK_FORMAT_R16_SNORM";
	case VK_FORMAT_R16_USCALED: return "VK_FORMAT_R16_USCALED";
	case VK_FORMAT_R16_SSCALED: return "VK_FORMAT_R16_SSCALED";
	case VK_FORMAT_R16_UINT: return "VK_FORMAT_R16_UINT";
	case VK_FORMAT_R16_SINT: return "VK_FORMAT_R16_SINT";
	case VK_FORMAT_R16_SFLOAT: return "VK_FORMAT_R16_SFLOAT";
	case VK_FORMAT_R16G16_UNORM: return "VK_FORMAT_R16G16_UNORM";
	case VK_FORMAT_R16G16_SNORM: return "VK_FORMAT_R16G16_SNORM";
	case VK_FORMAT_R16G16_USCALED: return "VK_FORMAT_R16G16_USCALED";
	case VK_FORMAT_R16G16_SSCALED: return "VK_FORMAT_R16G16_SSCALED";
	case VK_FORMAT_R16G16_UINT: return "VK_FORMAT_R16G16_UINT";
	case VK_FORMAT_R16G16_SINT: return "VK_FORMAT_R16G16_SINT";
	case VK_FORMAT_R16G16_SFLOAT: return "VK_FORMAT_R16G16_SFLOAT";
	case VK_FORMAT_R16G16B16_UNORM: return "VK_FORMAT_R16G16B16_UNORM";
	case VK_FORMAT_R16G16B16_SNORM: return "VK_FORMAT_R16G16B16_SNORM";
	case VK_FORMAT_R16G16B16_USCALED: return "VK_FORMAT_R16G16B16_USCALED";
	case VK_FORMAT_R16G16B16_SSCALED: return "VK_FORMAT_R16G16B16_SSCALED";
	case VK_FORMAT_R16G16B16_UINT: return "VK_FORMAT_R16G16B16_UINT";
	case VK_FORMAT_R16G16B16_SINT: return "VK_FORMAT_R16G16B16_SINT";
	case VK_FORMAT_R16G16B16_SFLOAT: return "VK_FORMAT_R16G16B16_SFLOAT";
	case VK_FORMAT_R16G16B16A16_UNORM: return "VK_FORMAT_R16G16B16A16_UNORM";
	case VK_FORMAT_R16G16B16A16_SNORM: return "VK_FORMAT_R16G16B16A16_SNORM";
	case VK_FORMAT_R16G16B16A16_USCALED: return "VK_FORMAT_R16G16B16A16_USCALED";
	case VK_FORMAT_R16G16B16A16_SSCALED: return "VK_FORMAT_R16G16B16A16_SSCALED";
	case VK_FORMAT_R16G16B16A16_UINT: return "VK_FORMAT_R16G16B16A16_UINT";
	case VK_FORMAT_R16G16B16A16_SINT: return "VK_FORMAT_R16G16B16A16_SINT";
	case VK_FORMAT_R16G16B16A16_SFLOAT: return "VK_FORMAT_R16G16B16A16_SFLOAT";
	case VK_FORMAT_R32_UINT: return "VK_FORMAT_R32_UINT";
	case VK_FORMAT_R32_SINT: return "VK_FORMAT_R32_SINT";
	case VK_FORMAT_R32_SFLOAT: return "VK_FORMAT_R32_SFLOAT";
	case VK_FORMAT_R32G32_UINT: return "VK_FORMAT_R32G32_UINT";
	case VK_FORMAT_R32G32_SINT: return "VK_FORMAT_R32G32_SINT";
	case VK_FORMAT_R32G32_SFLOAT: return "VK_FORMAT_R32G32_SFLOAT";
	case VK_FORMAT_R32G32B32_UINT: return "VK_FORMAT_R32G32B32_UINT";
	case VK_FORMAT_R32G32B32_SINT: return "VK_FORMAT_R32G32B32_SINT";
	case VK_FORMAT_R32G32B32_SFLOAT: return "VK_FORMAT_R32G32B32_SFLOAT";
	case VK_FORMAT_R32G32B32A32_UINT: return "VK_FORMAT_R32G32B32A32_UINT";
	case VK_FORMAT_R32G32B32A32_SINT: return "VK_FORMAT_R32G32B32A32_SINT";
	case VK_FORMAT_R32G32B32A32_SFLOAT: return "VK_FORMAT_R32G32B32A32_SFLOAT";
	case VK_FORMAT_R64_UINT: return "VK_FORMAT_R64_UINT";
	case VK_FORMAT_R64_SINT: return "VK_FORMAT_R64_SINT";
	case VK_FORMAT_R64_SFLOAT: return "VK_FORMAT_R64_SFLOAT";
	case VK_FORMAT_R64G64_UINT: return "VK_FORMAT_R64G64_UINT";
	case VK_FORMAT_R64G64_SINT: return "VK_FORMAT_R64G64_SINT";
	case VK_FORMAT_R64G64_SFLOAT: return "VK_FORMAT_R64G64_SFLOAT";
	case VK_FORMAT_R64G64B64_UINT: return "VK_FORMAT_R64G64B64_UINT";
	case VK_FORMAT_R64G64B64_SINT: return "VK_FORMAT_R64G64B64_SINT";
	case VK_FORMAT_R64G64B64_SFLOAT: return "VK_FORMAT_R64G64B64_SFLOAT";
	case VK_FORMAT_R64G64B64A64_UINT: return "VK_FORMAT_R64G64B64A64_UINT";
	case VK_FORMAT_R64G64B64A64_SINT: return "VK_FORMAT_R64G64B64A64_SINT";
	case VK_FORMAT_R64G64B64A64_SFLOAT: return "VK_FORMAT_R64G64B64A64_SFLOAT";
	case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return "VK_FORMAT_B10G11R11_UFLOAT_PACK32";
	case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32: return "VK_FORMAT_E5B9G9R9_UFLOAT_PACK32";
	case VK_FORMAT_D16_UNORM: return "VK_FORMAT_D16_UNORM";
	case VK_FORMAT_X8_D24_UNORM_PACK32: return "VK_FORMAT_X8_D24_UNORM_PACK32";
	case VK_FORMAT_D32_SFLOAT: return "VK_FORMAT_D32_SFLOAT";
	case VK_FORMAT_S8_UINT: return "VK_FORMAT_S8_UINT";
	case VK_FORMAT_D16_UNORM_S8_UINT: return "VK_FORMAT_D16_UNORM_S8_UINT";
	case VK_FORMAT_D24_UNORM_S8_UINT: return "VK_FORMAT_D24_UNORM_S8_UINT";
	case VK_FORMAT_D32_SFLOAT_S8_UINT: return "VK_FORMAT_D32_SFLOAT_S8_UINT";
	case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return "VK_FORMAT_BC1_RGB_UNORM_BLOCK";
	case VK_FORMAT_BC1_RGB_SRGB_BLOCK: return "VK_FORMAT_BC1_RGB_SRGB_BLOCK";
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return "VK_FORMAT_BC1_RGBA_UNORM_BLOCK";
	case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: return "VK_FORMAT_BC1_RGBA_SRGB_BLOCK";
	case VK_FORMAT_BC2_UNORM_BLOCK: return "VK_FORMAT_BC2_UNORM_BLOCK";
	case VK_FORMAT_BC2_SRGB_BLOCK: return "VK_FORMAT_BC2_SRGB_BLOCK";
	case VK_FORMAT_BC3_UNORM_BLOCK: return "VK_FORMAT_BC3_UNORM_BLOCK";
	case VK_FORMAT_BC3_SRGB_BLOCK: return "VK_FORMAT_BC3_SRGB_BLOCK";
	case VK_FORMAT_BC4_UNORM_BLOCK: return "VK_FORMAT_BC4_UNORM_BLOCK";
	case VK_FORMAT_BC4_SNORM_BLOCK: return "VK_FORMAT_BC4_SNORM_BLOCK";
	case VK_FORMAT_BC5_UNORM_BLOCK: return "VK_FORMAT_BC5_UNORM_BLOCK";
	case VK_FORMAT_BC5_SNORM_BLOCK: return "VK_FORMAT_BC5_SNORM_BLOCK";
	case VK_FORMAT_BC6H_UFLOAT_BLOCK: return "VK_FORMAT_BC6H_UFLOAT_BLOCK";
	case VK_FORMAT_BC6H_SFLOAT_BLOCK: return "VK_FORMAT_BC6H_SFLOAT_BLOCK";
	case VK_FORMAT_BC7_UNORM_BLOCK: return "VK_FORMAT_BC7_UNORM_BLOCK";
	case VK_FORMAT_BC7_SRGB_BLOCK: return "VK_FORMAT_BC7_SRGB_BLOCK";
	case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK: return "VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK";
	case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK: return "VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK";
	case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK: return "VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK";
	case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK: return "VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK";
	case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK: return "VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK";
	case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK: return "VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK";
	case VK_FORMAT_EAC_R11_UNORM_BLOCK: return "VK_FORMAT_EAC_R11_UNORM_BLOCK";
	case VK_FORMAT_EAC_R11_SNORM_BLOCK: return "VK_FORMAT_EAC_R11_SNORM_BLOCK";
	case VK_FORMAT_EAC_R11G11_UNORM_BLOCK: return "VK_FORMAT_EAC_R11G11_UNORM_BLOCK";
	case VK_FORMAT_EAC_R11G11_SNORM_BLOCK: return "VK_FORMAT_EAC_R11G11_SNORM_BLOCK";
	case VK_FORMAT_ASTC_4x4_UNORM_BLOCK: return "VK_FORMAT_ASTC_4x4_UNORM_BLOCK";
	case VK_FORMAT_ASTC_4x4_SRGB_BLOCK: return "VK_FORMAT_ASTC_4x4_SRGB_BLOCK";
	case VK_FORMAT_ASTC_5x4_UNORM_BLOCK: return "VK_FORMAT_ASTC_5x4_UNORM_BLOCK";
	case VK_FORMAT_ASTC_5x4_SRGB_BLOCK: return "VK_FORMAT_ASTC_5x4_SRGB_BLOCK";
	case VK_FORMAT_ASTC_5x5_UNORM_BLOCK: return "VK_FORMAT_ASTC_5x5_UNORM_BLOCK";
	case VK_FORMAT_ASTC_5x5_SRGB_BLOCK: return "VK_FORMAT_ASTC_5x5_SRGB_BLOCK";
	case VK_FORMAT_ASTC_6x5_UNORM_BLOCK: return "VK_FORMAT_ASTC_6x5_UNORM_BLOCK";
	case VK_FORMAT_ASTC_6x5_SRGB_BLOCK: return "VK_FORMAT_ASTC_6x5_SRGB_BLOCK";
	case VK_FORMAT_ASTC_6x6_UNORM_BLOCK: return "VK_FORMAT_ASTC_6x6_UNORM_BLOCK";
	case VK_FORMAT_ASTC_6x6_SRGB_BLOCK: return "VK_FORMAT_ASTC_6x6_SRGB_BLOCK";
	case VK_FORMAT_ASTC_8x5_UNORM_BLOCK: return "VK_FORMAT_ASTC_8x5_UNORM_BLOCK";
	case VK_FORMAT_ASTC_8x5_SRGB_BLOCK: return "VK_FORMAT_ASTC_8x5_SRGB_BLOCK";
	case VK_FORMAT_ASTC_8x6_UNORM_BLOCK: return "VK_FORMAT_ASTC_8x6_UNORM_BLOCK";
	case VK_FORMAT_ASTC_8x6_SRGB_BLOCK: return "VK_FORMAT_ASTC_8x6_SRGB_BLOCK";
	case VK_FORMAT_ASTC_8x8_UNORM_BLOCK: return "VK_FORMAT_ASTC_8x8_UNORM_BLOCK";
	case VK_FORMAT_ASTC_8x8_SRGB_BLOCK: return "VK_FORMAT_ASTC_8x8_SRGB_BLOCK";
	case VK_FORMAT_ASTC_10x5_UNORM_BLOCK: return "VK_FORMAT_ASTC_10x5_UNORM_BLOCK";
	case VK_FORMAT_ASTC_10x5_SRGB_BLOCK: return "VK_FORMAT_ASTC_10x5_SRGB_BLOCK";
	case VK_FORMAT_ASTC_10x6_UNORM_BLOCK: return "VK_FORMAT_ASTC_10x6_UNORM_BLOCK";
	case VK_FORMAT_ASTC_10x6_SRGB_BLOCK: return "VK_FORMAT_ASTC_10x6_SRGB_BLOCK";
	case VK_FORMAT_ASTC_10x8_UNORM_BLOCK: return "VK_FORMAT_ASTC_10x8_UNORM_BLOCK";
	case VK_FORMAT_ASTC_10x8_SRGB_BLOCK: return "VK_FORMAT_ASTC_10x8_SRGB_BLOCK";
	case VK_FORMAT_ASTC_10x10_UNORM_BLOCK: return "VK_FORMAT_ASTC_10x10_UNORM_BLOCK";
	case VK_FORMAT_ASTC_10x10_SRGB_BLOCK: return "VK_FORMAT_ASTC_10x10_SRGB_BLOCK";
	case VK_FORMAT_ASTC_12x10_UNORM_BLOCK: return "VK_FORMAT_ASTC_12x10_UNORM_BLOCK";
	case VK_FORMAT_ASTC_12x10_SRGB_BLOCK: return "VK_FORMAT_ASTC_12x10_SRGB_BLOCK";
	case VK_FORMAT_ASTC_12x12_UNORM_BLOCK: return "VK_FORMAT_ASTC_12x12_UNORM_BLOCK";
	case VK_FORMAT_ASTC_12x12_SRGB_BLOCK: return "VK_FORMAT_ASTC_12x12_SRGB_BLOCK";
	case VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG: return "VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG";
	case VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG: return "VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG";
	case VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG: return "VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG";
	case VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG: return "VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG";
	case VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG: return "VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG";
	case VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG: return "VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG";
	case VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG: return "VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG";
	case VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG: return "VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG";
	}
	return "";
}
inline const char* TopologyToString(VkPrimitiveTopology topology) {
	switch (topology) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: return "VK_PRIMITIVE_TOPOLOGY_POINT_LIST";                        
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: return "VK_PRIMITIVE_TOPOLOGY_LINE_LIST";
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: return "VK_PRIMITIVE_TOPOLOGY_LINE_STRIP";
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: return "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST";
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP";
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN: return "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN";
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY: return "VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY";
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY: return "VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY";
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY: return "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY";
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY: return "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY";
    case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST: return "VK_PRIMITIVE_TOPOLOGY_PATCH_LIST";
	}
	return "";
}

template<typename T>
std::string PrintKeys(const std::unordered_map<std::string, T>& map) {
	std::string str = "";
	for (const auto& kp : map) str += + "\"" + kp.first + "\", ";
	if (map.size()) str = str.substr(0, str.length()-1); // remove trailing space
	return str;
}
inline VkCompareOp atocmp(const std::string& str) {
	static const std::unordered_map<std::string, VkCompareOp> map {
		{ "less",	VK_COMPARE_OP_LESS },
		{ "greater",	VK_COMPARE_OP_GREATER },
		{ "lequal",	VK_COMPARE_OP_LESS_OR_EQUAL },
		{ "gequal",	VK_COMPARE_OP_GREATER_OR_EQUAL },
		{ "equal",	VK_COMPARE_OP_EQUAL },
		{ "nequal",	VK_COMPARE_OP_NOT_EQUAL },
		{ "never",	VK_COMPARE_OP_NEVER },
		{ "always",	VK_COMPARE_OP_ALWAYS }
	};
	if (!map.count(str)) fprintf_color(COLOR_YELLOW, stderr, "Error: Unknown comparison: %s (expected one of: '%s')\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}
inline VkColorComponentFlags atocolormask(const std::string& str) {
	VkColorComponentFlags mask = 0;
	for (uint32_t i = 0; i < str.length(); i++) {
		if (str[i] == 'r') mask |= VK_COLOR_COMPONENT_R_BIT;
		else if (str[i] == 'g') mask |= VK_COLOR_COMPONENT_G_BIT;
		else if (str[i] == 'b') mask |= VK_COLOR_COMPONENT_B_BIT;
		else if (str[i] == 'a') mask |= VK_COLOR_COMPONENT_A_BIT;
		fprintf_color(COLOR_YELLOW, stderr, "Error: Unknown color channel: %c (expected a concatenation of: 'r' 'g' 'b' 'a')\n", str[i]);
	}
	return mask;
}
inline VkBlendOp atoblendop(const std::string& str) {
	static const std::unordered_map<std::string, VkBlendOp> map {
		 { "add", VK_BLEND_OP_ADD },
		 { "subtract", VK_BLEND_OP_SUBTRACT },
		 { "reverseSubtract", VK_BLEND_OP_REVERSE_SUBTRACT },
		 { "min", VK_BLEND_OP_MIN },
		 { "max", VK_BLEND_OP_MAX }
	};
	if (!map.count(str)) fprintf_color(COLOR_YELLOW, stderr, "Error: Unknown blend op: %s (expected one of: %s)\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}
inline VkBlendFactor atoblendfactor(const std::string& str) {
	static const std::unordered_map<std::string, VkBlendFactor> map {
		{ "zero", VK_BLEND_FACTOR_ZERO },
		{ "one", VK_BLEND_FACTOR_ONE },
		{ "srcColor", VK_BLEND_FACTOR_SRC_COLOR },
		{ "dstColor", VK_BLEND_FACTOR_DST_COLOR },
  	{ "src1Color", VK_BLEND_FACTOR_SRC1_COLOR },
		{ "oneMinusSrcColor", VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR },
		{ "oneMinusDstColor", VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR },
  	{ "oneMinusSrc1Color", VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR },
		{ "srcAlpha", VK_BLEND_FACTOR_SRC_ALPHA },
		{ "dstAlpha", VK_BLEND_FACTOR_DST_ALPHA },
  	{ "src1Alpha", VK_BLEND_FACTOR_SRC1_ALPHA },
		{ "oneMinusSrcAlpha", VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA },
		{ "oneMinusDstAlpha", VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA },
  	{ "oneMinusSrc1Alpha", VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA },
		{ "srcAlphaSaturate", VK_BLEND_FACTOR_SRC_ALPHA_SATURATE }
	};
	if (!map.count(str)) fprintf_color(COLOR_YELLOW, stderr, "Error: Unknown blend factor: %s (expected one of: %s)\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}
inline VkFilter atofilter(const std::string& str) {
	static const std::unordered_map<std::string, VkFilter> map {
		{ "nearest", VK_FILTER_NEAREST },
		{ "linear", VK_FILTER_LINEAR },
		{ "cubic", VK_FILTER_CUBIC_IMG }
	};
	if (!map.count(str)) fprintf_color(COLOR_YELLOW, stderr, "Error: Unknown filter: %s (expected one of: %s)\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}
inline VkSamplerAddressMode atoaddressmode(const std::string& str) {
	static const std::unordered_map<std::string, VkSamplerAddressMode> map {
		{ "repeat", VK_SAMPLER_ADDRESS_MODE_REPEAT },
		{ "mirroredRepeat", VK_SAMPLER_ADDRESS_MODE_REPEAT },
		{ "clampEdge", VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE },
		{ "clampBorder", VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER },
		{ "mirrorClampEdge", VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE },
	};
	if (!map.count(str)) fprintf_color(COLOR_YELLOW, stderr, "Error: Unknown sampler address mode: %s (expected one of: %s)\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}
inline VkBorderColor atobordercolor(const std::string& str) {
	static const std::unordered_map<std::string, VkBorderColor> map {
		{ "floatTransparentBlack", VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK },
		{ "intTransparentBlack", VK_BORDER_COLOR_INT_TRANSPARENT_BLACK },
		{ "floatOpaqueBlack", VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK },
		{ "intOpaqueBlack", VK_BORDER_COLOR_INT_OPAQUE_BLACK },
		{ "floatOpaqueWhite", VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE },
		{ "intOpaqueWhite", VK_BORDER_COLOR_INT_OPAQUE_WHITE }
	};
	if (!map.count(str)) fprintf_color(COLOR_YELLOW, stderr, "Error: Unknown border color: %s (expected one of: %s)\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}
inline VkSamplerMipmapMode atomipmapmode(const std::string& str) {
	static const std::unordered_map<std::string, VkSamplerMipmapMode> map {
		{ "nearest", VK_SAMPLER_MIPMAP_MODE_NEAREST },
		{ "linear", VK_SAMPLER_MIPMAP_MODE_LINEAR }
	};
	if (!map.count(str)) fprintf_color(COLOR_YELLOW, stderr, "Error: Unknown mipmap mode: %s (expected one of: '%s')\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}

inline VkExtent2D To2D(const VkExtent3D& e) { return { e.width, e.height }; }
inline VkExtent3D To3D(const VkExtent2D& e) { return { e.width, e.height, 1}; }
inline bool operator !=(const VkExtent2D& a, const VkExtent2D& b) { return a.width != b.width || a.height != b.height; }
inline bool operator !=(const VkExtent3D& a, const VkExtent3D& b) { return a.width != b.width || a.height != b.height || a.depth != b.depth; }
inline bool operator ==(const VkExtent2D& a, const VkExtent2D& b) { return a.width == b.width && a.height == b.height; }
inline bool operator ==(const VkExtent3D& a, const VkExtent3D& b) { return a.width == b.width && a.height == b.height && a.height == b.depth; }

inline bool FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t& graphicsFamily, uint32_t& presentFamily) {
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

	bool g = false;
	bool p = false;

	uint32_t i = 0;
	for (const auto& queueFamily : queueFamilies) {
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			graphicsFamily = i;
			g = true;
		}

		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

		if (queueFamily.queueCount > 0 && presentSupport) {
			presentFamily = i;
			p = true;
		}

		i++;
	}

	return g && p;
}

template<typename T>
inline void ExecuteParallel(uint32_t iterations, T func) {
	uint32_t threadCount = min(iterations, (uint32_t)std::thread::hardware_concurrency());
	if (threadCount) {
		uint32_t counter = 0;
		std::vector<std::thread> threads;
		for (uint32_t j = 0; j < threadCount; j++) {
			threads.push_back(std::thread([&,j]() {
				for (uint32_t i = j; i < iterations; i += threadCount) {
					func(i);
					counter++;
				}
			}));
		}
		while (counter < iterations) std::this_thread::sleep_for(16ms);
		for (uint32_t i = 0; i < threads.size(); i++) if (threads[i].joinable()) threads[i].join();
	} else
		for (uint32_t i = 0; i < iterations; i++) func(i);
}