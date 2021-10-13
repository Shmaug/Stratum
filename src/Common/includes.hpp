#pragma once

#include <bit>
#include <chrono>
#include <cstdint>
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

#ifdef STRATUM_EXPORTS
#define STRATUM_API __declspec(dllexport)
#else
#define STRATUM_API __declspec(dllimport)
#endif

#ifdef STRATUM_PLUGIN_EXPORTS
#define PLUGIN_API __declspec(dllexport)
#else
#define PLUGIN_API __declspec(dllimport)
#endif

#endif // #ifdef WIN32

#ifdef __linux
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <dlfcn.h>
#pragma GCC diagnostic ignored "-Wformat-security"
#define STRATUM_API
#define PLUGIN_API
#endif // #ifdef __linux

#include <Eigen/Dense>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>