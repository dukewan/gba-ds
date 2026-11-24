/* The Pokémon games' second screen: see poke.h and docs/games/pokemon-gen3.md.
 *
 * Where things are (pret's pokeruby / pokeemerald / pokefirered, each checked on the games:
 * played to the first Pokémon and into its first battle in the harness, an item and badges put in
 * and seen in the game's own bag and trainer card): four layouts, told apart by which one's gMain
 * the ROM's code refers to most. Each Pokémon's 48 bytes of data are XORed with personality ^ OT id
 * and shuffled by personality % 24; their checksum tells a whole one from one being written. Money
 * and bag quantities are XORed with a key in FireRed / LeafGreen / Emerald. Icons, move data and
 * the species-to-national table come from the player's own ROM: the icon table, the palette
 * indices and the six palettes lie end to end, found by the palettes' tags (0xDAC0-0xDAC5); the
 * moves by their first two entries; the national table by its run 1..251. */
#include "poke.h"

#include <mgba/core/core.h>

#include <string.h>

#define W 240
#define H 160

/* ------------------------------------------------------------------ layouts */

typedef struct {
	uint32_t off;
	int n;
} Pocket;

typedef struct {
	const char* name;
	uint32_t gMain; /* the layout's: the ROM's code refers to it most */
	uint32_t count, party, enemy, battleMons;
	uint32_t sb1Ptr, sb2Ptr, sb1; /* the save blocks: pointers (FRLG, E) or where it is (RS) */
	uint32_t keyOff;              /* in save block 2: the money / quantity key (0: none) */
	uint32_t moneyOff, flagsOff;  /* in save block 1 */
	int badgeFlag;
	Pocket pockets[5]; /* 道具, 精灵球, 树果, 学习器, 重要物品 */
	/* the battle's menus, for a move tapped (0: not known here): battler 0's controller function,
	 * its action and move cursors; the two menus' functions found in the ROM by their first bytes */
	uint32_t controller, actionCursor, moveCursor;
	const char *actionSig, *moveSig;
	int moveAfterAction; /* the move menu's function this far after the action menu's, its first bytes not found */
	uint32_t sigFrom;    /* where in the ROM to look for them */
	/* FireRed / LeafGreen: the tutorial battles' controller (the rival's first, the old man's) has menus of
	 * its own — its action menu's starts as the player's (the next match), its move menu this far after */
	const char* tutMoveSig;
	int tutMoveAfter;
	uint32_t battlers;   /* gBattlersCount: 4 in a double battle */
	uint32_t storagePtr, storage; /* the PC's boxes: a pointer (FRLG, E) or where they are (RS) */
	uint32_t inBattle;            /* gMain's inBattle byte (bit 1), found by a dump in and out of a battle */
	uint32_t sb2;                 /* save block 2 where it's fixed (RS; FRLG / E: sb2Ptr): the Pokédex, play time */
	uint32_t mapHeader;           /* gMapHeader: the map the player is on (its layout first) */
	int primaryTiles, primaryPals; /* the primary tileset's tiles (and metatiles) and palettes: FRLG 640 / 7, else 512 / 6 */
	/* 背包 → the game's own bag, opened on an item (bagPocket 0: not here). The start menu: its callback (0
	 * while closed), cursor, count and order of entries, the bag's entry in it; the bag's remembered place —
	 * its pocket (bagPocketSize bytes), each pocket's cursor and items above (bagFieldSize bytes, bagStride
	 * apart) — and the rows it shows; each of our pockets' pocket in the game's bag (-1: kept in a key item,
	 * keyItemFor's) */
	uint32_t startCb, startCursor, startCount, startOrder;
	int bagId;
	uint32_t bagPocket, bagCursor, bagAbove;
	int bagPocketSize, bagStride, bagFieldSize, bagRows;
	int gamePocket[5], keyItemFor[5];
} Layout;

static const Layout layouts[] = {
	{ "FireRed / LeafGreen", 0x030030F0, 0x02024029, 0x02024284, 0x0202402C, 0x02023BE4, 0x03005008, 0x0300500C, 0,
	  0xF20, 0x290, 0xEE0, 0x820, { { 0x310, 42 }, { 0x430, 13 }, { 0x54C, 43 }, { 0x464, 58 }, { 0x3B8, 30 } },
	  0x03004FE0, 0x02023FF8, 0x02023FFC,
	  "\xf0\xb5\x47\x46\x80\xb4\x16\x4f\x16\x4c\x20\x78\x42\x02\xb9\x1c\x51\x18\x0d\x78\xf9\x1c\x52\x18\x11\x78\x09\x02\x0d\x43\x01\x21",
	  "\xf0\xb5\x47\x46\x80\xb4\x81\xb0\x00\x20\x80\x46\x13\x4e\x30\x78\x40\x02\x13\x49\x47\x18\x05\xf0\x4f\xf8\x12\x48\xc1\x8d\x01\x25", 0x5D8,
	  0, "\x00\xb5", 0x34C, 0x02023BCC, 0x03005010, 0, 0x03003529, 0, 0x02036DFC, 640, 7,
	  0x020370F0, 0x020370F4, 0x020370F5, 0x020370F6, 2,
	  0x0203AD02, 0x0203AD04, 0x0203AD0A, 2, 2, 2, 6, { 0, 2, -1, -1, 1 }, { 0, 0, 365, 364, 0 } },
	{ "Emerald", 0x030022C0, 0x020244E9, 0x020244EC, 0x02024744, 0x02024084, 0x03005D8C, 0x03005D90, 0,
	  0xAC, 0x490, 0x1270, 0x867, { { 0x560, 30 }, { 0x650, 16 }, { 0x790, 46 }, { 0x690, 64 }, { 0x5D8, 30 } },
	  0x03005D60, 0x020244AC, 0x020244B0,
	  "\xf0\xb5\x13\x4b\x13\x4c\x20\x78\x42\x02\x99\x1c\x51\x18\x0d\x78\x03\x33\xd2\x18\x11\x78\x09\x02\x0d\x43\x01\x21\x07\x22\x01\x23",
	  "\xf0\xb5\x47\x46\x80\xb4\x00\x20\x80\x46\x0a\x4b\x18\x78\x40\x02\x09\x49\x46\x18\x09\x4a\x91\x8d\xf0\x20\x08\x40\x1f\x1c\x00\x28", 0x674,
	  0, NULL, 0, 0x0202406C, 0x03005D94, 0, 0x030026F9, 0, 0x02037318, 512, 6,
	  0x03005DF4, 0x0203760E, 0x0203760F, 0x02037610, 2,
	  0x0203CE5D, 0x0203CE60, 0x0203CE6A, 1, 2, 2, 8, { 0, 1, 3, 2, 4 }, { 0 } },
	{ "Ruby / Sapphire", 0x03001770, 0x03004350, 0x03004360, 0x030045C0, 0x02024A80, 0, 0, 0x02025734,
	  0, 0x490, 0x1220, 0x807, { { 0x560, 20 }, { 0x600, 16 }, { 0x740, 46 }, { 0x640, 64 }, { 0x5B0, 20 } },
	  0x03004330, 0x02024E60, 0x02024E64,
	  "\xf0\xb5\x47\x46\x80\xb4\x17\x4f\x17\x4c\x20\x78\x42\x02\xb9\x1c\x51\x18\x0d\x78\xf9\x1c\x52\x18\x11\x78\x09\x02\x0d\x43\x01\x21",
	  "\xf0\xb5\x47\x46\x80\xb4\x82\xb0\x00\x20\x80\x46\x12\x4f\x3a\x78\x50\x02\x12\x49\x46\x18\x12\x48\xc1\x8d\x01\x25\x2c\x1c\x0c\x40", 0x5F4,
	  0, NULL, 0, 0x02024A68, 0, 0x020300A0, 0x03001BAD, 0x02024EA4, 0x0202E828, 512, 6,
	  0x03004AE8, 0x0202E8FC, 0x0202E8FD, 0x0202E8FE, 2,
	  0x02038559, 0x03005D10, 0x03005D11, 1, 4, 1, 8, { 0, 1, 3, 2, 4 }, { 0 } },
	{ "Ruby / Sapphire (Japan)", 0x030016E0, 0x03004280, 0x03004290, 0x030044F0, 0x020247E0, 0, 0, 0x02025494,
	  0, 0x490, 0x1220, 0x807, { { 0x560, 20 }, { 0x600, 16 }, { 0x740, 46 }, { 0x640, 64 }, { 0x5B0, 20 } },
	  0x03004260, 0x02024BC0, 0x02024BC4,
	  "\xf0\xb5\x47\x46\x80\xb4\x16\x4f\x16\x4c\x20\x78\x42\x02\xb9\x1c\x51\x18\x0d\x78\xf9\x1c\x52\x18\x11\x78\x09\x02\x0d\x43\x01\x21",
	  "\xf0\xb5\x47\x46\x80\xb4\x82\xb0\x00\x20\x80\x46\x12\x4e\x32\x78\x50\x02\x12\x49\x47\x18\x12\x48\xc1\x8d\x01\x25\x2c\x1c\x0c\x40", 0x5DC,
	  0, NULL, 0, 0x020247C8, 0, 0x0202FDC0, 0x03001B19, 0x02024C04, 0x0202E588, 512, 6,
	  0x03004A18, 0x0202E64C, 0x0202E64D, 0x0202E64E, 2,
	  0x02038258, 0x03005C30, 0x03005C31, 1, 4, 1, 8, { 0, 1, 3, 2, 4 }, { 0 } },
};
#define N_LAYOUTS (int) (sizeof(layouts) / sizeof(layouts[0]))

bool poke_supported(const char* gameCode) {
	static const char* const codes[] = { "BPRE", "BPGE", "BPEE", "AXVE", "AXPE", "AXVJ", "AXPJ" };
	for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i)
		if (!strncmp(codes[i], gameCode, 4)) return true;
	return false;
}

uint32_t poke_virt(Emu* e, uint32_t addr) {
	(void) e;
	(void) addr;
	return 0;
}

/* ------------------------------------------------------------------ what's in the ROM */

static struct {
	uint32_t crc;
	bool done;
	const Layout* lay;
	bool icons;
	uint32_t iconTable, iconIndices, iconPalettes; /* GBA addresses */
	uint32_t moves;                                /* gBattleMoves, 0: not found */
	uint32_t national;                             /* species-to-national (entry 0: species 1), 0: not found */
	uint32_t size;
	uint32_t actionFn, moveFn;   /* the battle menus' functions (Thumb: +1), 0: not found */
	uint32_t actionFn2, moveFn2; /* FRLG's tutorial battles' (0: none) */
	uint32_t overworld;          /* the overworld's callback 2 (Thumb: +1), 0: not found */
	uint32_t baseStats;        /* gBaseStats (28 bytes a species, entry 0: none), 0: not found */
	const uint8_t* bytes;      /* the ROM itself (the core's copy, while the game is loaded) */
} rom;

static uint32_t le32(const uint8_t* b) {
	return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t) b[3] << 24;
}
static uint16_t le16(const uint8_t* b) {
	return (uint16_t) (b[0] | b[1] << 8);
}

static void rom_scan(Emu* e) {
	if (rom.done && rom.crc == e->romCrc) return;
	memset(&rom, 0, sizeof(rom));
	rom.done = true, rom.crc = e->romCrc;
	if (e->core->platform(e->core) != mPLATFORM_GBA) return;
	size_t n = 0;
	const uint8_t* r = e->core->getMemoryBlock(e->core, 0x8 /* GBA_REGION_ROM0 */, &n);
	if (!r || n < 0x1000) return;
	rom.size = (uint32_t) n;
	rom.bytes = r;
	int refs[N_LAYOUTS] = { 0 };
	uint8_t seq[502];
	for (int i = 0; i < 251; ++i) seq[i * 2] = (uint8_t) (i + 1), seq[i * 2 + 1] = 0;
	static const uint8_t pound[17] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x28, 0x00, 0x64, 0x23 };
	/* Bulbasaur's base stats and types after an empty entry */
	static const uint8_t bulbasaur[36] = { [28] = 0x2D, 0x31, 0x31, 0x2D, 0x41, 0x41, 0x0C, 0x03 };
	for (size_t o = 0; o + 520 <= n; o += 2) {
		if (!(o & 3)) {
			uint32_t v = le32(r + o);
			for (int l = 0; l < N_LAYOUTS; ++l)
				if (v == layouts[l].gMain) ++refs[l];
			if (!rom.iconPalettes && o >= 2200 && v >> 24 == 0x08 && le32(r + o + 4) == 0xDAC0 && le32(r + o + 12) == 0xDAC1 &&
			    le32(r + o + 44) == 0xDAC5)
				rom.iconPalettes = 0x08000000 + (uint32_t) o;
			if (!rom.moves && !memcmp(r + o, pound, sizeof(pound)) && r[o + 25] == 50 && r[o + 26] == 1)
				rom.moves = 0x08000000 + (uint32_t) o;
			if (!rom.baseStats && r[o + 28] == 0x2D && !memcmp(r + o, bulbasaur, sizeof(bulbasaur)))
				rom.baseStats = 0x08000000 + (uint32_t) o;
		}
		if (!rom.national && r[o] == 1 && r[o + 2] == 2 && !memcmp(r + o, seq, sizeof(seq)))
			rom.national = 0x08000000 + (uint32_t) o;
	}
	int best = -1;
	for (int l = 0; l < N_LAYOUTS; ++l)
		if (refs[l] >= 100 && (best < 0 || refs[l] > refs[best])) best = l;
	rom.lay = best >= 0 ? &layouts[best] : NULL;
	if (rom.iconPalettes) {
		rom.iconIndices = rom.iconPalettes - 440;
		rom.iconTable = rom.iconIndices - 440 * 4;
		const uint8_t* t = r + (rom.iconTable - 0x08000000);
		const uint8_t* ix = r + (rom.iconIndices - 0x08000000);
		rom.icons = true;
		for (int i = 0; i < 440 && rom.icons; ++i)
			if (t[i * 4 + 3] != 0x08 && t[i * 4 + 3] != 0x09) rom.icons = false;
		for (int i = 0; i < 440 && rom.icons; ++i)
			if (ix[i] > 5) rom.icons = false;
	}
	/* the battle menus' functions: the action menu's by its first bytes (in FireRed past the first
	 * 256 KB, where an unrelated function starts the same); the move menu's too, or where it lies
	 * from the action menu's (LeafGreen) */
	const Layout* L = rom.lay;
	if (L && L->actionSig) {
		for (size_t o = L->sigFrom; o + 32 <= n && !rom.actionFn; o += 2)
			if (!memcmp(r + o, L->actionSig, 32)) rom.actionFn = 0x08000000 + (uint32_t) o + 1;
		for (size_t o = L->sigFrom; o + 32 <= n && !rom.moveFn && rom.actionFn; o += 2)
			if (!memcmp(r + o, L->moveSig, 32)) rom.moveFn = 0x08000000 + (uint32_t) o + 1;
		if (rom.actionFn && !rom.moveFn) {
			uint32_t at = rom.actionFn - 1 + L->moveAfterAction - 0x08000000;
			if (at + 2 <= n && r[at] == (uint8_t) L->moveSig[0] && r[at + 1] == (uint8_t) L->moveSig[1]) rom.moveFn = 0x08000000 + at + 1;
		}
		if (rom.actionFn && L->tutMoveSig) {
			for (size_t o = rom.actionFn + 1 - 0x08000000; o + 32 <= n && !rom.actionFn2; o += 2)
				if (!memcmp(r + o, L->actionSig, 32)) rom.actionFn2 = 0x08000000 + (uint32_t) o + 1;
			uint32_t at = rom.actionFn2 ? rom.actionFn2 - 1 + L->tutMoveAfter - 0x08000000 : 0;
			if (at && at + 2 <= n && r[at] == (uint8_t) L->tutMoveSig[0] && r[at + 1] == (uint8_t) L->tutMoveSig[1]) rom.moveFn2 = 0x08000000 + at + 1;
			else rom.actionFn2 = 0;
		}
	}
	/* the overworld's callback 2 (CB2_Overworld): its first 16 bytes and its 25th to 28th, the same in every
	 * Gen 3 game (the calls between differ): once in each of 36 ROMs and hacks tried */
	if (L && L->bagPocket) {
		static const uint8_t ow[16] = { 0x10, 0xb5, 0x09, 0x48, 0xc0, 0x79, 0xc0, 0x09, 0x04, 0x1c, 0x00, 0x2c, 0x02, 0xd0, 0x00, 0x20 };
		static const uint8_t ow2[4] = { 0x00, 0x2c, 0x01, 0xd0 };
		for (size_t o = 0; o + 32 <= n && !rom.overworld; o += 2)
			if (!memcmp(r + o, ow, 16) && !memcmp(r + o + 24, ow2, 4)) rom.overworld = 0x08000000 + (uint32_t) o + 1;
	}
	fprintf(stderr, "pokemon: battle menus %s%s\n", rom.actionFn && rom.moveFn ? "found" : "not found", rom.actionFn2 ? " (and the tutorial's)" : "");
	fprintf(stderr, "pokemon: %s (gMain refs %d/%d/%d/%d), icons %s, moves %s, national %s\n", rom.lay ? rom.lay->name : "no layout",
	        refs[0], refs[1], refs[2], refs[3], rom.icons ? "yes" : "no", rom.moves ? "yes" : "no", rom.national ? "yes" : "no");
}

/* a species' national number (0: unknown) */
static int national_of(Emu* e, int species) {
	if (species <= 0) return 0;
	if (rom.national && 0x08000000 + rom.size > rom.national + species * 2) {
		int v = emu_read16(e, rom.national + (species - 1) * 2);
		return v <= POKE_NATIONAL ? v : 0;
	}
	/* (no table: a hack that replaced the species with its own; not named after Pokémon) */
	return 0;
}

static const char* species_name(Emu* e, int species) {
	int nat = national_of(e, species);
	return nat && pokeNatNames[nat][0] ? pokeNatNames[nat] : "？？？";
}

/* the experience a level takes at a growth rate (the games' own formulas) */
static int32_t exp_for(int rate, int n) {
	int32_t c = n * n * n;
	switch (rate) {
	case 1: /* erratic */
		if (n <= 50) return c * (100 - n) / 50;
		if (n <= 68) return c * (150 - n) / 100;
		if (n <= 98) return c * ((1911 - 10 * n) / 3) / 500;
		return c * (160 - n) / 100;
	case 2: /* fluctuating */
		if (n <= 15) return c * ((n + 1) / 3 + 24) / 50;
		if (n <= 36) return c * (n + 14) / 50;
		return c * ((n / 2) + 32) / 50;
	case 3: /* medium slow */
		return 6 * c / 5 - 15 * n * n + 100 * n - 140;
	case 4: return 4 * c / 5; /* fast */
	case 5: return 5 * c / 4; /* slow */
	default: return c;        /* medium fast */
	}
}

/* a boxed Pokémon's level, from its experience (0: unknown — no base stats found) */
static int level_of(Emu* e, int species, uint32_t exp) {
	if (!rom.baseStats || species <= 0 || species > 2047 || rom.baseStats + species * 28 + 28 > 0x08000000 + rom.size) return 0;
	int rate = emu_read8(e, rom.baseStats + species * 28 + 0x13);
	if (rate > 5) return 0;
	int lv = 1;
	while (lv < 100 && (int32_t) exp >= exp_for(rate, lv + 1)) ++lv;
	return lv;
}

/* a move's type (0-17, -1: unknown) and base PP (0: unknown) */
static int move_type(Emu* e, int move) {
	if (!rom.moves || move <= 0 || move >= POKE_MOVES) return -1;
	int t = emu_read8(e, rom.moves + move * 12 + 2);
	return t < 18 ? t : -1;
}
static int move_pp(Emu* e, int move) {
	if (!rom.moves || move <= 0 || move >= POKE_MOVES) return 0;
	return emu_read8(e, rom.moves + move * 12 + 4);
}

/* ------------------------------------------------------------------ the game's memory */

typedef struct {
	int species; /* internal number */
	bool egg;
	int level, hp, maxHp;
	uint32_t status;
	uint32_t pid; /* personality: the gender, the nature */
	int item;     /* held */
	int moves[4], pp[4], ppBonuses;
	int stats[6]; /* max HP, attack, defence, speed, sp. attack, sp. defence */
	int abilityNum;
	uint32_t exp;
} Mon;

static const char orders[24][5] = {
	"GAEM", "GAME", "GEAM", "GEMA", "GMAE", "GMEA", "AGEM", "AGME", "AEGM", "AEMG", "AMGE", "AMEG",
	"EGAM", "EGMA", "EAGM", "EAMG", "EMGA", "EMAG", "MGAE", "MGEA", "MAGE", "MAEG", "MEGA", "MEAG",
};

static void read_bytes(Emu* e, uint32_t a, uint8_t* b, int n) {
	for (int j = 0; j < n; j += 4) {
		uint32_t v = emu_read32(e, a + j);
		b[j] = v, b[j + 1] = v >> 8, b[j + 2] = v >> 16, b[j + 3] = v >> 24;
	}
}

/* false: not a whole Pokémon (being written, an empty slot, garbage) */
static bool mon_decode(const uint8_t* b, Mon* m) {
	uint32_t pid = le32(b), key = pid ^ le32(b + 4);
	uint8_t d[48];
	uint16_t sum = 0;
	for (int i = 0; i < 48; i += 4) {
		uint32_t v = le32(b + 32 + i) ^ key;
		d[i] = v, d[i + 1] = v >> 8, d[i + 2] = v >> 16, d[i + 3] = v >> 24;
		sum += (uint16_t) (v + (v >> 16));
	}
	if (sum != le16(b + 28)) return false;
	const char* o = orders[pid % 24];
	const uint8_t *g = NULL, *misc = NULL, *at = NULL;
	for (int i = 0; i < 4; ++i) {
		if (o[i] == 'G') g = d + i * 12;
		if (o[i] == 'M') misc = d + i * 12;
		if (o[i] == 'A') at = d + i * 12;
	}
	m->species = le16(g);
	m->item = le16(g + 2);
	m->exp = le32(g + 4);
	m->ppBonuses = g[8];
	for (int i = 0; i < 4; ++i) m->moves[i] = le16(at + i * 2), m->pp[i] = at[8 + i];
	m->abilityNum = (le32(misc + 4) >> 31) & 1;
	for (int i = 0; i < 6; ++i) m->stats[i] = le16(b + 88 + i * 2);
	m->pid = pid;
	m->egg = (le32(misc + 4) >> 30) & 1;
	m->status = le32(b + 80);
	m->level = b[84];
	m->hp = le16(b + 86);
	m->maxHp = le16(b + 88);
	if (m->species <= 0 || m->species > 2047 || m->level < 1 || m->level > 100) return false;
	if (!m->egg && (m->maxHp <= 0 || m->hp > m->maxHp)) return false;
	return true;
}

/* the leading whole Pokémon of a party of 6 at a; -1: one there but not whole */
static int party_read(Emu* e, uint32_t a, int most, Mon* mons, uint32_t* key) {
	int n = 0;
	for (int i = 0; i < most; ++i) {
		uint8_t raw[100];
		read_bytes(e, a + i * 100, raw, 100);
		if (!le32(raw) && !le32(raw + 4)) break; /* an empty slot */
		if (!mon_decode(raw, &mons[i])) return -1;
		for (int j = 0; j < 100; ++j) *key = (*key ^ raw[j]) * 16777619u;
		++n;
	}
	return n;
}

/* a battler in the battle (gBattleMons): its moves and their PP too */
typedef struct {
	Mon m;
	int moves[4], pp[4], maxPp[4];
	int type1, type2;
} Battler;

static bool battler_read(Emu* e, uint32_t a, Battler* b, uint32_t* key) {
	uint8_t raw[0x58];
	read_bytes(e, a, raw, 0x58);
	for (int j = 0; j < 0x58; ++j) *key = (*key ^ raw[j]) * 16777619u;
	memset(b, 0, sizeof(*b));
	b->m.species = le16(raw);
	b->m.hp = le16(raw + 0x28);
	b->m.level = raw[0x2A];
	b->m.maxHp = le16(raw + 0x2C);
	b->m.status = le32(raw + 0x4C);
	b->m.item = le16(raw + 0x2E);
	b->m.pid = le32(raw + 0x48);
	b->type1 = raw[0x21], b->type2 = raw[0x22];
	if (b->m.species <= 0 || b->m.species > 2047 || b->m.maxHp <= 0 || b->m.hp > b->m.maxHp || b->m.level < 1 || b->m.level > 100)
		return false;
	for (int i = 0; i < 4; ++i) {
		b->moves[i] = le16(raw + 0x0C + i * 2);
		b->pp[i] = raw[0x24 + i];
		int base = move_pp(e, b->moves[i]), bonus = (raw[0x3B] >> (i * 2)) & 3;
		b->maxPp[i] = base ? base + base * bonus / 5 : 0;
	}
	return true;
}

static uint32_t sb1_of(Emu* e, const Layout* l) {
	return l->sb1Ptr ? emu_read32(e, l->sb1Ptr) : l->sb1;
}
static uint32_t key_of(Emu* e, const Layout* l) {
	return l->keyOff ? emu_read32(e, emu_read32(e, l->sb2Ptr) + l->keyOff) : 0;
}

static uint32_t sb2_of(Emu* e, const Layout* l) {
	return l->sb2Ptr ? emu_read32(e, l->sb2Ptr) : l->sb2;
}

/* -1 genderless, 0 male, 1 female (the species' ratio in its base stats against the personality's low byte) */
static int gender_of(Emu* e, int species, uint32_t pid) {
	if (!rom.baseStats || species <= 0 || species > 2047) return -1;
	int ratio = emu_read8(e, rom.baseStats + species * 28 + 0x10);
	if (ratio == 255) return -1;
	if (ratio == 254) return 1;
	if (ratio == 0) return 0;
	return (int) (pid & 0xFF) < ratio;
}

static int badges_of(Emu* e, const Layout* l) {
	uint32_t flags = sb1_of(e, l) + l->flagsOff;
	int n = 0;
	for (int i = 0; i < 8; ++i) {
		int f = l->badgeFlag + i;
		n |= ((emu_read8(e, flags + f / 8) >> (f % 8)) & 1) << i;
	}
	return n;
}

/* ------------------------------------------------------------------ drawing
 *
 * The pages are drawn at twice the GBA's resolution, 480 × 320 (the frame 游典 gets is 480 × 640 for these
 * games: libretro/gbads_libretro.c): the panels' edges and bars in GBA pixels (two of these), the words in
 * the 12-pixel font at its own size or doubled, the Pokémon's icons at twice their size where there's room. */

#define RGB(r, g, b) (0xFF000000u | (uint32_t) (b) << 16 | (uint32_t) (g) << 8 | (uint32_t) (r))
#define HW 480
#define HH_MAX 640
/* The page's height: 320 (the GBA's shape, doubled), or taller to fill a lower screen of another shape
 * (poke_set_page_height): the bars stay at the top and bottom, the pages use the rows between — more of a
 * list, a taller map, or what they show centred (dy). */
static int hh = 320;
#define HH hh
#define EXTRA (hh - 320)

static uint32_t* px;
static int dy; /* a page drawn centred in a taller one: its rows moved down this far */

static void put(int x, int y, uint32_t c) {
	y += dy;
	if (px && x >= 0 && x < HW && y >= 0 && y < HH) px[y * HW + x] = c;
}

static void fill(int x0, int y0, int x1, int y1, uint32_t c) {
	if (!px) return;
	y0 += dy, y1 += dy;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= HW) x1 = HW - 1;
	if (y1 >= HH) y1 = HH - 1;
	for (int y = y0; y <= y1; ++y)
		for (int x = x0; x <= x1; ++x) px[y * HW + x] = c;
}

static uint32_t mix(uint32_t a, uint32_t b, int share) {
	int r = (a & 0xFF) + (((int) (b & 0xFF) - (int) (a & 0xFF)) * share) / 100;
	int g = ((a >> 8) & 0xFF) + ((((int) (b >> 8) & 0xFF) - (int) ((a >> 8) & 0xFF)) * share) / 100;
	int bl = ((a >> 16) & 0xFF) + ((((int) (b >> 16) & 0xFF) - (int) ((a >> 16) & 0xFF)) * share) / 100;
	return RGB(r, g, bl);
}

/* a box with its corners cut, a 2-pixel edge, a face, a lighter line along its top */
static void panel(int x, int y, int w, int h, uint32_t edge, uint32_t face) {
	fill(x + 2, y, x + w - 3, y + h - 1, edge);
	fill(x, y + 2, x + w - 1, y + h - 3, edge);
	fill(x + 2, y + 2, x + w - 3, y + h - 3, face);
	fill(x + 4, y + 2, x + w - 5, y + 3, mix(face, RGB(255, 255, 255), 60));
	fill(x + 4, y + h - 5, x + w - 5, y + h - 4, mix(face, RGB(0, 0, 0), 12));
}

static uint32_t grey(uint32_t c, int share) {
	int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF, l = (r * 3 + g * 6 + b) / 10;
	r += (l - r) * share / 100, g += (l - g) * share / 100, b += (l - b) * share / 100;
	return RGB(r, g, b);
}

static uint32_t bgr555(uint16_t c) {
	int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
	return RGB(r * 255 / 31, g * 255 / 31, b * 255 / 31);
}

/* a Poké Ball, size across: (a hack's Pokémon the ROM has no icon of; a team's member in a battle) */
static void ball(int x, int y, int size, int state) {
	/* state 0: a whole one, 1: fainted (grey), 2: none (an outline) */
	int r = size / 2 - 1, cx = x + size / 2, cy = y + size / 2;
	for (int dy = -r; dy <= r; ++dy)
		for (int dx = -r; dx <= r; ++dx) {
			int d = dx * dx + dy * dy;
			if (d > r * r) continue;
			bool rim = d > (r - 2) * (r - 2), band = dy >= -1 && dy <= 0, hub = d <= (r / 3) * (r / 3);
			uint32_t c = rim || band ? RGB(40, 40, 48) : dy < 0 ? RGB(232, 64, 56) : RGB(248, 248, 248);
			if (hub) c = d <= (r / 3 - 2) * (r / 3 - 2) ? RGB(248, 248, 248) : RGB(40, 40, 48);
			if (state == 2 && !rim) continue;
			put(cx + dx, cy + dy, state == 1 ? grey(c, 80) : state == 2 ? RGB(120, 136, 168) : c);
		}
}

/* species' icon at (x, y), scale 1 (32 × 32) or 2 (64 × 64); frame 0 or 1 (the games' own two-frame
 * bob); look: 0 as it is, 1 greyed (fainted), 2 a silhouette (seen, not caught) */
static void icon_draw(Emu* e, int species, int x, int y, int scale, int frame, int look) {
	if (!rom.icons || species >= 440 || species <= 0) {
		ball(x, y, 32 * scale, look == 1 ? 1 : 0);
		return;
	}
	uint32_t gfx = emu_read32(e, rom.iconTable + species * 4);
	int pal = emu_read8(e, rom.iconIndices + species);
	if ((gfx >> 24) < 0x08 || (gfx >> 24) > 0x09 || pal > 5) {
		ball(x, y, 32 * scale, look == 1 ? 1 : 0);
		return;
	}
	gfx += frame ? 512 : 0;
	uint32_t pp = emu_read32(e, rom.iconPalettes + pal * 8);
	uint32_t colors[16];
	for (int i = 0; i < 16; ++i) colors[i] = bgr555(emu_read16(e, pp + i * 2));
	for (int t = 0; t < 16; ++t) {
		int tx = (t % 4) * 8, ty = (t / 4) * 8;
		for (int r = 0; r < 8; ++r) {
			uint32_t row = emu_read32(e, gfx + t * 32 + r * 4);
			for (int c = 0; c < 8; ++c) {
				int v = (row >> (c * 4)) & 15;
				if (!v) continue;
				uint32_t col = look == 1 ? grey(colors[v], 75) : look == 2 ? RGB(56, 72, 112) : colors[v];
				int X = x + (tx + c) * scale, Y = y + (ty + r) * scale;
				fill(X, Y, X + scale - 1, Y + scale - 1, col);
			}
		}
	}
}

static const PokeGlyph* glyph(unsigned cp) {
	int lo = 0, hi = pokeGlyphCount - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (pokeGlyphs[mid].cp == cp) return &pokeGlyphs[mid];
		if (pokeGlyphs[mid].cp < cp) lo = mid + 1;
		else hi = mid - 1;
	}
	return NULL;
}

static unsigned utf8_next(const char** s) {
	const unsigned char* p = (const unsigned char*) *s;
	unsigned c = *p++;
	if (c >= 0xF0) c = (c & 7) << 18 | (p[0] & 63) << 12 | (p[1] & 63) << 6 | (p[2] & 63), p += 3;
	else if (c >= 0xE0) c = (c & 15) << 12 | (p[0] & 63) << 6 | (p[1] & 63), p += 2;
	else if (c >= 0xC0) c = (c & 31) << 6 | (p[0] & 63), p += 1;
	*s = (const char*) p;
	return c;
}

static int text_width(const char* s, int scale) {
	int w = 0;
	while (*s) {
		const PokeGlyph* g = glyph(utf8_next(&s));
		w += (g ? g->adv : 6) * scale;
	}
	return w;
}

/* words with their top at y, at scale 1 (12 pixels high) or 2, cut off past x + most (0: whole);
 * shadow: drawn first one scale-pixel right and down (0: none). Returns the right edge. */
static int text_fit(int x, int y, const char* s, uint32_t color, uint32_t shadow, int scale, int most) {
	int end = x;
	for (int pass = shadow ? 0 : 1; pass < 2; ++pass) {
		int cx = x + (pass ? 0 : scale), cy = y + (pass ? 0 : scale);
		const char* p = s;
		while (*p) {
			const PokeGlyph* g = glyph(utf8_next(&p));
			int adv = (g ? g->adv : 6) * scale;
			if (most && cx + adv - x > most) break;
			if (g)
				for (int r = 0; r < 12; ++r)
					for (int c = 0; c < 16; ++c)
						if (g->rows[r] >> c & 1) fill(cx + c * scale, cy + r * scale, cx + c * scale + scale - 1, cy + r * scale + scale - 1, pass ? color : shadow);
			cx += adv;
		}
		end = cx;
	}
	return end;
}
static int text(int x, int y, const char* s, uint32_t color, uint32_t shadow, int scale) {
	return text_fit(x, y, s, color, shadow, scale, 0);
}
static void text_right(int right, int y, const char* s, uint32_t color, uint32_t shadow, int scale) {
	text(right - text_width(s, scale), y, s, color, shadow, scale);
}
static void text_mid(int mid, int y, const char* s, uint32_t color, uint32_t shadow, int scale) {
	text(mid - text_width(s, scale) / 2, y, s, color, shadow, scale);
}

static void number(char* out, int v) {
	char d[12];
	int k = 0;
	if (v < 0) *out++ = '-', v = -v;
	do d[k++] = '0' + v % 10, v /= 10;
	while (v && k < 11);
	while (k) *out++ = d[--k];
	*out = 0;
}

/* "HP", 3×5 letters in GBA pixels (2 × 2 each) */
static void hp_label(int x, int y, uint32_t c, uint32_t shadow) {
	static const char* const letters[2] = { "101101111101101", "111101111100100" };
	for (int pass = 0; pass < 2; ++pass)
		for (int l = 0; l < 2; ++l)
			for (int i = 0; i < 15; ++i)
				if (letters[l][i] == '1') {
					int X = x + (l * 4 + i % 3) * 2 + (pass ? 0 : 2), Y = y + (i / 3) * 2 + (pass ? 0 : 2);
					fill(X, Y, X + 1, Y + 1, pass ? c : shadow);
				}
}

static const uint32_t ink = RGB(48, 48, 56), soft = RGB(208, 208, 200), cream = RGB(248, 248, 236), navy = RGB(56, 72, 112),
                      white = RGB(248, 248, 248), orange = RGB(232, 96, 64);

/* the HP bar from x to right: its label, the track, the share left in green / yellow / red; 10 high */
static void hp_bar(int x, int y, int right, int hp, int maxHp) {
	fill(x, y, right, y + 11, RGB(64, 64, 72));
	fill(x + 2, y + 2, x + 23, y + 9, RGB(232, 176, 64));
	hp_label(x + 4, y + 1, white, RGB(160, 96, 32));
	int t0 = x + 26, t1 = right - 2;
	fill(t0, y + 2, t1, y + 9, RGB(80, 88, 96));
	if (!hp || maxHp <= 0) return;
	int share = hp * 100 / maxHp, len = (t1 - t0) * hp / maxHp;
	if (len < 2) len = 2;
	uint32_t hi = share > 50 ? RGB(112, 232, 128) : share > 20 ? RGB(248, 216, 64) : RGB(248, 88, 64);
	uint32_t lo = share > 50 ? RGB(56, 168, 80) : share > 20 ? RGB(200, 160, 32) : RGB(184, 48, 40);
	fill(t0, y + 2, t0 + len, y + 9, lo);
	fill(t0, y + 2, t0 + len, y + 5, hi);
}

static void hp_numbers(int right, int y, int hp, int maxHp, int scale) {
	char s[16];
	number(s, hp);
	size_t n = strlen(s);
	s[n] = '/';
	number(s + n + 1, maxHp);
	text_right(right, y, s, ink, soft, scale);
}

static void level(int right, int y, int lv, int scale) {
	char s[8] = "Lv";
	number(s + 2, lv);
	text_right(right, y, s, ink, soft, scale);
}

static const char* status_name(const Mon* m, uint32_t* color) {
	if (!m->hp) return *color = RGB(168, 64, 64), "濒死";
	if (m->status & 7) return *color = RGB(136, 136, 168), "睡眠";
	if (m->status & (1 << 3 | 1 << 7)) return *color = RGB(168, 88, 176), "中毒";
	if (m->status & 1 << 4) return *color = RGB(232, 104, 56), "灼伤";
	if (m->status & 1 << 5) return *color = RGB(96, 176, 224), "冰冻";
	if (m->status & 1 << 6) return *color = RGB(224, 184, 32), "麻痹";
	return NULL;
}

/* a tag: a coloured box with white words (1×), x, y its top left; returns its right edge */
static int tag(int x, int y, const char* s, uint32_t color) {
	int w = text_width(s, 1) + 8;
	fill(x + 1, y, x + w - 2, y + 15, color);
	fill(x, y + 1, x + w - 1, y + 14, color);
	text(x + 4, y + 2, s, white, mix(color, RGB(0, 0, 0), 40), 1);
	return x + w;
}

static int status_tag(int x, int y, const Mon* m) {
	uint32_t sc;
	const char* st = status_name(m, &sc);
	return st ? tag(x, y, st, sc) : x;
}

/* the name with its gender sign after it, scale 2, cut at most wide; returns the right edge */
static int name_and_gender(Emu* e, int x, int y, const char* name, int species, uint32_t pid, int most) {
	int end = text_fit(x, y, name, ink, soft, 2, most - 24);
	int g = gender_of(e, species, pid);
	if (g >= 0) end = text(end + 2, y, g ? "♀" : "♂", g ? RGB(232, 80, 104) : RGB(56, 112, 232), soft, 2);
	return end;
}

static const uint32_t typeColors[18] = {
	RGB(168, 168, 120), RGB(192, 48, 40), RGB(168, 144, 240), RGB(160, 64, 160), RGB(224, 192, 104), RGB(184, 160, 56),
	RGB(168, 184, 32), RGB(112, 88, 152), RGB(184, 184, 208), RGB(104, 160, 144), RGB(240, 128, 48), RGB(104, 144, 240),
	RGB(120, 200, 80), RGB(248, 208, 48), RGB(248, 88, 136), RGB(152, 216, 216), RGB(112, 56, 248), RGB(112, 88, 72),
};

/* Gen 3's type chart: how much a move of type a does to type d, in quarters (4 = normal) */
static int effect_one(int a, int d) {
	enum { NOR, FIG, FLY, POI, GRO, ROC, BUG, GHO, STE, MYS, FIR, WAT, GRA, ELE, PSY, ICE, DRA, DAR };
	static const signed char chart[][3] = {
		/* attacker, defender, 8 = ×2, 2 = ×½, 0 = none */
		{ NOR, ROC, 2 }, { NOR, STE, 2 }, { NOR, GHO, 0 },
		{ FIG, NOR, 8 }, { FIG, ICE, 8 }, { FIG, ROC, 8 }, { FIG, DAR, 8 }, { FIG, STE, 8 }, { FIG, POI, 2 }, { FIG, FLY, 2 }, { FIG, PSY, 2 }, { FIG, BUG, 2 }, { FIG, GHO, 0 },
		{ FLY, GRA, 8 }, { FLY, FIG, 8 }, { FLY, BUG, 8 }, { FLY, ELE, 2 }, { FLY, ROC, 2 }, { FLY, STE, 2 },
		{ POI, GRA, 8 }, { POI, POI, 2 }, { POI, GRO, 2 }, { POI, ROC, 2 }, { POI, GHO, 2 }, { POI, STE, 0 },
		{ GRO, FIR, 8 }, { GRO, ELE, 8 }, { GRO, POI, 8 }, { GRO, ROC, 8 }, { GRO, STE, 8 }, { GRO, GRA, 2 }, { GRO, BUG, 2 }, { GRO, FLY, 0 },
		{ ROC, FIR, 8 }, { ROC, ICE, 8 }, { ROC, FLY, 8 }, { ROC, BUG, 8 }, { ROC, FIG, 2 }, { ROC, GRO, 2 }, { ROC, STE, 2 },
		{ BUG, GRA, 8 }, { BUG, PSY, 8 }, { BUG, DAR, 8 }, { BUG, FIR, 2 }, { BUG, FIG, 2 }, { BUG, POI, 2 }, { BUG, FLY, 2 }, { BUG, GHO, 2 }, { BUG, STE, 2 },
		{ GHO, PSY, 8 }, { GHO, GHO, 8 }, { GHO, DAR, 2 }, { GHO, STE, 2 }, { GHO, NOR, 0 },
		{ STE, ICE, 8 }, { STE, ROC, 8 }, { STE, FIR, 2 }, { STE, WAT, 2 }, { STE, ELE, 2 }, { STE, STE, 2 },
		{ FIR, GRA, 8 }, { FIR, ICE, 8 }, { FIR, BUG, 8 }, { FIR, STE, 8 }, { FIR, FIR, 2 }, { FIR, WAT, 2 }, { FIR, ROC, 2 }, { FIR, DRA, 2 },
		{ WAT, FIR, 8 }, { WAT, GRO, 8 }, { WAT, ROC, 8 }, { WAT, WAT, 2 }, { WAT, GRA, 2 }, { WAT, DRA, 2 },
		{ GRA, WAT, 8 }, { GRA, GRO, 8 }, { GRA, ROC, 8 }, { GRA, FIR, 2 }, { GRA, GRA, 2 }, { GRA, POI, 2 }, { GRA, FLY, 2 }, { GRA, BUG, 2 }, { GRA, DRA, 2 }, { GRA, STE, 2 },
		{ ELE, WAT, 8 }, { ELE, FLY, 8 }, { ELE, ELE, 2 }, { ELE, GRA, 2 }, { ELE, DRA, 2 }, { ELE, GRO, 0 },
		{ PSY, FIG, 8 }, { PSY, POI, 8 }, { PSY, PSY, 2 }, { PSY, STE, 2 }, { PSY, DAR, 0 },
		{ ICE, GRA, 8 }, { ICE, GRO, 8 }, { ICE, FLY, 8 }, { ICE, DRA, 8 }, { ICE, FIR, 2 }, { ICE, WAT, 2 }, { ICE, ICE, 2 }, { ICE, STE, 2 },
		{ DRA, DRA, 8 }, { DRA, STE, 2 },
		{ DAR, PSY, 8 }, { DAR, GHO, 8 }, { DAR, FIG, 2 }, { DAR, DAR, 2 }, { DAR, STE, 2 },
	};
	for (size_t i = 0; i < sizeof(chart) / sizeof(chart[0]); ++i)
		if (chart[i][0] == a && chart[i][1] == d) return chart[i][2];
	return 4;
}
/* in sixteenths: 16 normal, 0 none, 64 ×4… */
static int effect(int a, int d1, int d2) {
	int e1 = effect_one(a, d1);
	int e2 = d2 != d1 && d2 >= 0 && d2 < 18 ? effect_one(a, d2) : 4;
	return e1 * e2;
}

/* ------------------------------------------------------------------ pages */

enum { PAGE_PARTY, PAGE_BATTLE, PAGE_MAP, PAGE_DEX, PAGE_BOX, PAGE_BAG, N_PAGES };
static const char* const pageNames[N_PAGES] = { "队伍", "对战", "地图", "图鉴", "箱子", "背包" };
static const uint32_t pageColors[N_PAGES] = { RGB(216, 72, 64), RGB(232, 136, 40), RGB(200, 168, 48), RGB(64, 160, 96), RGB(40, 144, 168), RGB(80, 104, 200) };
static const char* const pocketNames[5] = { "道具", "精灵球", "树果", "学习器", "重要物品" };

static struct {
	int page, pageBefore; /* before the battle, gone back to after it */
	bool inBattle;
	int pocket, bagStart;
	int bagPick; /* the item tapped in the pocket shown (-1: none) */
	int box;     /* the box shown (-1: the game's current one) */
	int boxPick; /* the place picked in it */
	int dexPage, dexPick; /* the Pokédex's page (36 a page) and the national number picked (0: none) */
	int summary;          /* the party's member opened in full (-1: none) */
	/* touches: a tap is taken where the finger went down */
	bool down, tapped;
	int tapX, tapY;
} ui = { .box = -1, .summary = -1, .bagPick = -1 };

void poke_touch(int x, int y, bool down) {
	if (down && !ui.down) ui.tapped = true, ui.tapX = x, ui.tapY = y;
	ui.down = down;
}

#define TOP 32      /* the bar along the top's height */
#define TABS_Y (hh - 42) /* the tabs along the bottom */
#define TAB_W 78

static int tab_x(int i) {
	return 5 + i * TAB_W;
}

static void ground(void) {
	for (int y = TOP; y < TABS_Y; ++y)
		for (int x = 0; x < HW; ++x) px[y * HW + x] = (x / 16 + y / 16) % 2 ? RGB(104, 168, 192) : RGB(112, 176, 200);
}

static void top_bar(Emu* e, const Layout* l) {
	fill(0, 0, HW - 1, TOP - 3, RGB(40, 56, 96));
	fill(0, TOP - 2, HW - 1, TOP - 1, RGB(24, 32, 56));
	/* the badges: eight gems, lit once won */
	int badges = badges_of(e, l);
	text(8, 4, "徽章", RGB(176, 192, 232), RGB(24, 32, 56), 1);
	for (int i = 0; i < 8; ++i) {
		int x = 36 + i * 16, y = 6;
		bool won = badges >> i & 1;
		uint32_t c = won ? RGB(248, 208, 64) : RGB(64, 80, 128), hi = won ? RGB(255, 248, 200) : RGB(88, 104, 152);
		fill(x + 3, y, x + 8, y + 11, c);
		fill(x, y + 3, x + 11, y + 8, c);
		fill(x + 1, y + 1, x + 10, y + 10, c);
		fill(x + 3, y + 2, x + 5, y + 4, hi);
	}
	/* the play time */
	uint32_t sb2 = sb2_of(e, l);
	if (sb2 >> 24 == 0x02) {
		int hours = emu_read16(e, sb2 + 0x0E), minutes = emu_read8(e, sb2 + 0x10);
		char t[24] = "⏱ ";
		number(t + strlen(t), hours);
		strcat(t, ":");
		if (minutes < 10) strcat(t, "0");
		number(t + strlen(t), minutes);
		text(180, 4, t, white, RGB(24, 32, 56), 2);
	}
	uint32_t sb1 = sb1_of(e, l), money = emu_read32(e, sb1 + l->moneyOff) ^ key_of(e, l);
	if (money <= 999999) {
		char m[20], d[12];
		number(d, (int) money);
		int n = 0, k = (int) strlen(d);
		for (int i = 0; i < k; ++i) {
			m[n++] = d[i];
			if ((k - 1 - i) && (k - 1 - i) % 3 == 0) m[n++] = ',';
		}
		strcpy(m + n, "元");
		text_right(HW - 8, 4, m, RGB(248, 232, 128), RGB(24, 32, 56), 2);
	}
}

static void tabs(void) {
	fill(0, TABS_Y - 2, HW - 1, HH - 1, RGB(32, 40, 64));
	for (int i = 0; i < N_PAGES; ++i) {
		int x = tab_x(i);
		bool on = ui.page == i, off = i == PAGE_BATTLE && !ui.inBattle;
		uint32_t c = off ? RGB(72, 80, 104) : on ? mix(pageColors[i], RGB(255, 255, 255), 15) : mix(pageColors[i], RGB(0, 0, 0), 30);
		panel(x, TABS_Y + (on ? 0 : 3), TAB_W - 4, HH - TABS_Y - (on ? 2 : 5), on ? white : mix(c, RGB(0, 0, 0), 35), c);
		text_mid(x + (TAB_W - 4) / 2, TABS_Y + (on ? 7 : 10), pageNames[i], off ? RGB(128, 136, 160) : white, mix(c, RGB(0, 0, 0), 45), 2);
	}
}

/* ------------------------------------------------------------------ 队伍 */

static int animFrame; /* the icons' two frames, in turn (as the games' own party screens bob them) */

static void party_card(Emu* e, const Mon* m, int i) {
	int pitch = 81 + EXTRA / 3;
	int x = 4 + (i % 2) * 238, y = TOP + 3 + (i / 2) * pitch + (pitch - 81) / 2, w = 234, h = 78;
	bool fainted = !m->egg && !m->hp;
	panel(x, y, w, h, i == 0 ? orange : navy, fainted ? RGB(208, 200, 192) : cream);
	icon_draw(e, m->egg ? 412 : m->species, x + 4, y + 7, 2, fainted ? 0 : animFrame, fainted ? 1 : 0);
	if (m->egg) {
		text(x + 76, y + 8, "蛋", ink, soft, 2);
		return;
	}
	name_and_gender(e, x + 76, y + 4, species_name(e, m->species), m->species, m->pid, w - 76 - 8);
	/* the level, and what it holds */
	char lv[8] = "Lv";
	number(lv + 2, m->level);
	int after = text(x + 76, y + 32, lv, ink, soft, 1) + 8;
	if (m->item > 0 && m->item < POKE_ITEMS && pokeItemNames[m->item][0]) {
		fill(after, y + 35, after + 5, y + 40, RGB(232, 176, 64));
		text_fit(after + 9, y + 32, pokeItemNames[m->item], RGB(112, 104, 96), 0, 1, x + w - 8 - after - 9);
	}
	hp_bar(x + 76, y + 48, x + w - 8, m->hp, m->maxHp);
	hp_numbers(x + w - 8, y + 62, m->hp, m->maxHp, 1);
	status_tag(x + 76, y + 61, m);
}

/* a party member in full (a tap on its card): its nature, ability, item, stats, moves and their PP */
static void party_summary(Emu* e, const Mon* m) {
	panel(4, TOP + 3, 472, 240, orange, cream);
	icon_draw(e, m->egg ? 412 : m->species, 14, TOP + 12, 2, animFrame, !m->egg && !m->hp ? 1 : 0);
	if (m->egg) {
		text(90, TOP + 30, "蛋", ink, soft, 2);
		return;
	}
	name_and_gender(e, 90, TOP + 10, species_name(e, m->species), m->species, m->pid, 200);
	char lv[8] = "Lv";
	number(lv + 2, m->level);
	text(90, TOP + 40, lv, ink, soft, 2);
	status_tag(160, TOP + 44, m);
	/* the left column: nature, ability, item, HP */
	int y = TOP + 84;
	text(14, y, "性格", RGB(120, 112, 104), 0, 1);
	text(56, y, pokeNatureNames[m->pid % 25], ink, 0, 1);
	y += 18;
	text(14, y, "特性", RGB(120, 112, 104), 0, 1);
	if (rom.baseStats) {
		int ab = emu_read8(e, rom.baseStats + m->species * 28 + 0x16 + m->abilityNum);
		if (!ab) ab = emu_read8(e, rom.baseStats + m->species * 28 + 0x16);
		if (ab > 0 && ab < 78 && pokeAbilityNames[ab][0]) text(56, y, pokeAbilityNames[ab], ink, 0, 1);
	}
	y += 18;
	text(14, y, "携带", RGB(120, 112, 104), 0, 1);
	if (m->item > 0 && m->item < POKE_ITEMS && pokeItemNames[m->item][0]) text_fit(56, y, pokeItemNames[m->item], ink, 0, 1, 170);
	y += 22;
	hp_bar(14, y, 226, m->hp, m->maxHp);
	hp_numbers(226, y + 14, m->hp, m->maxHp, 1);
	/* the right column: the stats */
	static const char* const statNames[5] = { "攻击", "防御", "速度", "特攻", "特防" };
	/* (shown in the games' order: attack, defence, sp. attack, sp. defence, speed) */
	static const int order[5] = { 1, 2, 4, 5, 3 };
	panel(244, TOP + 10, 224, 110, navy, RGB(236, 240, 248));
	for (int i = 0; i < 5; ++i) {
		int k = order[i];
		char v[8];
		number(v, m->stats[k]);
		text(256, TOP + 18 + i * 20, statNames[k - 1], RGB(80, 88, 112), 0, 1);
		/* a bar: up to 400 */
		int len = m->stats[k] * 120 / 400;
		if (len > 120) len = 120;
		fill(300, TOP + 22 + i * 20, 300 + len, TOP + 27 + i * 20, RGB(96, 152, 232));
		text_right(460, TOP + 18 + i * 20, v, ink, 0, 1);
	}
	/* the moves */
	for (int i = 0; i < 4; ++i) {
		int x = 8 + (i % 2) * 234, y2 = TOP + 166 + (i / 2) * 36, w = 230;
		if (!m->moves[i]) {
			panel(x, y2, w, 34, RGB(160, 168, 184), RGB(216, 220, 228));
			continue;
		}
		int t = move_type(e, m->moves[i]);
		uint32_t tc = t >= 0 ? typeColors[t] : RGB(128, 128, 136);
		panel(x, y2, w, 34, mix(tc, RGB(0, 0, 0), 30), mix(tc, RGB(255, 255, 255), 78));
		int tx = x + 6;
		if (t >= 0) tx = tag(tx, y2 + 9, pokeTypeNames[t], tc) + 6;
		const char* name = m->moves[i] < POKE_MOVES && pokeMoveNames[m->moves[i]][0] ? pokeMoveNames[m->moves[i]] : "？？？";
		text_fit(tx, y2 + 9, name, ink, 0, 1, x + w - 60 - tx);
		char pp[16];
		number(pp, m->pp[i]);
		int base = move_pp(e, m->moves[i]), bonus = (m->ppBonuses >> (i * 2)) & 3;
		if (base) {
			size_t n = strlen(pp);
			pp[n] = '/';
			number(pp + n + 1, base + base * bonus / 5);
		}
		text_right(x + w - 8, y2 + 9, pp, ink, 0, 1);
	}
}

/* ------------------------------------------------------------------ 对战 */

/* a battler's card: icon (2×), name and gender, level, HP; lit: the player's whose turn it is */
static void battler_card(Emu* e, const Battler* b, int x, int y, int w, bool lit, bool mine) {
	panel(x, y, w, 64, lit ? orange : navy, cream);
	icon_draw(e, b->m.species, x + 4, y + 1, 2, b->m.hp ? animFrame : 0, b->m.hp ? 0 : 1);
	int right = x + w - 8;
	name_and_gender(e, x + 72, y + 4, species_name(e, b->m.species), b->m.species, b->m.pid, w - 72 - 70);
	level(right, y + 4, b->m.level, 2);
	hp_bar(x + 72, y + 32, right, b->m.hp, b->m.maxHp);
	if (mine || w > 300) hp_numbers(right, y + 47, b->m.hp, b->m.maxHp, 1);
	status_tag(x + 72, y + 46, &b->m);
}

#define MOVE_Y 170
#define MOVE_W 177
#define MOVE_H 50
#define ACT_X 362

static const char* const actionNames[3] = { "背包", "宝可梦", "逃跑" };
static const uint32_t actionColors[3] = { RGB(80, 120, 216), RGB(72, 168, 96), RGB(120, 128, 152) };

/* the battle: bat[0..3] as the game numbers them (0, 2 the player's; 1, 3 the foe's), 2 or 4 of them;
 * `mine` the player's Pokémon whose moves are shown (0, or 2 when it's that one's turn) */
static void page_battle(Emu* e, const Battler* bat, int nBat, int mine, const Mon* team, int nTeam, bool canAct) {
	if (nBat == 4) {
		battler_card(e, &bat[1], 4, TOP + 3, 234, false, false);
		battler_card(e, &bat[3], 242, TOP + 3, 234, false, false);
		battler_card(e, &bat[0], 4, TOP + 70, 234, mine == 0, true);
		battler_card(e, &bat[2], 242, TOP + 70, 234, mine == 2, true);
	} else {
		bool trainer = nTeam > 1;
		battler_card(e, &bat[1], 4, TOP + 3, trainer ? 354 : 472, false, false);
		if (trainer) {
			/* the trainer's team, as the games show it: a ball each, grey once fainted */
			panel(362, TOP + 3, 114, 64, navy, RGB(232, 232, 220));
			for (int i = 0; i < 6; ++i)
				ball(370 + (i % 3) * 34, TOP + 9 + (i / 3) * 28, 24, i >= nTeam ? 2 : team[i].hp ? 0 : 1);
		}
		battler_card(e, &bat[0], 4, TOP + 70, 472, true, true);
	}
	/* the moves, two by two: name, type, how well it does against the foe, PP */
	const Battler* me = &bat[mine];
	const Battler* foe = &bat[nBat == 4 ? 1 : 1];
	for (int i = 0; i < 4; ++i) {
		int x = 4 + (i % 2) * (MOVE_W + 2), y = MOVE_Y + (i / 2) * (MOVE_H + 2), w = MOVE_W;
		if (!me->moves[i]) {
			panel(x, y, w, MOVE_H, RGB(88, 112, 152), RGB(144, 168, 200));
			continue;
		}
		int t = move_type(e, me->moves[i]);
		uint32_t tc = t >= 0 ? typeColors[t] : RGB(128, 128, 136);
		bool empty = !me->pp[i];
		panel(x, y, w, MOVE_H, mix(tc, RGB(0, 0, 0), 30), empty ? RGB(200, 200, 200) : mix(tc, RGB(255, 255, 255), 75));
		const char* name = me->moves[i] < POKE_MOVES && pokeMoveNames[me->moves[i]][0] ? pokeMoveNames[me->moves[i]] : "？？？";
		text_fit(x + 8, y + 5, name, ink, mix(tc, RGB(255, 255, 255), 50), 2, w - 14);
		int tx = x + 8;
		if (t >= 0) tx = tag(tx, y + 31, pokeTypeNames[t], tc) + 3;
		/* (status moves, power 0: no tag) */
		int power = rom.moves && me->moves[i] < POKE_MOVES ? emu_read8(e, rom.moves + me->moves[i] * 12 + 1) : 0;
		if (t >= 0 && power > 0 && nBat == 2 && foe->m.species > 0) {
			int f = effect(t, foe->type1, foe->type2);
			if (f >= 32) tag(tx, y + 31, "效果绝佳", RGB(208, 64, 48));
			else if (f == 0) tag(tx, y + 31, "没有效果", RGB(72, 72, 80));
			else if (f < 16) tag(tx, y + 31, "效果不好", RGB(128, 128, 144));
		}
		char pp[16];
		number(pp, me->pp[i]);
		if (me->maxPp[i]) {
			size_t n = strlen(pp);
			pp[n] = '/';
			number(pp + n + 1, me->maxPp[i]);
		}
		bool low = me->maxPp[i] ? me->pp[i] * 4 <= me->maxPp[i] : me->pp[i] == 0;
		text_right(x + w - 8, y + 33, pp, low ? RGB(200, 64, 48) : ink, 0, 1);
	}
	/* the other actions: the game's own bag, Pokémon and run, as its menu would */
	for (int i = 0; i < 3; ++i) {
		int y = MOVE_Y + i * 35;
		uint32_t c = canAct ? actionColors[i] : RGB(128, 136, 152);
		panel(ACT_X, y, 114, 33, mix(c, RGB(0, 0, 0), 35), c);
		text_mid(ACT_X + 57, y + 5, actionNames[i], white, mix(c, RGB(0, 0, 0), 45), 2);
	}
}

/* ------------------------------------------------------------------ 图鉴 */

#define DEX_N 386
#define DEX_COLS 9
#define DEX_ROWS (4 + EXTRA / DEX_CH)
#define DEX_PER (DEX_COLS * DEX_ROWS)
#define DEX_Y (TOP + 34)
#define DEX_CW 52
#define DEX_CH 50

static bool dex_bit(Emu* e, uint32_t base, int nat) {
	return (emu_read8(e, base + (nat - 1) / 8) >> ((nat - 1) % 8)) & 1;
}

/* the internal species of a national number (0: none found) */
static int species_of_national(Emu* e, int nat) {
	static int cache[POKE_NATIONAL + 1];
	static uint32_t cacheFor;
	if (cacheFor != rom.crc) {
		memset(cache, 0, sizeof(cache));
		cacheFor = rom.crc;
		for (int s = 1; s <= 440; ++s) {
			int n = national_of(e, s);
			if (n > 0 && n <= POKE_NATIONAL && !cache[n]) cache[n] = s;
		}
	}
	return nat > 0 && nat <= POKE_NATIONAL ? cache[nat] : 0;
}

static void page_dex(Emu* e, const Layout* l, uint32_t* key) {
	uint32_t sb2 = sb2_of(e, l);
	if (sb2 >> 24 != 0x02) return;
	uint32_t owned = sb2 + 0x28, seen = sb2 + 0x5C;
	int nOwned = 0, nSeen = 0;
	for (int i = 0; i < 49; ++i) {
		int o = emu_read8(e, owned + i), s = emu_read8(e, seen + i);
		*key = (*key ^ (uint32_t) (o | s << 8)) * 16777619u;
		for (int b = 0; b < 8; ++b) nOwned += o >> b & 1, nSeen += s >> b & 1;
	}
	int pages = (DEX_N + DEX_PER - 1) / DEX_PER;
	if (ui.dexPage < 0 || ui.dexPage >= pages) ui.dexPage = 0;
	*key = (*key ^ (uint32_t) (ui.dexPage | ui.dexPick << 8 | animFrame << 20)) * 16777619u;
	if (!px) return;
	/* the counts, or the one picked; the page's arrows */
	panel(4, TOP + 3, 472, 30, navy, RGB(80, 104, 168));
	if (ui.dexPick > 0) {
		char s[64] = "No.";
		if (ui.dexPick < 100) strcat(s, "0");
		if (ui.dexPick < 10) strcat(s, "0");
		number(s + strlen(s), ui.dexPick);
		strcat(s, " ");
		strcat(s, pokeNatNames[ui.dexPick]);
		strcat(s, dex_bit(e, owned, ui.dexPick) ? "  已捕获" : dex_bit(e, seen, ui.dexPick) ? "  已见" : "");
		text(14, TOP + 6, dex_bit(e, seen, ui.dexPick) ? s : "No.？？？", white, RGB(40, 56, 96), 2);
	} else {
		char s[48] = "已捕获 ";
		number(s + strlen(s), nOwned);
		strcat(s, "  已见 ");
		number(s + strlen(s), nSeen);
		text(14, TOP + 6, s, white, RGB(40, 56, 96), 2);
	}
	char pg[16];
	number(pg, ui.dexPage + 1);
	strcat(pg, "/");
	number(pg + strlen(pg), pages);
	text_mid(398, TOP + 9, pg, RGB(208, 216, 240), 0, 1);
	for (int d = 0; d < 2; ++d) {
		int ax = d ? 444 : 336;
		panel(ax, TOP + 6, 28, 24, RGB(32, 40, 72), RGB(64, 88, 152));
		for (int i = 0; i < 7; ++i) fill(d ? ax + 18 - i : ax + 10 + i, TOP + 18 - i, d ? ax + 18 - i : ax + 10 + i, TOP + 18 + i, white);
	}
	for (int i = 0; i < DEX_PER; ++i) {
		int nat = ui.dexPage * DEX_PER + i + 1;
		if (nat > DEX_N) break;
		int x = 6 + (i % DEX_COLS) * DEX_CW, y = DEX_Y + (i / DEX_COLS) * DEX_CH;
		bool o = dex_bit(e, owned, nat), s = dex_bit(e, seen, nat);
		uint32_t face = nat == ui.dexPick ? RGB(248, 216, 160) : o ? cream : s ? RGB(208, 216, 224) : RGB(168, 184, 200);
		panel(x, y, DEX_CW - 4, DEX_CH - 4, nat == ui.dexPick ? orange : navy, face);
		char n[8];
		number(n, nat);
		text(x + 4, y + 3, n, o ? RGB(160, 80, 64) : RGB(104, 112, 128), 0, 1);
		int sp = s ? species_of_national(e, nat) : 0;
		if (sp) icon_draw(e, sp, x + 8, y + 10, 1, o ? animFrame : 0, o ? 0 : 2);
	}
}

/* ------------------------------------------------------------------ 地图
 *
 * The map the player is on, drawn from the game's own tiles as the game draws it (gMapHeader's layout: its
 * blocks, each a metatile of the primary or secondary tileset — two layers of 2 × 2 tiles; the tiles,
 * LZ77-compressed or not, and the palettes from the ROM), the player a red dot. A map that fits at half its
 * size or more is shown whole; a larger one at half size round the player. */

typedef struct {
	uint32_t at;        /* the tileset (GBA address); 0: none */
	uint8_t tiles[1024 * 32];
	uint32_t pal[16][16];
	uint32_t metatiles; /* GBA address */
} TileSet;
static TileSet tsets[2];

/* the GBA BIOS's LZ77 (type 0x10) from ROM offset o into out (most bytes); false if it isn't one */
static bool lz77(uint32_t o, uint8_t* out, size_t most) {
	const uint8_t* r = rom.bytes;
	if (!r || o + 4 > rom.size || r[o] != 0x10) return false;
	size_t size = r[o + 1] | r[o + 2] << 8 | r[o + 3] << 16, n = 0;
	if (size > most) size = most;
	o += 4;
	while (n < size && o < rom.size) {
		uint8_t flags = r[o++];
		for (int b = 0; b < 8 && n < size && o < rom.size; ++b, flags <<= 1) {
			if (flags & 0x80) {
				if (o + 1 >= rom.size) return false;
				int len = (r[o] >> 4) + 3, disp = ((r[o] & 15) << 8 | r[o + 1]) + 1;
				o += 2;
				if ((size_t) disp > n) return false;
				for (int i = 0; i < len && n < size; ++i, ++n) out[n] = out[n - disp];
			} else {
				out[n++] = r[o++];
			}
		}
	}
	return true;
}

static TileSet* tileset_load(Emu* e, uint32_t at, int slot) {
	TileSet* t = &tsets[slot];
	if (t->at == at) return t;
	t->at = 0;
	if (at >> 24 != 0x08 || !rom.bytes) return NULL;
	bool compressed = emu_read8(e, at);
	uint32_t tiles = emu_read32(e, at + 4), pals = emu_read32(e, at + 8);
	t->metatiles = emu_read32(e, at + 12);
	if (tiles >> 24 != 0x08 || pals >> 24 != 0x08 || t->metatiles >> 24 != 0x08) return NULL;
	memset(t->tiles, 0, sizeof(t->tiles));
	if (compressed) {
		if (!lz77(tiles - 0x08000000, t->tiles, sizeof(t->tiles))) return NULL;
	} else {
		uint32_t o = tiles - 0x08000000;
		size_t n = o + sizeof(t->tiles) <= rom.size ? sizeof(t->tiles) : rom.size - o;
		memcpy(t->tiles, rom.bytes + o, n);
	}
	for (int p = 0; p < 16; ++p)
		for (int c = 0; c < 16; ++c) t->pal[p][c] = bgr555(emu_read16(e, pals + p * 32 + c * 2));
	t->at = at;
	return t;
}

#define MAP_Y (TOP + 3)
#define MAP_H (242 + EXTRA)

/* the map drawn without the player, kept until the map or the view changes (drawing it is the slow part:
 * the dot is drawn over a copy each time) */
static uint32_t mapBase[472 * (242 + HH_MAX - 320)];
static struct {
	uint32_t layout;
	int vx, vy, ox, oy, dw, dh;
	double sc;
	int ah;
	bool valid;
} mapCache;

static uint16_t rom16(uint32_t a) {
	uint32_t o = a - 0x08000000;
	return o + 1 < rom.size ? (uint16_t) (rom.bytes[o] | rom.bytes[o + 1] << 8) : 0;
}

static void page_map(Emu* e, const Layout* l, uint32_t* key) {
	uint32_t layout = emu_read32(e, l->mapHeader);
	if (layout >> 24 != 0x08 || !rom.bytes) return;
	int mapW = (int) emu_read32(e, layout), Hb = (int) emu_read32(e, layout + 4);
	uint32_t blocks = emu_read32(e, layout + 12), p0 = emu_read32(e, layout + 16), p1 = emu_read32(e, layout + 20);
	if (mapW <= 0 || Hb <= 0 || mapW > 1024 || Hb > 1024 || blocks >> 24 != 0x08) return;
	uint32_t sb1 = sb1_of(e, l);
	int pxb = (int16_t) emu_read16(e, sb1), pyb = (int16_t) emu_read16(e, sb1 + 2);
	*key = (*key ^ layout) * 16777619u;
	*key = (*key ^ ((uint32_t) pxb << 16 | ((uint32_t) pyb & 0xFFFF))) * 16777619u;
	*key = (*key ^ (uint32_t) animFrame) * 16777619u;
	if (!px) return;
	/* (another game: nothing kept) */
	static uint32_t forCrc;
	if (forCrc != rom.crc) forCrc = rom.crc, mapCache.valid = false, tsets[0].at = tsets[1].at = 0;
	int aw = 472, ah = MAP_H, ax = 4, ay = MAP_Y;
	fill(ax - 2, ay - 2, ax + aw + 1, ay + ah + 1, navy);
	/* the scale (map pixels to page pixels): whole if at half size or more, else half round the player */
	int fw = mapW * 16, fh = Hb * 16;
	double sc = (double) aw / fw < (double) ah / fh ? (double) aw / fw : (double) ah / fh;
	if (sc > 1.5) sc = 1.5;
	int vx = 0, vy = 0;
	int dw = (int) (fw * sc), dh = (int) (fh * sc);
	if (sc < 0.5) {
		sc = 0.5;
		/* (a side that fits at half size: whole and centred) */
		dw = fw / 2 < aw ? fw / 2 : aw, dh = fh / 2 < ah ? fh / 2 : ah;
		/* (the view moves a block at a time: redrawn only then) */
		vx = (pxb * 16 + 8 - (int) (aw / sc / 2)) / 16 * 16, vy = (pyb * 16 + 8 - (int) (ah / sc / 2)) / 16 * 16;
		if (vx > fw - (int) (aw / sc)) vx = fw - (int) (aw / sc);
		if (vy > fh - (int) (ah / sc)) vy = fh - (int) (ah / sc);
		if (vx < 0) vx = 0;
		if (vy < 0) vy = 0;
	}
	int ox = (aw - dw) / 2, oy = (ah - dh) / 2;
	if (!(mapCache.valid && mapCache.layout == layout && mapCache.vx == vx && mapCache.vy == vy && mapCache.sc == sc && mapCache.ah == ah)) {
		TileSet* t0 = tileset_load(e, p0, 0);
		TileSet* t1 = tileset_load(e, p1, 1);
		if (!t0 || !t1) return;
		for (int i = 0; i < aw * ah; ++i) mapBase[i] = RGB(24, 32, 48);
		int PT = l->primaryTiles, PP = l->primaryPals;
		for (int y = 0; y < dh; ++y) {
			int my = (int) (vy + y / sc);
			if (my >= fh) break;
			for (int x = 0; x < dw; ++x) {
				int mx = (int) (vx + x / sc);
				if (mx >= fw) break;
				int id = rom16(blocks + ((my / 16) * mapW + mx / 16) * 2) & 0x3FF;
				uint32_t meta = id < PT ? t0->metatiles + id * 16 : t1->metatiles + (id - PT) * 16;
				int sub = ((my % 16) / 8) * 2 + (mx % 16) / 8, tx0 = mx % 8, ty0 = my % 8;
				uint32_t c = t0->pal[0][0];
				for (int layer = 0; layer < 2; ++layer) {
					int en = rom16(meta + (layer * 4 + sub) * 2);
					int tile = en & 0x3FF, pal = en >> 12;
					int tx = en & 0x400 ? 7 - tx0 : tx0, ty = en & 0x800 ? 7 - ty0 : ty0;
					const TileSet* ts = tile < PT ? t0 : t1;
					int ti = tile < PT ? tile : tile - PT;
					if (ti >= 1024) continue;
					int v = ts->tiles[ti * 32 + ty * 4 + tx / 2];
					v = tx & 1 ? v >> 4 : v & 15;
					if (!v) continue; /* (see-through: the layer below, or the backdrop) */
					c = (pal < PP ? t0 : t1)->pal[pal][v];
				}
				mapBase[(oy + y) * aw + ox + x] = c;
			}
		}
		mapCache = (typeof(mapCache)) { layout, vx, vy, ox, oy, dw, dh, sc, ah, true };
	}
	for (int y = 0; y < ah; ++y) memcpy(px + (ay + y) * HW + ax, mapBase + y * aw, (size_t) aw * 4);
	/* the player: a red dot with a white ring, pulsing */
	int cx = ax + ox + (int) ((pxb * 16 + 8 - vx) * sc), cy = ay + oy + (int) ((pyb * 16 + 8 - vy) * sc);
	int r = animFrame ? 7 : 6;
	for (int dy = -r - 2; dy <= r + 2; ++dy)
		for (int dx = -r - 2; dx <= r + 2; ++dx) {
			int d = dx * dx + dy * dy;
			/* (only on the map: a player off its edge, a warp's first frames, isn't drawn on the bars) */
			if (cx + dx < ax || cx + dx >= ax + aw || cy + dy < ay || cy + dy >= ay + ah) continue;
			if (d <= r * r) put(cx + dx, cy + dy, RGB(232, 48, 48));
			else if (d <= (r + 2) * (r + 2)) put(cx + dx, cy + dy, white);
		}
}

/* ------------------------------------------------------------------ 箱子 */

typedef struct {
	int species; /* 0: an empty place */
	bool egg;
	uint32_t exp, pid;
	int item;
	int moves[4], abilityNum;
} Boxed;

/* a boxed Pokémon (the party's first 80 bytes); false: not whole */
static bool boxed_decode(const uint8_t* b, Boxed* m) {
	memset(m, 0, sizeof(*m));
	if (!le32(b) && !le32(b + 4)) return true;
	uint32_t pid = le32(b), key = pid ^ le32(b + 4);
	uint8_t d[48];
	uint16_t sum = 0;
	for (int i = 0; i < 48; i += 4) {
		uint32_t v = le32(b + 32 + i) ^ key;
		d[i] = v, d[i + 1] = v >> 8, d[i + 2] = v >> 16, d[i + 3] = v >> 24;
		sum += (uint16_t) (v + (v >> 16));
	}
	if (sum != le16(b + 28)) return false;
	const char* o = orders[pid % 24];
	const uint8_t *g = NULL, *misc = NULL, *at = NULL;
	for (int i = 0; i < 4; ++i) {
		if (o[i] == 'G') g = d + i * 12;
		if (o[i] == 'M') misc = d + i * 12;
		if (o[i] == 'A') at = d + i * 12;
	}
	m->species = le16(g);
	m->item = le16(g + 2);
	m->exp = le32(g + 4);
	m->pid = pid;
	m->egg = (le32(misc + 4) >> 30) & 1;
	m->abilityNum = (le32(misc + 4) >> 31) & 1;
	for (int i = 0; i < 4; ++i) m->moves[i] = le16(at + i * 2);
	return m->species > 0 && m->species <= 2047;
}

static uint32_t storage_of(Emu* e, const Layout* l) {
	return l->storagePtr ? emu_read32(e, l->storagePtr) : l->storage;
}

#define BOX_X 6
#define BOX_Y (TOP + 34)
#define BOX_CW 46
#define BOX_CH 40

static void page_box(Emu* e, const Layout* l, uint32_t* key) {
	uint32_t st = storage_of(e, l);
	if (st >> 24 != 0x02) return;
	int current = emu_read8(e, st);
	if (ui.box < 0 || ui.box > 13) ui.box = current < 14 ? current : 0;
	Boxed mons[30];
	int count = 0;
	for (int i = 0; i < 30; ++i) {
		uint8_t raw[80];
		read_bytes(e, st + 4 + (ui.box * 30 + i) * 80, raw, 80);
		for (int j = 0; j < 80; j += 4) *key = (*key ^ le32(raw + j)) * 16777619u;
		if (!boxed_decode(raw, &mons[i])) mons[i].species = -1;
		if (mons[i].species > 0) ++count;
	}
	*key = (*key ^ (uint32_t) (ui.box | ui.boxPick << 8 | animFrame << 16)) * 16777619u;
	if (!px) return;
	/* the box's bar: ◀ 箱子 N ▶, how many */
	panel(4, TOP + 3, 280, 30, navy, RGB(64, 128, 168));
	char name[24] = "箱子 ";
	number(name + strlen(name), ui.box + 1);
	text_mid(120, TOP + 6, name, white, RGB(32, 64, 96), 2);
	for (int d = 0; d < 2; ++d) {
		int ax = d ? 252 : 8;
		for (int i = 0; i < 8; ++i) fill(d ? ax + 18 - i : ax + 6 + i, TOP + 18 - i, d ? ax + 18 - i : ax + 6 + i, TOP + 18 + i, white);
	}
	char cnt[12];
	number(cnt, count);
	strcat(cnt, "/30");
	/* the grid: 6 by 5 */
	panel(BOX_X - 2, BOX_Y - 2, 6 * BOX_CW + 4, 5 * BOX_CH + 4, navy, RGB(220, 228, 220));
	for (int i = 0; i < 30; ++i) {
		int x = BOX_X + (i % 6) * BOX_CW, y = BOX_Y + (i / 6) * BOX_CH;
		if (i == ui.boxPick) fill(x + 1, y + 1, x + BOX_CW - 2, y + BOX_CH - 2, RGB(248, 200, 120));
		if (mons[i].species > 0) icon_draw(e, mons[i].egg ? 412 : mons[i].species, x + 7, y + 3, 1, i == ui.boxPick ? animFrame : 0, 0);
	}
	text_right(276 - 24, TOP + 11, cnt, RGB(208, 224, 240), 0, 1);
	/* the one picked: icon, name, gender, level, item */
	panel(288, TOP + 3, 188, 242, navy, cream);
	const Boxed* m = &mons[ui.boxPick];
	if (m->species <= 0) {
		text_mid(382, TOP + 110, "空", RGB(144, 144, 152), 0, 2);
		return;
	}
	icon_draw(e, m->egg ? 412 : m->species, 296, TOP + 8, 2, animFrame, 0);
	if (m->egg) {
		text(364, TOP + 26, "蛋", ink, soft, 2);
		return;
	}
	name_and_gender(e, 364, TOP + 10, species_name(e, m->species), m->species, m->pid, 108);
	int lv = level_of(e, m->species, m->exp);
	if (lv) {
		char s[8] = "Lv";
		number(s + 2, lv);
		text(364, TOP + 40, s, ink, soft, 2);
	}
	/* nature, ability, item; its moves */
	int y = TOP + 80;
	text(298, y, "性格", RGB(120, 112, 104), 0, 1);
	text(336, y, pokeNatureNames[m->pid % 25], ink, 0, 1);
	y += 18;
	text(298, y, "特性", RGB(120, 112, 104), 0, 1);
	if (rom.baseStats && m->species < 2048) {
		int ab = emu_read8(e, rom.baseStats + m->species * 28 + 0x16 + m->abilityNum);
		if (!ab) ab = emu_read8(e, rom.baseStats + m->species * 28 + 0x16);
		if (ab > 0 && ab < 78) text_fit(336, y, pokeAbilityNames[ab], ink, 0, 1, 132);
	}
	y += 18;
	text(298, y, "携带", RGB(120, 112, 104), 0, 1);
	if (m->item > 0 && m->item < POKE_ITEMS && pokeItemNames[m->item][0]) text_fit(336, y, pokeItemNames[m->item], ink, 0, 1, 132);
	y += 24;
	for (int i = 0; i < 4 && m->moves[i]; ++i) {
		int t = move_type(e, m->moves[i]);
		uint32_t tc = t >= 0 ? typeColors[t] : RGB(128, 128, 136);
		panel(294, y + i * 26, 176, 24, mix(tc, RGB(0, 0, 0), 30), mix(tc, RGB(255, 255, 255), 78));
		const char* name = m->moves[i] < POKE_MOVES && pokeMoveNames[m->moves[i]][0] ? pokeMoveNames[m->moves[i]] : "？？？";
		text_fit(302, y + i * 26 + 6, name, ink, 0, 1, 160);
	}
}

/* ------------------------------------------------------------------ 背包 */

static const int pocketX[5] = { 4, 64, 148, 208, 292 };

/* a pocket's items as the bag lists them (its places in order, the empty ones left out); how many */
static int pocket_list(Emu* e, const Layout* l, int pocket, int* ids, int* qty, uint32_t* key) {
	uint32_t sb1 = sb1_of(e, l), k = key_of(e, l) & 0xFFFF;
	const Pocket* p = &l->pockets[pocket];
	int n = 0;
	for (int i = 0; i < p->n && i < 64; ++i) {
		uint32_t v = emu_read32(e, sb1 + p->off + i * 4);
		int id = v & 0xFFFF, q = (int) ((v >> 16) ^ k);
		if (key) *key = (*key ^ v) * 16777619u;
		if (!id) continue;
		ids[n] = id, qty[n] = q, ++n;
	}
	return n;
}

static void page_bag(Emu* e, const Layout* l, uint32_t* key) {
	int ids[64], qty[64], n = pocket_list(e, l, ui.pocket, ids, qty, key);
	*key = (*key ^ (uint32_t) (ui.pocket | ui.bagStart << 4 | (ui.bagPick + 1) << 12)) * 16777619u;
	if (!px) return;
	for (int i = 0; i < 5; ++i) {
		int x = pocketX[i], w = text_width(pocketNames[i], 2) + 12;
		bool on = ui.pocket == i;
		panel(x, TOP + 3, w, 32, on ? orange : navy, on ? cream : RGB(80, 104, 168));
		text(x + 6, TOP + 7, pocketNames[i], on ? ink : white, on ? soft : RGB(40, 56, 96), 2);
	}
	int ROWS = 7 + EXTRA / 28, PER = ROWS * 2;
	if (ui.bagStart >= n) ui.bagStart = 0;
	panel(4, TOP + 38, 472, 206 + EXTRA, navy, cream);
	if (!n) {
		text_mid(HW / 2, TOP + 128 + EXTRA / 2, "空", RGB(144, 144, 152), 0, 2);
		return;
	}
	int shown = n - ui.bagStart > PER ? PER - 1 : n - ui.bagStart;
	bool keyItem = ui.pocket == 4;
	for (int j = 0; j < shown; ++j) {
		int i = ui.bagStart + j, x = 12 + (j / ROWS) * 234, y = TOP + 44 + (j % ROWS) * 28;
		const char* name = ids[i] < POKE_ITEMS && pokeItemNames[ids[i]][0] ? pokeItemNames[ids[i]] : "？？？";
		char q[12] = "×";
		number(q + strlen(q), qty[i]);
		int qw = keyItem ? 0 : text_width(q, 2) + 8;
		if (i == ui.bagPick) panel(x - 5, y - 2, 230, 27, orange, RGB(248, 216, 160));
		text_fit(x, y, name, ink, soft, 2, 222 - qw);
		if (!keyItem) text_right(x + 222, y, q, ink, soft, 2);
	}
	if (shown < n - ui.bagStart) {
		char more[24] = "还有";
		number(more + strlen(more), n - ui.bagStart - shown);
		strcat(more, "个…");
		text(12 + 234, TOP + 44 + (ROWS - 1) * 28, more, orange, soft, 2);
	}
}

/* ------------------------------------------------------------------ a tap in a battle */

/* Played as the player would on the game's own menus — a move: on the action menu FIGHT chosen (its
 * cursor put on it) and A, on the move menu the move's place and A (in a double battle, one more A: the
 * target the game offers); 背包 / 宝可梦 / 逃跑: the action menu's cursor put on it and A — only while the game
 * is on one of those menus for that Pokémon; anything else (a message, an animation) and it waits, given up
 * after three seconds. */
static struct {
	int move;    /* 0-3 a move, 4-6 the action menu's BAG / POKéMON / RUN; -1: none */
	int battler; /* 0 or 2 */
	int frames;  /* since the tap */
	int press;   /* frames left of an A being pressed (then let go) */
	int stage;   /* 0: menus; 1: the last A pressed; 2: the target's A (doubles) */
	int wait;    /* frames before the target's A */
	bool doubles;
} act = { -1, 0, 0, 0, 0, 0, false };
static uint32_t injected;

uint32_t poke_keys(void) {
	return injected;
}

static bool is_action_menu(uint32_t f) {
	return f && (f == rom.actionFn || f == rom.actionFn2);
}

static bool is_move_menu(uint32_t f) {
	return f && (f == rom.moveFn || f == rom.moveFn2);
}

/* the player's Pokémon (0 or 2) whose turn the menus are for; -1: neither's */
static int menu_battler(Emu* e, const Layout* l, int nBat) {
	for (int b = 0; b < nBat; b += 2) {
		uint32_t f = emu_read32(e, l->controller + b * 4);
		if (is_action_menu(f) || is_move_menu(f)) return b;
	}
	return -1;
}

static void battle_act(Emu* e, const Layout* l, const Battler* bat) {
	injected = 0;
	if (act.move < 0) return;
	if (++act.frames > 180 || !ui.inBattle || !rom.actionFn || !rom.moveFn) {
		act.move = -1;
		return;
	}
	if (act.press > 0) {
		/* A down three frames, up three */
		injected = act.press > 3 ? K_A : 0;
		if (--act.press == 0) {
			if (act.stage == 1 && act.doubles && act.move < 4) act.stage = 2, act.wait = 15; /* (the target menu: its A after a moment) */
			else if (act.stage >= 1) act.move = -1;
		}
		return;
	}
	if (act.wait > 0) {
		if (--act.wait == 0) act.press = 6, injected = K_A;
		return;
	}
	uint32_t f = emu_read32(e, l->controller + act.battler * 4);
	if (is_action_menu(f) && act.stage == 0) {
		if (act.move >= 4) {
			emu_write8(e, l->actionCursor + act.battler, (uint8_t) (act.move - 3));
			act.press = 6, act.stage = 1, injected = K_A;
		} else {
			emu_write8(e, l->actionCursor + act.battler, 0);
			act.press = 6, injected = K_A;
		}
	} else if (is_move_menu(f) && act.stage == 0) {
		if (act.move >= 4) {
			/* (on the move menu: back to the action menu first) */
			act.press = 0, injected = K_B;
			return;
		}
		const Battler* me = &bat[act.battler];
		if (!me->moves[act.move] || !me->pp[act.move]) {
			act.move = -1;
			return;
		}
		emu_write8(e, l->moveCursor + act.battler, (uint8_t) act.move);
		act.press = 6, act.stage = 1, injected = K_A;
	}
}

/* ------------------------------------------------------------------ 背包 → the game's own bag
 *
 * A tap on an item opens the game's bag on it, its cursor there, for the player's A: in a battle by the action
 * menu's BAG (as a tapped action), on the overworld by START and the start menu's bag entry. The bag's
 * remembered place is written first (it opens where it was left). Nothing is used for the player. */
static struct {
	int stage; /* 0: none; 1: START to press; 2: the start menu awaited; 3: its cursor to the bag; 4: A pressed */
	int frames, press, settle, entry;
	uint32_t keys, lastCb;
	int gp, idx, count;
} bagGo;

static void write_n(Emu* e, uint32_t a, int size, int v) {
	if (size == 1) emu_write8(e, a, (uint8_t) v);
	else emu_write16(e, a, (uint16_t) v);
}

static void bag_place(Emu* e, const Layout* l, int gp, int idx, int count) {
	int total = count + 1; /* (the list's last row: 取消) */
	int maxAbove = total > l->bagRows ? total - l->bagRows : 0;
	/* (on the first screen: no scrolling; further down: as far as it goes, the item at the top) */
	int above = idx < l->bagRows - 1 ? 0 : idx < maxAbove ? idx : maxAbove;
	write_n(e, l->bagPocket, l->bagPocketSize, gp);
	write_n(e, l->bagCursor + (uint32_t) (gp * l->bagStride), l->bagFieldSize, idx - above);
	write_n(e, l->bagAbove + (uint32_t) (gp * l->bagStride), l->bagFieldSize, above);
}

static void bag_open_on(Emu* e, const Layout* l, int pocket, int i, int mine) {
	if (!l || !l->bagPocket) return;
	int ids[64], qty[64];
	int gp = l->gamePocket[pocket], idx = i, from = pocket;
	if (gp < 0) {
		/* (FRLG's berries and TMs: their key item, 树果袋 / 学习器盒) */
		int n4 = pocket_list(e, l, 4, ids, qty, NULL);
		idx = -1;
		for (int j = 0; j < n4; ++j)
			if (ids[j] == l->keyItemFor[pocket]) idx = j;
		if (idx < 0) return;
		from = 4, gp = l->gamePocket[4];
	}
	int count = pocket_list(e, l, from, ids, qty, NULL);
	if (idx < 0 || idx >= count) return;
	if (ui.inBattle) {
		/* (key items aren't a battle's) */
		if (from == 4 || !rom.actionFn || act.move >= 0) return;
		bag_place(e, l, gp, idx, count);
		act = (typeof(act)) { 4, mine, 0, 0, 0, 0, act.doubles };
		return;
	}
	if (bagGo.stage || !rom.overworld) return;
	bagGo = (typeof(bagGo)) { .stage = 1, .gp = gp, .idx = idx, .count = count };
}

static void bag_go(Emu* e, const Layout* l) {
	injected = 0;
	if (!bagGo.stage) return;
	if (++bagGo.frames > 150) {
		bagGo.stage = 0;
		return;
	}
	if (bagGo.press > 0) {
		injected = bagGo.press > 3 ? bagGo.keys : 0;
		if (--bagGo.press == 0 && bagGo.stage == 4) bagGo.stage = 0;
		return;
	}
	if (bagGo.stage == 1) {
		/* only on the overworld with the start menu closed (a message, a script: START does nothing, and the
		 * wait below runs out) */
		if (emu_read32(e, l->gMain + 4) != rom.overworld || emu_read32(e, l->startCb)) {
			bagGo.stage = 0;
			return;
		}
		bag_place(e, l, bagGo.gp, bagGo.idx, bagGo.count);
		bagGo.keys = K_START, bagGo.press = 6, injected = K_START, bagGo.stage = 2;
		return;
	}
	if (bagGo.stage == 2) {
		uint32_t cb = emu_read32(e, l->startCb);
		if (!cb || cb != bagGo.lastCb) {
			bagGo.lastCb = cb, bagGo.settle = 0;
			return;
		}
		if (++bagGo.settle < 6) return;
		int n = emu_read8(e, l->startCount);
		bagGo.entry = -1;
		for (int j = 0; j < n && j < 10; ++j)
			if (emu_read8(e, l->startOrder + (uint32_t) j) == l->bagId) bagGo.entry = j;
		if (bagGo.entry < 0) {
			bagGo.stage = 0;
			return;
		}
		bagGo.stage = 3;
	}
	int cur = emu_read8(e, l->startCursor);
	bagGo.keys = cur == bagGo.entry ? K_A : cur < bagGo.entry ? K_DOWN : K_UP;
	if (bagGo.keys == K_A) bagGo.stage = 4;
	bagGo.press = 6, injected = bagGo.keys;
}

/* a tap: on a page's tab, a move or action, a box's place or arrow, the Pokédex, a pocket, an item */
static void take_tap(Emu* e, const Battler* bat, int mine) {
	if (!ui.tapped) return;
	ui.tapped = false;
	int x = ui.tapX, y = ui.tapY;
	if (y >= TABS_Y) {
		int i = (x - 5) / TAB_W;
		if (i >= 0 && i < N_PAGES && !(i == PAGE_BATTLE && !ui.inBattle)) ui.page = i, ui.summary = -1;
		return;
	}
	/* (the pages drawn centred in a taller one: in their own rows) */
	if (ui.page == PAGE_BATTLE || ui.page == PAGE_BOX) y -= EXTRA / 2;
	if (ui.page == PAGE_PARTY) {
		/* a card: that one in full; again (anywhere): back to the party */
		if (ui.summary >= 0) ui.summary = -1;
		else if (y >= TOP + 3) {
			int c = x >= 240, r = (y - TOP - 3) / (81 + EXTRA / 3);
			if (r < 3) ui.summary = r * 2 + c;
		}
		return;
	}
	if (ui.page == PAGE_BATTLE && y >= MOVE_Y && rom.actionFn && rom.moveFn && act.move < 0) {
		if (x >= ACT_X) {
			int i = (y - MOVE_Y) / 35;
			if (i < 3) act = (typeof(act)) { 4 + i, mine, 0, 0, 0, 0, act.doubles };
			return;
		}
		int i = (y - MOVE_Y) / (MOVE_H + 2) * 2 + (x >= 4 + MOVE_W + 2);
		const Battler* me = &bat[mine];
		if (i < 4 && me->moves[i] && me->pp[i]) act = (typeof(act)) { i, mine, 0, 0, 0, 0, act.doubles };
		return;
	}
	if (ui.page == PAGE_DEX) {
		if (y < TOP + 34) {
			if (x >= 444) ui.dexPage++, ui.dexPick = 0;
			else if (x >= 336) ui.dexPage = ui.dexPage > 0 ? ui.dexPage - 1 : (DEX_N - 1) / DEX_PER, ui.dexPick = 0;
			else ui.dexPick = 0;
			return;
		}
		int c = (x - 6) / DEX_CW, r = (y - DEX_Y) / DEX_CH;
		if (x >= 6 && y >= DEX_Y && c < DEX_COLS && r < DEX_ROWS) {
			int nat = ui.dexPage * DEX_PER + r * DEX_COLS + c + 1;
			ui.dexPick = nat <= DEX_N && nat != ui.dexPick ? nat : 0;
		}
		(void) e;
		return;
	}
	if (ui.page == PAGE_BOX) {
		if (y < TOP + 34 && x < 284) {
			ui.box = (ui.box + (x < 144 ? 13 : 1)) % 14;
			return;
		}
		int c = (x - BOX_X) / BOX_CW, r = (y - BOX_Y) / BOX_CH;
		if (x >= BOX_X && y >= BOX_Y && c < 6 && r < 5) ui.boxPick = r * 6 + c;
		return;
	}
	if (ui.page == PAGE_BAG) {
		if (y < TOP + 36) {
			for (int i = 4; i >= 0; --i)
				if (x >= pocketX[i]) {
					if (ui.pocket == i) ui.bagStart += (7 + EXTRA / 28) * 2 - 1;
					else ui.pocket = i, ui.bagStart = 0;
					ui.bagPick = -1;
					return;
				}
		}
		/* an item: the game's bag opened on it; elsewhere on the list (还有…个): its next items */
		int ids[64], qty[64], n = rom.lay ? pocket_list(e, rom.lay, ui.pocket, ids, qty, NULL) : 0;
		int rows = 7 + EXTRA / 28;
		int row = (y - (TOP + 42)) / 28, col = x >= 240 ? 1 : 0, j = col * rows + row;
		int shown = n - ui.bagStart > rows * 2 ? rows * 2 - 1 : n - ui.bagStart;
		if (y >= TOP + 42 && row < rows && j < shown) {
			ui.bagPick = ui.bagStart + j;
			bag_open_on(e, rom.lay, ui.pocket, ui.bagPick, mine);
			return;
		}
		ui.bagStart += rows * 2 - 1, ui.bagPick = -1;
	}
}

/* ------------------------------------------------------------------ the page */

static uint32_t hiOut[HW * HH_MAX];
static bool hiValid;

uint32_t poke_player_keys(uint32_t keys) {
	static uint32_t held;
	uint32_t down = keys & ~held & (K_L | K_R);
	held = keys;
	if (!hiValid || ui.page != PAGE_BAG) return keys;
	if (down & K_R) ui.pocket = (ui.pocket + 1) % 5, ui.bagStart = 0, ui.bagPick = -1;
	if (down & K_L) ui.pocket = (ui.pocket + 4) % 5, ui.bagStart = 0, ui.bagPick = -1;
	return keys & ~(uint32_t) (K_L | K_R);
}

void poke_set_page_height(int h) {
	if (h < 320) h = 320;
	if (h > HH_MAX) h = HH_MAX;
	if (h == hh) return;
	hh = h;
	mapCache.valid = false;
}

int poke_page_height(void) {
	return hh;
}

const uint32_t* poke_page_hi(void) {
	return hiValid ? hiOut : NULL;
}

const uint32_t* poke_live_view(Emu* e, const char* romPath) {
	(void) romPath;
	static uint32_t lo[240 * 160];
	static uint32_t lastKey;
	static bool have;
	static unsigned frames;
	hiValid = false;
	if (!poke_supported(e->gameCode)) return NULL;
	rom_scan(e);
	const Layout* l = rom.lay;
	if (!l) return NULL;
	int count = emu_read8(e, l->count);
	if (count < 1 || count > 6) return have = false, NULL;
	Mon mons[6];
	uint32_t key = 2166136261u;
	int n = party_read(e, l->party, count, mons, &key);
	/* a party being written (a switch, a new Pokémon): the last picture meanwhile; none yet: the logo */
	if (n <= 0) {
		if (n < 0 && have) {
			hiValid = true;
			return lo;
		}
		return have = false, NULL;
	}
	animFrame = (++frames / 20) & 1;
	/* in a battle: its page, and back to the page before once it's over */
	bool inBattle = emu_read8(e, l->inBattle) & 2;
	Battler bat[4];
	memset(bat, 0, sizeof(bat));
	int nBat = 2, mine = 0;
	Mon team[6];
	int nTeam = 0;
	bool canAct = false;
	if (inBattle) {
		nBat = emu_read8(e, l->battlers) == 4 ? 4 : 2;
		for (int b = 0; b < nBat && inBattle; ++b)
			if (!battler_read(e, l->battleMons + b * 0x58, &bat[b], &key) && b < 2) inBattle = false;
		nTeam = party_read(e, l->enemy, 6, team, &key);
		if (nTeam < 0) nTeam = 0;
		if (rom.actionFn) {
			int b = menu_battler(e, l, nBat);
			if (b >= 0) mine = b, canAct = true;
		}
	}
	act.doubles = nBat == 4;
	if (inBattle != ui.inBattle) {
		if (inBattle) ui.pageBefore = ui.page, ui.page = PAGE_BATTLE;
		else if (ui.page == PAGE_BATTLE) ui.page = ui.pageBefore == PAGE_BATTLE ? PAGE_PARTY : ui.pageBefore;
		ui.inBattle = inBattle;
	}
	take_tap(e, bat, mine);
	if (inBattle) battle_act(e, l, bat);
	else act.move = -1, bag_go(e, l);
	if (inBattle) bagGo.stage = 0;
	uint32_t sb1 = sb1_of(e, l), sb2 = sb2_of(e, l);
	key = (key ^ emu_read32(e, sb1 + l->moneyOff)) * 16777619u;
	key = (key ^ (uint32_t) badges_of(e, l)) * 16777619u;
	if (sb2 >> 24 == 0x02) key = (key ^ (uint32_t) (emu_read16(e, sb2 + 0x0E) << 8 | emu_read8(e, sb2 + 0x10))) * 16777619u;
	key = (key ^ (uint32_t) (ui.page | ui.inBattle << 4 | mine << 5 | nBat << 8 | canAct << 12 | animFrame << 13 | (ui.summary + 1) << 16)) * 16777619u;
	key = (key ^ (uint32_t) hh) * 16777619u;
	/* (a page's contents in the key too: read without drawing first) */
	px = NULL;
	if (ui.page == PAGE_BAG) page_bag(e, l, &key);
	if (ui.page == PAGE_BOX) page_box(e, l, &key);
	if (ui.page == PAGE_DEX) page_dex(e, l, &key);
	if (ui.page == PAGE_MAP) page_map(e, l, &key);
	if (have && key == lastKey) {
		hiValid = true;
		return lo;
	}
	px = hiOut;
	ground();
	top_bar(e, l);
	uint32_t k2 = 0;
	switch (ui.page) {
	case PAGE_BATTLE:
		dy = EXTRA / 2;
		page_battle(e, bat, nBat, mine, team, nTeam, canAct);
		dy = 0;
		break;
	case PAGE_DEX:
		page_dex(e, l, &k2);
		break;
	case PAGE_MAP:
		page_map(e, l, &k2);
		break;
	case PAGE_BOX:
		dy = EXTRA / 2;
		page_box(e, l, &k2);
		dy = 0;
		break;
	case PAGE_BAG:
		page_bag(e, l, &k2);
		break;
	default:
		if (ui.summary >= 0 && ui.summary < n) dy = EXTRA / 2, party_summary(e, &mons[ui.summary]), dy = 0;
		else
			for (int i = 0; i < n; ++i) party_card(e, &mons[i], i);
	}
	tabs();
	/* (240 × 160 for a frontend that takes the second screen at the GBA's size: every other pixel) */
	for (int y = 0; y < 160; ++y)
		for (int x = 0; x < 240; ++x) lo[y * 240 + x] = hiOut[(y * hh / 160) * HW + x * 2];
	lastKey = key;
	have = true;
	hiValid = true;
	return lo;
}
