#!/usr/bin/env bash
wc -l $(git ls-files '*.c' '*.h' '*.cpp' '*.pio' | grep -vF frontend/imgui | grep -vF frontend/imstb_ | grep -vF frontend/imconfig.h | grep -vF stb_ds.h)
