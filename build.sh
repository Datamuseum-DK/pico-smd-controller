#!/usr/bin/env bash
set -e
cd $(dirname $0)

if [ ! -d build ] ; then
	# XXX this used to work automatically, however:
	#  - Something in the cmake build chain started to change the
	#    modification time of our CMakeLists.txt when building the project.
	#    WHY?! Imagine if `make` changed the modification time of Makefile
	#    or C files - see the problem?! I used the modification
	#    time of ./CMakeLists.txt vs build/Makefile to determine if a
	#    ''reconf'' was necessary, but that's no longer possible.
	#  - It also used to:
	#       git submodule update --init pico-sdk
	#    and in pico-sdk/
	#       git submodule update --init lib/tinyusb
	#    but this works really bad when attempting to upgrade the SDK (it
	#    reverts to the committed version).
	echo "build/ directory does not exist; create it and initialize cmake:"
	echo "  $ mkdir build"
	echo "  $ cd build"
	echo "  $ cmake .."
	echo "  $ cd .."
	echo "To do a ''clean build'', delete build/ and do the above again."
	echo "You probably need git submodule sync/update if this is your first build."
	exit 1
fi

pushd build > /dev/null
make -j$(nproc)
popd

echo
echo "BUILD OK, stats:"
bin=build/smd_pico_controller.bin
echo "  $bin: $( cat $bin | wc -c | xargs echo ) bytes"
echo "  BSS size (static RAM): $((16#$(readelf --sections build/smd_pico_controller.elf | grep -w bss | awk '{print $6}'))) bytes"
echo
