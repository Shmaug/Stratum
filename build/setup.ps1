param ([String] $buildType='Release',  $generator='Ninja')


Set-Variable -Name STRATUM_DIR      -Value (Resolve-Path (Join-Path $PSScriptRoot /..)).Path
Set-Variable -Name ASSIMP_DIR       -Value "$STRATUM_DIR/ThirdParty/repos/assimp"
Set-Variable -Name SHADERC_DIR      -Value "$STRATUM_DIR/ThirdParty/repos/shaderc"
Set-Variable -Name MSDFGEN_DIR      -Value "$STRATUM_DIR/ThirdParty/repos/msdfgen"
Set-Variable -Name MSDFGEN_DIR      -Value "$STRATUM_DIR/ThirdParty/repos/msdfgen"
Set-Variable -Name SPIRV_CROSS_DIR  -Value "$STRATUM_DIR/ThirdParty/repos/SPIRV-Cross"
Set-Variable -Name OPENXR_DIR       -Value "$STRATUM_DIR/ThirdParty/repos/OpenXR-SDK"

git submodule update --init

Set-Location "$SHADERC_DIR"
python utils/git-sync-deps

Set-Location "$STRATUM_DIR"

Write-Host "Configuring dependencies [$buildType] [$generator]"
Write-Host ""

cmake -S "$ASSIMP_DIR"      -B "$ASSIMP_DIR/build"      -G $generator -DCMAKE_BUILD_TYPE="$buildType" -DCMAKE_INSTALL_PREFIX="$STRATUM_DIR/ThirdParty" -Wno-dev -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ZLIB=ON -DINJECT_DEBUG_POSTFIX=OFF -DLIBRARY_SUFFIX=""
cmake -S "$MSDFGEN_DIR"     -B "$MSDFGEN_DIR/build"     -G $generator -DCMAKE_BUILD_TYPE="$buildType" -DCMAKE_INSTALL_PREFIX="$STRATUM_DIR/ThirdParty" -Wno-dev -DMSDFGEN_BUILD_MSDFGEN_STANDALONE=OFF -DMSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" -DFREETYPE_INCLUDE_DIRS="$MSDFGEN_DIR/freetype/include" -DFREETYPE_LIBRARY="$MSDFGEN_DIR/freetype/win64/freetype.lib"
cmake -S "$OPENXR_DIR"      -B "$OPENXR_DIR/build"      -G $generator -DCMAKE_BUILD_TYPE="$buildType" -DCMAKE_INSTALL_PREFIX="$STRATUM_DIR/ThirdParty" -Wno-dev -DDYNAMIC_LOADER=ON
cmake -S "$SHADERC_DIR"     -B "$SHADERC_DIR/build"     -G $generator -DCMAKE_BUILD_TYPE="$buildType" -DCMAKE_INSTALL_PREFIX="$STRATUM_DIR/ThirdParty" -Wno-dev -DSHADERC_ENABLE_SHARED_CRT=ON -DBUILD_SHARED_LIBS=OFF -DSHADERC_SKIP_TESTS=ON
cmake -S "$SPIRV_CROSS_DIR" -B "$SPIRV_CROSS_DIR/build" -G $generator -DCMAKE_BUILD_TYPE="$buildType" -DCMAKE_INSTALL_PREFIX="$STRATUM_DIR/ThirdParty" -Wno-dev -DSPIRV_CROSS_ENABLE_TESTS=OFF

Write-Host "Building dependencies"
Write-Host ""

cmake --build . --config $buildType --target add-copyright

cmake --build "$ASSIMP_DIR/build"      --config $buildType --target install
cmake --build "$MSDFGEN_DIR/build"     --config $buildType --target install
cmake --build "$OPENXR_DIR/build"      --config $buildType --target install
cmake --build "$SHADERC_DIR/build"     --config $buildType --target install
cmake --build "$SPIRV_CROSS_DIR/build" --config $buildType --target install