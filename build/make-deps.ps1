param(
  [String] $BuildConfiguration = "Release",
  [switch] $Clean = $false
)

$STRATUM_DIR = (Resolve-Path (Join-Path $PSScriptRoot /..)).Path

if ($Clean) {
  Remove-Item -LiteralPath $STRATUM_DIR/extern/ -Force -Recurse
}

mkdir -Force extern/include
mkdir -Force extern/src

Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp" -OutFile $STRATUM_DIR/extern/include/json.hpp
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h" -OutFile $STRATUM_DIR/extern/include/stb_image.h
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h" -OutFile $STRATUM_DIR/extern/include/stb_image_write.h
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/syoyo/tinygltf/master/tiny_gltf.h" -OutFile $STRATUM_DIR/extern/include/tiny_gltf.h

git submodule update --init

python ./extern/src/shaderc/utils/git-sync-deps

function BuildTarget ($FolderName, $ConfigureArguments) {
  $prev_dir = Get-Location
  mkdir -Force "$STRATUM_DIR/extern/src/$FolderName/build"
  Set-Location "$STRATUM_DIR/extern/src/$FolderName/build"
  $command = "cmake -S ../ -B . -DCMAKE_INSTALL_PREFIX=$STRATUM_DIR/extern -DCMAKE_BUILD_TYPE=$BuildConfiguration -G Ninja -Wno-dev $ConfigureArguments"
  Invoke-Expression $command
  cmake --build . --target install
  Set-Location $prev_dir
}

BuildTarget assimp      '-DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ZLIB=ON'
BuildTarget freetype2   '-DMSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL'
BuildTarget msdfgen     '-DMSDFGEN_BUILD_MSDFGEN_STANDALONE=OFF -DMSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL -DFREETYPE_INCLUDE_DIRS="${STRATUM_DIR}/extern/include" -DFREETYPE_LIBRARY="${STRATUM_DIR}/extern/lib/freetype.lib"'
BuildTarget OpenXR-SDK  '-DDYNAMIC_LOADER=ON'
BuildTarget shaderc     '-DSHADERC_ENABLE_SHARED_CRT=ON -DBUILD_SHARED_LIBS=OFF -DSHADERC_SKIP_TESTS=ON -DBUILD_TESTING=OFF -DSPIRV_SKIP_EXECUTABLES=ON'
BuildTarget SPIRV-Cross '-DSPIRV_CROSS_ENABLE_TESTS=OFF'