#!/bin/sh
set -ev

mkdir build
pushd build

cmake -DCMAKE_BUILD_TYPE=Release ..
make

popd
