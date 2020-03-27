#!/bin/bash

STRATUM_DIR=$(pwd)
ASSIMP_DIR=$(pwd)/ThirdParty/assimp
SHADERC_DIR=$(pwd)/ThirdParty/shaderc
SPIRV_CROSS_DIR=$(pwd)/ThirdParty/shaderc/third_party/spirv-cross
OPENXR_DIR=$(pwd)/ThirdParty/OpenXR-SDK

echo Installing dependencies...
wget -qO - http://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo apt-key add -
wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.2.131-bionic.list http://packages.lunarg.com/vulkan/1.2.131/lunarg-vulkan-1.2.131-bionic.list
apt update -y
apt install -y vulkan-sdk
apt install -y python3 libz-dev libx11-dev libxrandr-dev
apt install -y build-essential libgl1-mesa-dev libvulkan-dev libx11-xcb-dev libxcb-dri2-0-dev libxcb-glx0-dev libxcb-icccm4-dev libxcb-keysyms1-dev libxcb-randr0-dev libxrandr-dev libxxf86vm-dev mesa-common-dev
echo Dependencies installed.

echo Updating submodules...
git submodule update --init
echo Submodules updated.

echo Configuring Assimp...
cd $ASSIMP_DIR
cmake CMakeLists.txt -S "$ASSIMP_DIR" -B "$ASSIMP_DIR" -Wno-dev -DCMAKE_BUILD_TYPE=Release -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ZLIB=ON -DINJECT_DEBUG_POSTFIX=OFF -DLIBRARY_SUFFIX="" -DCMAKE_INSTALL_PREFIX="$ASSIMP_DIR"
echo Assimp configured.
echo Building Assimp...
make install -j16
echo Assimp built.


cd $SHADERC_DIR
python3 utils/git-sync-deps

echo Configuring Shaderc...
cmake CMakeLists.txt -S "$SHADERC_DIR" -B "$SHADERC_DIR" -Wno-dev -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DSHADERC_SKIP_TESTS=ON -DSPIRV_SKIP_EXECUTABLES=ON -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$SHADERC_DIR"
echo Shaderc configured.
echo Building Shaderc...
make add-copyright
make -j16
make install -j16
echo Shaderc built.

echo Building SPIRV-cross...
cd $SPIRV_CROSS_DIR
make -j16
echo SPIRV-cross built.

cd "$OPENXR_DIR"
echo Configuring OpenXR...
cmake CMakeLists.txt -S "$OPENXR_DIR" -B "$OPENXR_DIR" -Wno-dev -DDYNAMIC_LOADER=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$OPENXR_DIR"
echo OpenXR configured.
echo Building OpenXR
make -j16
make install
echo OpenXR built.

cd $STRATUM_DIR