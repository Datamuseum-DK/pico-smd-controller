#!/usr/bin/env bash
if [ -z "$1" ] ; then
	echo "Usage: $0 </path/to/blockdevice>" > /dev/stderr
	exit 1
fi
cd $(dirname $0)
set -e
./build.sh
mnt="_picomnt"
if mount | grep $mnt > /dev/null ; then
	sudo umount $mnt
fi
if [ -e "$mnt" ] ; then
	sudo rm -rf $mnt
fi
mkdir $mnt
sudo mount $1 $mnt
sudo cp build/smd_pico_controller.uf2 $mnt/
sync
sudo umount $mnt
sudo rm -rf $mnt
