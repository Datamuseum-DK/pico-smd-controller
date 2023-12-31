#!/usr/bin/env bash
set -e
cd $(dirname $0)
git submodule update --init pico-sdk
pushd pico-sdk > /dev/null
git submodule update --init lib/tinyusb
popd
rm -rf build
mkdir build
cd build
cmake ..
