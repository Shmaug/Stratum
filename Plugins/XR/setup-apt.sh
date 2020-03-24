#!/bin/bash

STRATUM_XR_DIR=$(pwd)
OPENXR_DIR=$(pwd)/ThirdParty/OpenXR-SDK


apt install -y build-essential libgl1-mesa-dev libvulkan-dev libx11-xcb-dev libxcb-dri2-0-dev libxcb-glx0-dev libxcb-icccm4-dev libxcb-keysyms1-dev libxcb-randr0-dev libxrandr-dev libxxf86vm-dev mesa-common-dev

cd "$OPENXR_DIR"

cmake CMakeLists.txt -S "$OPENXR_DIR" -B "$OPENXR_DIR" -Wno-dev -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$OPENXR_DIR"
cmake --build . --config Release --target install