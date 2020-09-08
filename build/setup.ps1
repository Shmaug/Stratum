$STRATUM_DIR = (Resolve-Path (Join-Path $PSScriptRoot /..)).Path

function BuildTarget ($FolderName, $ConfigureArguments) {
  $command = "cmake -S $STRATUM_DIR/extern/src/$FolderName -B $STRATUM_DIR/extern/src/$FolderName/build"
  $command = $command + " -G Ninja"
  $command = $command + " -DCMAKE_CONFIGURATION_TYPES='Debug;Release' -DCMAKE_INSTALL_PREFIX="+$STRATUM_DIR+"/extern -Wno-dev " + $ConfigureArguments
  Invoke-Expression $command
  cmake --build "${STRATUM_DIR}/extern/src/$FolderName/build" --config Release --target install
  cmake --build "${STRATUM_DIR}/extern/src/$FolderName/build" --config Debug --target install
}

Remove-Item -LiteralPath $STRATUM_DIR/extern/ -Force -Recurse
mkdir -Force extern/include

python ./extern/src/shaderc/utils/git-sync-deps

BuildTarget assimp      '-DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ZLIB=ON'
BuildTarget freetype2   '-DMSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL'
BuildTarget msdfgen     '-DMSDFGEN_BUILD_MSDFGEN_STANDALONE=OFF -DMSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL -DFREETYPE_INCLUDE_DIRS="${STRATUM_DIR}/extern/include" -DFREETYPE_LIBRARY="${STRATUM_DIR}/extern/lib/freetype.lib"'
BuildTarget OpenXR-SDK  '-DDYNAMIC_LOADER=ON'
BuildTarget shaderc     '-DSHADERC_ENABLE_SHARED_CRT=ON -DBUILD_SHARED_LIBS=OFF -DSHADERC_SKIP_TESTS=ON -DBUILD_TESTING=OFF -DSPIRV_SKIP_EXECUTABLES=ON'
BuildTarget SPIRV-Cross '-DSPIRV_CROSS_ENABLE_TESTS=OFF'