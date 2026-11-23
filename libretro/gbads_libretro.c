/* GBA DS as a libretro core, for 游典's player (github.com/kyannon/gba-ds, GPL-3.0; this file too).
 *
 * GBA DS's own game code runs each frame (src/game.c through libretro/lr_game.inc): the game on its mGBA
 * wrapper, and everything its second screen does — the map / status capture on a silent copy of the game,
 * the HUD split, the pinball tables over both screens, the status snapshots, fights, the game's logo
 * when there's nothing else. Nothing of its windows, menus or sound device: libretro/shim.c and the
 * generated libretro/stubs.c stand in for SDL and its ui.c. The frontend gets one picture 240 x 320, the
 * game screen above, the second screen below (as GBA DS draws it, a GBA pixel each) — split over a
 * dual-screen handheld's two screens, stacked on one.
 */
#include "libretro.h"

/* mGBA's first: GBA DS's emu.h defines GBA_H (the screen's height), mGBA's gba.h's include guard */
#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/memory.h>
#include <mgba/internal/gba/savedata.h>
#include <mgba-util/memory.h>
#include <mgba-util/vfs.h>
#undef GBA_H

#include "../src/emu.h"
#include "../src/mapcap.h"
#include "../src/poke.h"
#include "../src/touch.h"

bool lr_second_paged(void);
void lr_second_tap(void);
int lr_second_page(void); /* the page shown now (-1: none) */
void lr_second_refresh(void);
#include "../src/profiles.h"

#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUT_W GBA_W
#define OUT_H (GBA_H * 2)
#define GBA_FPS (16777216.0 / 280896.0)
#define MAP_INTERVAL 60

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;

static bool loaded;
static unsigned avRate; /* the sound rate last given in retro_get_system_av_info */
static char romPath[4096];
static void* savedata;
static bool saveAttached;
static uint32_t out[OUT_W * OUT_H];
static int16_t audioBuf[4096 * 2];

/* GBA DS's game (src/game.c, libretro/lr_game.inc): its emulator, its second screen */
bool lr_open(const char* rom, const char* stateDir);
void lr_close(void);
void lr_frame(uint32_t keys);
void lr_state_loaded(void);
Emu* lr_emu(void);
const char* lr_profile_title(void);
int lr_kind(void);
const uint32_t* lr_game_picture(bool* dim);
const uint32_t* lr_second_picture(int* w, int* h, bool* dimmed, uint32_t* ground);
#define emu (*lr_emu())

static void logf_(enum retro_log_level level, const char* fmt, ...) {
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (log_cb) log_cb(level, "[gbads] %s\n", buf);
}

/* GBA DS logs to stderr (what the second screen did and why): its lines to the frontend's log */
static void* stderr_reader(void* arg) {
	FILE* in = fdopen((int) (intptr_t) arg, "r");
	char line[512];
	while (in && fgets(line, sizeof(line), in)) {
		line[strcspn(line, "\n")] = 0;
		if (line[0] && log_cb) log_cb(RETRO_LOG_INFO, "[gbads] %s\n", line);
	}
	return NULL;
}

static void stderr_to_log(void) {
	static bool done;
	int fds[2];
	if (done || pipe(fds) != 0) return;
	done = true;
	setvbuf(stderr, NULL, _IOLBF, 0);
	dup2(fds[1], 2);
	pthread_t t;
	pthread_create(&t, NULL, stderr_reader, (void*) (intptr_t) fds[0]);
	pthread_detach(t);
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

/* 下屏页面: a frontend's core option (游典's 内核设置) */
static void read_options(void) {
	struct retro_variable var = { "gbads_pages", NULL };
	if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		gbads_mapOnly = strcmp(var.value, "cycle") != 0;
}

void retro_set_environment(retro_environment_t cb) {
	environ_cb = cb;
	static const struct retro_variable vars[] = {
		{ "gbads_pages", "Second screen pages (The Minish Cap); map|cycle" },
		{ NULL, NULL },
	};
	cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*) vars);
	bool yes = true;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &(bool) { false });
	(void) yes;
	struct retro_log_callback logging;
	if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging)) log_cb = logging.log;
	static const struct retro_input_descriptor desc[] = {
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
		{ 0 },
	};
	cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void*) desc);
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void) cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }
void retro_set_controller_port_device(unsigned port, unsigned device) { (void) port, (void) device; }

void retro_init(void) {
	stderr_to_log();
	savedata = anonymousMemoryMap(GBA_SIZE_FLASH1M);
	memset(savedata, 0xFF, GBA_SIZE_FLASH1M);
}

void retro_deinit(void) {
	if (savedata) mappedMemoryFree(savedata, GBA_SIZE_FLASH1M);
	savedata = NULL;
}

void retro_get_system_info(struct retro_system_info* info) {
	memset(info, 0, sizeof(*info));
	info->library_name = "GBA DS";
	info->library_version = "1.5.6-youdian";
	info->valid_extensions = "gba|zip|7z";
	info->need_fullpath = true;
	info->block_extract = true;
}

void retro_get_system_av_info(struct retro_system_av_info* info) {
	memset(info, 0, sizeof(*info));
	info->geometry.base_width = OUT_W;
	info->geometry.base_height = OUT_H;
	info->geometry.max_width = OUT_W;
	info->geometry.max_height = OUT_H;
	info->geometry.aspect_ratio = (float) OUT_W / OUT_H;
	info->timing.fps = GBA_FPS;
	info->timing.sample_rate = loaded ? emu_audio_rate(&emu) : 32768;
	avRate = (unsigned) info->timing.sample_rate;
}

/* Where GBA DS keeps what it makes of a game (the logo it extracts from the title screen): beside the core,
 * in 游典's own storage (not the member's saves, which 云同步 goes through). */
static void state_dir(char* out, size_t n) {
	Dl_info info;
	if (dladdr((void*) retro_load_game, &info) && info.dli_fname) {
		snprintf(out, n, "%s", info.dli_fname);
		char* slash = strrchr(out, '/');
		if (slash) *slash = 0;
		strncat(out, "/gbads", n - strlen(out) - 1);
	} else {
		snprintf(out, n, "/data/local/tmp/gbads");
	}
	mkdir(out, 0755);
}

bool retro_load_game(const struct retro_game_info* game) {
	if (!game || !game->path) return false;
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) return false;
	read_options();
	snprintf(romPath, sizeof(romPath), "%s", game->path);
	char dir[4096];
	state_dir(dir, sizeof(dir));
	if (!lr_open(romPath, dir)) {
		logf_(RETRO_LOG_ERROR, "could not open %s", romPath);
		return false;
	}
	loaded = true;
	saveAttached = false;
	int kind = lr_kind();
	const MapProfile* p = profile_find(emu.gameCode);
	if (kind) {
		logf_(RETRO_LOG_INFO, "second screen: %s (%s%s%s%s%s%s)", p ? p->title : emu.gameCode, kind & 1 ? "map/status capture " : "",
		      kind & 2 ? "HUD split " : "", kind & 4 ? "pinball table " : "", kind & 8 ? "snapshots " : "", kind & 16 ? "fights " : "",
		      kind & 32 ? "logo only" : "");
	} else {
		logf_(RETRO_LOG_INFO, "no second screen for %s: one screen, its logo below", emu.gameCode);
	}
	logf_(RETRO_LOG_INFO, "%s (%s)", romPath, emu.gameCode);
	return true;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info* info, size_t num) {
	(void) type, (void) info, (void) num;
	return false;
}

void retro_unload_game(void) {
	if (!loaded) return;
	lr_close();
	loaded = false;
}

void retro_reset(void) {
	if (loaded) {
		emu_reset(&emu);
		lr_state_loaded();
	}
}

static uint32_t read_keys(void) {
	static const struct { unsigned id; uint32_t key; } map[] = {
		{ RETRO_DEVICE_ID_JOYPAD_A, K_A }, { RETRO_DEVICE_ID_JOYPAD_B, K_B },
		{ RETRO_DEVICE_ID_JOYPAD_SELECT, K_SELECT }, { RETRO_DEVICE_ID_JOYPAD_START, K_START },
		{ RETRO_DEVICE_ID_JOYPAD_RIGHT, K_RIGHT }, { RETRO_DEVICE_ID_JOYPAD_LEFT, K_LEFT },
		{ RETRO_DEVICE_ID_JOYPAD_UP, K_UP }, { RETRO_DEVICE_ID_JOYPAD_DOWN, K_DOWN },
		{ RETRO_DEVICE_ID_JOYPAD_R, K_R }, { RETRO_DEVICE_ID_JOYPAD_L, K_L },
	};
	uint32_t keys = 0;
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
		if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, map[i].id)) keys |= map[i].key;
	}
	/* (no left+right, up+down at once: some games break on it, as mGBA's own frontends keep it) */
	if ((keys & (K_LEFT | K_RIGHT)) == (K_LEFT | K_RIGHT)) keys &= ~(K_LEFT | K_RIGHT);
	if ((keys & (K_UP | K_DOWN)) == (K_UP | K_DOWN)) keys &= ~(K_UP | K_DOWN);
	return keys;
}

/* mGBA's XBGR8888 (bytes R, G, B, X) to libretro's XRGB8888 */
static inline uint32_t xrgb(uint32_t c) {
	return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
}

/* [px] (w x h) into the 240 x 160 box at y0: as large as fits at a whole multiple, centred; one larger
 * than the box shrunk to fit (each pixel the nearest one) */
static void place(const uint32_t* px, int w, int h, int y0, uint32_t ground) {
	uint32_t* box = out + y0 * OUT_W;
	for (int i = 0; i < OUT_W * GBA_H; ++i) box[i] = ground;
	if (!px || w <= 0 || h <= 0) return;
	if (w <= OUT_W && h <= GBA_H) {
		int k = OUT_W / w < GBA_H / h ? OUT_W / w : GBA_H / h;
		int dw = w * k, dh = h * k, x0 = (OUT_W - dw) / 2, yy = (GBA_H - dh) / 2;
		for (int y = 0; y < dh; ++y) {
			const uint32_t* src = px + (y / k) * w;
			uint32_t* dst = box + (yy + y) * OUT_W + x0;
			for (int x = 0; x < dw; ++x) dst[x] = xrgb(src[x / k]);
		}
		return;
	}
	/* larger: fit, keeping its shape */
	double s = (double) OUT_W / w < (double) GBA_H / h ? (double) OUT_W / w : (double) GBA_H / h;
	int dw = (int) (w * s), dh = (int) (h * s), x0 = (OUT_W - dw) / 2, yy = (GBA_H - dh) / 2;
	for (int y = 0; y < dh; ++y) {
		const uint32_t* src = px + (int) (y / s) * w;
		uint32_t* dst = box + (yy + y) * OUT_W + x0;
		for (int x = 0; x < dw; ++x) dst[x] = xrgb(src[(int) (x / s)]);
	}
}

/* The battery save in its buffer given to the game, once the frontend has filled it: before the first
 * frame or a state's size is asked, as mGBA's libretro core does (its deferred setup) — a state's size
 * depends on it, and 游典 tells a state of another core's from this one's by its size. */
static void attach_save(void) {
	if (saveAttached || !loaded) return;
	struct VFile* vf = VFileFromMemory(savedata, GBA_SIZE_FLASH1M);
	if (vf && !emu.core->loadSave(emu.core, vf)) vf->close(vf);
	saveAttached = true;
}

/* dimmed as GBA DS dims a picture: black over it at [alpha] / 255 */
static inline uint32_t dim(uint32_t c, int alpha) {
	int k = 255 - alpha;
	return (((c >> 16) & 0xFF) * k / 255) << 16 | (((c >> 8) & 0xFF) * k / 255) << 8 | ((c & 0xFF) * k / 255);
}

void retro_run(void) {
	if (!loaded) return;
	bool changed = false;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &changed) && changed) read_options();
	attach_save();
	input_poll_cb();
	/* the Pokémon games' second screen takes touches (its pages): the frame's lower half, a
	 * second-screen pixel each (polled only for them, so other games' taps stay the frontend's) */
	/* (a game whose second screen turns pages, or has its own taps: GBA DS's touch.c) */
	bool pokemon = poke_supported(emu.gameCode);
	if (pokemon || lr_second_paged() || touch_wanted(&emu)) {
		static bool wasDown;
		bool down = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED);
		int px = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
		int py = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
		int fx = (px + 0x7FFF) * GBA_W / 0xFFFF, fy = (py + 0x7FFF) * (2 * GBA_H) / 0xFFFF - GBA_H;
		down = down && fy >= 0;
		if (pokemon) poke_touch(fx, fy, down);
		else if (down && !wasDown) {
			if (touch_tap(&emu, lr_second_page(), fx, fy)) lr_second_refresh();
			else lr_second_tap();
		}
		wasDown = down;
	}
	lr_frame(read_keys() | (poke_supported(emu.gameCode) ? poke_keys() : touch_keys()));

	bool dimGame = false;
	const uint32_t* game = lr_game_picture(&dimGame);
	for (int i = 0; i < GBA_W * GBA_H; ++i) out[i] = dimGame ? dim(xrgb(game[i]), 150) : xrgb(game[i]);
	int w = 0, h = 0;
	bool dimmed = false;
	uint32_t ground = 0;
	const uint32_t* px = lr_second_picture(&w, &h, &dimmed, &ground);
	if (px && w == GBA_W * 2 && h == GBA_H * 2) {
		/* the HUD's picture, at 2x (a grid cell per GBA pixel): a GBA pixel each */
		uint32_t* box = out + GBA_H * OUT_W;
		for (int y = 0; y < GBA_H; ++y) for (int x = 0; x < GBA_W; ++x) box[y * OUT_W + x] = xrgb(px[(y * 2) * w + x * 2]);
	} else {
		place(px, w, h, GBA_H, xrgb(ground));
	}
	if (dimmed) for (int i = GBA_W * GBA_H; i < OUT_W * OUT_H; ++i) out[i] = dim(out[i], 120);
	video_cb(out, OUT_W, OUT_H, OUT_W * sizeof(uint32_t));

	/* The game's own sound rate, told when it changes (a game sets it as it starts: Mario Kart's 65536 after
	 * the 32768 it boots at), as mGBA's libretro core does (its _audioRateChanged): a frontend playing 65536
	 * as 32768 takes two seconds over each second's sound — 游典, paced by its sound, ran such games at
	 * half speed, an octave down. Before this frame's sound is handed over. */
	unsigned rate = emu_audio_rate(&emu);
	if (rate && avRate && rate != avRate) {
		struct retro_system_av_info info;
		retro_get_system_av_info(&info); /* (avRate is this rate from here) */
		environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &info);
	}
	size_t got;
	while ((got = emu_audio_read(&emu, audioBuf, 4096)) > 0) audio_batch_cb(audioBuf, got);
}

/* mGBA's libretro core's form: 游典's 继续 and slots go between it and this core */
size_t retro_serialize_size(void) {
	attach_save();
	return loaded ? emu_state_named_size(&emu) : 0;
}
bool retro_serialize(void* data, size_t size) {
	attach_save();
	return loaded && emu_state_named_save(&emu, data, size);
}
bool retro_unserialize(const void* data, size_t size) {
	attach_save();
	if (!loaded) return false;
	bool ok = emu_state_named_load(&emu, data, size);
	if (ok) lr_state_loaded();
	return ok;
}

void retro_cheat_reset(void) {
	if (loaded) emu_cheats_clear(&emu);
}
void retro_cheat_set(unsigned index, bool enabled, const char* code) {
	if (!loaded || !enabled || !code) return;
	const char* codes[1] = { code };
	char name[16];
	snprintf(name, sizeof(name), "%u", index);
	emu_cheats_add(&emu, name, codes, 1, EMU_CHEAT_DETECT);
}

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

void* retro_get_memory_data(unsigned id) {
	if (id == RETRO_MEMORY_SAVE_RAM) return savedata;
	if (id == RETRO_MEMORY_SYSTEM_RAM && loaded) {
		size_t n = 0;
		return emu_memory(&emu, 2, &n);
	}
	return NULL;
}

size_t retro_get_memory_size(unsigned id) {
	if (!loaded) return 0;
	if (id == RETRO_MEMORY_SAVE_RAM) {
		struct GBA* gba = emu.core->board;
		if (gba->memory.savedata.type == GBA_SAVEDATA_AUTODETECT) return GBA_SIZE_FLASH1M;
		return GBASavedataSize(&gba->memory.savedata);
	}
	if (id == RETRO_MEMORY_SYSTEM_RAM) {
		size_t n = 0;
		emu_memory(&emu, 2, &n);
		return n;
	}
	return 0;
}
