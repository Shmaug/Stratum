cmake_minimum_required(VERSION 3.20)

project(Stratum)

# Find dependencies

find_package(Vulkan REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(imgui CONFIG  REQUIRED)
find_package(OpenXR CONFIG)
find_package(miniz CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(OpenVDB CONFIG)

add_subdirectory(src)