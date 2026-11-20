#!/usr/bin/env python3
"""libretro/stubs.c from the linker's undefined symbols (build once with an empty stubs.c, then:
   cmake --build build-libretro 2>&1 | grep 'undefined symbol' | sed 's/.*undefined symbol: //' | sort -u > /tmp/undef.txt
   libretro/make_stubs.py /tmp/undef.txt > libretro/stubs.c).
Functions return 0 / NULL; the variables (ui.h's btnNow / btnPrev, hotkeys.h's hotkeyBtnNames) are zeroed room."""
import sys
DATA = {'btnNow', 'btnPrev', 'hotkeyBtnNames'}
syms = open(sys.argv[1]).read().split()
print("/* GENERATED (libretro/make_stubs.py): what GBA DS's game.c names of SDL, ui.c, hotkeys.c, bezel.c and")
print(" * android.c but never calls in 游典's core (no window, no menus, no sound device of its own). Each")
print(" * function returns 0 / NULL; the variables are zeroed room. libretro/shim.c has the ones that are called. */")
print("#include <stdint.h>\n")
for s in sorted(syms):
    print(f"uint64_t {s}[512];" if s in DATA else f"intptr_t {s}(void) {{ return 0; }}")
