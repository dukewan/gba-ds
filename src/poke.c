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
	uint32_t gMain; /* in battle: byte +0x439, bit 1 */
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
} Layout;

static const Layout layouts[] = {
	{ "FireRed / LeafGreen", 0x030030F0, 0x02024029, 0x02024284, 0x0202402C, 0x02023BE4, 0x03005008, 0x0300500C, 0,
	  0xF20, 0x290, 0xEE0, 0x820, { { 0x310, 42 }, { 0x430, 13 }, { 0x54C, 43 }, { 0x464, 58 }, { 0x3B8, 30 } },
	  0x03004FE0, 0x02023FF8, 0x02023FFC,
	  "\xf0\xb5\x47\x46\x80\xb4\x16\x4f\x16\x4c\x20\x78\x42\x02\xb9\x1c\x51\x18\x0d\x78\xf9\x1c\x52\x18\x11\x78\x09\x02\x0d\x43\x01\x21",
	  "\x00\xb5\x47\xf7\x41\xf8\x07\x4a\x07\x49\x08\x48\x00\x78\x80\x00\x40\x18\x11\x68\x00\x68\x01\x40\x00\x29\x01\xd1\x01\xf0\x7a\xf8", 0x34C },
	{ "Emerald", 0x030022C0, 0x020244E9, 0x020244EC, 0x02024744, 0x02024084, 0x03005D8C, 0x03005D90, 0,
	  0xAC, 0x490, 0x1270, 0x867, { { 0x560, 30 }, { 0x650, 16 }, { 0x790, 46 }, { 0x690, 64 }, { 0x5D8, 30 } },
	  0x03005D60, 0x020244AC, 0x020244B0,
	  "\xf0\xb5\x13\x4b\x13\x4c\x20\x78\x42\x02\x99\x1c\x51\x18\x0d\x78\x03\x33\xd2\x18\x11\x78\x09\x02\x0d\x43\x01\x21\x07\x22\x01\x23",
	  "\xf0\xb5\x47\x46\x80\xb4\x00\x20\x80\x46\x0a\x4b\x18\x78\x40\x02\x09\x49\x46\x18\x09\x4a\x91\x8d\xf0\x20\x08\x40\x1f\x1c\x00\x28", 0x674 },
	{ "Ruby / Sapphire", 0x03001770, 0x03004350, 0x03004360, 0x030045C0, 0x02024A80, 0, 0, 0x02025734,
	  0, 0x490, 0x1220, 0x807, { { 0x560, 20 }, { 0x600, 16 }, { 0x740, 46 }, { 0x640, 64 }, { 0x5B0, 20 } } },
	{ "Ruby / Sapphire (Japan)", 0x030016E0, 0x03004280, 0x03004290, 0x030044F0, 0x020247E0, 0, 0, 0x02025494,
	  0, 0x490, 0x1220, 0x807, { { 0x560, 20 }, { 0x600, 16 }, { 0x740, 46 }, { 0x640, 64 }, { 0x5B0, 20 } } },
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
	uint32_t actionFn, moveFn; /* the battle menus' functions (Thumb: +1), 0: not found */
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
	int refs[N_LAYOUTS] = { 0 };
	uint8_t seq[502];
	for (int i = 0; i < 251; ++i) seq[i * 2] = (uint8_t) (i + 1), seq[i * 2 + 1] = 0;
	static const uint8_t pound[17] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x28, 0x00, 0x64, 0x23 };
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
	/* the battle menus' functions: the action menu's by its first bytes, in the battle code (past the
	 * first 256 KB, where an unrelated function starts the same in FireRed); the move menu's too, or
	 * where it lies from the action menu's (LeafGreen) */
	const Layout* L = rom.lay;
	if (L && L->actionSig) {
		for (size_t o = 0x40000; o + 32 <= n && !rom.actionFn; o += 2)
			if (!memcmp(r + o, L->actionSig, 32)) rom.actionFn = 0x08000000 + (uint32_t) o + 1;
		for (size_t o = 0x40000; o + 32 <= n && !rom.moveFn && rom.actionFn; o += 2)
			if (!memcmp(r + o, L->moveSig, 32)) rom.moveFn = 0x08000000 + (uint32_t) o + 1;
		if (rom.actionFn && !rom.moveFn) {
			uint32_t at = rom.actionFn - 1 + L->moveAfterAction - 0x08000000;
			if (at + 2 <= n && r[at] == (uint8_t) L->moveSig[0] && r[at + 1] == (uint8_t) L->moveSig[1]) rom.moveFn = 0x08000000 + at + 1;
		}
	}
	fprintf(stderr, "pokemon: battle menus %s\n", rom.actionFn && rom.moveFn ? "found" : "not found");
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
	const uint8_t *g = NULL, *misc = NULL;
	for (int i = 0; i < 4; ++i) {
		if (o[i] == 'G') g = d + i * 12;
		if (o[i] == 'M') misc = d + i * 12;
	}
	m->species = le16(g);
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

static int badges_of(Emu* e, const Layout* l) {
	uint32_t flags = sb1_of(e, l) + l->flagsOff;
	int n = 0;
	for (int i = 0; i < 8; ++i) {
		int f = l->badgeFlag + i;
		n |= ((emu_read8(e, flags + f / 8) >> (f % 8)) & 1) << i;
	}
	return n;
}

/* ------------------------------------------------------------------ drawing */

#define RGB(r, g, b) (0xFF000000u | (uint32_t) (b) << 16 | (uint32_t) (g) << 8 | (uint32_t) (r))

static uint32_t* px;

static void put(int x, int y, uint32_t c) {
	if (x >= 0 && x < W && y >= 0 && y < H) px[y * W + x] = c;
}

static void fill(int x0, int y0, int x1, int y1, uint32_t c) {
	for (int y = y0; y <= y1; ++y)
		for (int x = x0; x <= x1; ++x) put(x, y, c);
}

/* a box with its corners cut: an edge, a face */
static void panel(int x, int y, int w, int h, uint32_t edge, uint32_t face) {
	fill(x + 1, y, x + w - 2, y + h - 1, edge);
	fill(x, y + 1, x + w - 1, y + h - 2, edge);
	fill(x + 1, y + 1, x + w - 2, y + h - 2, face);
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

/* a Poké Ball, for a Pokémon the ROM has no icon of (a hack's new ones), size x size */
static void ball(int x, int y, int size, bool greyed) {
	int r = size / 2 - 2, cx = x + size / 2, cy = y + size / 2;
	for (int dy = -r; dy <= r; ++dy)
		for (int dx = -r; dx <= r; ++dx) {
			int d = dx * dx + dy * dy;
			if (d > r * r) continue;
			uint32_t c = d > (r - 1) * (r - 1) || (dy >= -1 && dy <= 0) ? RGB(40, 40, 48) : dy < 0 ? RGB(232, 64, 56) : RGB(248, 248, 248);
			if (dx * dx + dy * dy <= 4) c = RGB(248, 248, 248);
			put(cx + dx, cy + dy, greyed ? grey(c, 70) : c);
		}
}

/* species' first icon frame at (x, y), at full size (32x32) or half; greyed: a fainted one */
static void icon_draw(Emu* e, int species, int x, int y, bool half, bool greyed) {
	if (!rom.icons || species >= 440) {
		ball(x, y, half ? 16 : 32, greyed);
		return;
	}
	uint32_t gfx = emu_read32(e, rom.iconTable + species * 4);
	int pal = emu_read8(e, rom.iconIndices + species);
	if ((gfx >> 24) < 0x08 || (gfx >> 24) > 0x09 || pal > 5) {
		ball(x, y, half ? 16 : 32, greyed);
		return;
	}
	uint32_t pp = emu_read32(e, rom.iconPalettes + pal * 8);
	uint32_t colors[16];
	for (int i = 0; i < 16; ++i) colors[i] = bgr555(emu_read16(e, pp + i * 2));
	for (int t = 0; t < 16; ++t) {
		int tx = (t % 4) * 8, ty = (t / 4) * 8;
		for (int r = 0; r < 8; ++r) {
			uint32_t row = emu_read32(e, gfx + t * 32 + r * 4);
			for (int c = 0; c < 8; ++c) {
				int v = (row >> (c * 4)) & 15, X = tx + c, Y = ty + r;
				if (!v || (half && ((X | Y) & 1))) continue;
				put(x + (half ? X / 2 : X), y + (half ? Y / 2 : Y), greyed ? grey(colors[v], 70) : colors[v]);
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

static int text_width(const char* s) {
	int w = 0;
	while (*s) {
		const PokeGlyph* g = glyph(utf8_next(&s));
		w += g ? g->adv : 6;
	}
	return w;
}

/* text with its top at y, cut off past x + most (most 0: whole); shadow: drawn first a pixel right
 * and down (0: none) */
static void text_fit(int x, int y, const char* s, uint32_t color, uint32_t shadow, int most) {
	for (int pass = shadow ? 0 : 1; pass < 2; ++pass) {
		int cx = x + (pass ? 0 : 1), cy = y + (pass ? 0 : 1);
		const char* p = s;
		while (*p) {
			const PokeGlyph* g = glyph(utf8_next(&p));
			int adv = g ? g->adv : 6;
			if (most && cx + adv - x > most) break;
			if (g)
				for (int r = 0; r < 12; ++r)
					for (int c = 0; c < 16; ++c)
						if (g->rows[r] >> c & 1) put(cx + c, cy + r, pass ? color : shadow);
			cx += adv;
		}
	}
}
static void text(int x, int y, const char* s, uint32_t color, uint32_t shadow) {
	text_fit(x, y, s, color, shadow, 0);
}

static void number(char* out, int v) {
	char d[12];
	int k = 0;
	do d[k++] = '0' + v % 10, v /= 10;
	while (v && k < 11);
	while (k) *out++ = d[--k];
	*out = 0;
}

/* "HP", 3x5 pixel letters, for the bar's label */
static void hp_label(int x, int y, uint32_t c, uint32_t shadow) {
	static const char* const letters[2] = { "101101111101101", "111101111100100" };
	for (int pass = 0; pass < 2; ++pass)
		for (int l = 0; l < 2; ++l)
			for (int i = 0; i < 15; ++i)
				if (letters[l][i] == '1') put(x + l * 4 + i % 3 + !pass, y + i / 3 + !pass, pass ? c : shadow);
}

static const uint32_t ink = RGB(48, 48, 56), soft = RGB(208, 208, 200), cream = RGB(248, 248, 232), navy = RGB(56, 72, 112);

/* the HP bar, (bw + 12) wide: its label, the track, the share left in green / yellow / red */
static void hp_bar(int bx, int by, int bw, int hp, int maxHp) {
	fill(bx - 1, by - 1, bx + bw + 12, by + 5, RGB(64, 64, 72));
	fill(bx, by, bx + 10, by + 4, RGB(232, 176, 64));
	hp_label(bx + 1, by, RGB(248, 248, 248), RGB(160, 96, 32));
	fill(bx + 12, by, bx + bw + 11, by + 4, RGB(80, 88, 96));
	if (!hp || maxHp <= 0) return;
	int share = hp * 100 / maxHp, len = (bw - 1) * hp / maxHp;
	uint32_t hi = share > 50 ? RGB(96, 224, 120) : share > 20 ? RGB(248, 216, 64) : RGB(248, 88, 64);
	uint32_t lo = share > 50 ? RGB(56, 168, 80) : share > 20 ? RGB(200, 160, 32) : RGB(184, 48, 40);
	fill(bx + 12, by, bx + 12 + len, by + 4, lo);
	fill(bx + 12, by, bx + 12 + len, by + 2, hi);
}

static void hp_numbers(int right, int y, int hp, int maxHp) {
	char s[16];
	number(s, hp);
	size_t n = strlen(s);
	s[n] = '/';
	number(s + n + 1, maxHp);
	text(right - text_width(s), y, s, ink, soft);
}

static void level(int right, int y, int lv) {
	char s[8] = "Lv";
	number(s + 2, lv);
	text(right - text_width(s), y, s, ink, soft);
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

static void status_tag(int x, int y, const Mon* m) {
	uint32_t sc;
	const char* st = status_name(m, &sc);
	if (!st) return;
	fill(x, y, x + text_width(st) + 3, y + 12, sc);
	text(x + 2, y, st, RGB(248, 248, 248), 0);
}

static const uint32_t typeColors[18] = {
	RGB(168, 168, 120), RGB(192, 48, 40), RGB(168, 144, 240), RGB(160, 64, 160), RGB(224, 192, 104), RGB(184, 160, 56),
	RGB(168, 184, 32), RGB(112, 88, 152), RGB(184, 184, 208), RGB(104, 160, 144), RGB(240, 128, 48), RGB(104, 144, 240),
	RGB(120, 200, 80), RGB(248, 208, 48), RGB(248, 88, 136), RGB(152, 216, 216), RGB(112, 56, 248), RGB(112, 88, 72),
};

/* ------------------------------------------------------------------ pages */

enum { PAGE_PARTY, PAGE_BATTLE, PAGE_BAG, N_PAGES };
static const char* const pageNames[N_PAGES] = { "队伍", "对战", "背包" };
static const char* const pocketNames[5] = { "道具", "精灵球", "树果", "学习器", "重要物品" };

static struct {
	int page, pageBefore; /* before the battle, gone back to after it */
	bool inBattle;
	int pocket, bagStart;
	/* touches: a tap is taken where the finger went down */
	bool down, tapped;
	int tapX, tapY;
} ui;

void poke_touch(int x, int y, bool down) {
	if (down && !ui.down) ui.tapped = true, ui.tapX = x, ui.tapY = y;
	ui.down = down;
}

static int tab_x(int i) {
	return 3 + i * 34;
}

static void header(Emu* e, const Layout* l) {
	fill(0, 0, W - 1, 15, RGB(40, 56, 96));
	fill(0, 16, W - 1, 16, RGB(24, 32, 56));
	for (int i = 0; i < N_PAGES; ++i) {
		int x = tab_x(i);
		bool on = ui.page == i, off = i == PAGE_BATTLE && !ui.inBattle;
		if (on) panel(x, 1, 32, 14, RGB(120, 144, 208), RGB(80, 104, 168));
		text(x + 4, 2, pageNames[i], off ? RGB(96, 112, 152) : RGB(248, 248, 248), on ? RGB(40, 56, 96) : RGB(24, 32, 56));
	}
	/* the badges: eight gems, lit once won */
	int badges = badges_of(e, l);
	for (int i = 0; i < 8; ++i) {
		int x = 108 + i * 7, y = 5;
		bool won = badges >> i & 1;
		uint32_t c = won ? RGB(248, 208, 64) : RGB(64, 80, 128), hi = won ? RGB(255, 248, 200) : RGB(88, 104, 152);
		fill(x + 1, y, x + 3, y + 4, c);
		fill(x, y + 1, x + 4, y + 3, c);
		put(x + 1, y + 1, hi);
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
		text(W - 5 - text_width(m), 2, m, RGB(248, 232, 128), RGB(24, 32, 56));
	}
}

static void party_card(Emu* e, const Mon* m, int i) {
	int x = 4 + (i % 2) * 118, y = 20 + (i / 2) * 46, w = 114, h = 43;
	bool fainted = !m->egg && !m->hp;
	panel(x, y, w, h, i == 0 ? RGB(232, 96, 64) : navy, fainted ? RGB(200, 192, 184) : cream);
	fill(x + 2, y + 2, x + w - 3, y + 2, RGB(255, 255, 255));
	fill(x + 2, y + h - 4, x + w - 3, y + h - 3, fainted ? RGB(176, 168, 160) : RGB(216, 208, 184));
	icon_draw(e, m->egg ? 412 : m->species, x + 1, y + 4, false, fainted);
	if (m->egg) {
		text(x + 34, y + 4, "蛋", ink, soft);
		return;
	}
	text_fit(x + 34, y + 4, species_name(e, m->species), ink, soft, w - 34 - 30);
	level(x + w - 5, y + 4, m->level);
	int bx = x + 34, by = y + 20, bw = w - 34 - 12 - 5;
	hp_bar(bx, by, bw, m->hp, m->maxHp);
	hp_numbers(bx + bw + 12, by + 7, m->hp, m->maxHp);
	status_tag(bx, by + 8, m);
}

static void ground(void) {
	for (int y = 17; y < H; ++y)
		for (int x = 0; x < W; ++x) px[y * W + x] = (x / 8 + y / 8) % 2 ? RGB(104, 168, 192) : RGB(112, 176, 200);
}

/* a battler's panel: icon, name, level, HP, condition (x, y: its top left; w wide, 38 high) */
static void battler_panel(Emu* e, const Battler* b, int x, int y, int w, bool mine) {
	panel(x, y, w, 38, mine ? RGB(232, 96, 64) : navy, cream);
	fill(x + 2, y + 2, x + w - 3, y + 2, RGB(255, 255, 255));
	icon_draw(e, b->m.species, x + 2, y + 3, false, !b->m.hp);
	text_fit(x + 36, y + 3, species_name(e, b->m.species), ink, soft, w - 36 - 34);
	level(x + w - 5, y + 3, b->m.level);
	int bx = x + 36, by = y + 17, bw = w - 36 - 12 - 6;
	hp_bar(bx, by, bw, b->m.hp, b->m.maxHp);
	hp_numbers(bx + bw + 12, by + 7, b->m.hp, b->m.maxHp);
	status_tag(bx, by + 8, &b->m);
}

static void page_battle(Emu* e, const Battler* me, const Battler* foe, const Mon* team, int nTeam) {
	/* the foe, its team beside it in a trainer's battle (wild: one) */
	bool trainer = nTeam > 1;
	battler_panel(e, foe, 4, 19, trainer ? 166 : 232, false);
	if (trainer) {
		panel(172, 19, 64, 38, navy, RGB(232, 232, 216));
		for (int i = 0; i < nTeam && i < 6; ++i)
			icon_draw(e, team[i].species, 175 + (i % 3) * 20, 21 + (i / 3) * 17, true, !team[i].hp);
	}
	battler_panel(e, me, 4, 59, 232, true);
	/* the moves, two by two: name, type, PP */
	for (int i = 0; i < 4; ++i) {
		int x = 4 + (i % 2) * 118, y = 99 + (i / 2) * 30, w = 114;
		if (!me->moves[i]) {
			panel(x, y, w, 29, RGB(88, 112, 152), RGB(144, 168, 200));
			continue;
		}
		int t = move_type(e, me->moves[i]);
		uint32_t tc = t >= 0 ? typeColors[t] : RGB(128, 128, 136);
		panel(x, y, w, 29, navy, cream);
		fill(x + 1, y + 1, x + 4, y + 27, tc);
		const char* name = me->moves[i] < POKE_MOVES && pokeMoveNames[me->moves[i]][0] ? pokeMoveNames[me->moves[i]] : "？？？";
		text_fit(x + 8, y + 2, name, ink, soft, w - 12);
		if (t >= 0) {
			int tw = text_width(pokeTypeNames[t]);
			fill(x + 8, y + 15, x + 8 + tw + 3, y + 26, tc);
			text(x + 10, y + 15, pokeTypeNames[t], RGB(248, 248, 248), RGB(40, 40, 48));
		}
		char pp[16] = "PP ";
		number(pp + 3, me->pp[i]);
		if (me->maxPp[i]) {
			size_t n = strlen(pp);
			pp[n] = '/';
			number(pp + n + 1, me->maxPp[i]);
		}
		bool low = me->maxPp[i] ? me->pp[i] * 4 <= me->maxPp[i] : me->pp[i] == 0;
		text(x + w - 5 - text_width(pp), y + 15, pp, low ? RGB(200, 64, 48) : ink, soft);
	}
}

static int pocket_x(int i) {
	static const int xs[5] = { 4, 34, 76, 106, 148 };
	return xs[i];
}

static void page_bag(Emu* e, const Layout* l, uint32_t* key) {
	uint32_t sb1 = sb1_of(e, l), k = key_of(e, l) & 0xFFFF;
	for (int i = 0; i < 5; ++i) {
		int x = pocket_x(i), w = text_width(pocketNames[i]) + 6;
		bool on = ui.pocket == i;
		panel(x, 19, w, 14, on ? RGB(232, 96, 64) : navy, on ? cream : RGB(80, 104, 168));
		text(x + 3, 20, pocketNames[i], on ? ink : RGB(248, 248, 248), on ? soft : RGB(40, 56, 96));
	}
	const Pocket* p = &l->pockets[ui.pocket];
	int ids[64], qty[64], n = 0;
	for (int i = 0; i < p->n && i < 64; ++i) {
		uint32_t v = emu_read32(e, sb1 + p->off + i * 4);
		int id = v & 0xFFFF, q = (int) ((v >> 16) ^ k);
		*key = (*key ^ v) * 16777619u;
		if (!id) continue;
		ids[n] = id, qty[n] = q, ++n;
	}
	enum { ROWS = 9, PER = ROWS * 2 };
	if (ui.bagStart >= n) ui.bagStart = 0;
	panel(4, 35, 232, 122, navy, cream);
	if (!n) {
		text(W / 2 - text_width("空") / 2, 88, "空", RGB(144, 144, 152), 0);
		return;
	}
	int shown = n - ui.bagStart > PER ? PER - 1 : n - ui.bagStart;
	for (int j = 0; j < shown; ++j) {
		int i = ui.bagStart + j, x = 8 + (j / ROWS) * 114, y = 37 + (j % ROWS) * 13;
		const char* name = ids[i] < POKE_ITEMS && pokeItemNames[ids[i]][0] ? pokeItemNames[ids[i]] : "？？？";
		char q[12] = "×";
		number(q + strlen(q), qty[i]);
		bool keyItem = ui.pocket == 4;
		int qw = keyItem ? 0 : text_width(q) + 4;
		text_fit(x, y, name, ink, soft, 110 - qw);
		if (!keyItem) text(x + 108 - text_width(q), y, q, ink, soft);
	}
	if (shown < n - ui.bagStart) {
		char more[24] = "还有";
		number(more + strlen(more), n - ui.bagStart - shown);
		strcat(more, "个…");
		text(8 + 114, 37 + (ROWS - 1) * 13, more, RGB(232, 96, 64), soft);
	}
}

/* A move tapped on the battle page: played as the player would — on the action menu FIGHT chosen
 * (its cursor put on it) and A, on the move menu the move's place and A — only while the game is
 * on one of those menus; anything else (a message, an animation) and it waits, given up after
 * three seconds. */
static struct {
	int move;   /* -1: none */
	int frames; /* since the tap */
	int press;  /* frames left of an A being pressed (then let go) */
	bool last;  /* the move menu's A: done once let go */
} act = { -1, 0, 0, false };
static uint32_t injected;

uint32_t poke_keys(void) {
	return injected;
}

static void battle_act(Emu* e, const Layout* l, const Battler* me) {
	injected = 0;
	if (act.move < 0) return;
	if (++act.frames > 180 || !ui.inBattle || !rom.actionFn || !rom.moveFn) {
		act.move = -1;
		return;
	}
	if (act.press > 0) {
		/* A down three frames, up three */
		injected = act.press > 3 ? K_A : 0;
		if (--act.press == 0 && act.last) act.move = -1;
		return;
	}
	uint32_t f = emu_read32(e, l->controller);
	if (f == rom.actionFn) {
		emu_write8(e, l->actionCursor, 0);
		act.press = 6, injected = K_A;
	} else if (f == rom.moveFn) {
		if (!me->moves[act.move] || !me->pp[act.move]) {
			act.move = -1;
			return;
		}
		emu_write8(e, l->moveCursor, (uint8_t) act.move);
		act.press = 6, act.last = true, injected = K_A;
	}
}

/* a tap: on a page's tab, a pocket (again: its next items), a move */
static void take_tap(const Battler* me) {
	if (!ui.tapped) return;
	ui.tapped = false;
	int x = ui.tapX, y = ui.tapY;
	if (ui.page == PAGE_BATTLE && y >= 99 && y < 158 && rom.actionFn && rom.moveFn && act.move < 0) {
		int i = (y - 99) / 30 * 2 + (x >= 118);
		if (i < 4 && me->moves[i] && me->pp[i]) act = (typeof(act)) { i, 0, 0, false };
		return;
	}
	if (y < 17) {
		for (int i = 0; i < N_PAGES; ++i)
			if (x >= tab_x(i) && x < tab_x(i) + 32 && !(i == PAGE_BATTLE && !ui.inBattle)) ui.page = i;
		return;
	}
	if (ui.page == PAGE_BAG && y >= 19 && y < 34) {
		for (int i = 4; i >= 0; --i)
			if (x >= pocket_x(i)) {
				if (ui.pocket == i) ui.bagStart += 17;
				else ui.pocket = i, ui.bagStart = 0;
				return;
			}
	}
	/* a tap on the list's "还有…": its next items */
	if (ui.page == PAGE_BAG && y >= 35) ui.bagStart += 17;
}

const uint32_t* poke_live_view(Emu* e, const char* romPath) {
	(void) romPath;
	static uint32_t out[W * H];
	static uint32_t lastKey;
	static bool have;
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
	if (n <= 0) return n < 0 && have ? out : (have = false, NULL);
	/* in a battle: its page, and back to the page before once it's over */
	bool inBattle = emu_read8(e, l->gMain + 0x439) & 2;
	Battler me = { 0 }, foe = { 0 };
	Mon team[6];
	int nTeam = 0;
	if (inBattle) {
		inBattle = battler_read(e, l->battleMons, &me, &key) && battler_read(e, l->battleMons + 0x58, &foe, &key);
		nTeam = party_read(e, l->enemy, 6, team, &key);
		if (nTeam < 0) nTeam = 0;
	}
	if (inBattle != ui.inBattle) {
		if (inBattle) ui.pageBefore = ui.page, ui.page = PAGE_BATTLE;
		else if (ui.page == PAGE_BATTLE) ui.page = ui.pageBefore == PAGE_BATTLE ? PAGE_PARTY : ui.pageBefore;
		ui.inBattle = inBattle;
	}
	take_tap(&me);
	if (inBattle) battle_act(e, l, &me);
	else injected = 0, act.move = -1;
	px = out;
	uint32_t sb1 = sb1_of(e, l);
	key = (key ^ emu_read32(e, sb1 + l->moneyOff)) * 16777619u;
	key = (key ^ (uint32_t) badges_of(e, l)) * 16777619u;
	key = (key ^ (uint32_t) (ui.page | ui.pocket << 4 | ui.bagStart << 8 | ui.inBattle << 20)) * 16777619u;
	if (ui.page == PAGE_BAG) {
		/* (the bag's contents in the key too: read as it's drawn, so drawn into a scratch first) */
		static uint32_t scratch[W * H];
		px = scratch;
		uint32_t k2 = key;
		page_bag(e, l, &k2);
		px = out;
		key = k2;
	}
	if (have && key == lastKey) return out;
	ground();
	header(e, l);
	switch (ui.page) {
	case PAGE_BATTLE:
		page_battle(e, &me, &foe, team, nTeam);
		break;
	case PAGE_BAG: {
		uint32_t k2 = 0;
		page_bag(e, l, &k2);
		break;
	}
	default:
		for (int i = 0; i < n; ++i) party_card(e, &mons[i], i);
	}
	lastKey = key;
	have = true;
	return out;
}
