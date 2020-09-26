param ($Generator = "'Visual Studio 16 2019' -A x64", [Switch]$BuildDebug)

$STRATUM_DIR = (Resolve-Path $PSScriptRoot).Path

mkdir -Force "$STRATUM_DIR/extern/include"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp" -OutFile "$STRATUM_DIR/extern/include/json.hpp"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"                        -OutFile "$STRATUM_DIR/extern/include/stb_image.h"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h"                  -OutFile "$STRATUM_DIR/extern/include/stb_image_write.h"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/syoyo/tinygltf/master/tiny_gltf.h"                      -OutFile "$STRATUM_DIR/extern/include/tiny_gltf.h"

git submodule update --init
python $STRATUM_DIR/extern/src/shaderc/utils/git-sync-deps

function BuildTarget($FolderName, $Configuration, $ConfigureArguments) {
  $command = "cmake -S $STRATUM_DIR/extern/src/$FolderName -B $STRATUM_DIR/extern/build/$FolderName -G $Generator -DCMAKE_INSTALL_PREFIX=$STRATUM_DIR/extern -Wno-dev $ConfigureArguments"
  if ($Generator -eq "Ninja") {
    $command = $command + " -DCMAKE_BUILD_TYPE=$Configuration"
  }
  Invoke-Expression $command

  Write-Output $command
  
  $prev = Get-Location
  Set-Location "$STRATUM_DIR/extern/build/$FolderName"
  cmake --build . --config $Configuration --target install -- -maxCpuCount
  Set-Location $prev
}

$cfg = $BuildDebug ? "Debug" : "Release"

BuildTarget assimp      "Debug" "-DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_ZLIB=ON"
BuildTarget assimp      "Release" "-DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_ZLIB=ON"
BuildTarget freetype2   "Debug" ""
BuildTarget freetype2   "Release" ""
BuildTarget msdfgen     $cfg "-DFREETYPE_INCLUDE_DIRS='${STRATUM_DIR}/extern/include' -DFREETYPE_LIBRARY='${STRATUM_DIR}/extern/lib/freetype.lib'"
BuildTarget OpenXR-SDK  $cfg ""
BuildTarget shaderc     $cfg "-DEFFCEE_BUILD_SAMPLES=OFF -DEFFCEE_BUILD_TESTING=OFF -DSHADERC_SKIP_TESTS=ON"
BuildTarget SPIRV-Cross $cfg ""