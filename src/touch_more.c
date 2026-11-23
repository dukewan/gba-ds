/* See touch_more.h. */
#include "touch_more.h"

#include "profiles.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------ Final Fantasy Tactics Advance
 *
 * The turn order page (FT-9): a tap on a unit's row puts the battle map's cursor on that unit, the way the
 * game's own list does — L opens it (on its first row, the active unit), DOWN to the row, B closes it with
 * the cursor on that row's unit and the camera on it (the second screen then shows its Status, FT-8). Only
 * on the player's turn with nothing open on the map, in the same turn the page's list was taken in (the
 * rows are then the game's own); each step waits for the list (FT_UI 3) and stops if it isn't what's
 * expected. L, DOWN and B only: nothing is chosen. */
static struct {
	Emu* e;
	int row;    /* the list row to go to */
	int stage;  /* 0 idle, 1 L, 2 the list opening, 3 DOWN, 4 B */
	int frames; /* in this stage */
	int downs;  /* DOWNs pressed */
	int held;   /* frames the list has been up */
} ft;

static bool ffta(const Emu* e) {
	return !strncmp(e->gameCode, "AFX", 3);
}

static bool ft_tap(Emu* e, int page, int x, int y) {
	(void) page;
	int row = profile_ft_turn_entry(x, y);
	if (row < 0) return false; /* (a Status's pages, or beside the rows: the page turns) */
	if (ft.stage || !profile_ft_turn_ready(e)) return true;
	ft.e = e, ft.row = row, ft.stage = 1, ft.frames = 0, ft.downs = 0, ft.held = 0;
	fprintf(stderr, "touch: Final Fantasy Tactics Advance, turn order row %d\n", row + 1);
	return true;
}

static uint32_t ft_keys(void) {
	if (!ft.stage) return 0;
	Emu* e = ft.e;
	int ui = profile_ft_ui(e), f = ft.frames++;
	bool list = ui == 3 || ui == 4; /* (the list up: 4 while the camera goes to the row's unit) */
	switch (ft.stage) {
	case 1: /* L, three frames, from the map with nothing open */
		if (f == 0 && ui != 1) break;
		if (f < 3) return K_L;
		ft.stage = 2, ft.frames = 0;
		return 0;
	case 2: /* the list up, and held up for 24 frames (it opens over about 20: before, keys are lost) */
		ft.held = list ? ft.held + 1 : 0;
		if (ft.held >= 24) {
			ft.stage = ft.row ? 3 : 4, ft.frames = 0;
			return 0;
		}
		if (f > 60) break;
		return 0;
	case 3: /* DOWN, three frames down and five up, to the row */
		if (!list) break;
		if (f < 3) return K_DOWN;
		if (f < 8) return 0;
		ft.frames = 0;
		if (++ft.downs >= ft.row) ft.stage = 4;
		return 0;
	case 4: /* B: the list closes on the row's unit */
		if (f == 0 && !list) break;
		if (f < 3) return K_B;
		ft.stage = 0;
		return 0;
	}
	/* (not as expected: stop; the list left open is closed) */
	ft.stage = list ? 4 : 0, ft.frames = 0;
	if (ft.stage) ft.row = 0;
	return 0;
}

/* ---------------------------------------------------------- */

bool touch_more_wanted(Emu* e) {
	return ffta(e);
}

bool touch_more_tap(Emu* e, int page, int x, int y) {
	if (ffta(e)) return ft_tap(e, page, x, y);
	return false;
}

uint32_t touch_more_keys(void) {
	return ft_keys();
}
