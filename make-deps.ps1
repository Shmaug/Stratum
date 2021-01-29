param ($Generator="Visual Studio 16 2019", [Switch]$BuildDebug)


$STRATUM_DIR = (Resolve-Path $PSScriptRoot).Path

mkdir -Force "$STRATUM_DIR/extern/include"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp" -OutFile "$STRATUM_DIR/extern/include/json.hpp"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"                        -OutFile "$STRATUM_DIR/extern/include/stb_image.h"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h"                  -OutFile "$STRATUM_DIR/extern/include/stb_image_write.h"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/syoyo/tinygltf/master/tiny_gltf.h"                      -OutFile "$STRATUM_DIR/extern/include/tiny_gltf.h"
(Invoke-WebRequest -Uri "https://gitlab.com/libeigen/eigen/-/archive/3.3.9/eigen-3.3.9.zip").Content | Expand-Archive -Path "$STRATUM_DIR/extern/include/eigen"

python "$STRATUM_DIR/extern/src/shaderc/utils/git-sync-deps"

function BuildTarget($FolderName, $Configuration, $ConfigureArguments) {
  $command = "cmake -S '$STRATUM_DIR/extern/src/$FolderName' -B '$STRATUM_DIR/extern/build/$FolderName' -G '$Generator'"
  if ($Generator -eq "Ninja") {
    $command = $command + " -DCMAKE_BUILD_TYPE='$Configuration'"
  } elseif ($Generator -eq "Visual Studio 16 2019") {
    $command = $command + " -A x64"
  }
  $command = $command + " -Wno-dev -DCMAKE_INSTALL_PREFIX='$STRATUM_DIR/extern' $ConfigureArguments"
  Write-Output $command
  Invoke-Expression $command
  
  $prev = Get-Location
  Set-Location "$STRATUM_DIR/extern/build/$FolderName"
  cmake --build . --config $Configuration --target install -- -maxCpuCount
  Set-Location $prev
}

$cfg = $BuildDebug ? "Debug" : "Release"

BuildTarget assimp      "Debug" '-DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_ZLIB=ON'
BuildTarget assimp      "Release" '-DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_ZLIB=ON'
BuildTarget freetype2   "Debug" ''
BuildTarget freetype2   "Release" ''
BuildTarget msdfgen     $cfg "-DFREETYPE_INCLUDE_DIRS='$STRATUM_DIR/extern/include' -DFREETYPE_LIBRARY='$STRATUM_DIR/extern/lib/freetype.lib'"
BuildTarget OpenXR-SDK  $cfg ''
BuildTarget shaderc     $cfg "-DEFFCEE_BUILD_SAMPLES=OFF -DEFFCEE_BUILD_TESTING=OFF -DSHADERC_SKIP_TESTS=ON"
BuildTarget SPIRV-Cross $cfg ""
BuildTarget DirectXShaderCompiler $cfg '-DHLSL_OPTIONAL_PROJS_IN_DEFAULT:BOOL=OFF -DHLSL_ENABLE_ANALYZE:BOOL=OFF -DHLSL_OFFICIAL_BUILD:BOOL=OFF -DHLSL_ENABLE_FIXED_VER:BOOL=OFF -DHLSL_ENABLE_FIXED_VER:BOOL=OFF -DHLSL_BUILD_DXILCONV:BOOL=ON -DENABLE_SPIRV_CODEGEN:BOOL=TRUE -DSPIRV_BUILD_TESTS:BOOL=OFF -DCLANG_ENABLE_ARCMT:BOOL=OFF -DCLANG_ENABLE_STATIC_ANALYZER:BOOL=OFF -DCLANG_INCLUDE_TESTS:BOOL=OFF -DLLVM_INCLUDE_TESTS:BOOL=OFF -DHLSL_INCLUDE_TESTS:BOOL=ON -DLLVM_TARGETS_TO_BUILD:STRING=None -DLLVM_INCLUDE_DOCS:BOOL=OFF -DLLVM_INCLUDE_EXAMPLES:BOOL=OFF -DLIBCLANG_BUILD_STATIC:BOOL=ON -DLLVM_OPTIMIZED_TABLEGEN:BOOL=OFF -DLLVM_REQUIRES_EH:BOOL=ON -DLLVM_APPEND_VC_REV:BOOL=ON -DLLVM_ENABLE_RTTI:BOOL=ON -DLLVM_ENABLE_EH:BOOL=ON -DLLVM_DEFAULT_TARGET_TRIPLE:STRING="dxil-ms-dx" -DCLANG_BUILD_EXAMPLES:BOOL=OFF -DLLVM_REQUIRES_RTTI:BOOL=ON -DCLANG_CL:BOOL=OFF -DCMAKE_SYSTEM_VERSION="10.0.14393.0" -DDXC_BUILD_ARCH=x64'

Copy-Item -Path "$STRATUM_DIR/extern/build/DirectXShaderCompiler/Release/*" -Destination "$STRATUM_DIR/extern/" -Recurse -Force