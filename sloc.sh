#!/usr/bin/env bash
echo -n "controller: "
cat $(git ls-files '*.c' '*.h' '*.pio' | grep -vF frontend_graphical | grep -vF print_pin_config_html.c) | wc -l
echo "-----"
F0=frontend_graphical
wc -l $(git ls-files '*.c' '*.h' '*.cpp' '*.pio' | grep -vF $F0/imgui | grep -vF $F0/imstb_ | grep -vF $F0/imconfig.h | grep -vF stb_ds.h)
