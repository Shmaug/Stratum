cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 17)

project(ShaderCompiler)

add_executable(ShaderCompiler "ShaderCompiler.cpp")

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

target_include_directories(ShaderCompiler PUBLIC "${STRATUM_HOME}" "${STRATUM_HOME}/ThirdParty/shaderc/include" "${STRATUM_HOME}/ThirdParty/SPIRV-Cross/include")
  
if(WIN32)
	target_compile_definitions(ShaderCompiler PUBLIC WINDOWS WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

	if(NOT DEFINED ENV{VULKAN_SDK})
		message(FATAL_ERROR "Error: VULKAN_SDK not set!")
	endif()
	target_include_directories(ShaderCompiler PUBLIC "$ENV{VULKAN_SDK}/include")

	target_link_libraries(ShaderCompiler "${STRATUM_HOME}/ThirdParty/shaderc/lib/shaderc_combined.lib")
	if (CMAKE_BUILD_TYPE STREQUAL "Debug")
		target_link_libraries(ShaderCompiler "${STRATUM_HOME}/ThirdParty/SPIRV-Cross/lib/spirv-cross-cored.lib")
	else()
		target_link_libraries(ShaderCompiler "${STRATUM_HOME}/ThirdParty/SPIRV-Cross/lib/spirv-cross-core.lib")
	endif()
else()
	target_link_libraries(ShaderCompiler
		stdc++fs
		pthread
		"${STRATUM_HOME}/ThirdParty/shaderc/lib/libshaderc_combined.a"
		"${STRATUM_HOME}/ThirdParty/SPIRV-Cross/libspirv-cross.a" )
endif()