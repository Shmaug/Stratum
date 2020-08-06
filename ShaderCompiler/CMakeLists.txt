cmake_minimum_required (VERSION 3.1)

project(ShaderCompiler)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/")

include("${STRATUM_HOME}/build/stratum.cmake")

add_executable(ShaderCompiler "ShaderCompiler.cpp")

target_include_directories(ShaderCompiler PUBLIC
	"${STRATUM_HOME}/ThirdParty/shaderc/include"
  "${STRATUM_HOME}/ThirdParty/SPIRV-Cross/include")
  
if(WIN32)
	target_link_libraries(ShaderCompiler "${STRATUM_HOME}/ThirdParty/shaderc/lib/shaderc_combined.lib")
	if (CMAKE_BUILD_TYPE STREQUAL "Debug")
		add_compile_definitions(_CRTDBG_MAP_ALLOC)
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