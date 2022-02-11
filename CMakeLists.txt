cmake_minimum_required(VERSION 3.20)

project(Stratum)

# Find dependencies

find_package(Vulkan REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(imgui CONFIG  REQUIRED)
find_package(OpenXR CONFIG)
find_package(miniz CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)

file(DOWNLOAD https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/master/include/vk_mem_alloc.h  ${CMAKE_CURRENT_LIST_DIR}/src/extern/vk_mem_alloc.h)
file(DOWNLOAD https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp                        ${CMAKE_CURRENT_LIST_DIR}/src/extern/json.hpp)
file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/stb_image.h                                               ${CMAKE_CURRENT_LIST_DIR}/src/extern/stb_image.h)
file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h                                         ${CMAKE_CURRENT_LIST_DIR}/src/extern/stb_image_write.h)
file(DOWNLOAD https://raw.githubusercontent.com/syoyo/tinygltf/master/tiny_gltf.h                                             ${CMAKE_CURRENT_LIST_DIR}/src/extern/tiny_gltf.h)
file(DOWNLOAD https://raw.githubusercontent.com/syoyo/tinyexr/master/tinyexr.h                                                ${CMAKE_CURRENT_LIST_DIR}/src/extern/tiny_exr.h)

add_subdirectory(src)