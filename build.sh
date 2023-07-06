#!/usr/bin/env bash
set -e
cd $(dirname $0)
if [ ! -d build ] ; then
	./_rebuild.sh
else
	cd build
	make
fi
