#!/usr/bin/env bash
set -e
cd $(dirname $0)
if [ ! -d build -o ! -e build/Makefile -o CMakeLists.txt -nt build/Makefile ] ; then
	./reconf.sh
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
