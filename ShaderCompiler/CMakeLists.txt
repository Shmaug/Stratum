cmake_minimum_required(VERSION 3.0)

project(ShaderCompiler)
set(CMAKE_CXX_STANDARD 17)

find_package(Vulkan REQUIRED)
find_package(spirv_cross_core REQUIRED)
find_package(SPIRV-Tools REQUIRED)

add_executable(ShaderCompiler "ShaderCompiler.cpp")
target_link_libraries(ShaderCompiler shaderc_combined SPIRV-Tools-static spirv-cross-core)

set_target_properties(ShaderCompiler PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/"
	LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/"
	ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib/"
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/"
	LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/"
	ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/lib/"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/"
	LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/"
	ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib/")

target_include_directories(ShaderCompiler PUBLIC "${STRATUM_HOME}" "${STRATUM_HOME}/ThirdParty/include")
target_link_directories(ShaderCompiler PUBLIC "${STRATUM_HOME}/ThirdParty/lib")

if(WIN32)
	if(NOT DEFINED ENV{VULKAN_SDK})
		message(FATAL_ERROR "Error: VULKAN_SDK not set!")
	endif()
	target_include_directories(ShaderCompiler PUBLIC "$ENV{VULKAN_SDK}/include")
	target_compile_definitions(ShaderCompiler PUBLIC WINDOWS WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
	target_link_libraries(ShaderCompiler "stdc++fs" "pthread")
endif()