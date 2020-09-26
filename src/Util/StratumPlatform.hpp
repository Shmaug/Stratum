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