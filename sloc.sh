#!/usr/bin/env bash
cd $(dirname $0)
echo -n "controller: "
cat $(git ls-files '*.c' '*.h' '*.pio' | grep -vF / ) | wc -l
echo "-----"
F0=frontend_graphical
wc -l $(git ls-files '*.c' '*.h' '*.cpp' '*.pio' | grep -vF $F0/imgui | grep -vF $F0/imstb_ | grep -vF $F0/imconfig.h | grep -vF $F0/stb_ds.h)
