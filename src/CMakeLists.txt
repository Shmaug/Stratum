cmake_minimum_required(VERSION 3.20)

include(CompileShader.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CXX_STANDARD_REQUIRED TRUE)

file(GLOB_RECURSE STRATUM_CPP "**.cpp")
add_executable(Stratum "${STRATUM_CPP}" stratum.rc)
set_target_properties(Stratum PROPERTIES ENABLE_EXPORTS TRUE)

target_include_directories(Stratum PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/extern>"
    "$<INSTALL_INTERFACE:include>"
    "$<INSTALL_INTERFACE:include/extern>")

target_link_libraries(Stratum PUBLIC Eigen3::Eigen Vulkan::Vulkan imgui::imgui)
target_link_libraries(Stratum PRIVATE miniz::miniz pugixml pugixml::shared pugixml::pugixml)
if (UNIX)
    target_link_libraries(Stratum PUBLIC xcb xcb-keysyms)
endif()

option(STRATUM_ENABLE_DEBUG_LAYERS "Enable debug layers" TRUE)
if (${STRATUM_ENABLE_DEBUG_LAYERS})
    target_compile_definitions(Stratum PUBLIC STRATUM_ENABLE_DEBUG_LAYERS)
endif()

target_compile_definitions(Stratum PRIVATE STRATUM_EXPORTS)
target_compile_definitions(Stratum PUBLIC STRATUM_VERSION_MAJOR=1 STRATUM_VERSION_MINOR=5 _USE_MATH_DEFINES IMGUI_DEFINE_MATH_OPERATORS)
if (UNIX)
    target_compile_definitions(Stratum PUBLIC VK_USE_PLATFORM_XCB_KHR)
elseif(WIN32)
    target_compile_definitions(Stratum PUBLIC VK_USE_PLATFORM_WIN32_KHR WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS NOMINMAX)
    target_compile_options(Stratum PUBLIC /bigobj)
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
    target_link_libraries(Stratum PRIVATE assimp::assimp)
    target_compile_definitions(Stratum PUBLIC STRATUM_ENABLE_ASSIMP)
    message(STATUS "Assimp enabled")
endif()

# Shaders

file(GLOB_RECURSE STRATUM_SHADERS "**.[gh]lsl")
stm_add_shaders(SOURCES ${STRATUM_SHADERS} DEPENDS Stratum)
file(GLOB_RECURSE STRATUM_SHADERS "**.frag")
stm_add_shaders(SOURCES ${STRATUM_SHADERS} DEPENDS Stratum)
file(GLOB_RECURSE STRATUM_SHADERS "**.vert")
stm_add_shaders(SOURCES ${STRATUM_SHADERS} DEPENDS Stratum)

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
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/StratumConfig.cmake DESTINATION lib/cmake)