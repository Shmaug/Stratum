#pragma once

#ifdef WIN32

#ifdef _DEBUG
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <WS2tcpip.h>
#include <filesystem>
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

#ifdef __GNUC__
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <experimental/filesystem>
#define STRATUM_API
#define PLUGIN_EXPORT
#endif