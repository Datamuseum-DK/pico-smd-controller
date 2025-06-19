#!/usr/bin/env bash

if [ -z "$1" ] ; then
	echo "Usage: $0 </path/to/blockdevice>" > /dev/stderr
	echo "(sudo/doas's a lot; you may want to have a look inside before running)"
	exit 1
fi
cd $(dirname $0)
set -ev
./build.sh
mnt="_picomnt"
if mount | grep $mnt > /dev/null ; then
	doas umount $mnt
fi
if [ -e "$mnt" ] ; then
	doas rm -rf $mnt
fi
mkdir $mnt
doas mount $1 $mnt
doas cp build/smd_pico_controller.uf2 $mnt/
sync
doas umount $mnt
doas rm -rf $mnt
