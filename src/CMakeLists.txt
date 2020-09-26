cmake_minimum_required(VERSION 3.0)

# ShaderCompiler

find_package(spirv_cross_core REQUIRED)

add_executable(ShaderCompiler ShaderCompiler.cpp)
target_compile_definitions(ShaderCompiler PUBLIC ${STM_COMPILE_DEFINITIONS})
target_include_directories(ShaderCompiler PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(ShaderCompiler PRIVATE ${STRATUM_DIR}/extern/include)
target_link_directories(ShaderCompiler PRIVATE ${STRATUM_DIR}/extern/lib)
target_link_libraries(ShaderCompiler PUBLIC Vulkan::Vulkan PRIVATE spirv-cross-core shaderc_shared)


# Stratum


file(GLOB_RECURSE STRATUM_CPP "Core/**.cpp" "Data/**.cpp" "Input/**.cpp" "Scene/**.cpp" "Util/**.cpp")
add_executable(Stratum main.cpp ${STRATUM_CPP} stratum.rc)
target_compile_definitions(Stratum PUBLIC ${STM_COMPILE_DEFINITIONS} PRIVATE STRATUM_CORE)
target_include_directories(Stratum PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(Stratum PUBLIC Vulkan::Vulkan OpenXR::openxr_loader PRIVATE assimp::assimp msdfgen::msdfgen msdfgen::msdfgen-ext)

set_target_properties(Stratum PROPERTIES ENABLE_EXPORTS TRUE)

file(GLOB_RECURSE STRATUM_SHADER_SOURCES "Shaders/**.hlsl" "Shaders/**.frag" "Shaders/**.vert" "Shaders/**.glsl")
stm_compile_shaders(TARGET_NAME StratumShaders SOURCES ${STRATUM_SHADER_SOURCES})
add_dependencies(Stratum StratumShaders)

install(TARGETS ShaderCompiler Stratum LIBRARY DESTINATION lib ARCHIVE DESTINATION lib RUNTIME DESTINATION bin)