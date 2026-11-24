# Pokémon FireRed, LeafGreen, Emerald, Ruby and Sapphire (游典's fork)

**An exception to "only the game's own graphics", made for 游典 (the owner, 11-22-2026), not GBA DS's.**
Players asked for a Pokémon second screen like PokeDaisy's and GBADuo's (both GPL-3.0; neither's code
or art is used here). The second screen is a page GBA DS draws itself, in GBA pixels, from the game's
memory; the Pokémon pictures on it are the game's own party icons, read from the player's ROM.

Codes: BPRE, BPGE, BPEE, AXVE, AXPE, AXVJ, AXPJ — and hacks under those codes. The engine is told
by which of four memory layouts (FireRed / LeafGreen, Emerald, Ruby / Sapphire USA, Ruby / Sapphire
Japan) the ROM's code refers to, not by the code: Chinese translations often carry a code that
doesn't match their engine (an AXVJ that is the US engine). A ROM that refers to none of them
(a decomp hack with its memory moved): the logo, as before. Other releases (Japanese FireRed /
LeafGreen / Emerald, European ones): no profile.

## Rules

- **PK-1 The party.** Once the player has a Pokémon, the second screen is the party: a card a
  Pokémon, in party order, two to a row; the first one's card edged in orange. Each card: its icon,
  its name (Chinese, by species — nicknames aren't read), its level, an HP bar (green over half,
  yellow over a fifth, red below) with HP / max HP, and its condition when it has one (睡眠, 中毒,
  灼伤, 冰冻, 麻痹; 濒死 at 0 HP, the card and icon greyed). An egg: its icon and 蛋, nothing else.
- **PK-0 Twice the size.** The pages are drawn at 480 × 320 (the frame 游典 gets is 480 × 640 for these
  games, the game at 2×): words in the 12-pixel font at its size or doubled, icons doubled where there's
  room, edges and bars in GBA pixels. Icons bob in the games' own two frames.
- **PK-0b The lower screen's shape.** 游典 says the size of the screen the pages go to (the variable
  gbads_lower_size, "WxH"); the pages are then 480 wide and as tall as that shape makes them (320 to 640: a
  Thor's 1240 × 1080 lower screen, 418), so they fill it unstretched on any handheld (the owner, 11-23). The bars
  stay at the top and bottom; the 背包 and 图鉴 show more rows, the map is taller, the party's cards spread
  out, the battle, a member in full and the boxes are centred. Not told (another frontend, one screen): 320.
- **PK-2 The bars.** Along the bottom six tabs — 队伍, 对战, 地图, 图鉴, 箱子, 背包 — a tap on one shows its
  page (对战 only in a battle); along the top eight badge gems, lit as they're won, the play time, the
  player's money (…元).
- **PK-3 Live.** HP, conditions, levels and order follow the game as they change, in battle too
  (the party's HP is the battle's).
- **PK-4 Before there's a party** (the title, the intro, a new game before the first Pokémon): the
  logo, as for any game.
- **PK-5 Half-written.** While the party is being rewritten (a Pokémon caught or moved), the last
  page stays; a Pokémon that doesn't read whole (its checksum) is never drawn.

- **PK-6 The battle.** When a battle starts, its page comes up (and the page before comes back
  when it ends): the foe in play (icon, name, level, HP, condition), the trainer's whole team beside
  it (small icons, fainted ones greyed; none in a wild battle), the player's Pokémon in play, and its
  four moves — name, type, PP left of the most. A double battle: the foe's two and the player's two,
  side by side; the moves are those of the player's Pokémon whose turn it is (its card edged orange).
- **PK-7 A move tapped.** While the game waits on its action menu (FIGHT chosen for the player) or
  its move menu, a tap on a move plays it, as A on it would. Anywhere else in the battle (a message,
  an animation) the tap waits for the menu, and is dropped after three seconds; a move without PP
  does nothing. In a double battle, after the move the target the game offers is taken (A).
- **PK-8 The bag.** Pockets along the top (道具, 精灵球, 树果, 学习器, 重要物品), each one's items with
  how many; past 17, 还有…个 — a tap on the list or the pocket shows the next ones. While the bag page is shown, L and R
  turn its pockets (R the next, L the one before) and the game doesn't get them (the owner, 11-23).
  A tap on an item opens the game's own bag on it, for the player's A — nothing is used
  for them: in a battle by the action menu's BAG (as PK-12), on the overworld by START and the start menu's
  bag entry, the bag's remembered pocket and place written first. Their bag has three pockets: a berry or a TM
  opens it on 树果袋 / 技能盒 among the key items (Emerald's, Ruby's and Sapphire's five pockets: each its own);
  key items aren't offered in a battle. On the first screen of its list nothing is scrolled. Elsewhere (a
  message, another menu) nothing happens. Checked in all four layouts — FireRed, Emerald, Ruby (Japan) and
  Ruby (USA) — on the overworld (an item, a TM, a berry) and in a battle.
- **PK-10 The PC's boxes.** The box the game has open first, ◀ ▶ for the others; its 30 places
  (small icons), how many are filled; a tap on a place shows that Pokémon — icon, name, level (from
  its experience and its species' growth rate in the ROM; none where the ROM has no base stats).
- **PK-11 A member in full.** A tap on a party card: its nature, ability, held item, HP, its five stats in
  the games' order, its four moves with type and PP; a tap anywhere goes back. Cards show gender and the
  held item.
- **PK-12 The battle's other actions.** Beside the moves, 背包 · 宝可梦 · 逃跑: the game's own action menu's
  (its cursor put there and A) — lit while the game waits on it. A move against the foe in play is marked
  效果绝佳 / 效果不好 / 没有效果 (Gen 3's type chart; status moves unmarked).
- **PK-13 图鉴.** The national Pokédex, 36 a page (◀ ▶): caught ones in colour, seen ones as silhouettes,
  the others empty; counts of caught and seen; a tap on one names it.
- **PK-14 地图.** The map the player is on, drawn from the game's own tiles and palettes in the ROM (the
  layout's blocks, the tilesets' metatiles, two layers), the player a red dot: whole when it fits at half
  size or more, else at half size round the player.
- **PK-9 A hack's own Pokémon.** A species the ROM has no national number for (a hack that replaced
  them): ？？？ for its name; one the ROM has no icon for: a Poké Ball.

Names are today's official ones (PokeAPI): a translation's own older ones (利爪 for 抓) differ.

## Not yet

Place names (no Chinese names for Gen 3's places anywhere open); the region map. A move tapped in a real double battle (drawn with one made up in memory,
not played).

## How

src/poke.c: the addresses per release, the Pokémon decryption, the icon table found by its
palettes' tags, the drawing. src/poke_data.c is generated by tools/poke/gen_poke_data.py (names:
PokeAPI's zh-Hans list by national number, mapped from the games' internal numbers through the ROM's
own table; glyphs: Fusion Pixel 12px, OFL-1.1, tools/poke/OFL.txt). Checked by playing each of
FireRed, Emerald and Ruby (AXVJ) to the first Pokémon and into its first battle in the harness
(`live NAME` writes the page, `touch X Y` taps it, `pokerun N` runs frames with its keys), an item
and badges put in and seen in each game's own bag and trainer card; FireRed on the AYN Thor in 游典
(the tabs, a move tapped from the action menu). The layouts' RAM: found by diffing dumps around
each menu; the battle menus' functions by their first 32 bytes in the ROM. FireRed / LeafGreen have two sets: the
player's, and the tutorial battles' (the rival's first, the old man's), whose action menu starts the same
and whose move menu lies 0x34C after it — only the tutorial's had been found, so after the first battle a
move tapped did nothing (the owner's save, 11-23; both found now).
