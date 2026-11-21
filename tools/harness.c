/* Headless scripted runner used for development and regression tests.
 *
 * usage: harness ROM OUTDIR [SCRIPT]   (script read from stdin if omitted)
 *
 * Script commands (one per line, '#' comments):
 *   hold KEYS N          hold KEYS for N frames (KEYS: A+B+START..., or '-')
 *   wait N               run N frames with no input
 *   tap KEYS [COUNT]     press KEYS 3 frames then release 9 frames, COUNT times
 *   shot NAME            write OUTDIR/NAME.png
 *   seq NAME N STEP      N shots STEP frames apart (no input) NAME_00.png...
 *   save NAME / load NAME   savestate to/from OUTDIR/NAME.ss (or NAME itself, if it has a '/')
 *   map NAME             run shadow map capture, write OUTDIR/NAME.png
 *   peek ADDR LEN        hex dump memory
 *   watch ADDR LEN       record bytes; 'diff' prints addresses that changed
 *   diff
 *   poke ADDR LEN VAL    write VAL (little-endian, LEN bytes)
 *   layershot NAME       per-layer renders of the next frame (transparent = magenta)
 *   dumpram NAME         write EWRAM+IWRAM to OUTDIR/NAME.ram
 */
#include "../src/emu.h"
#include "../src/mapcap.h"
#include "../src/profiles.h"
#include "../src/logo.h"

#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#include <mgba-util/vfs.h>
#include <fcntl.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Emu emu;
static const char* outdir;
static uint32_t frameNo;

static uint32_t parse_keys(const char* s) {
	uint32_t k = 0;
	char buf[128];
	strncpy(buf, s, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	for (char* t = strtok(buf, "+"); t; t = strtok(NULL, "+")) {
		if (!strcmp(t, "A")) k |= K_A;
		else if (!strcmp(t, "B")) k |= K_B;
		else if (!strcmp(t, "SELECT") || !strcmp(t, "SEL")) k |= K_SELECT;
		else if (!strcmp(t, "START")) k |= K_START;
		else if (!strcmp(t, "RIGHT")) k |= K_RIGHT;
		else if (!strcmp(t, "LEFT")) k |= K_LEFT;
		else if (!strcmp(t, "UP")) k |= K_UP;
		else if (!strcmp(t, "DOWN")) k |= K_DOWN;
		else if (!strcmp(t, "R")) k |= K_R;
		else if (!strcmp(t, "L")) k |= K_L;
		else if (!strcmp(t, "-")) ;
		else fprintf(stderr, "unknown key %s\n", t);
	}
	return k;
}

static void run(uint32_t keys, int n) {
	for (int i = 0; i < n; ++i) {
		emu_frame(&emu, keys);
		++frameNo;
	}
}

static uint32_t* hudBuf; /* split's picture */

static void shot(const char* name) {
	char path[512];
	snprintf(path, sizeof(path), "%s/%s.png", outdir, name);
	write_png(path, emu.fb, GBA_W, GBA_H, GBA_W);
}

static uint8_t watchBuf[0x40000];
static uint32_t watchAddr, watchLen;

int main(int argc, char** argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: %s ROM OUTDIR [SCRIPT]\n", argv[0]);
		return 1;
	}
	outdir = argv[2];
	FILE* in = argc > 3 ? fopen(argv[3], "r") : stdin;
	if (!in) {
		perror("script");
		return 1;
	}
	if (!emu_open(&emu, argv[1], NULL, false)) {
		return 1;
	}
	fprintf(stderr, "game code %s\n", emu.gameCode);
	/* the profile's ROM patches, as the app applies them (HUD split games) */
	if (profile_patch_rom(profile_find(emu.gameCode), &emu)) fprintf(stderr, "ROM patched\n");
	MapCapture* mc = mapcap_create(argv[1], emu.gameCode, getenv("FULLMAP") == NULL);

	char line[512];
	while (fgets(line, sizeof(line), in)) {
		char* p = line;
		while (isspace((unsigned char) *p)) ++p;
		if (!*p || *p == '#') continue;
		char cmd[32] = "", a1[256] = "", a2[64] = "", a3[64] = "";
		sscanf(p, "%31s %255s %63s %63s", cmd, a1, a2, a3);
		if (!strcmp(cmd, "auto")) {
			/* auto MAXFRAMES NAME: plays on its own (START / A taps through intros, menus and name entry,
			 * walking about in between) until the profile says it's in the game for 10 s at once — the map
			 * actually captured, for a map game — then writes NAME.state in mGBA's state-file form (save
			 * data and clock in it: what 游典's core and GBA DS's own states both read). Shots every 20 s. */
			const MapProfile* pf = profile_find(emu.gameCode);
			int maxf = atoi(a1), good = 0, step = 0;
			bool done = false;
			while ((int) frameNo < maxf && !done) {
				/* not in the game yet: START and A only (a menu's first choice is the new game; START jumps a
				 * name entry to its OK); in it: walking about, A for its talk */
				static const struct { const char* k; int n; } pre[] = {
					{ "START", 4 }, { "-", 40 }, { "A", 4 }, { "-", 40 }, { "A", 4 }, { "-", 40 },
				}, play[] = {
					{ "RIGHT", 60 }, { "A", 4 }, { "-", 20 }, { "LEFT", 30 }, { "A", 4 }, { "-", 20 },
				};
				/* AUTO_PATTERN 1: A only; 2: A and START with DOWN (a menu whose new game is the second) */
				static const struct { const char* k; int n; } preA[] = {
					{ "A", 4 }, { "-", 40 }, { "A", 4 }, { "-", 40 }, { "A", 4 }, { "-", 40 },
				}, preB[] = {
					{ "START", 4 }, { "-", 40 }, { "DOWN", 4 }, { "-", 20 }, { "A", 4 }, { "-", 40 },
				};
				int pt = getenv("AUTO_PATTERN") ? atoi(getenv("AUTO_PATTERN")) : 0;
				bool inGame = good > 0;
				int np = 6;
				uint32_t k = parse_keys(inGame ? play[step % np].k : pt == 1 ? preA[step % np].k : pt == 2 ? preB[step % np].k : pre[step % np].k);
				int n = inGame ? play[step % np].n : pt == 1 ? preA[step % np].n : pt == 2 ? preB[step % np].n : pre[step % np].n;
				++step;
				run(k, n);
				bool in = false;
				if (mc) in = mapcap_live_ok(mc, &emu) && !mapcap_in_menu(mc, &emu);
				else if (pf && pf->hud.on[0].addr) in = memprobes_match(pf->hud.on, 4, &emu);
				else if (pf && pf->tall.on[0].addr) in = memprobes_match(pf->tall.on, 2, &emu);
				good = in ? good + n : 0;
				if (getenv("AUTO_TRACE") && frameNo % 600 < (unsigned) n) fprintf(stderr, "frame %u in %d good %d live %d menu %d\n", frameNo, in, good,
					mc ? mapcap_live_ok(mc, &emu) : -1, mc ? mapcap_in_menu(mc, &emu) : -1);
				if (good >= 600) {
					if (mc) {
						MapResult r = mapcap_capture_sync(mc, &emu);
						/* the profile's probes can match an attract demo too (Circle of the Moon's): only a map
						 * actually opened counts; else back to START and A */
						if (!r.ok) { good = 0; continue; }
						char mp[600];
						snprintf(mp, sizeof(mp), "%s/%s.map.png", outdir, a2);
						write_png(mp, r.pixels, r.w, r.h, r.w);
					}
					done = true;
				}
				if (frameNo % 1200 < (unsigned) n) {
					char nm[300];
					snprintf(nm, sizeof(nm), "%s_%05u", a2, frameNo);
					shot(nm);
				}
			}
			/* the moment it was found in the game, kept: the walk below may end in a menu */
			size_t ssz = emu_state_size(&emu);
			void* inState = done ? malloc(ssz) : NULL;
			if (inState) emu_state_save(&emu, inState);
			/* (a3: frames more of walking about once in, deeper into the game) */
			for (int extra = a3[0] ? atoi(a3) : 0, i = 0; done && extra > 0; ++i) {
				static const char* walk[] = { "RIGHT", "A", "-", "RIGHT", "UP", "A", "LEFT", "DOWN" };
				run(parse_keys(walk[i % 8]), 30);
				extra -= 30;
			}
			if (inState) {
				bool still = false;
				if (mc) still = mapcap_live_ok(mc, &emu) && !mapcap_in_menu(mc, &emu);
				else if (pf && pf->hud.on[0].addr) still = memprobes_match(pf->hud.on, 4, &emu);
				else if (pf && pf->tall.on[0].addr) still = memprobes_match(pf->tall.on, 2, &emu);
				if (!still) {
					emu_state_load(&emu, inState);
					run(0, 2);
					printf("auto %s: back to where it was found in the game (the walk ended out of it)\n", a2);
				}
				free(inState);
			}
			if (done && mc) {
				MapResult r = mapcap_capture_sync(mc, &emu);
				if (r.ok) {
					char mp[600];
					snprintf(mp, sizeof(mp), "%s/%s.map.png", outdir, a2);
					write_png(mp, r.pixels, r.w, r.h, r.w);
				}
			}
			char sp[600];
			snprintf(sp, sizeof(sp), "%s/%s.%s", outdir, a2, done ? "state" : "notyet.state");
			/* (into memory first: mGBA maps the file it writes a state to, which a write-only file can't be) */
			struct VFile* vf = VFileMemChunk(NULL, 0);
			mCoreSaveStateNamed(emu.core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC);
			ssize_t vn = vf->size(vf);
			void* vb = malloc((size_t) vn);
			vf->seek(vf, 0, SEEK_SET);
			vf->read(vf, vb, (size_t) vn);
			vf->close(vf);
			FILE* sf = fopen(sp, "wb");
			if (sf) fwrite(vb, 1, (size_t) vn, sf), fclose(sf);
			free(vb);
			char fin[300];
			snprintf(fin, sizeof(fin), "%s_end", a2);
			shot(fin);
			printf("auto %s: %s at frame %u\n", a2, done ? "in the game" : "not in the game", frameNo);
		} else if (!strcmp(cmd, "hold")) {
			run(parse_keys(a1), atoi(a2));
		} else if (!strcmp(cmd, "wait")) {
			run(0, atoi(a1));
		} else if (!strcmp(cmd, "tap")) {
			int c = a2[0] ? atoi(a2) : 1;
			uint32_t k = parse_keys(a1);
			for (int i = 0; i < c; ++i) {
				run(k, 3);
				run(0, 9);
			}
		} else if (!strcmp(cmd, "shot")) {
			shot(a1);
		} else if (!strcmp(cmd, "split")) {
			/* split BGMASK PRIO (on) / split off: the HUD split (emu_hud_split) */
			static uint32_t hud[GBA_W * GBA_H];
			if (!strcmp(a1, "off")) emu_hud_split(&emu, false, 0, NULL, 0, hud);
			else {
				/* sprites of priority PRIO */
				SpriteRule r = { -512, -256, 511, 255, 0, 1023, atoi(a2) };
				emu_hud_split(&emu, true, strtoul(a1, NULL, 0), &r, 1, hud);
			}
			hudBuf = hud;
		} else if (!strcmp(cmd, "hudshot")) {
			/* the HUD split's picture */
			char path[512];
			snprintf(path, sizeof(path), "%s/%s.png", outdir, a1);
			if (hudBuf) write_png(path, hudBuf, GBA_W, GBA_H, GBA_W);
		} else if (!strcmp(cmd, "seq")) {
			int n = atoi(a2), step = atoi(a3);
			for (int i = 0; i < n; ++i) {
				char name[300];
				snprintf(name, sizeof(name), "%s_%02d", a1, i);
				shot(name);
				run(0, step);
			}
		} else if (!strcmp(cmd, "save") || !strcmp(cmd, "load")) {
			char path[512];
			/* (a path with a '/' as it is: a bug report's states) */
			if (strchr(a1, '/')) snprintf(path, sizeof(path), "%s", a1);
			else snprintf(path, sizeof(path), "%s/%s.ss", outdir, a1);
			bool ok = cmd[0] == 's' ? emu_state_save_file(&emu, path) : emu_state_load_file(&emu, path);
			if (!ok) fprintf(stderr, "%s %s failed\n", cmd, path);
		} else if (!strcmp(cmd, "map")) {
			if (!mc) {
				fprintf(stderr, "no map profile for %s\n", emu.gameCode);
				continue;
			}
			MapResult r = mapcap_capture_sync(mc, &emu);
			fprintf(stderr, "map capture: %s\n", r.ok ? "ok" : r.why);
			if (r.ok) {
				char path[512];
				snprintf(path, sizeof(path), "%s/%s.png", outdir, a1);
				write_png(path, r.pixels, r.w, r.h, r.w);
				for (int i = 1; i < r.nFrames; ++i) {
					snprintf(path, sizeof(path), "%s/%s_f%02d.png", outdir, a1, i);
					write_png(path, r.frames[i], r.w, r.h, r.w);
				}
				fprintf(stderr, "map frames: %d\n", r.nFrames);
			}
		} else if (!strcmp(cmd, "peek")) {
			uint32_t addr = strtoul(a1, NULL, 0), len = strtoul(a2, NULL, 0);
			for (uint32_t i = 0; i < len; ++i) {
				if (i % 16 == 0) printf("%s%08X:", i ? "\n" : "", addr + i);
				printf(" %02X", emu_read8(&emu, addr + i));
			}
			printf("\n");
		} else if (!strcmp(cmd, "io")) {
			/* I/O registers as the renderer sees them: io OFFSET COUNT (16-bit) */
			uint32_t off = strtoul(a1, NULL, 0), n = strtoul(a2, NULL, 0);
			printf("IO %03X:", off);
			for (uint32_t i = 0; i < n; ++i) printf(" %04X", emu_io16(&emu, off + i * 2));
			printf("\n");
		} else if (!strcmp(cmd, "poke")) {
			uint32_t addr = strtoul(a1, NULL, 0), len = strtoul(a2, NULL, 0), val = strtoul(a3, NULL, 0);
			/* (little-endian: len bytes of val) */
			for (uint32_t i = 0; i < len; ++i) emu.core->rawWrite8(emu.core, addr + i, -1, i < 4 ? (val >> (8 * i)) & 0xFF : 0);
		} else if (!strcmp(cmd, "logo")) {
			char path[512];
			snprintf(path, sizeof(path), "%s/%s.png", outdir, a1);
			bool ok = logo_extract(argv[1], emu.gameCode, path);
			fprintf(stderr, "%s: %s\n", cmd, ok ? "ok" : "failed");
		} else if (!strcmp(cmd, "layershot")) {
			/* For each layer (bg0-3, obj) alone: render the next frame twice with
			 * different backdrop colours; pixels that differ are transparent
			 * (written as magenta). Also writes NAME_all for reference. */
			size_t sz = emu_state_size(&emu);
			void* st = malloc(sz);
			emu_state_save(&emu, st);
			static uint32_t a[GBA_W * GBA_H], b[GBA_W * GBA_H];
			/* b = backdrop only (all layers off) */
			emu_state_load(&emu, st);
			for (int i = 0; i < 5; ++i) emu.core->enableVideoLayer(emu.core, i, false);
			emu_frame(&emu, 0);
			memcpy(b, emu.fb, sizeof(b));
			for (int L = -1; L < 5; ++L) {
				emu_state_load(&emu, st);
				for (int i = 0; i < 5; ++i) emu.core->enableVideoLayer(emu.core, i, L < 0 || i == L);
				emu_frame(&emu, 0);
				memcpy(a, emu.fb, sizeof(a));
				if (L >= 0) {
					for (int i = 0; i < GBA_W * GBA_H; ++i) {
						if (a[i] == b[i]) a[i] = 0xFFFF00FF;
					}
				}
				char path[512];
				if (L < 0) snprintf(path, sizeof(path), "%s/%s_all.png", outdir, a1);
				else snprintf(path, sizeof(path), "%s/%s_L%d.png", outdir, a1, L);
				write_png(path, a, GBA_W, GBA_H, GBA_W);
			}
			for (int i = 0; i < 5; ++i) emu.core->enableVideoLayer(emu.core, i, true);
			emu_state_load(&emu, st);
			free(st);
		} else if (!strcmp(cmd, "savestate")) {
			/* savestate FILE: mGBA's state-file form (what loadstate, 游典's core and GBA DS read) */
			struct VFile* vf = VFileMemChunk(NULL, 0);
			mCoreSaveStateNamed(emu.core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC);
			ssize_t vn = vf->size(vf);
			void* vb = malloc((size_t) vn);
			vf->seek(vf, 0, SEEK_SET);
			vf->read(vf, vb, (size_t) vn);
			vf->close(vf);
			FILE* sf = fopen(a1, "wb");
			if (sf) fwrite(vb, 1, (size_t) vn, sf), fclose(sf);
			free(vb);
		} else if (!strcmp(cmd, "loadstate")) {
			/* loadstate FILE: a state file in mGBA's form (auto's, 游典's, GBA DS's) */
			struct VFile* vf = VFileOpen(a1, O_RDONLY);
			bool ok = vf && mCoreLoadStateNamed(emu.core, vf, SAVESTATE_RTC);
			if (vf) vf->close(vf);
			fprintf(stderr, "loadstate %s: %s\n", a1, ok ? "ok" : "failed");
		} else if (!strcmp(cmd, "dumpram")) {
			/* EWRAM (256K) followed by IWRAM (32K) */
			char path[512];
			snprintf(path, sizeof(path), "%s/%s.ram", outdir, a1);
			FILE* f = fopen(path, "wb");
			for (uint32_t i = 0; i < 0x40000; ++i) fputc(emu_read8(&emu, 0x02000000 + i), f);
			for (uint32_t i = 0; i < 0x8000; ++i) fputc(emu_read8(&emu, 0x03000000 + i), f);
			fclose(f);
		} else if (!strcmp(cmd, "watch")) {
			watchAddr = strtoul(a1, NULL, 0);
			watchLen = strtoul(a2, NULL, 0);
			if (watchLen > sizeof(watchBuf)) watchLen = sizeof(watchBuf);
			for (uint32_t i = 0; i < watchLen; ++i) watchBuf[i] = emu_read8(&emu, watchAddr + i);
		} else if (!strcmp(cmd, "diff")) {
			int shown = 0;
			for (uint32_t i = 0; i < watchLen && shown < 200; ++i) {
				uint8_t v = emu_read8(&emu, watchAddr + i);
				if (v != watchBuf[i]) {
					printf("%08X: %02X -> %02X\n", watchAddr + i, watchBuf[i], v);
					++shown;
				}
			}
		} else {
			fprintf(stderr, "unknown command %s\n", cmd);
		}
	}
	fprintf(stderr, "ran %u frames\n", frameNo);
	mapcap_destroy(mc);
	emu_close(&emu);
	return 0;
}
