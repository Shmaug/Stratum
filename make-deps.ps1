param([String] $Generator = "Visual Studio 16 2019")

$STRATUM_DIR = (Resolve-Path $PSScriptRoot).Path

mkdir -Force "$STRATUM_DIR/extern/include"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp" -OutFile "$STRATUM_DIR/extern/include/json.hpp"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"                        -OutFile "$STRATUM_DIR/extern/include/stb_image.h"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h"                  -OutFile "$STRATUM_DIR/extern/include/stb_image_write.h"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/syoyo/tinygltf/master/tiny_gltf.h"                      -OutFile "$STRATUM_DIR/extern/include/tiny_gltf.h"

git submodule update --init
python $STRATUM_DIR/extern/src/shaderc/utils/git-sync-deps

function BuildTarget($FolderName, $ConfigureArguments) {
  $prev = Get-Location
  mkdir -Force "$STRATUM_DIR/extern/src/$FolderName/build"
  Set-Location "$STRATUM_DIR/extern/src/$FolderName/build"
  $command = "cmake -S ../ -B . -G '$Generator' -DCMAKE_INSTALL_PREFIX=$STRATUM_DIR/extern -Wno-dev $ConfigureArguments"
  Invoke-Expression $command
  cmake --build . --config Debug --target install -- -maxCpuCount
  cmake --build . --config Release --target install -- -maxCpuCount
  Set-Location $prev
}

BuildTarget assimp      "-DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ZLIB=ON"
BuildTarget freetype2   "-DMSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
BuildTarget msdfgen     "-DMSDFGEN_BUILD_MSDFGEN_STANDALONE=OFF -DMSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL -DFREETYPE_INCLUDE_DIRS='${STRATUM_DIR}/extern/include' -DFREETYPE_LIBRARY='${STRATUM_DIR}/extern/lib/freetype.lib'"
BuildTarget OpenXR-SDK  "-DDYNAMIC_LOADER=ON"
BuildTarget shaderc     "-DSHADERC_ENABLE_SHARED_CRT=ON -DBUILD_SHARED_LIBS=OFF -DSHADERC_SKIP_TESTS=ON -DBUILD_TESTING=OFF -DSPIRV_SKIP_EXECUTABLES=ON"
BuildTarget SPIRV-Cross "-DSPIRV_CROSS_ENABLE_TESTS=OFF"