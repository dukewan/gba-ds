/* (游典's fork) Taps on the second screen that do something in a game, game by game — beyond turning
 * its pages (any paged game) and the Pokémon pages (poke.c). docs/games/<game>.md say what each does. */
#ifndef GBADS_TOUCH_H
#define GBADS_TOUCH_H

#include "emu.h"

#include <stdbool.h>
#include <stdint.h>

/* Whether the game running has taps of its own (its touches are then taken). */
bool touch_wanted(Emu* e);
/* A tap at (x, y) on the second screen (its pixels) while page `page` of its pictures shows: true if
 * the game took it (else the page turns, if there are pages). */
bool touch_tap(Emu* e, int page, int x, int y);
/* Keys pressed for the player this frame (a tap's presses on a game's own menus: touch_more.c). */
uint32_t touch_keys(void);

#endif
