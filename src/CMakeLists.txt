cmake_minimum_required(VERSION 3.20)

include(CompileShader.cmake)

set(CMAKE_CXX_STANDARD 23)
set(CXX_STANDARD_REQUIRED TRUE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

file(GLOB_RECURSE TMP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/**.cpp")
list(APPEND STRATUM_SOURCE ${TMP_SRC})
file(GLOB_RECURSE TMP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/extern/miniz.c")
list(APPEND STRATUM_SOURCE ${TMP_SRC})

add_executable(Stratum ${STRATUM_SOURCE} main.cpp stratum.rc)
set_target_properties(Stratum PROPERTIES ENABLE_EXPORTS TRUE)
set_target_properties(Stratum PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(Stratum PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/extern>"
    "$<BUILD_INTERFACE:${SLANG_ROOT}>"
    "$<INSTALL_INTERFACE:include>"
    "$<INSTALL_INTERFACE:include/extern>")

target_compile_definitions(Stratum PRIVATE STRATUM_EXPORTS)
target_compile_definitions(Stratum PUBLIC STRATUM_VERSION_MAJOR=1 STRATUM_VERSION_MINOR=5 _USE_MATH_DEFINES IMGUI_DEFINE_MATH_OPERATORS)

option(STRATUM_ENABLE_DEBUG_LAYERS "Enable debug layers" TRUE)
if (${STRATUM_ENABLE_DEBUG_LAYERS})
    target_compile_definitions(Stratum PUBLIC STRATUM_ENABLE_DEBUG_LAYERS)
endif()

# Link dependencies

target_link_libraries(Stratum PUBLIC Eigen3::Eigen Vulkan::Vulkan)
if (UNIX)
    target_link_libraries(Stratum PUBLIC pthread ${CMAKE_DL_LIBS} xcb xcb-keysyms)
    target_compile_definitions(Stratum PUBLIC VK_USE_PLATFORM_XCB_KHR)
    target_link_libraries(Stratum PUBLIC "${SLANG_ROOT}/bin/linux-x64/release/libslang.so")
elseif(WIN32)
    target_compile_definitions(Stratum PUBLIC VK_USE_PLATFORM_WIN32_KHR WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS NOMINMAX)
    target_compile_options(Stratum PUBLIC /bigobj)
    target_link_libraries(Stratum PUBLIC "${SLANG_ROOT}/bin/windows-x64/release/slang.lib")
	configure_file("${SLANG_ROOT}/bin/windows-x64/release/slang.dll" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/slang.dll" COPYONLY)
endif()

if (OpenXR_FOUND)
    target_link_libraries(Stratum PUBLIC OpenXR::openxr_loader)
    target_compile_definitions(Stratum PUBLIC STRATUM_ENABLE_OPENXR XR_USE_GRAPHICS_API_VULKAN)
    message(STATUS "OpenXR enabled")
endif()

if (OpenVDB_FOUND)
    target_link_libraries(Stratum PRIVATE OpenVDB::openvdb)
    target_compile_definitions(Stratum PUBLIC STRATUM_ENABLE_OPENVDB)
    message(STATUS "OpenVDB enabled")
endif()

if (assimp_FOUND)
if (WIN32)
    target_link_libraries(Stratum PRIVATE assimp::assimp)
else()
    target_link_libraries(Stratum PRIVATE assimp)
endif()
    target_compile_definitions(Stratum PUBLIC STRATUM_ENABLE_ASSIMP)
    message(STATUS "Assimp enabled")
endif()

# Shaders
make_directory("${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Shaders")

file(GLOB_RECURSE STRATUM_SHADERS "**.[gh]lsl")
stm_add_shaders(SOURCES ${STRATUM_SHADERS} DST_FOLDER "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Shaders" INCLUDES "${CMAKE_CURRENT_LIST_DIR}/Shaders" "${CMAKE_CURRENT_LIST_DIR}/extern" DEPENDS Stratum)
file(GLOB_RECURSE STRATUM_SHADERS "**.frag")
stm_add_shaders(SOURCES ${STRATUM_SHADERS} DST_FOLDER "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Shaders" INCLUDES "${CMAKE_CURRENT_LIST_DIR}/Shaders" "${CMAKE_CURRENT_LIST_DIR}/extern" DEPENDS Stratum)
file(GLOB_RECURSE STRATUM_SHADERS "**.vert")
stm_add_shaders(SOURCES ${STRATUM_SHADERS} DST_FOLDER "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Shaders" INCLUDES "${CMAKE_CURRENT_LIST_DIR}/Shaders" "${CMAKE_CURRENT_LIST_DIR}/extern" DEPENDS Stratum)
file(GLOB_RECURSE STRATUM_SHADERS "**.slang")
stm_add_shaders(SOURCES ${STRATUM_SHADERS} DST_FOLDER "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Shaders" INCLUDES "${CMAKE_CURRENT_LIST_DIR}/Shaders" "${CMAKE_CURRENT_LIST_DIR}/extern" DEPENDS Stratum)

# Install rules

install(TARGETS Stratum
        EXPORT StratumTargets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/ DESTINATION include FILES_MATCHING PATTERN "*.h")
install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/ DESTINATION include FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/ DESTINATION include FILES_MATCHING PATTERN "*.hlsli")
install(FILES CompileShader.cmake DESTINATION lib/cmake)
install(EXPORT StratumTargets FILE StratumTargets.cmake DESTINATION lib/cmake)

include(CMakePackageConfigHelpers)
configure_package_config_file(Config.cmake.in StratumConfig.cmake INSTALL_DESTINATION lib/cmake)
install(FILES ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/StratumConfig.cmake DESTINATION lib/cmake)