#!/bin/bash

git submodule update --init

mkdir -p extern/include
wget -qO "extern/include/json.hpp" "https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp"
wget -qO "extern/include/stb_image.h" "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"                       
wget -qO "extern/include/stb_image_write.h" "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h"                 
wget -qO "extern/include/tiny_gltf.h" "https://raw.githubusercontent.com/syoyo/tinygltf/master/tiny_gltf.h"
wget -qO "extern/include/vk_mem_alloc.h" "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/master/src/vk_mem_alloc.h"