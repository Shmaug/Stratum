cmake_minimum_required(VERSION 3.7)

project(Stratum)
set(CMAKE_CXX_STANDARD 20)

set(STRATUM_DIR ${CMAKE_CURRENT_LIST_DIR})

find_package(Vulkan REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(OpenXR CONFIG REQUIRED)

link_libraries(Eigen3::Eigen Vulkan::Vulkan)

file(DOWNLOAD https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp ${STRATUM_DIR}/extern/include/json.hpp)
file(DOWNLOAD https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/master/src/vk_mem_alloc.h ${STRATUM_DIR}/extern/include/vk_mem_alloc.h)
file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/stb_image.h ${STRATUM_DIR}/extern/include/stb_image.h)
file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h ${STRATUM_DIR}/extern/include/stb_image_write.h)
file(DOWNLOAD https://raw.githubusercontent.com/syoyo/tinygltf/master/tiny_gltf.h ${STRATUM_DIR}/extern/include/tiny_gltf.h)
include_directories(${STRATUM_DIR}/extern/include)

add_subdirectory(src)
#add_subdirectory(Plugins/DicomVis)