cmake_minimum_required(VERSION 3.7)

list(APPEND CMAKE_MODULE_PATH "$ENV{VULKAN_SDK}")
list(APPEND CMAKE_MODULE_PATH "extern/")

find_package(Vulkan REQUIRED)
find_package(assimp REQUIRED)
find_package(msdfgen REQUIRED)
find_package(OpenXR REQUIRED)

project(Stratum)

option(ENABLE_DEBUG_LAYERS "Enable debug layers?" TRUE)
set(STRATUM_HOME ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Directory of Stratum")


include("${STRATUM_HOME}/cmake/stmutil.cmake")


file(GLOB STRATUM_CPP Core/*.cpp Data/*.cpp Input/*.cpp Util/*.cpp Scene/*.cpp Scene/Renderers/*.cpp)
add_executable(Stratum ${STRATUM_CPP} "build/stratum.rc" "main.cpp" "Util/Profiler.cpp" "Util/Tokenizer.cpp")

stm_compile_definitions(Stratum)
target_compile_definitions(Stratum PUBLIC STRATUM_CORE)

target_link_libraries(Stratum Vulkan::Vulkan assimp::assimp msdfgen::msdfgen OpenXR::openxr_loader)

if(WIN32)
	target_link_libraries(Stratum "msdfgen.lib" "msdfgen-ext.lib" "openxr_loader.lib")
	
	if (CMAKE_BUILD_TYPE STREQUAL "Debug")
		target_link_libraries(Stratum "assimpd.lib" "zlibstaticd.lib" "IrrXMLd.lib")
		target_compile_definitions(Stratum PUBLIC _CRTDBG_MAP_ALLOC)
	else()
		target_link_libraries(Stratum "assimp.lib" "zlibstatic.lib" "IrrXML.lib")
	endif()
endif()

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

# Create symbolic link to Assets folder so the executable can find assets
add_custom_command(
	TARGET Stratum POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E create_symlink "${STRATUM_HOME}/build/Assets/" "${PROJECT_BINARY_DIR}/bin/Assets"
	DEPENDS "${PROJECT_BINARY_DIR}/bin/Assets"
	COMMENT "Linking ${STRATUM_HOME}/build/Assets/ to ${PROJECT_BINARY_DIR}/bin/Assets" )

stm_shader_target(Shaders "${STRATUM_HOME}/Shaders/")
add_dependencies(Stratum Shaders)

add_subdirectory("ShaderCompiler/")
add_subdirectory("Plugins/")