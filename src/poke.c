/* The Pokémon games' second screen: see poke.h and docs/games/pokemon-gen3.md.
 *
 * Where things are (pret's pokeruby / pokeemerald / pokefirered, checked on the
 * games: a new game played to the first Pokémon in each, its party read back):
 * the party count and the party (6 x 100 bytes); money, kept XORed with a key
 * in FireRed / LeafGreen / Emerald. Each Pokémon's 48 bytes of data are XORed
 * with personality ^ OT id and shuffled by personality % 24; their checksum
 * tells a party being written from one that's there. Icons come from the
 * player's own ROM: the icon table, the palette indices and the six palettes
 * lie end to end, found by the palettes' tags (0xDAC0-0xDAC5). */
#include "poke.h"

#include <mgba/core/core.h>

#include <string.h>

#define W 240
#define H 160

enum { MONEY_PLAIN, MONEY_FRLG, MONEY_E };

typedef struct {
	char code[5];
	uint32_t count, party;
	int money;
	uint32_t moneyAt; /* MONEY_PLAIN: where it is */
} PokeGame;

static const PokeGame games[] = {
	{ "BPRE", 0x02024029, 0x02024284, MONEY_FRLG, 0 }, /* FireRed */
	{ "BPGE", 0x02024029, 0x02024284, MONEY_FRLG, 0 }, /* LeafGreen */
	{ "BPEE", 0x020244E9, 0x020244EC, MONEY_E, 0 },    /* Emerald */
	{ "AXVE", 0x03004350, 0x03004360, MONEY_PLAIN, 0x02025BC4 }, /* Ruby */
	{ "AXPE", 0x03004350, 0x03004360, MONEY_PLAIN, 0x02025BC4 }, /* Sapphire */
	{ "AXVJ", 0x03004280, 0x03004290, MONEY_PLAIN, 0x02025924 }, /* Ruby (Japan) */
	{ "AXPJ", 0x03004280, 0x03004290, MONEY_PLAIN, 0x02025924 }, /* Sapphire (Japan) */
};

static const PokeGame* game_of(const char* code) {
	for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); ++i)
		if (!strncmp(games[i].code, code, 4)) return &games[i];
	return NULL;
}

bool poke_supported(const char* gameCode) {
	return game_of(gameCode) != NULL;
}

uint32_t poke_virt(Emu* e, uint32_t addr) {
	(void) e;
	(void) addr;
	return 0;
}

/* ------------------------------------------------------------------ the party */

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

static uint32_t rd32(const uint8_t* b) {
	return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t) b[3] << 24;
}
static uint16_t rd16(const uint8_t* b) {
	return (uint16_t) (b[0] | b[1] << 8);
}

/* false: not a whole Pokémon (being written, or garbage) */
static bool mon_read(const uint8_t* b, Mon* m) {
	uint32_t pid = rd32(b), key = pid ^ rd32(b + 4);
	uint8_t d[48];
	uint16_t sum = 0;
	for (int i = 0; i < 48; i += 4) {
		uint32_t v = rd32(b + 32 + i) ^ key;
		d[i] = v, d[i + 1] = v >> 8, d[i + 2] = v >> 16, d[i + 3] = v >> 24;
		sum += (uint16_t) (v + (v >> 16));
	}
	if (sum != rd16(b + 28)) return false;
	const char* o = orders[pid % 24];
	const uint8_t *g = NULL, *misc = NULL;
	for (int i = 0; i < 4; ++i) {
		if (o[i] == 'G') g = d + i * 12;
		if (o[i] == 'M') misc = d + i * 12;
	}
	m->species = rd16(g);
	m->egg = (rd32(misc + 4) >> 30) & 1;
	m->status = rd32(b + 80);
	m->level = b[84];
	m->hp = rd16(b + 86);
	m->maxHp = rd16(b + 88);
	if (m->species <= 0 || m->species >= POKE_SPECIES || m->level < 1 || m->level > 100) return false;
	if (!m->egg && (m->maxHp <= 0 || m->hp > m->maxHp)) return false;
	return true;
}

static uint32_t money_of(Emu* e, const PokeGame* g) {
	switch (g->money) {
	case MONEY_FRLG: {
		uint32_t sb1 = emu_read32(e, 0x03005008), sb2 = emu_read32(e, 0x0300500C);
		return emu_read32(e, sb1 + 0x290) ^ emu_read32(e, sb2 + 0xF20);
	}
	case MONEY_E: {
		uint32_t sb1 = emu_read32(e, 0x03005D8C), sb2 = emu_read32(e, 0x03005D90);
		return emu_read32(e, sb1 + 0x490) ^ emu_read32(e, sb2 + 0xAC);
	}
	default:
		return emu_read32(e, g->moneyAt);
	}
}

/* ------------------------------------------------------------------ icons */

static struct {
	uint32_t crc;
	bool tried, found;
	uint32_t table, indices, palettes; /* GBA addresses */
} icons;

static void icons_find(Emu* e) {
	if (icons.tried && icons.crc == e->romCrc) return;
	icons.tried = true, icons.found = false, icons.crc = e->romCrc;
	size_t size = e->core->romSize(e->core);
	if (size > 0x2000000) size = 0x2000000;
	for (uint32_t a = 0x08000000; a + 48 <= 0x08000000 + size; a += 4) {
		if (emu_read32(e, a + 4) != 0xDAC0) continue;
		if (emu_read32(e, a + 12) != 0xDAC1 || emu_read32(e, a + 44) != 0xDAC5) continue;
		if ((emu_read32(e, a) >> 24) != 0x08) continue;
		icons.palettes = a;
		icons.indices = a - 440;
		icons.table = icons.indices - 440 * 4;
		icons.found = (emu_read32(e, icons.table) >> 24) == 0x08;
		return;
	}
}

#define RGB(r, g, b) (0xFF000000u | (uint32_t) (b) << 16 | (uint32_t) (g) << 8 | (uint32_t) (r))

static uint32_t bgr555(uint16_t c) {
	int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
	return RGB(r * 255 / 31, g * 255 / 31, b * 255 / 31);
}

static uint32_t* px;

static void put(int x, int y, uint32_t c) {
	if (x >= 0 && x < W && y >= 0 && y < H) px[y * W + x] = c;
}

static void fill(int x0, int y0, int x1, int y1, uint32_t c) {
	for (int y = y0; y <= y1; ++y)
		for (int x = x0; x <= x1; ++x) put(x, y, c);
}

static uint32_t grey(uint32_t c, int share) {
	int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF, l = (r * 3 + g * 6 + b) / 10;
	r += (l - r) * share / 100, g += (l - g) * share / 100, b += (l - b) * share / 100;
	return RGB(r, g, b);
}

/* species' first icon frame, 32x32, at (x, y); greyed: a fainted one */
static void icon_draw(Emu* e, int species, int x, int y, bool greyed) {
	if (!icons.found) return;
	uint32_t gfx = emu_read32(e, icons.table + species * 4);
	int pal = emu_read8(e, icons.indices + species);
	if ((gfx >> 24) != 0x08 || pal > 5) return;
	uint32_t pp = emu_read32(e, icons.palettes + pal * 8);
	uint32_t colors[16];
	for (int i = 0; i < 16; ++i) colors[i] = bgr555(emu_read16(e, pp + i * 2));
	for (int t = 0; t < 16; ++t) {
		int tx = (t % 4) * 8, ty = (t / 4) * 8;
		for (int r = 0; r < 8; ++r) {
			uint32_t row = emu_read32(e, gfx + t * 32 + r * 4);
			for (int c = 0; c < 8; ++c) {
				int v = (row >> (c * 4)) & 15;
				if (v) put(x + tx + c, y + ty + r, greyed ? grey(colors[v], 70) : colors[v]);
			}
		}
	}
}

/* ------------------------------------------------------------------ text */

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

/* text with its top at y; shadow: drawn first a pixel right and down (0: none) */
static int text(int x, int y, const char* s, uint32_t color, uint32_t shadow) {
	for (int pass = shadow ? 0 : 1; pass < 2; ++pass) {
		int cx = x + (pass ? 0 : 1), cy = y + (pass ? 0 : 1);
		const char* p = s;
		while (*p) {
			const PokeGlyph* g = glyph(utf8_next(&p));
			if (!g) {
				cx += 6;
				continue;
			}
			for (int r = 0; r < 12; ++r)
				for (int c = 0; c < 16; ++c)
					if (g->rows[r] >> c & 1) put(cx + c, cy + r, pass ? color : shadow);
			cx += g->adv;
		}
		if (pass) x = cx;
	}
	return x;
}

/* "HP", 3x5 pixel letters, for the bar's label */
static void hp_label(int x, int y, uint32_t c, uint32_t shadow) {
	static const char* const letters[2] = { "101101111101101", "111101111100100" };
	for (int pass = 0; pass < 2; ++pass)
		for (int l = 0; l < 2; ++l)
			for (int i = 0; i < 15; ++i)
				if (letters[l][i] == '1') put(x + l * 4 + i % 3 + !pass, y + i / 3 + !pass, pass ? c : shadow);
}

/* ------------------------------------------------------------------ the page */

static const char* status_name(const Mon* m, uint32_t* color) {
	if (!m->hp) return *color = RGB(168, 64, 64), "濒死";
	if (m->status & 7) return *color = RGB(136, 136, 168), "睡眠";
	if (m->status & (1 << 3 | 1 << 7)) return *color = RGB(168, 88, 176), "中毒";
	if (m->status & 1 << 4) return *color = RGB(232, 104, 56), "灼伤";
	if (m->status & 1 << 5) return *color = RGB(96, 176, 224), "冰冻";
	if (m->status & 1 << 6) return *color = RGB(224, 184, 32), "麻痹";
	return NULL;
}

static void card(Emu* e, const Mon* m, int i) {
	int x = 4 + (i % 2) * 118, y = 20 + (i / 2) * 46, w = 114, h = 43;
	bool fainted = !m->egg && !m->hp;
	uint32_t face = fainted ? RGB(200, 192, 184) : RGB(248, 248, 232);
	uint32_t edge = i == 0 ? RGB(232, 96, 64) : RGB(56, 72, 112);
	uint32_t ink = RGB(48, 48, 56), soft = RGB(208, 208, 200);
	/* the card: an outline with its corners cut, a light top edge and a darker foot */
	fill(x + 1, y, x + w - 2, y + h - 1, edge);
	fill(x, y + 1, x + w - 1, y + h - 2, edge);
	fill(x + 1, y + 1, x + w - 2, y + h - 2, face);
	fill(x + 2, y + 2, x + w - 3, y + 2, RGB(255, 255, 255));
	fill(x + 2, y + h - 4, x + w - 3, y + h - 3, fainted ? RGB(176, 168, 160) : RGB(216, 208, 184));
	icon_draw(e, m->egg ? 412 : m->species, x + 1, y + 4, fainted);
	if (m->egg) {
		text(x + 34, y + 4, "蛋", ink, soft);
		return;
	}
	text(x + 34, y + 4, pokeNames[m->species], ink, soft);
	char lv[8];
	int n = 0, v = m->level;
	lv[n++] = 'L', lv[n++] = 'v';
	if (v >= 100) lv[n++] = '0' + v / 100;
	if (v >= 10) lv[n++] = '0' + v / 10 % 10;
	lv[n++] = '0' + v % 10;
	lv[n] = 0;
	text(x + w - 5 - text_width(lv), y + 4, lv, ink, soft);
	/* the HP bar: its label, the track, the share left in green / yellow / red */
	int bx = x + 34, by = y + 20, bw = w - 34 - 12 - 5;
	fill(bx - 1, by - 1, bx + bw + 12, by + 5, RGB(64, 64, 72));
	fill(bx, by, bx + 10, by + 4, RGB(232, 176, 64));
	hp_label(bx + 1, by, RGB(248, 248, 248), RGB(160, 96, 32));
	fill(bx + 12, by, bx + bw + 11, by + 4, RGB(80, 88, 96));
	int share = m->hp * 100 / m->maxHp, len = m->hp ? (bw - 1) * m->hp / m->maxHp : -1;
	if (m->hp && len < 0) len = 0;
	uint32_t hi = share > 50 ? RGB(96, 224, 120) : share > 20 ? RGB(248, 216, 64) : RGB(248, 88, 64);
	uint32_t lo = share > 50 ? RGB(56, 168, 80) : share > 20 ? RGB(200, 160, 32) : RGB(184, 48, 40);
	if (len >= 0) {
		fill(bx + 12, by, bx + 12 + len, by + 4, lo);
		fill(bx + 12, by, bx + 12 + len, by + 2, hi);
	}
	char hp[16];
	n = 0;
	int parts[2] = { m->hp, m->maxHp };
	for (int p = 0; p < 2; ++p) {
		char d[6];
		int k = 0, q = parts[p];
		do d[k++] = '0' + q % 10, q /= 10;
		while (q && k < 5);
		while (k) hp[n++] = d[--k];
		if (!p) hp[n++] = '/';
	}
	hp[n] = 0;
	text(bx + bw + 12 - text_width(hp), by + 7, hp, ink, soft);
	uint32_t sc;
	const char* st = status_name(m, &sc);
	if (st) {
		int tw = text_width(st);
		fill(bx, by + 8, bx + tw + 3, by + 20, sc);
		text(bx + 2, by + 8, st, RGB(248, 248, 248), 0);
	}
}

const uint32_t* poke_live_view(Emu* e, const char* rom) {
	(void) rom;
	static uint32_t out[W * H];
	static uint32_t lastKey;
	static bool have;
	const PokeGame* g = game_of(e->gameCode);
	if (!g) return NULL;
	int count = emu_read8(e, g->count);
	if (count < 1 || count > 6) return have = false, NULL;
	uint8_t raw[6][100];
	Mon mons[6];
	uint32_t key = 2166136261u;
	for (int i = 0; i < count; ++i) {
		for (int j = 0; j < 100; j += 4) {
			uint32_t v = emu_read32(e, g->party + i * 100 + j);
			raw[i][j] = v, raw[i][j + 1] = v >> 8, raw[i][j + 2] = v >> 16, raw[i][j + 3] = v >> 24;
		}
		/* a party being written (a switch, a new Pokémon): the last picture
		 * meanwhile; none yet: the logo */
		if (!mon_read(raw[i], &mons[i])) return have ? out : NULL;
		for (int j = 0; j < 100; ++j) key = (key ^ raw[i][j]) * 16777619u;
	}
	uint32_t money = money_of(e, g);
	key = (key ^ (uint32_t) count) * 16777619u;
	key = (key ^ money) * 16777619u;
	if (have && key == lastKey) return out;
	icons_find(e);
	px = out;
	/* the ground: a soft check of two blues; the bar along the top */
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x) out[y * W + x] = (x / 8 + y / 8) % 2 ? RGB(104, 168, 192) : RGB(112, 176, 200);
	fill(0, 0, W - 1, 15, RGB(40, 56, 96));
	fill(0, 16, W - 1, 16, RGB(24, 32, 56));
	text(6, 2, "队伍", RGB(248, 248, 248), RGB(24, 32, 56));
	if (money <= 999999) {
		char m[16];
		char d[8];
		int k = 0, n = 0;
		uint32_t q = money;
		do d[k++] = '0' + q % 10, q /= 10;
		while (q);
		m[n++] = (char) 0xC2, m[n++] = (char) 0xA5; /* ¥ */
		for (int i = k - 1; i >= 0; --i) {
			m[n++] = d[i];
			if (i && i % 3 == 0) m[n++] = ',';
		}
		m[n] = 0;
		text(W - 6 - text_width(m), 2, m, RGB(248, 232, 128), RGB(24, 32, 56));
	}
	for (int i = 0; i < count; ++i) card(e, &mons[i], i);
	lastKey = key;
	have = true;
	return out;
}
