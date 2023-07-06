#!/usr/bin/env bash
if [ -z "$PICO_SDK_PATH" ] ; then
	echo "The PICO_SDK_PATH environment variable must be set (should point to root of https://github.com/raspberrypi/pico-sdk clone)" > /dev/stderr
	exit 1
fi
set -e
cd $(dirname $0)
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .
rm -rf build
mkdir build
cd build
cmake -DPICO_PLATFORM=rp2040 -DPICO_SDK_PATH=${PICO_SDK_PATH} ..
make
