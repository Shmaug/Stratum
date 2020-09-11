cmake_minimum_required(VERSION 3.7)

project(Stratum)
option(ENABLE_DEBUG_LAYERS "Enable debug layers?" TRUE)

get_filename_component(STRATUM_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

set_directory_properties(PROPERTIES
	CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/"
	CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/"
	CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/")

list(APPEND CMAKE_MODULE_PATH "${STRATUM_DIR}/extern/")
list(APPEND CMAKE_PREFIX_PATH "${STRATUM_DIR}/extern/")

include("cmake/stmutil.cmake")

find_package(Vulkan REQUIRED)
find_package(assimp REQUIRED)
find_package(OpenXR REQUIRED)
find_package(msdfgen REQUIRED)

file(GLOB STRATUM_CPP src/**.cpp)
add_executable(Stratum ${STRATUM_CPP} "stratum.rc")
set_target_properties(Stratum PROPERTIES ENABLE_EXPORTS TRUE)
target_compile_definitions(Stratum PUBLIC STRATUM_CORE ${STM_COMPILE_DEFINITIONS})
target_link_libraries(Stratum Vulkan::Vulkan assimp::assimp msdfgen::msdfgen OpenXR::openxr_loader)

add_subdirectory("ShaderCompiler/")

file(GLOB_RECURSE SHADER_SOURCES "Shaders/**.hlsl" "Shaders/**.frag" "Shaders/**.vert" "Shaders/**.glsl")
stm_compile_shaders(ADD_DEPENDS Stratum SHADER_SOURCES ${SOURCES})

add_subdirectory("Plugins/")

# Create symbolic link to Assets folder so the executable can find assets
add_custom_command(
	TARGET Stratum POST_BUILD
	DEPENDS "${PROJECT_BINARY_DIR}/bin/Assets"
	COMMAND ${CMAKE_COMMAND} -E create_symlink "${STRATUM_DIR}/Assets/" "${PROJECT_BINARY_DIR}/bin/Assets")