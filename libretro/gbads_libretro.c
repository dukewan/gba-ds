/* GBA DS as a libretro core, for 游典's player (github.com/kyannon/gba-ds, GPL-3.0; this file too).
 *
 * The game runs on GBA DS's mGBA wrapper (src/emu.c); its second screen is what GBA DS's map capture
 * makes of it (src/mapcap.c, the games' profiles in src/profiles.c): a silent copy of the game, given
 * the live game's state now and then, opens the game's own map / status screen and hands the picture
 * back. The frontend gets one picture 240 x 320: the game above, the second screen below — split over
 * a dual-screen handheld's two screens, stacked on one. The refresh follows GBA DS's own game loop
 * (src/game.c, "map refresh"): every map_interval frames, after a room transition (a dark frame) or
 * a change of the profile's room key, backing off while captures keep failing, back to idle when the
 * game is on its title / file menus.
 *
 * Not here (yet): the HUD split, the pinball tables over both screens, the logo on the idle screen.
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
#include "../src/profiles.h"

#include <stdarg.h>
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

static Emu emu;
static bool loaded;
static char romPath[4096];
static void* savedata;
static bool saveAttached;
static uint32_t out[OUT_W * OUT_H];
static int16_t audioBuf[4096 * 2];

/* the second screen */
static MapCapture* mc;
static MapResult mapRes;
static int mapShownIdx = -1; /* the capture's frame shown, -1: idle */
static unsigned long long frameCount, mapAnimStart;
static int sinceMapRequest;
static long refreshAt = -1;
static bool wasDark, forceMap = true;
static uint32_t seenRoomKey, lastRoomKey;
static int seenRoomFor;
static int lastFails;
static const MapProfile* hideWhenProf;
static int hideWhenN;
static bool hideWhenOn;
static bool liveOn;

static void logf_(enum retro_log_level level, const char* fmt, ...) {
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (log_cb) log_cb(level, "[gbads] %s\n", buf);
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_set_environment(retro_environment_t cb) {
	environ_cb = cb;
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
}

/* (src/game.c frame_is_dark: a room transition's fade) */
static bool frame_is_dark(const uint32_t* fb) {
	int bright = 0, total = 0;
	for (int i = 0; i < GBA_W * GBA_H; i += 7) {
		uint32_t c = fb[i];
		int lum = ((c & 0xFF) + ((c >> 8) & 0xFF) * 2 + ((c >> 16) & 0xFF)) >> 2;
		bright += lum > 40;
		++total;
	}
	return bright * 40 < total;
}

static void second_setup(const char* code) {
	mc = mapcap_create(romPath, code, true);
	const MapProfile* lp = profile_find(code);
	memprobe_virtual = lp ? lp->virt : NULL;
	if (!mc) {
		logf_(RETRO_LOG_INFO, "no second-screen profile for %s: one screen", code);
		return;
	}
	logf_(RETRO_LOG_INFO, "second-screen profile: %s", mapcap_title(mc));
	mapcap_set_still(mc, false);
	/* the full map is always up: the game's own corner mini-map hidden (src/game.c) */
	const MapProfile* prof = mapcap_profile(mc);
	int nRules = 0;
	while (prof && nRules < (int) (sizeof(prof->hideSprites) / sizeof(prof->hideSprites[0])) && prof->hideSprites[nRules].x1) ++nRules;
	hideWhenProf = prof && nRules && prof->hideWhen[0].addr ? prof : NULL;
	hideWhenN = nRules, hideWhenOn = false;
	emu_hide_sprites(&emu, prof && !hideWhenProf ? prof->hideSprites : NULL, hideWhenProf ? 0 : nRules);
	emu_hide_grouped(&emu, prof && prof->hideGrouped);
}

bool retro_load_game(const struct retro_game_info* game) {
	if (!game || !game->path) return false;
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) return false;
	snprintf(romPath, sizeof(romPath), "%s", game->path);
	if (!emu_open(&emu, romPath, NULL, true)) {
		logf_(RETRO_LOG_ERROR, "could not open %s", romPath);
		return false;
	}
	loaded = true;
	saveAttached = false;
	profile_set_rom(emu.gameCode, emu.romCrc);
	frameCount = mapAnimStart = 0;
	sinceMapRequest = 0;
	refreshAt = -1, wasDark = false, forceMap = true;
	seenRoomKey = lastRoomKey = 0, seenRoomFor = 0, lastFails = 0;
	mapShownIdx = -1, liveOn = false;
	memset(&mapRes, 0, sizeof(mapRes));
	second_setup(emu.gameCode);
	logf_(RETRO_LOG_INFO, "%s (%s)", romPath, emu.gameCode);
	return true;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info* info, size_t num) {
	(void) type, (void) info, (void) num;
	return false;
}

void retro_unload_game(void) {
	if (!loaded) return;
	if (mc) mapcap_destroy(mc);
	mc = NULL;
	emu_close(&emu);
	loaded = false;
}

void retro_reset(void) {
	if (loaded) emu_reset(&emu);
	mapShownIdx = -1, forceMap = true;
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

/* GBA DS's refresh of the second screen, a frame's part of it (src/game.c's game loop) */
static void second_frame(void) {
	if (!mc) return;
	++sinceMapRequest;
	if (hideWhenProf && memprobes_match(hideWhenProf->hideWhen, 2, &emu) != hideWhenOn) {
		hideWhenOn = !hideWhenOn;
		emu_hide_sprites(&emu, hideWhenOn ? hideWhenProf->hideSprites : NULL, hideWhenOn ? hideWhenN : 0);
	}
	bool dark = frame_is_dark(emu.fb);
	if (wasDark && !dark) refreshAt = (long) frameCount + 6;
	wasDark = dark;
	if (refreshAt >= 0 && (long) frameCount >= refreshAt) {
		forceMap = true;
		refreshAt = -1;
	}
	if (mapShownIdx >= 0 && frameCount % 30 == 0 && mapcap_in_menu(mc, &emu)) {
		/* back on the title: the map belonged to the old session */
		mapShownIdx = -1;
		forceMap = true;
	}
	uint32_t rk = mapcap_room_key(mc, &emu);
	if (rk != seenRoomKey) seenRoomKey = rk, seenRoomFor = 0;
	else ++seenRoomFor;
	bool roomChanged = rk && rk != lastRoomKey && seenRoomFor >= 2;
	if (roomChanged && mapcap_busy(mc)) mapcap_cancel(mc);
	const char* why = NULL;
	int fails = mapcap_failures(mc, &why);
	if (fails != lastFails) {
		if (fails && why) logf_(RETRO_LOG_DEBUG, "capture failed (%d in a row): %s", fails, why);
		lastFails = fails;
	}
	bool cheapFail = why && !strcmp(why, "map button did nothing");
	const MapProfile* ip = mapcap_profile(mc);
	int every = ip && ip->interval > 0 && ip->interval < MAP_INTERVAL ? ip->interval : MAP_INTERVAL;
	int interval = cheapFail ? every : every << (fails < 3 ? fails : 3);
	bool due = sinceMapRequest >= interval || ((forceMap || roomChanged) && (!fails || sinceMapRequest >= every));
	if (due && !mapcap_busy(mc) && mapcap_live_ok(mc, &emu)) {
		if (mapcap_request(mc, &emu)) {
			sinceMapRequest = 0;
			lastRoomKey = rk;
			forceMap = false;
		}
	}
	bool fresh = mapcap_poll(mc, mapRes.serial, &mapRes);
	if (fresh && !mapcap_in_menu(mc, &emu)) {
		bool pages = mapRes.nFrames > 1 && mapRes.frameStep >= 60;
		if (!pages) mapAnimStart = frameCount;
		mapShownIdx = 0;
	}
	if (mapShownIdx >= 0 && mapRes.nFrames > 1 && !(ip && ip->mapStill)) {
		unsigned long long step = mapRes.frameStep > 0 ? (unsigned long long) mapRes.frameStep : 1;
		mapShownIdx = frameCount < mapAnimStart ? 0 : (int) (((frameCount - mapAnimStart) / step) % (unsigned long long) mapRes.nFrames);
	}
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

void retro_run(void) {
	if (!loaded) return;
	attach_save();
	input_poll_cb();
	emu_frame(&emu, read_keys());
	++frameCount;
	second_frame();

	for (int i = 0; i < GBA_W * GBA_H; ++i) out[i] = xrgb(emu.fb[i]);
	const MapProfile* lvp = mc ? mapcap_profile(mc) : NULL;
	const uint32_t* lv = lvp && lvp->liveView ? lvp->liveView(&emu, romPath) : NULL;
	int mw = 0, mh = 0;
	if (mc) mapcap_size(mc, &mw, &mh);
	if (lv) {
		place(lv, mw, mh, GBA_H, 0);
		liveOn = true;
	} else if (mapShownIdx >= 0 && mapRes.ok && mapRes.frames[mapShownIdx]) {
		place(mapRes.frames[mapShownIdx], mapRes.w, mapRes.h, GBA_H, 0);
	} else {
		/* idle: GBA DS's dark grey (profiles.h SCREEN_GREY) */
		place(NULL, 0, 0, GBA_H, 0x303030);
	}
	video_cb(out, OUT_W, OUT_H, OUT_W * sizeof(uint32_t));

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
	if (ok) forceMap = true;
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
