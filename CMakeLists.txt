cmake_minimum_required (VERSION 3.1)

project(Stratum)

option(ENABLE_DEBUG_LAYERS "Enable debug layers?" TRUE)
set(STRATUM_HOME ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Directory of Stratum")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/")

add_subdirectory("ShaderCompiler/")

include("${STRATUM_HOME}/build/stratum.cmake")

file(GLOB STRATUM_CPP Core/*.cpp Data/*.cpp Input/*.cpp Util/*.cpp Scene/*.cpp XR/*.cpp)
add_executable(Stratum ${STRATUM_CPP} "build/stratum.rc" "main.cpp" "ThirdParty/json11.cpp" "Util/Profiler.cpp" "Util/Tokenizer.cpp")

add_compile_definitions(STRATUM_VERSION_MAJOR=1)
add_compile_definitions(STRATUM_VERSION_MINOR=1)

target_compile_definitions(Stratum PUBLIC STRATUM_CORE)

target_include_directories(Stratum PUBLIC
	"${STRATUM_HOME}/ThirdParty/msdfgen/include"
	"${STRATUM_HOME}/ThirdParty/openvr/headers"
	"${STRATUM_HOME}/ThirdParty/OpenXR-SDK/include")

if(WIN32)
	target_link_libraries(Stratum
		"${STRATUM_HOME}/ThirdParty/msdfgen/lib/msdfgen.lib"
		"${STRATUM_HOME}/ThirdParty/msdfgen/lib/msdfgen-ext.lib"
		"${STRATUM_HOME}/ThirdParty/openvr/lib/win64/openvr_api.lib"
		"${STRATUM_HOME}/ThirdParty/OpenXR-SDK/lib/openxr_loader.lib" )
	target_link_directories(Stratum PUBLIC "${STRATUM_HOME}/ThirdParty/freetype/build/${CMAKE_BUILD_TYPE}")
	
	if (CMAKE_BUILD_TYPE STREQUAL "Debug")
		add_compile_definitions(_CRTDBG_MAP_ALLOC)
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

	configure_file("${STRATUM_HOME}/ThirdParty/openvr/bin/win64/openvr_api.dll" "${CMAKE_BINARY_DIR}/bin/openvr_api.dll" COPYONLY)
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
		"${STRATUM_HOME}/ThirdParty/openvr/lib/linux64/libopenvr_api.so"
		"${STRATUM_HOME}/ThirdParty/assimp/lib/libzlibstatic.a"
		"${STRATUM_HOME}/ThirdParty/assimp/lib/libassimp.a"
		"${STRATUM_HOME}/ThirdParty/assimp/lib/libIrrXML.a" )

	configure_file("ThirdParty/openvr/bin/linux64/libopenvr_api.so" "${CMAKE_BINARY_DIR}/bin/vrclient.so" COPYONLY)
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