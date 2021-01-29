cmake_minimum_required(VERSION 3.0)


# ShaderCompiler

add_executable(ShaderCompiler ShaderCompiler.cpp)
target_compile_definitions(ShaderCompiler PUBLIC ${STM_COMPILE_DEFINITIONS})
target_include_directories(ShaderCompiler PUBLIC "${EXTERN_DIR}/include")
target_link_directories(ShaderCompiler PUBLIC "${EXTERN_DIR}/lib")
target_link_libraries(ShaderCompiler PUBLIC Eigen3::Eigen Vulkan::Vulkan PRIVATE spirv-cross-core shaderc_shared)
# TODO: use find_library here
if (EXISTS "${EXTERN_DIR}/bin/dxcompiler.dll" AND EXISTS "${EXTERN_DIR}/lib/dxcompiler.lib")
  message("Compiling ShaderCompiler with DXC")
  target_compile_definitions(ShaderCompiler PUBLIC WITH_DXC)
  target_include_directories(ShaderCompiler PUBLIC "${EXTERN_DIR}/src/DirectXShaderCompiler/include")
  target_link_libraries(ShaderCompiler PUBLIC "${EXTERN_DIR}/lib/dxclib.lib" "${EXTERN_DIR}/lib/dxcompiler.lib")
  configure_file("${EXTERN_DIR}/bin/dxcompiler.dll" dxcompiler.dll COPYONLY)
endif()


# Stratum

file(GLOB_RECURSE STRATUM_HPP "Core/**.hpp" "Input/**.hpp" "Scene/**.hpp" "Util/**.hpp")
file(GLOB_RECURSE STRATUM_CPP "Core/**.cpp" "Input/**.cpp" "Scene/**.cpp" "Util/**.cpp")

add_executable(Stratum main.cpp ${STRATUM_CPP} stratum.rc)
target_compile_definitions(Stratum PUBLIC ${STM_COMPILE_DEFINITIONS} STRATUM_CORE)
target_link_libraries(Stratum PUBLIC Eigen3::Eigen Vulkan::Vulkan OpenXR::openxr_loader PRIVATE assimp::assimp msdfgen::msdfgen msdfgen::msdfgen-ext)

set_target_properties(Stratum PROPERTIES ENABLE_EXPORTS TRUE)


# stmShaders

file(GLOB_RECURSE STRATUM_SHADER_SOURCES "Shaders/**.hlsl" "Shaders/**.frag" "Shaders/**.vert" "Shaders/**.glsl")
stm_compile_shaders(TARGET_NAME stmShaders SOURCES ${STRATUM_SHADER_SOURCES})
add_dependencies(Stratum stmShaders)



install(TARGETS ShaderCompiler Stratum LIBRARY DESTINATION lib ARCHIVE DESTINATION lib RUNTIME DESTINATION bin)