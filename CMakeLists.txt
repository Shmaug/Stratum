cmake_minimum_required(VERSION 3.0)

project(Stratum)

option(ENABLE_DEBUG_LAYERS "Enable debug layers?" TRUE)
set(STRATUM_HOME ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Directory of Stratum")

list(APPEND CMAKE_PREFIX_PATH "${STRATUM_HOME}/ThirdParty")

include("${STRATUM_HOME}/build/stratum.cmake")

add_subdirectory("ShaderCompiler/")

find_package(Vulkan REQUIRED)
find_package(assimp REQUIRED)
find_package(msdfgen REQUIRED)
find_package(OpenXR REQUIRED)

file(GLOB STRATUM_CPP Core/*.cpp Data/*.cpp Input/*.cpp Util/*.cpp Scene/*.cpp Scene/Renderers/*.cpp)
add_executable(Stratum ${STRATUM_CPP} "build/stratum.rc" "main.cpp" "Util/Profiler.cpp" "Util/Tokenizer.cpp")

stm_compile_definitions(Stratum)

target_link_libraries(Stratum Vulkan assimp::assimp msdfgen::msdfgen OpenXR::openxr_loader)

target_compile_definitions(Stratum PUBLIC STRATUM_CORE)

set_target_properties(Stratum PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/"
	LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/"
	ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib/"
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/"
	LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/"
	ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/lib/"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/"
	LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/"
	ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib/")

if(WIN32)
	configure_file(
		"${STRATUM_HOME}/ThirdParty/bin/openxr_loader.dll"
		"${CMAKE_BINARY_DIR}/bin/openxr_loader.dll" COPYONLY)

	target_link_libraries(Stratum "msdfgen.lib" "msdfgen-ext.lib" "openxr_loader.lib")
	
	if (CMAKE_BUILD_TYPE STREQUAL "Debug")
		target_link_libraries(Stratum "assimpd.lib" "zlibstaticd.lib" "IrrXMLd.lib")
		target_compile_definitions(Stratum PUBLIC _CRTDBG_MAP_ALLOC)
	else()
		target_link_libraries(Stratum "assimp.lib" "zlibstatic.lib" "IrrXML.lib")
	endif()
else()
	target_link_libraries(Stratum
		"stdc++fs"
		"pthread"
		"libX11.so"
		"libXrandr.so"
		"libX11-xcb.so"
		"libxcb-keysyms.so"
		"libxcb-randr.so")
endif()

# Create symbolic link to Assets folder so the executable can find assets
add_custom_command(
	TARGET Stratum POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E create_symlink "${STRATUM_HOME}/build/Assets/" "${PROJECT_BINARY_DIR}/bin/Assets"
	DEPENDS "${PROJECT_BINARY_DIR}/bin/Assets"
	COMMENT "Linking ${STRATUM_HOME}/build/Assets/ to ${PROJECT_BINARY_DIR}/bin/Assets" )

add_shader_target(Shaders "${STRATUM_HOME}/Shaders/")
add_dependencies(Stratum Shaders)

add_subdirectory("Plugins/")