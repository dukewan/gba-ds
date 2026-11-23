/* (游典's fork) Fire Emblem's taps on the second screen (TOUCH-4, docs/touch.md): on the unit list's
 * page (FE-11: the map menu's 部队, taken on the hidden copy while the cursor is free on no unit), a
 * tap on a unit's row brings the game's own map cursor onto that unit — by the D-pad, a press at a
 * time, as the player would (the game's HandleMapCursorInput; the camera follows the cursor as it
 * does for the player) — on the player's phase with the cursor free and nothing else under way.
 * Only the cursor moves: nothing is selected. Anything else: nothing (the page turns, TOUCH-1). */
#include "touch.h"

#include "profiles.h"

#include <stdio.h>
#include <string.h>

/* the list's rows on its picture (the game's own: from y 56, 16 pixels each; a 240 x 160 picture,
 * placed 1:1 on the second screen) */
#define FE_ROW_Y0 56
#define FE_ROW_H 16

static struct {
	Emu* e;
	uint32_t unit; /* the unit's data */
	int frames;
	uint32_t keys; /* the press under way */
	int hold;      /* its frames still to come (the last one let go) */
	int ex, ey;    /* where the last press put the cursor (-1: none yet) */
} drive;

bool touch_fe_game(const Emu* e) {
	return !strncmp(e->gameCode, "AE7", 3) || !strncmp(e->gameCode, "BE8", 3) || !strncmp(e->gameCode, "AFE", 3);
}

/* The unit's place, if it's one the cursor can be brought to (on the map, not dead, hidden or
 * carried: Unit.state bits 1, 4, 8, 0x20 — a 16-bit word in Fuuin no Tsurugi, its x, y 2 bytes
 * earlier). */
static bool unit_place(Emu* e, uint32_t unit, int unitXY, int* x, int* y) {
	if (!unit || (unit >> 24) != 0x02 || emu_read8(e, unit + 3) != 8) return false;
	uint32_t state = unitXY == 0x10 ? emu_read32(e, unit + 0x0C) : emu_read16(e, unit + 0x0C);
	if (state & (0x1 | 0x4 | 0x8 | 0x20)) return false;
	*x = (int8_t) emu_read8(e, unit + unitXY), *y = (int8_t) emu_read8(e, unit + unitXY + 1);
	return *x >= 0 && *y >= 0;
}

bool touch_fe_tap(Emu* e, int page, int x, int y) {
	uint32_t units[FE_LIST_ROWS];
	if (profile_fe_page(page, units) != FE_PAGE_LIST) return false;
	if (y < FE_ROW_Y0 || y >= FE_ROW_Y0 + FE_LIST_ROWS * FE_ROW_H || x < 4 || x > 235) return false;
	int row = (y - FE_ROW_Y0) / FE_ROW_H;
	uint32_t cursor;
	int unitXY, ux, uy;
	/* (an empty row: taken, so the page doesn't turn under the finger) */
	if (!units[row]) return true;
	if (!profile_fe_idle(e, &cursor, &unitXY) || !unit_place(e, units[row], unitXY, &ux, &uy)) return false;
	drive.e = e, drive.unit = units[row], drive.frames = 0, drive.hold = 0, drive.ex = drive.ey = -1;
	fprintf(stderr, "touch: Fire Emblem, the cursor from %d,%d to the unit at %d,%d (row %d)\n", emu_read16(e, cursor), emu_read16(e, cursor + 2), ux, uy, row);
	return true;
}

uint32_t touch_fe_keys(void) {
	Emu* e = drive.e;
	if (!e) return 0;
	uint32_t cursor;
	int unitXY, tx, ty;
	/* (half a minute at most; anything else under way, or the unit gone: it stops) */
	if (++drive.frames > 1800 || !profile_fe_idle(e, &cursor, &unitXY) || !unit_place(e, drive.unit, unitXY, &tx, &ty)) {
		drive.e = NULL;
		return 0;
	}
	int cx = (int16_t) emu_read16(e, cursor), cy = (int16_t) emu_read16(e, cursor + 2);
	if (cx == tx && cy == ty) {
		drive.e = NULL;
		return 0;
	}
	/* a press (two frames) only once the cursor is drawn where it is, then let go: each a new press,
	 * a step */
	if (drive.hold) {
		--drive.hold;
		return drive.hold ? drive.keys : 0;
	}
	int gx = (int16_t) emu_read16(e, cursor + 0x8), gy = (int16_t) emu_read16(e, cursor + 0xA);
	int dx = (int16_t) emu_read16(e, cursor + 0xC), dy = (int16_t) emu_read16(e, cursor + 0xE);
	if (dx != cx * 16 || dy != cy * 16 || gx != dx || gy != dy) return 0;
	/* (the cursor somewhere else than the last press put it: the player's own keys — it gives way) */
	if (drive.ex >= 0 && (cx != drive.ex || cy != drive.ey)) {
		drive.e = NULL;
		return 0;
	}
	uint32_t keys = (tx > cx ? K_RIGHT : tx < cx ? K_LEFT : 0) | (ty > cy ? K_DOWN : ty < cy ? K_UP : 0);
	drive.keys = keys, drive.hold = 2;
	drive.ex = cx + (tx > cx) - (tx < cx), drive.ey = cy + (ty > cy) - (ty < cy);
	return keys;
}
