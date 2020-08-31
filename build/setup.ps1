param ([String] $buildType='RelWithDebInfo')

Write-Host "Setting up Stratum with the configuration:" $buildType
Write-Host ""

Set-Variable -Name STRATUM_DIR      -Value (Resolve-Path (Join-Path $PSScriptRoot /..)).Path
Set-Variable -Name ASSIMP_DIR       -Value "$STRATUM_DIR/ThirdParty/assimp"
Set-Variable -Name SHADERC_DIR      -Value "$STRATUM_DIR/ThirdParty/shaderc"
Set-Variable -Name MSDFGEN_DIR      -Value "$STRATUM_DIR/ThirdParty/msdfgen"
Set-Variable -Name MSDFGEN_DIR      -Value "$STRATUM_DIR/ThirdParty/msdfgen"
Set-Variable -Name SPIRV_CROSS_DIR  -Value "$STRATUM_DIR/ThirdParty/SPIRV-Cross"
Set-Variable -Name OPENXR_DIR       -Value "$STRATUM_DIR/ThirdParty/OpenXR-SDK"

git submodule update --init

Write-Host "Building Assimp..."

mkdir -p "$ASSIMP_DIR/build" -Force
Set-Location "$ASSIMP_DIR/build"
cmake -S "$ASSIMP_DIR" -B . -Wno-dev -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ZLIB=ON -DINJECT_DEBUG_POSTFIX=OFF -DLIBRARY_SUFFIX="" -DCMAKE_INSTALL_PREFIX="$ASSIMP_DIR"
cmake --build . --config $buildType --target install -- /m

Write-Host "Building msdfgen"

mkdir -p "$MSDFGEN_DIR/build" -Force
Set-Location "$MSDFGEN_DIR/build"
cmake -S "$MSDFGEN_DIR" -B . -Wno-dev -DMSDFGEN_BUILD_MSDFGEN_STANDALONE=OFF -DMSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" -DFREETYPE_INCLUDE_DIRS="$MSDFGEN_DIR/freetype/include" -DFREETYPE_LIBRARY="$MSDFGEN_DIR/freetype/win64/freetype.lib" -DCMAKE_INSTALL_PREFIX="$MSDFGEN_DIR"
cmake --build . --config $buildType --target install -- /m

Write-Host "Building OpenXR..."

mkdir -p "$OPENXR_DIR/build" -Force
Set-Location "$OPENXR_DIR/build"
cmake -S "$OPENXR_DIR" -B . -Wno-dev -DDYNAMIC_LOADER=ON -DCMAKE_INSTALL_PREFIX="$OPENXR_DIR"
cmake --build . --config $buildType --target install -- /m



Write-Host "Building shaderc..."

Set-Location "$SHADERC_DIR"
python utils/git-sync-deps
mkdir -p "$SHADERC_DIR/build" -Force
Set-Location "$SHADERC_DIR/build"
cmake -S "$SHADERC_DIR" -B . -Wno-dev -DCMAKE_BUILD_TYPE="$buildType" -DSHADERC_ENABLE_SHARED_CRT=ON -DBUILD_SHARED_LIBS=OFF -DSHADERC_SKIP_TESTS=ON -DCMAKE_INSTALL_PREFIX="$SHADERC_DIR"
cmake --build . --config $buildType --target add-copyright
cmake --build . --config $buildType --target install -- /m

Write-Host "Building Spirv-Cross..."

mkdir -p "$SPIRV_CROSS_DIR/build" -Force
Set-Location "$SPIRV_CROSS_DIR/build"
cmake -S "$SPIRV_CROSS_DIR" -B . -Wno-dev -DCMAKE_BUILD_TYPE="$buildType" -DSPIRV_CROSS_ENABLE_TESTS=OFF -DCMAKE_INSTALL_PREFIX="$SPIRV_CROSS_DIR" 
cmake --build . --config $buildType --target install -- /m

Set-Location "$STRATUM_DIR"