#pragma once

#define _USE_MATH_DEFINES
#include <cmath>

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
#include <optional>
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

#include <vulkan/vulkan.hpp>

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

#define safe_delete(x) {if (x != nullptr) { delete x; x = nullptr; } }
#define safe_delete_array(x) { if (x != nullptr) { delete[] x; x = nullptr; } }

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
	throw;
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
	throw;
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
std::string PrintKeys(const std::unordered_map<std::string, T>& map) {
	std::string str = "";
	for (const auto& kp : map) str += + "\"" + kp.first + "\", ";
	if (map.size()) str = str.substr(0, str.length()-1); // remove trailing space
	return str;
}
inline vk::CompareOp atocmp(const std::string& str) {
	static const std::unordered_map<std::string, vk::CompareOp> map {
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
		if (str[i] == 'r') mask |= vk::ColorComponentFlagBits::eR;
		else if (str[i] == 'g') mask |= vk::ColorComponentFlagBits::eG;
		else if (str[i] == 'b') mask |= vk::ColorComponentFlagBits::eB;
		else if (str[i] == 'a') mask |= vk::ColorComponentFlagBits::eA;
		fprintf_color(ConsoleColorBits::eYellow, stderr, "Error: Unknown color channel: %c (expected a concatenation of: 'r' 'g' 'b' 'a')\n", str[i]);
	}
	return mask;
}
inline vk::BlendOp atoblendop(const std::string& str) {
	static const std::unordered_map<std::string, vk::BlendOp> map {
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
	static const std::unordered_map<std::string, vk::BlendFactor> map {
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
	static const std::unordered_map<std::string, vk::Filter> map {
		{ "nearest", vk::Filter::eNearest },
		{ "linear", vk::Filter::eLinear },
		{ "cubic", vk::Filter::eCubicIMG }
	};
	if (!map.count(str)) fprintf_color(ConsoleColorBits::eYellow, stderr, "Error: Unknown filter: %s (expected one of: %s)\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}
inline vk::SamplerAddressMode atoaddressmode(const std::string& str) {
	static const std::unordered_map<std::string, vk::SamplerAddressMode> map {
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
	static const std::unordered_map<std::string, vk::BorderColor> map {
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
	static const std::unordered_map<std::string, vk::SamplerMipmapMode> map {
		{ "nearest", vk::SamplerMipmapMode::eNearest },
		{ "linear", vk::SamplerMipmapMode::eLinear }
	};
	if (!map.count(str)) fprintf_color(ConsoleColorBits::eYellow, stderr, "Error: Unknown mipmap mode: %s (expected one of: '%s')\n", str.c_str(), PrintKeys(map).c_str());
	return map.at(str);
}

inline bool FindQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface, uint32_t& graphicsFamily, uint32_t& presentFamily) {
	std::vector<vk::QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();

	bool g = false;
	bool p = false;

	uint32_t i = 0;
	for (const auto& queueFamily : queueFamilies) {
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) {
			graphicsFamily = i;
			g = true;
		}

		vk::Bool32 presentSupport = VK_FALSE;
		device.getSurfaceSupportKHR(i, surface, &presentSupport);

		if (queueFamily.queueCount > 0 && presentSupport) {
			presentFamily = i;
			p = true;
		}

		i++;
	}

	return g && p;
}