/* (游典's fork) Golden Sun 1 and 2: a tap on the Status pages' party row (docs/touch.md TOUCH-4).
 *
 * The second screen shows the Status screen walking about, a page per party member in the game's order
 * (gs_compose / gs2_compose, profiles.c). Each page has the game's party row at its top left: a window
 * (x 0-103, y 0-39) with up to four walking figures 24 pixels apart (their sprites at x 0, 24, 48, 72, 32
 * wide; the hand under the one whose page it is). The Lost Age's party of up to eight scrolls the row a
 * whole four at a time: the pages are taken by RIGHT from the first, so page k's row shows members
 * k - k % 4 on (checked with parties of 4, 6 and 8; Golden Sun's has four at most). A tap on a figure
 * shows that member's page — the second screen's own pages, nothing in the game is touched, so it's safe
 * at any moment; an empty place in the row does nothing; elsewhere on the page, the next page as before.
 * Not on a battle's Status (one page, no row) or the logo.
 *
 * Why not the Djinn (a tap to Set / Standby one, as the game's Djinn menu does): the Status page shows a
 * Djinni per element with how many there are, not each Djinni — a tap can't say which one — and the
 * change would have to be made by the game's own menu, keys injected into the live game over seconds
 * while the player may press theirs. */
#include "touch.h"

#include "profiles.h"

#include <stdio.h>
#include <string.h>

#define GS_ROW_W 104
#define GS_ROW_H 40

bool touch_gs_game(const Emu* e) {
	/* (Golden Sun's AGS?, The Lost Age's AGF? — every release has the profile's pages; 游典's Chinese ones
	 * are AGSJ and AGFE) */
	return !strncmp(e->gameCode, "AGS", 3) || !strncmp(e->gameCode, "AGF", 3);
}

bool touch_gs_tap(Emu* e, int page, int x, int y, int* show) {
	(void) e;
	int n = profile_gs_status_pages();
	if (n <= 0 || page < 0 || page >= n) return false;
	if (x < 0 || y < 0 || x >= GS_ROW_W || y >= GS_ROW_H) return false;
	int slot = (x - 4) / 24;
	if (slot < 0) slot = 0;
	if (slot > 3) slot = 3;
	int member = page - page % 4 + slot;
	if (member >= n) return true; /* (an empty place in the row: taken, nothing shown) */
	*show = member;
	fprintf(stderr, "touch: Golden Sun, party row place %d: page %d of %d\n", slot, member + 1, n);
	return true;
}
