# Taps on the second screen (游典's fork)

游典's handhelds have a touch screen under the second screen (the AYN Thor's, the Pocket DS's): GBA DS
there takes taps — an exception to "no in-game touch" made for 游典 (the owner, 11-23-2026), not GBA DS's.
Touches are read only for the games below; any other game's taps stay the frontend's.

## Rules

- **TOUCH-1 Pages.** A game whose second screen shows pages in turn (Fire Emblem, Golden Sun 1 and 2,
  The Minish Cap, Final Fantasy Tactics Advance, Mario & Luigi, MOTHER 3, Summon Night 1 and 2, Tactics
  Ogre, Banjo-Kazooie, Zoids, Rebelstar): a tap shows the next page, held there half a minute; then they
  turn on from it.
- **TOUCH-2 The Minish Cap.** With 只显示地图 (the default) the map stays, and the other pages are still
  taken: a tap turns to them. On the Items page a tap on an item puts it on B, as B on it in the pause
  screen does (on A before: A gets what B had); the page is taken again so its A / B marks follow. Only
  while Link is playing; a tap on an empty place does nothing; elsewhere on the page, the next page.
- **TOUCH-3 Pokémon** (FireRed, LeafGreen, Emerald, Ruby, Sapphire): docs/games/pokemon-gen3.md.
- **TOUCH-4 Fire Emblem** (烈火之剑, 圣魔之光石, 封印之剑: the Japanese releases, which the Chinese
  translations are): with the cursor free on no unit, the second screen is the game's unit list (the
  map menu's 部队), six units a page (FE-11). A tap on a unit's row brings the map's cursor onto that
  unit — step by step, as the D-pad would, the map scrolling with it — and the unit's status follows
  (FE-3). Only on your phase with the cursor free (no menu, unit selected, conversation or fight): a
  tap then does nothing but turn the page. Nothing is selected or moved: only the cursor. A press of
  your own stops it where it is (it gives way to the game). On an empty row, nothing; elsewhere on the
  page, and on a unit's status, the next page.

## How

libretro/gbads_libretro.c polls the frontend's pointer for these games (the frame's lower half);
libretro/lr_game.inc lr_second_tap (the page held: game.c map_anim_index), src/touch.c the games' own
taps (The Minish Cap: zeldaret/tmc's gItemMenuTable places, gSave.stats.equipped 0x02002AF4, the
Items page's items read from gPauseMenu.items 0x02000090 as mc_compose takes the page; Fire Emblem:
src/touch_fe.c — the list's rows' units recorded by fe_compose from the game's own sorted list,
gSortedUnits, the cursor driven by D-pad presses through touch_keys, one a step once the cursor is
drawn where it is, while the player phase waits for input). Checked in the
harness (`tap2 PAGE X,Y`) and on the AYN Thor in 游典 with a player's own save.
