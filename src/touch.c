/* See touch.h. */
#include "touch.h"

#include "profiles.h"
#include "touch_more.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------- The Minish Cap
 *
 * The Items page (the pause screen's, taken on the hidden copy): a tap on an item puts it on B, as B on it
 * in the game's own menu does (ForceEquipItem, zeldaret/tmc src/itemUtils.c): gSave.stats.equipped
 * (0x02002AF4 A, 0x02002AF5 B); if A held the same item, A gets what B had. The places are the menu's own
 * (gItemMenuTable, src/menu/pauseMenu.c): four rows of four, the sprites' x / y, drawn about 5 pixels above
 * their y. Only while Link is playing (gMain task 2, state 2, at 2): the HUD shows the change at once. */
#define MC_EQUIPPED 0x02002AF4
static const uint8_t mcSlotX[16] = { 0x42, 0x66, 0x8A, 0xAE, 0x42, 0x66, 0x8A, 0xAE, 0x42, 0x66, 0x8A, 0xAE, 0x42, 0x5A, 0x72, 0x8A };
static const uint8_t mcSlotY[16] = { 0x33, 0x33, 0x33, 0x33, 0x4B, 0x4B, 0x4B, 0x4B, 0x63, 0x63, 0x63, 0x63, 0x7B, 0x7B, 0x7B, 0x7B };

static bool mc(const Emu* e) {
	return !strncmp(e->gameCode, "BZM", 3);
}

static bool mc_tap(Emu* e, int page, int x, int y) {
	if (profile_mc_page_kind(page) != 1) return false;
	uint8_t items[16];
	if (!profile_mc_items(items)) return false;
	int best = -1;
	for (int i = 0; i < 16; ++i) {
		int half = i >= 12 ? 11 : 17;
		if (x >= mcSlotX[i] - half && x <= mcSlotX[i] + half && y >= mcSlotY[i] - 5 - 11 && y <= mcSlotY[i] - 5 + 11) best = i;
	}
	if (best < 0) return false;
	/* (a tap between the places, or on an empty one: taken, so the page doesn't turn under the finger) */
	if (!items[best]) return true;
	if (emu_read8(e, 0x03001002) != 2 || emu_read8(e, 0x03001003) != 2 || emu_read8(e, 0x03001004) != 2) return true;
	uint8_t a = emu_read8(e, MC_EQUIPPED), b = emu_read8(e, MC_EQUIPPED + 1), item = items[best];
	if (b == item) return true;
	if (a == item) emu_write8(e, MC_EQUIPPED, b);
	emu_write8(e, MC_EQUIPPED + 1, item);
	fprintf(stderr, "touch: The Minish Cap, item %d (place %d) on B\n", item, best);
	return true;
}

/* ---------------------------------------------------------- */

/* Golden Sun 1 and 2: touch_gs.c */
bool touch_gs_game(const Emu* e);
bool touch_gs_tap(Emu* e, int page, int x, int y, int* show);

static int pageAsked = -1;

bool touch_wanted(Emu* e) {
	return mc(e) || touch_gs_game(e) || touch_more_wanted(e) || touch_fe_game(e);
}

bool touch_tap(Emu* e, int page, int x, int y) {
	pageAsked = -1;
	if (mc(e)) return mc_tap(e, page, x, y);
	if (touch_gs_game(e)) return touch_gs_tap(e, page, x, y, &pageAsked);
	if (touch_more_wanted(e)) return touch_more_tap(e, page, x, y);
	if (touch_fe_game(e)) return touch_fe_tap(e, page, x, y);
	return false;
}

int touch_page(void) {
	return pageAsked;
}

uint32_t touch_keys(void) {
	return touch_more_keys() | touch_fe_keys();
}
