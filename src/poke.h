/* The Pokémon games' second screen (游典's fork only): pages drawn from the
 * game's memory in GBA pixels — the party, the battle, the bag — a picture of
 * GBA DS's own, an exception to "only the game's own graphics" that 游典 chose
 * (docs/games/pokemon-gen3.md). FireRed, LeafGreen, Emerald, Ruby and Sapphire,
 * and hacks of them that keep their memory where it was: the engine is told by
 * which layout the ROM's code refers to, not by the game code (Chinese
 * translations carry codes that don't match their engine). */
#ifndef GBADS_POKE_H
#define GBADS_POKE_H

#include "emu.h"

#include <stdbool.h>
#include <stdint.h>

#define POKE_SPECIES 412   /* the vanilla games' internal species numbers, 0 (none) to 411 */
#define POKE_NATIONAL 1025 /* national numbers named */
#define POKE_MOVES 355     /* moves named, 0 (none) to 354 */
#define POKE_ITEMS 377     /* items named by their Gen 3 index */
#define POKE_V_NEVER 0xF00000B0 /* (no captures: the pages are drawn live) */

/* A character of the screen's font: 12 rows, a bit a pixel (bit 0 the left). */
typedef struct {
	uint16_t cp;
	uint8_t adv;
	uint16_t rows[12];
} PokeGlyph;

/* poke_data.c (tools/poke/gen_poke_data.py) */
extern const char* const pokeNatNames[POKE_NATIONAL + 1];
extern const uint16_t pokeNational[POKE_SPECIES];
extern const char* const pokeMoveNames[POKE_MOVES];
extern const char* const pokeItemNames[POKE_ITEMS];
extern const char* const pokeTypeNames[18];
extern const int pokeGlyphCount;
extern const PokeGlyph pokeGlyphs[];

/* Whether the game (its 4-letter code) is one of the Gen 3 games (or a hack under its code):
 * the pages may be drawn, and touches are taken. */
bool poke_supported(const char* gameCode);
/* A touch on the second screen this frame, in the 480 × 320 page's pixels; down: pressed. */
void poke_touch(int x, int y, bool down);
/* Keys the pages press for the player this frame (a move tapped in a battle), ORed into theirs. */
uint32_t poke_keys(void);
/* The page at twice the GBA's size, 480 × 320 (ABGR8888), as poke_live_view last drew it this frame; NULL
 * when there's none (the logo). poke_live_view's own picture is it at 240 × 160. Taps (poke_touch) are in
 * its pixels. */
const uint32_t* poke_page_hi(void);
/* MapProfile.liveView: the page (240x160, ABGR8888) while the player has a
 * party, NULL otherwise (the title, a new game: the logo). */
const uint32_t* poke_live_view(Emu* e, const char* rom);
/* MapProfile.virt: POKE_V_NEVER is never 1 */
uint32_t poke_virt(Emu* e, uint32_t addr);

#endif
