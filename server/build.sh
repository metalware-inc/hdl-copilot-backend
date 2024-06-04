#!/bin/bash
BUILD_TYPE=$1
if [ -z "$BUILD_TYPE" ]; then
    BUILD_TYPE="Release"
fi

mkdir -p build
set -ex

pushd build
cmake --log-level=DEBUG -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DCMAKE_EXPORT_COMPILE_COMMANDS=1 ..

make -j4
popd
