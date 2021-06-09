cmake_minimum_required(VERSION 3.7)

project(Stratum)
set(CMAKE_CXX_STANDARD 20)

set(STRATUM_PATH ${CMAKE_CURRENT_LIST_DIR})

file(DOWNLOAD https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp ${STRATUM_PATH}/extern/include/json.hpp)
file(DOWNLOAD https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/master/include/vk_mem_alloc.h ${STRATUM_PATH}/extern/include/vk_mem_alloc.h)
file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/stb_image.h ${STRATUM_PATH}/extern/include/stb_image.h)
file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h ${STRATUM_PATH}/extern/include/stb_image_write.h)
file(DOWNLOAD https://raw.githubusercontent.com/syoyo/tinygltf/master/tiny_gltf.h ${STRATUM_PATH}/extern/include/tiny_gltf.h)

if (NOT extern/include/imgui)
    execute_process(COMMAND git clone https://github.com/ocornut/imgui extern/include/imgui WORKING_DIRECTORY ${STRATUM_PATH})
endif()

find_package(Vulkan REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(OpenXR CONFIG REQUIRED)

link_libraries(Eigen3::Eigen Vulkan::Vulkan)
include_directories(${STRATUM_PATH}/extern/include)

include(compile_shader.cmake)

add_subdirectory(src)
#add_subdirectory(Plugins/DicomVis)