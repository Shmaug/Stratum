cmake_minimum_required(VERSION 3.20)

project(Stratum)

# Find dependencies

find_package(Vulkan REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(imgui  REQUIRED CONFIG)
find_package(OpenXR CONFIG)

file(DOWNLOAD https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp                       ${CMAKE_CURRENT_LIST_DIR}/src/extern/json.hpp)
file(DOWNLOAD https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/master/include/vk_mem_alloc.h ${CMAKE_CURRENT_LIST_DIR}/src/extern/vk_mem_alloc.h)
file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/stb_image.h                                              ${CMAKE_CURRENT_LIST_DIR}/src/extern/stb_image.h)
file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h                                        ${CMAKE_CURRENT_LIST_DIR}/src/extern/stb_image_write.h)
file(DOWNLOAD https://raw.githubusercontent.com/syoyo/tinygltf/master/tiny_gltf.h                                            ${CMAKE_CURRENT_LIST_DIR}/src/extern/tiny_gltf.h)

add_subdirectory(src)