/* What GBA DS's game.c needs of SDL and of its ui.c in 游典's libretro core, where nothing is drawn on a
 * window: textures are pixels in memory (the core reads the second screen's from them), surfaces are
 * plain buffers, the logo is read from where GBA DS extracts it. Everything else of SDL / ui.c that the
 * game's code names but never calls here is an empty stub (libretro/stubs.c, generated). */
#include "../src/ui.h"
#include "../src/logo.h"
#include "../src/profiles.h"
#include "../src/paths.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

SDL_Surface* image_load(const char* path); /* image.c */

View views[2];
int nViews;
Screen screens[2];
int nScreens;

struct SDL_Texture {
	int w, h;
	uint32_t* px;
};

SDL_Texture* SDL_CreateTexture(SDL_Renderer* r, Uint32 format, int access, int w, int h) {
	(void) r, (void) format, (void) access;
	if (w <= 0 || h <= 0) return NULL;
	SDL_Texture* t = calloc(1, sizeof(*t));
	if (!t) return NULL;
	t->w = w, t->h = h;
	t->px = calloc((size_t) w * h, 4);
	if (!t->px) {
		free(t);
		return NULL;
	}
	return t;
}

void SDL_DestroyTexture(SDL_Texture* t) {
	if (!t) return;
	free(t->px);
	free(t);
}

int SDL_SetTextureBlendMode(SDL_Texture* t, SDL_BlendMode m) { (void) t, (void) m; return 0; }
int SDL_SetTextureScaleMode(SDL_Texture* t, SDL_ScaleMode m) { (void) t, (void) m; return 0; }
int SDL_SetTextureAlphaMod(SDL_Texture* t, Uint8 a) { (void) t, (void) a; return 0; }
int SDL_SetTextureColorMod(SDL_Texture* t, Uint8 r, Uint8 g, Uint8 b) { (void) t, (void) r, (void) g, (void) b; return 0; }
int SDL_QueryTexture(SDL_Texture* t, Uint32* f, int* a, int* w, int* h) {
	if (f) *f = SDL_PIXELFORMAT_ABGR8888;
	if (a) *a = SDL_TEXTUREACCESS_STREAMING;
	if (w) *w = t ? t->w : 0;
	if (h) *h = t ? t->h : 0;
	return t ? 0 : -1;
}

void ui_update_texture(SDL_Texture* t, const void* px, int pitch) {
	if (!t || !px) return;
	for (int y = 0; y < t->h; ++y) memcpy(t->px + (size_t) y * t->w, (const uint8_t*) px + (size_t) y * pitch, (size_t) t->w * 4);
}

void ui_free_image(SDL_Texture* tex[2]) {
	for (int i = 0; i < 2; ++i) {
		SDL_DestroyTexture(tex[i]);
		tex[i] = NULL;
	}
}

/* (libretro/lr_game.inc) */
const uint32_t* lr_texture_pixels(SDL_Texture* t, int* w, int* h) {
	if (!t) return NULL;
	*w = t->w, *h = t->h;
	return t->px;
}

/* ---- surfaces: plain RGBA32 buffers (GBA DS's images are SDL_PIXELFORMAT_RGBA32 / ABGR8888) */

static SDL_PixelFormat abgr = { .format = SDL_PIXELFORMAT_ABGR8888, .BitsPerPixel = 32, .BytesPerPixel = 4,
                                .Rmask = 0x000000FF, .Gmask = 0x0000FF00, .Bmask = 0x00FF0000, .Amask = 0xFF000000 };

SDL_Surface* SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int w, int h, int depth, Uint32 format) {
	(void) flags, (void) depth, (void) format;
	SDL_Surface* s = calloc(1, sizeof(*s));
	if (!s) return NULL;
	s->w = w, s->h = h, s->pitch = w * 4, s->format = &abgr, s->refcount = 1;
	s->pixels = calloc((size_t) w * h, 4);
	if (!s->pixels) {
		free(s);
		return NULL;
	}
	return s;
}

SDL_Surface* SDL_CreateRGBSurfaceWithFormatFrom(void* px, int w, int h, int depth, int pitch, Uint32 format) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, depth, format);
	if (!s) return NULL;
	for (int y = 0; y < h; ++y) memcpy((uint8_t*) s->pixels + (size_t) y * s->pitch, (uint8_t*) px + (size_t) y * pitch, (size_t) w * 4);
	return s;
}

void SDL_FreeSurface(SDL_Surface* s) {
	if (!s) return;
	free(s->pixels);
	free(s);
}

int SDL_LockSurface(SDL_Surface* s) { (void) s; return 0; }
void SDL_UnlockSurface(SDL_Surface* s) { (void) s; }

SDL_Surface* SDL_ConvertSurfaceFormat(SDL_Surface* s, Uint32 format, Uint32 flags) {
	(void) format, (void) flags;
	return s ? SDL_CreateRGBSurfaceWithFormatFrom(s->pixels, s->w, s->h, 32, s->pitch, SDL_PIXELFORMAT_ABGR8888) : NULL;
}

/* (ui.c ui_resample: a picture made smaller, each pixel the average of those it covers) */
SDL_Surface* ui_resample(const SDL_Surface* s, int w, int h) {
	SDL_Surface* o = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
	if (!o) return NULL;
	for (int y = 0; y < h; ++y) {
		int y0 = y * s->h / h, y1 = SDL_max(y0 + 1, (y + 1) * s->h / h);
		for (int x = 0; x < w; ++x) {
			int x0 = x * s->w / w, x1 = SDL_max(x0 + 1, (x + 1) * s->w / w);
			unsigned sum[4] = { 0 }, n = 0;
			for (int yy = y0; yy < y1; ++yy) {
				const uint8_t* p = (const uint8_t*) s->pixels + (size_t) yy * s->pitch + (size_t) x0 * 4;
				for (int xx = x0; xx < x1; ++xx, p += 4, ++n) for (int c = 0; c < 4; ++c) sum[c] += p[c];
			}
			uint8_t* q = (uint8_t*) o->pixels + (size_t) y * o->pitch + (size_t) x * 4;
			for (int c = 0; c < 4; ++c) q[c] = (uint8_t) (sum[c] / (n ? n : 1));
		}
	}
	return o;
}

/* ---- the game's logo: the one GBA DS extracts from the game's title screen (logo.c), in
 * <stateDir>/logos/<CODE>.png (ui.c extracted_path); asked for in the background when it isn't there */

static void extracted_path(const Config* c, const char* code, char* path, size_t n) {
	const MapProfile* prof = profile_find(code);
	if (prof && prof->logo.rev) snprintf(path, n, "%s/logos/%.4s-r%d.png", c->stateDir, code, prof->logo.rev);
	else snprintf(path, n, "%s/logos/%.4s.png", c->stateDir, code);
}

/* The frontend's logo for the game (游典图鉴's: libretro/lr_game.inc lr_open takes it from
 * <stateDir>/next_logo), <stateDir>/logos/<CODE>-own: as GBA DS takes a logo it scraped, before the
 * extracted one — shrunk to a GBA picture's logo size (it's drawn at a GBA pixel each here). */
static void own_path(const Config* c, const char* code, char* path, size_t n) {
	snprintf(path, n, "%s/logos/%.4s-own", c->stateDir, code);
}

static SDL_Surface* own_logo(const Config* c, const char* code) {
	char path[1200];
	own_path(c, code, path, sizeof(path));
	SDL_Surface* s = image_load(path);
	if (!s) return NULL;
	const int maxW = 200, maxH = 100;
	if (s->w > maxW || s->h > maxH) {
		float k = SDL_min((float) maxW / s->w, (float) maxH / s->h);
		SDL_Surface* r = ui_resample(s, SDL_max(1, (int) (s->w * k)), SDL_max(1, (int) (s->h * k)));
		if (r) SDL_FreeSurface(s), s = r;
	}
	return s;
}

SDL_Surface* ui_logo_surface(const Config* c, const char* rom, const char* code, bool* pixelArt, int* sub) {
	(void) rom;
	char path[1200];
	if (sub) *sub = 1;
	if (pixelArt) *pixelArt = true;
	SDL_Surface* own = own_logo(c, code);
	if (own) return own;
	extracted_path(c, code, path, sizeof(path));
	return image_load(path);
}

/* (lr_open) the frontend's logo handed over for this game */
void lr_take_logo(const Config* c, const char* code) {
	char from[1200], to[1200], dir[1200];
	snprintf(from, sizeof(from), "%s/next_logo", c->stateDir);
	own_path(c, code, to, sizeof(to));
	snprintf(dir, sizeof(dir), "%s/logos", c->stateDir);
	mkdirs(dir);
	if (file_exists(from)) rename(from, to);
}

bool ui_load_logo(const Config* c, const char* rom, const char* code, SDL_Texture* tex[2], int* w, int* h, bool* pixelArt) {
	char dir[1200], path[1200];
	snprintf(dir, sizeof(dir), "%s/logos", c->stateDir);
	mkdirs(dir);
	extracted_path(c, code, path, sizeof(path));
	*pixelArt = true;
	SDL_Surface* s = own_logo(c, code);
	if (!s) s = image_load(path);
	if (!s) {
		if (rom) logo_request(rom, code, path);
		return false;
	}
	/* (only its size is wanted here: the canvases are made from ui_logo_surface) */
	*w = s->w, *h = s->h;
	tex[0] = SDL_CreateTexture(NULL, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, s->w, s->h);
	tex[1] = NULL;
	if (tex[0]) ui_update_texture(tex[0], s->pixels, s->pitch);
	SDL_FreeSurface(s);
	return true;
}

bool ui_logo_provisional(void) { return false; }
bool ui_logo_custom(const Config* c, const char* rom, const char* code) { (void) c, (void) rom, (void) code; return false; }

/* ---- time */

Uint64 SDL_GetPerformanceCounter(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (Uint64) t.tv_sec * 1000000000ull + (Uint64) t.tv_nsec;
}
Uint64 SDL_GetPerformanceFrequency(void) { return 1000000000ull; }
Uint32 SDL_GetTicks(void) { return (Uint32) (SDL_GetPerformanceCounter() / 1000000ull); }
Uint64 SDL_GetTicks64(void) { return SDL_GetPerformanceCounter() / 1000000ull; }
