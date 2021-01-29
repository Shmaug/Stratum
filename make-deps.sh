#!/bin/bash

STRATUM_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

c_compiler=
cxx_compiler=
rc_compiler=
linker=
cmake_generator="Ninja"
build_debug=0
cxx_flags=
exe_linker_flags=

while getopts c:C:r:l:g:d: opt; do
    case "$opt" in
        c) c_compiler=${OPTARG};;
        C) cxx_compiler=${OPTARG};;
        r) rc_compiler=${OPTARG};;
        l) linker=${OPTARG};;
        g) cmake_generator=${OPTARG};;
        d) build_debug=1;;
    esac
done

build_target() {
  # USAGE:
  # build_target <src_folder> <cmake_build_type> <cmake_config_args>
  
  config_cmd="cmake -S '$STRATUM_DIR/extern/src/$1' -B '$STRATUM_DIR/extern/build/$1' -Wno-dev"
  if [ -n "$cmake_generator" ]; then
    config_cmd="$config_cmd -G $cmake_generator"
  fi
  config_cmd="$config_cmd -DCMAKE_INSTALL_PREFIX='$STRATUM_DIR/extern' -DCMAKE_INSTALL_MESSAGE=LAZY"
  
  if [ -n "$c_compiler" ]; then
    config_cmd="$config_cmd -DCMAKE_C_COMPILER='$c_compiler'"
  fi
  if [ -n "$cxx_compiler" ]; then
    config_cmd="$config_cmd -DCMAKE_CXX_COMPILER='$cxx_compiler'"
  fi
  if [ -n "$rc_compiler" ]; then
    config_cmd="$config_cmd -DCMAKE_RC_COMPILER='$rc_compiler'"
  fi
  if [ -n "$linker" ]; then
    config_cmd="$config_cmd -DCMAKE_LINKER='$linker'"
  fi
  if [ -n "$cxx_flags" ]; then
    config_cmd="$config_cmd -DCMAKE_CXX_FLAGS='$cxx_flags'"
  fi
  if [ -n "$exe_linker_flags" ]; then
    config_cmd="$config_cmd -DCMAKE_EXE_LINKER_FLAGS='$exe_linker_flags'"
  fi

  if [[ $cmake_generator != "Visual Studio"* ]]; then
    config_cmd="$config_cmd -DCMAKE_BUILD_TYPE=$2"
  fi
  config_cmd="$config_cmd $3"

  echo $config_cmd
  eval $config_cmd
  
  prev=$PWD
  cd "$STRATUM_DIR/extern/build/$1"
  if [[ $cmake_generator == "Visual Studio"* ]]; then
    cmake --build . --config $2 --target install -- -maxCpuCount
  else
    cmake --build . --target install
  fi
  cd $prev
}

cfg="Release"
if [ $build_debug = 1 ]; then
  $cfg="Debug"
fi

build_target eigen "Release"

build_target shaderc $cfg "-DEFFCEE_BUILD_SAMPLES=OFF -DEFFCEE_BUILD_TESTING=OFF -DSHADERC_SKIP_TESTS=ON"

build_target SPIRV-Cross $cfg "-DSPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS=ON"

build_target DirectXShaderCompiler $cfg "-DHLSL_OPTIONAL_PROJS_IN_DEFAULT:BOOL=OFF -DHLSL_ENABLE_ANALYZE:BOOL=OFF -DHLSL_OFFICIAL_BUILD:BOOL=OFF -DHLSL_ENABLE_FIXED_VER:BOOL=OFF -DHLSL_ENABLE_FIXED_VER:BOOL=OFF -DHLSL_BUILD_DXILCONV:BOOL=ON -DENABLE_SPIRV_CODEGEN:BOOL=TRUE -DSPIRV_BUILD_TESTS:BOOL=OFF -DCLANG_ENABLE_ARCMT:BOOL=OFF -DCLANG_ENABLE_STATIC_ANALYZER:BOOL=OFF -DCLANG_INCLUDE_TESTS:BOOL=OFF -DLLVM_INCLUDE_TESTS:BOOL=OFF -DHLSL_INCLUDE_TESTS:BOOL=ON -DLLVM_TARGETS_TO_BUILD:STRING=None -DLLVM_INCLUDE_DOCS:BOOL=OFF -DLLVM_INCLUDE_EXAMPLES:BOOL=OFF -DLIBCLANG_BUILD_STATIC:BOOL=ON -DLLVM_OPTIMIZED_TABLEGEN:BOOL=OFF -DLLVM_REQUIRES_EH:BOOL=ON -DLLVM_APPEND_VC_REV:BOOL=ON -DLLVM_ENABLE_RTTI:BOOL=ON -DLLVM_ENABLE_EH:BOOL=ON -DLLVM_DEFAULT_TARGET_TRIPLE:STRING='dxil-ms-dx' -DCLANG_BUILD_EXAMPLES:BOOL=OFF -DLLVM_REQUIRES_RTTI:BOOL=ON -DCLANG_CL:BOOL=OFF -DCMAKE_SYSTEM_VERSION='10.0.14393.0' -DDXC_BUILD_ARCH=x64"
cp -rf "$STRATUM_DIR/extern/build/DirectXShaderCompiler/Release/*" "$STRATUM_DIR/extern"

build_target assimp "Debug" "-DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_ZLIB=ON"
build_target assimp "Release" "-DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_ZLIB=ON"

build_target freetype2 "Debug"
build_target freetype2 "Release"

build_target msdfgen $cfg "-DFREETYPE_INCLUDE_DIRS='$STRATUM_DIR/extern/include' -DFREETYPE_LIBRARY='$STRATUM_DIR/extern/lib/freetype.lib'"

build_target OpenXR-SDK $cfg