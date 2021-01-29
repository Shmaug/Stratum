cmake_minimum_required(VERSION 3.7)

project(Stratum)

set(STRATUM_DIR ${CMAKE_CURRENT_LIST_DIR})
set(EXTERN_DIR ${CMAKE_CURRENT_LIST_DIR}/extern)

set(CMAKE_CXX_STANDARD 20)

list(APPEND CMAKE_MODULE_PATH ${EXTERN_DIR})
list(APPEND CMAKE_PREFIX_PATH ${EXTERN_DIR})

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Vulkan REQUIRED)
find_package(OpenXR CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(msdfgen CONFIG REQUIRED) 

set(STRATUM_VERSION_MAJOR 1)
set(STRATUM_VERSION_MINOR 2)

option(ENABLE_DEBUG_LAYERS "Enable debug layers" TRUE)

include(cmake/stmutil.cmake)

add_subdirectory(src)
add_subdirectory(Plugins/DicomVis)