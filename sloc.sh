#!/usr/bin/env bash
F0=frontend_graphical
wc -l $(git ls-files '*.c' '*.h' '*.cpp' '*.pio' | grep -vF $F0/imgui | grep -vF $F0/imstb_ | grep -vF $F0/imconfig.h | grep -vF stb_ds.h)
