cmake_minimum_required(VERSION 3.0)

option(ENABLE_DEBUG_LAYERS "Enable debug layers?" TRUE)
set(STRATUM_HOME ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Directory of Stratum")

add_compile_definitions(STRATUM_VERSION_MAJOR=1)
add_compile_definitions(STRATUM_VERSION_MINOR=1)

add_subdirectory("ShaderCompiler/")

project(Stratum)

include("${STRATUM_HOME}/build/stratum.cmake")

file(GLOB STRATUM_CPP Core/*.cpp Data/*.cpp Input/*.cpp Util/*.cpp Scene/*.cpp Scene/Renderers/*.cpp)
add_executable(Stratum ${STRATUM_CPP} "build/stratum.rc" "main.cpp" "Util/Profiler.cpp" "Util/Tokenizer.cpp")

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

target_compile_definitions(Stratum PUBLIC STRATUM_CORE)

if(WIN32)
	configure_file(
		"${STRATUM_HOME}/ThirdParty/OpenXR-SDK/bin/openxr_loader.dll"
		"${CMAKE_BINARY_DIR}/bin/openxr_loader.dll" COPYONLY)

	target_link_libraries(Stratum
		"${STRATUM_HOME}/ThirdParty/msdfgen/lib/msdfgen.lib"
		"${STRATUM_HOME}/ThirdParty/msdfgen/lib/msdfgen-ext.lib"
		"${STRATUM_HOME}/ThirdParty/OpenXR-SDK/lib/openxr_loader.lib" )
	target_link_directories(Stratum PUBLIC "${STRATUM_HOME}/ThirdParty/freetype/build/${CMAKE_BUILD_TYPE}")
	
	if (CMAKE_BUILD_TYPE STREQUAL "Debug")
		target_compile_definitions(Stratum PUBLIC _CRTDBG_MAP_ALLOC)
		target_link_libraries(Stratum
			"${STRATUM_HOME}/ThirdParty/assimp/lib/assimpd.lib"
			"${STRATUM_HOME}/ThirdParty/assimp/lib/zlibstaticd.lib"
			"${STRATUM_HOME}/ThirdParty/assimp/lib/IrrXMLd.lib" )
	else()
		target_link_libraries(Stratum
			"${STRATUM_HOME}/ThirdParty/assimp/lib/assimp.lib"
			"${STRATUM_HOME}/ThirdParty/assimp/lib/zlibstatic.lib"
			"${STRATUM_HOME}/ThirdParty/assimp/lib/IrrXML.lib" )
	endif()
else()
	target_link_libraries(Stratum
		stdc++fs
		pthread
		"libvulkan.so.1"
		"libX11.so"
		"libXrandr.so"
		"libX11-xcb.so"
		"libxcb-keysyms.so"
		"libxcb-randr.so"
		"${STRATUM_HOME}/ThirdParty/OpenXR-SDK/lib/libopenxr_loader.a"
		"${STRATUM_HOME}/ThirdParty/assimp/lib/libzlibstatic.a"
		"${STRATUM_HOME}/ThirdParty/assimp/lib/libassimp.a"
		"${STRATUM_HOME}/ThirdParty/assimp/lib/libIrrXML.a" )
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