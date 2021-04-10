#!/bin/bash

STRATUM_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

echo STRATUM_DIR = $STRATUM_DIR

c_compiler=
cxx_compiler=
rc_compiler=
linker=
cmake_generator=
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

cfg="Release"
if [ $build_debug = 1 ]; then
  cfg="Debug"
fi

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
  cmake --build . --config $2 --target install
  cd $prev
}

build_target eigen "Release"

build_target assimp "Debug" "-DASSIMP_BUILD_ASSIMP_TOOLS=OFF"
build_target assimp "Release" "-DASSIMP_BUILD_ASSIMP_TOOLS=OFF"

build_target OpenXR-SDK $cfg
