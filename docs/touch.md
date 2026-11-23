# Taps on the second screen (游典's fork)

游典's handhelds have a touch screen under the second screen (the AYN Thor's, the Pocket DS's): GBA DS
there takes taps — an exception to "no in-game touch" made for 游典 (the owner, 11-23-2026), not GBA DS's.
Touches are read only for the games below; any other game's taps stay the frontend's.

## Rules

- **TOUCH-1 Pages.** A game whose second screen shows pages in turn (Fire Emblem, Golden Sun 1 and 2,
  The Minish Cap, Final Fantasy Tactics Advance, Mario & Luigi, MOTHER 3, Summon Night 1 and 2, Tactics
  Ogre, Banjo-Kazooie, Zoids, Rebelstar): a tap shows the next page, held there half a minute; then they
  turn on from it.
- **TOUCH-1b One picture.** Any other game with a second screen of its own (a map, a status screen): a
  tap takes it again at once (a map that has fallen behind the game).
- **TOUCH-2 The Minish Cap.** With 只显示地图 (the default) the map stays, and the other pages are still
  taken: a tap turns to them. On the Items page a tap on an item puts it on B, as B on it in the pause
  screen does (on A before: A gets what B had); the page is taken again so its A / B marks follow. Only
  while Link is playing; a tap on an empty place does nothing; elsewhere on the page, the next page.
- **TOUCH-3 Pokémon** (FireRed, LeafGreen, Emerald, Ruby, Sapphire): docs/games/pokemon-gen3.md.
- **TOUCH-4 Golden Sun 1 and 2.** On the Status pages a tap on a figure in the party row at the top left
  shows that member's page, held half a minute; an empty place in the row does nothing; elsewhere on the
  page, the next page. The Lost Age's row shows four at a time (the first four's pages, then the next
  four's). The game itself isn't touched; a battle's Status (one page) takes no taps. (Not the Djinn: the
  page shows how many of each element, not each Djinni, so a tap couldn't say which one to Set or put on
  Standby.)
- **TOUCH-5 Final Fantasy Tactics Advance.** On the turn order page, a tap on a unit's row puts the
  battle map's cursor on that unit, as the game's own list (L) does: the list shows on the game screen for
  a moment, goes to the row and closes, the camera on the unit, and the second screen shows its Status.
  Only on your turn with nothing open on the map; otherwise a tap on a row does nothing
  (docs/games/final-fantasy-tactics-advance.md FT-15).
- **TOUCH-6 Mario & Luigi: Superstar Saga.** Pages only (TOUCH-1): Bros. Info is something to read, and
  the game has nothing on the field a tap there could point at.
- **TOUCH-7 MOTHER 3.** Pages only: the area map has no cursor or places to pick in the game, and the
  Status pages are to read.
- **TOUCH-8 Summon Night: Swordcraft Story 1 and 2.** Pages only: each page is a character's Status, to
  read; nothing in the game follows a character chosen there.
- **TOUCH-9 Tactics Ogre: The Knight of Lodis.** Pages only: the pages are the details of the unit already
  under the field cursor (or the battle's conditions), so there's nothing else to point the cursor at.
- **TOUCH-10 Banjo-Kazooie: Grunty's Revenge.** Pages only: its pages are totals, to read.
- **TOUCH-11 Zoids: Legacy.** Pages only: the world map has no cursor or places to pick in the game, and a
  Zoid's Status is the one already choosing.
- **TOUCH-12 Rebelstar: Tactical Command.** Pages only: the pages are the soldier already under the field
  cursor (or the Objectives), so there's nothing else to point the cursor at.

## How

libretro/gbads_libretro.c polls the frontend's pointer for these games (the frame's lower half);
libretro/lr_game.inc lr_second_tap (the page held: game.c map_anim_index), src/touch.c the games' own
taps (The Minish Cap: zeldaret/tmc's gItemMenuTable places, gSave.stats.equipped 0x02002AF4, the
Items page's items read from gPauseMenu.items 0x02000090 as mc_compose takes the page; Golden Sun:
src/touch_gs.c, the row's places from the game's sprites, the pages' count from gs_compose / gs2_compose,
a page asked for shown by lr_second_show).
; src/touch_more.c
the others' (Final Fantasy Tactics Advance: the turn order's rows placed as ft_turn_layout draws them,
then L, DOWN to the row and B pressed for the player — touch_keys — each step checked on FT_UI: 1
nothing open, 3 or 4 the list up). Checked in the
harness (`tap2 PAGE X,Y`) and on the AYN Thor in 游典 with a player's own save.
