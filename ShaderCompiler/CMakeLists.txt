cmake_minimum_required(VERSION 3.7)

project(ShaderCompiler)
set(CMAKE_CXX_STANDARD 17)

find_package(Vulkan REQUIRED)
find_package(SPIRV-Tools REQUIRED)
find_package(spirv_cross_core REQUIRED)

add_executable(ShaderCompiler "ShaderCompiler.cpp")
target_compile_definitions(ShaderCompiler PUBLIC ${STM_COMPILE_DEFINITIONS})
target_include_directories(ShaderCompiler PUBLIC ${STRATUM_DIR} Vulkan_INCLUDE_DIR)
target_link_libraries(ShaderCompiler shaderc_combined SPIRV-Tools-static spirv-cross-core)