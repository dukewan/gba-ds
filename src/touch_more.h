/* (游典's fork) Second-screen taps for more paged games (touch.c dispatches to them): docs/touch.md. */
#ifndef GBADS_TOUCH_MORE_H
#define GBADS_TOUCH_MORE_H

#include "emu.h"

#include <stdbool.h>
#include <stdint.h>

bool touch_more_wanted(Emu* e);
bool touch_more_tap(Emu* e, int page, int x, int y);
/* keys pressed for the player this frame (a tap's few presses on the game's own menus) */
uint32_t touch_more_keys(void);

#endif
