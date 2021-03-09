cmake_minimum_required(VERSION 3.0)

# TODO: why does freetype require png??
find_package(PNG REQUIRED)

# ShaderCompiler

add_executable(ShaderCompiler ShaderCompiler.cpp)
target_compile_definitions(ShaderCompiler PUBLIC ${STM_COMPILE_DEFINITIONS})
target_include_directories(ShaderCompiler PUBLIC "${EXTERN_DIR}/include")
target_link_directories(ShaderCompiler PUBLIC "${EXTERN_DIR}/lib")
target_link_libraries(ShaderCompiler PUBLIC Eigen3::Eigen Vulkan::Vulkan PRIVATE spirv-cross-core shaderc_shared)
# TODO: use find_library here
if (EXISTS "${EXTERN_DIR}/bin/dxcompiler.dll" AND EXISTS "${EXTERN_DIR}/lib/dxcompiler.lib")
  target_compile_definitions(ShaderCompiler PUBLIC WITH_DXC)
  target_include_directories(ShaderCompiler PUBLIC "${EXTERN_DIR}/src/DirectXShaderCompiler/include")
  target_link_libraries(ShaderCompiler PUBLIC "${EXTERN_DIR}/lib/dxclib.lib" "${EXTERN_DIR}/lib/dxcompiler.lib")
  configure_file("${EXTERN_DIR}/bin/dxcompiler.dll" dxcompiler.dll COPYONLY)
endif()


# Stratum

file(GLOB_RECURSE STRATUM_HPP "Core/*.hpp")
file(GLOB_RECURSE STRATUM_CPP "Core/*.cpp")

add_executable(Stratum main.cpp ${STRATUM_CPP} stratum.rc)
target_compile_definitions(Stratum PUBLIC ${STM_COMPILE_DEFINITIONS} STRATUM_CORE)
target_link_libraries(Stratum PUBLIC Eigen3::Eigen Vulkan::Vulkan OpenXR::openxr_loader PRIVATE assimp::assimp msdfgen::msdfgen-ext PNG::PNG zlibstatic)

set_target_properties(Stratum PROPERTIES ENABLE_EXPORTS TRUE)


file(GLOB_RECURSE CORE_SHADERS_SOURCES "Shaders/**.hlsl" "Shaders/**.frag" "Shaders/**.vert" "Shaders/**.glsl")
set(CORE_SHADERS_BIN ${STRATUM_DIR}/Assets/core_shaders.spvm)
add_custom_command(
  OUTPUT ${CORE_SHADERS_BIN}
  COMMAND ShaderCompiler -o ${CORE_SHADERS_BIN} -I ${STRATUM_DIR}/src/Shaders/include ${CORE_SHADERS_SOURCES}
  DEPENDS ${CORE_SHADERS_SOURCES}
  IMPLICIT_DEPENDS c ${CORE_SHADERS_SOURCES})
add_custom_target(CoreShaders ALL DEPENDS ${CORE_SHADERS_SOURCES} ShaderCompiler ${CORE_SHADERS_BIN})
add_dependencies(Stratum CoreShaders)


install(TARGETS ShaderCompiler Stratum LIBRARY DESTINATION lib ARCHIVE DESTINATION lib RUNTIME DESTINATION bin)