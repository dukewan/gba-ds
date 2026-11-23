/* The Pokémon games' second screen (游典's fork only): the party, drawn from the
 * game's memory in GBA pixels — a picture of GBA DS's own, an exception to
 * "only the game's own graphics" that 游典 chose (docs/games/pokemon-gen3.md).
 * FireRed, LeafGreen, Emerald, Ruby and Sapphire (USA, and Ruby / Sapphire's
 * Japanese releases, which Chinese translations are made from). */
#ifndef GBADS_POKE_H
#define GBADS_POKE_H

#include "emu.h"

#include <stdbool.h>
#include <stdint.h>

#define POKE_SPECIES 412 /* the games' internal species numbers, 0 (none) to 411 */
#define POKE_V_NEVER 0xF00000B0 /* (no captures: the party is drawn live) */

/* A character of the screen's font: 12 rows, a bit a pixel (bit 0 the left). */
typedef struct {
	uint16_t cp;
	uint8_t adv;
	uint16_t rows[12];
} PokeGlyph;

/* poke_data.c (tools/poke/gen_poke_data.py) */
extern const char* const pokeNames[POKE_SPECIES];
extern const int pokeGlyphCount;
extern const PokeGlyph pokeGlyphs[];

/* Whether the game (its 4-letter code) is one this screen reads. */
bool poke_supported(const char* gameCode);
/* MapProfile.liveView: the party page (240x160, ABGR8888) while the player
 * has a party, NULL otherwise (the title, a new game: the logo). */
const uint32_t* poke_live_view(Emu* e, const char* rom);
/* MapProfile.virt: POKE_V_NEVER is never 1 */
uint32_t poke_virt(Emu* e, uint32_t addr);

#endif
