param ([String] $buildType='RelWithDebInfo')

Write-Host "Setting up Stratum with the configuration:" $buildType
Write-Host ""

Set-Variable -Name STRATUM_DIR      -Value (Resolve-Path (Join-Path $PSScriptRoot /..)).Path
Set-Variable -Name ASSIMP_DIR       -Value "$STRATUM_DIR/ThirdParty/assimp"
Set-Variable -Name SHADERC_DIR      -Value "$STRATUM_DIR/ThirdParty/shaderc"
Set-Variable -Name MSDFGEN_DIR      -Value "$STRATUM_DIR/ThirdParty/msdfgen"
Set-Variable -Name FREETYPE_DIR     -Value "$STRATUM_DIR/ThirdParty/freetype"
Set-Variable -Name MSDFGEN_DIR      -Value "$STRATUM_DIR/ThirdParty/msdfgen"
Set-Variable -Name SPIRV_CROSS_DIR  -Value "$STRATUM_DIR/ThirdParty/SPIRV-Cross"
Set-Variable -Name OPENXR_DIR       -Value "$STRATUM_DIR/ThirdParty/OpenXR-SDK"

git submodule update --init

Write-Host "Building Assimp..."

mkdir -p "$ASSIMP_DIR/build" -Force
Set-Location "$ASSIMP_DIR/build"
cmake -S "$ASSIMP_DIR" -B . -Wno-dev -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ZLIB=ON -DINJECT_DEBUG_POSTFIX=OFF -DLIBRARY_SUFFIX="" -DCMAKE_INSTALL_PREFIX="$ASSIMP_DIR"
cmake --build . --config $buildType --target install

Write-Host "Building FreeType..."

Set-Location "$FREETYPE_DIR"
cmake -B build -D CMAKE_BUILD_TYPE=$buildType -DCMAKE_INSTALL_PREFIX=.
cmake --build build --config $buildType --target install

Write-Host "Building msdfgen"

mkdir -p "$MSDFGEN_DIR/build" -Force
Set-Location "$MSDFGEN_DIR/build"
if ($buildType -eq "Debug") {
  cmake -S "$MSDFGEN_DIR" -B . -Wno-dev -DFREETYPE_INCLUDE_DIRS="$FREETYPE_DIR/include" -DFREETYPE_LIBRARY="$FREETYPE_DIR/build/$buildType/freetyped.lib" -DCMAKE_INSTALL_PREFIX="$MSDFGEN_DIR"
} else {
  cmake -S "$MSDFGEN_DIR" -B . -Wno-dev -DFREETYPE_INCLUDE_DIRS="$FREETYPE_DIR/include" -DFREETYPE_LIBRARY="$FREETYPE_DIR/build/$buildType/freetype.lib" -DCMAKE_INSTALL_PREFIX="$MSDFGEN_DIR"
}
cmake --build . --config $buildType --target install

Write-Host "Building OpenXR..."

mkdir -p "$OPENXR_DIR/build" -Force
Set-Location "$OPENXR_DIR/build"
cmake -S "$OPENXR_DIR" -B . -Wno-dev -DCMAKE_INSTALL_PREFIX="$OPENXR_DIR"
cmake --build . --config $buildType --target install

Write-Host "Building shaderc..."

Set-Location "$SHADERC_DIR"
python utils/git-sync-deps
mkdir -p "$SHADERC_DIR/build" -Force
Set-Location "$SHADERC_DIR/build"
cmake -S "$SHADERC_DIR" -B . -Wno-dev -DCMAKE_BUILD_TYPE=Release -DSHADERC_ENABLE_SHARED_CRT=ON -DBUILD_SHARED_LIBS=OFF -DSHADERC_SKIP_TESTS=ON -DSPIRV_SKIP_EXECUTABLES=ON -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$SHADERC_DIR"
cmake --build . --config Release --target add-copyright
cmake --build . --config Release --target install

Write-Host "Building Spirv-Cross..."

mkdir -p "$SPIRV_CROSS_DIR/build" -Force
Set-Location "$SPIRV_CROSS_DIR/build"
cmake -S "$SPIRV_CROSS_DIR" -B . -Wno-dev -DCMAKE_BUILD_TYPE=Release -DSPIRV_CROSS_SHARED=OFF -DSPIRV_CROSS_STATIC=ON -DSPIRV_CROSS_ENABLE_TESTS=OFF -DCMAKE_INSTALL_PREFIX="$SPIRV_CROSS_DIR" 
cmake --build . --config Release --target install

Set-Location "$STRATUM_DIR"

#Write-Host "Downloading DXC..."
#Invoke-WebRequest -OutFile "ThirdParty/dxc.zip" -Uri "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.5.2003/dxc_2020_03-25.zip"
#Expand-Archive "ThirdParty/dxc.zip" -DestinationPath "ThirdParty/dxc"
#Remove-Item "ThirdParty/dxc.zip"