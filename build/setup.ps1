$STRATUM_DIR = (Resolve-Path (Join-Path $PSScriptRoot /..)).Path

function BuildTarget ($FolderName, $ConfigureArguments) {
  $command = "cmake -S $STRATUM_DIR/ThirdParty/src/$FolderName -B $STRATUM_DIR/ThirdParty/src/$FolderName/build"
  $command = $command + " -G Ninja"
  $command = $command + " -DCMAKE_CONFIGURATION_TYPES='Debug;Release' -DCMAKE_INSTALL_PREFIX="+$STRATUM_DIR+"/ThirdParty -Wno-dev " + $ConfigureArguments
  Invoke-Expression $command
  cmake --build "${STRATUM_DIR}/ThirdParty/src/$FolderName/build" --target install
}

Remove-Item -LiteralPath $STRATUM_DIR/ThirdParty/ -Force -Recurse
mkdir -Force ThirdParty/include

Invoke-Webrequest -Outfile ThirdParty/include/json.hpp          -Uri "https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp"
Invoke-Webrequest -Outfile ThirdParty/include/stb_image.h       -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
Invoke-Webrequest -Outfile ThirdParty/include/stb_image_write.h -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h"
Invoke-Webrequest -Outfile ThirdParty/include/tiny_gltf.h       -Uri "https://raw.githubusercontent.com/syoyo/tinygltf/master/tiny_gltf.h"

git submodule update --init
python ./ThirdParty/src/shaderc/utils/git-sync-deps

BuildTarget assimp      '-DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ZLIB=ON'
BuildTarget freetype2   '-DMSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL'
BuildTarget msdfgen     '-DMSDFGEN_BUILD_MSDFGEN_STANDALONE=OFF -DMSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL -DFREETYPE_INCLUDE_DIRS="${STRATUM_DIR}/ThirdParty/include" -DFREETYPE_LIBRARY="${STRATUM_DIR}/ThirdParty/lib/freetype.lib"'
BuildTarget OpenXR-SDK  '-DDYNAMIC_LOADER=ON'
BuildTarget shaderc     '-DSHADERC_ENABLE_SHARED_CRT=ON -DBUILD_SHARED_LIBS=OFF -DSHADERC_SKIP_TESTS=ON -DBUILD_TESTING=OFF -DSPIRV_SKIP_EXECUTABLES=ON'
BuildTarget SPIRV-Cross '-DSPIRV_CROSS_ENABLE_TESTS=OFF'