param (
  [String]$BuildType="RelWithDebInfo", 
  [Boolean]$ConfigureDeps=$true, 
  [Boolean]$BuildDeps=$false
)

$STRATUM_DIR     = (Resolve-Path (Join-Path $PSScriptRoot /..)).Path
$ASSIMP_DIR      = "$STRATUM_DIR/ThirdParty/src/assimp"
$SHADERC_DIR     = "$STRATUM_DIR/ThirdParty/src/shaderc"
$MSDFGEN_DIR     = "$STRATUM_DIR/ThirdParty/src/msdfgen"
$MSDFGEN_DIR     = "$STRATUM_DIR/ThirdParty/src/msdfgen"
$SPIRV_CROSS_DIR = "$STRATUM_DIR/ThirdParty/src/SPIRV-Cross"
$OPENXR_DIR      = "$STRATUM_DIR/ThirdParty/src/OpenXR-SDK"

function ConfigureTarget ($dir, $configure_args) {
  $command = "cmake -S $dir -B $dir/build"
  $command = $command + " -DCMAKE_BUILD_TYPE="+$BuildType+" -DCMAKE_INSTALL_PREFIX="+$STRATUM_DIR+"/ThirdParty -Wno-dev " + $configure_args
  Invoke-Expression $command
}

Invoke-Webrequest https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp -Outfile ThirdParty/include/json.hpp
Invoke-Webrequest https://raw.githubusercontent.com/nothings/stb/master/stb_image.h -Outfile ThirdParty/include/stb_image.h
Invoke-Webrequest https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h -Outfile ThirdParty/include/stb_image_write.h
Invoke-Webrequest https://raw.githubusercontent.com/syoyo/tinygltf/master/tiny_gltf.h -Outfile ThirdParty/include/tiny_gltf.h

git submodule update --init
Set-Location $SHADERC_DIR
python utils/git-sync-deps
Set-Location $STRATUM_DIR


if ($ConfigureDeps) {
  Write-Host "Configuring dependencies [$BuildType]"
  Write-Host ""

  ConfigureTarget $ASSIMP_DIR '-DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ZLIB=ON'
  ConfigureTarget $MSDFGEN_DIR '-DMSDFGEN_BUILD_MSDFGEN_STANDALONE=OFF -DMSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL -DFREETYPE_INCLUDE_DIRS="$MSDFGEN_DIR/freetype/include" -DFREETYPE_LIBRARY="$MSDFGEN_DIR/freetype/win64/freetype.lib" '
  ConfigureTarget $OPENXR_DIR '-DDYNAMIC_LOADER=ON'
  ConfigureTarget $SHADERC_DIR '-DSHADERC_ENABLE_SHARED_CRT=ON -DBUILD_SHARED_LIBS=OFF -DSHADERC_SKIP_TESTS=ON -DBUILD_TESTING=OFF -DSPIRV_SKIP_EXECUTABLES=ON'
  ConfigureTarget $SPIRV_CROSS_DIR '-DSPIRV_CROSS_ENABLE_TESTS=OFF'
}

if ($BuildDeps) {
  Write-Host "Building dependencies"
  Write-Host ""

  cmake --build . --config $BuildType --target add-copyright

  cmake --build "$ASSIMP_DIR/build"      --config $BuildType --target install
  cmake --build "$MSDFGEN_DIR/build"     --config $BuildType --target install
  cmake --build "$OPENXR_DIR/build"      --config $BuildType --target install
  cmake --build "$SHADERC_DIR/build"     --config $BuildType --target install
  cmake --build "$SPIRV_CROSS_DIR/build" --config $BuildType --target install
}