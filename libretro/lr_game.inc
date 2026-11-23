/* 游典's libretro core (libretro/gbads_libretro.c): GBA DS's game, frame by frame, with everything its second
 * screen does (the map capture, the HUD split, the pinball tables, the status snapshots, fights, the logo),
 * GBA DS's own code. Included at the end of src/game.c (GBADS_LIBRETRO builds only): its state is game.c's.
 * The two blocks marked GENERATED are game_run_inner's frame loop as it is (copied from game_run_inner when this file was written: 「a compilation」 to the map refresh's end, and the snapshots / table / HUD / fights calls). */

const uint32_t* lr_texture_pixels(SDL_Texture* t, int* w, int* h); /* (libretro/shim.c) */

/* game_run_inner's locals the frame loop's map refresh keeps */
static uint32_t lastRoomKey, seenRoomKey;
static int seenRoomFor, sinceMapRequest, lastFails;
static bool forceMap, wasDark;
static long refreshAt;
static uint32_t lrLogoGen;
static Config lrCfg;
static GamePaths gpLr;

/* Opens [rom] as game_run_inner does, without the menus, the sound device or the windows: one view, two
 * screens (the second screen's set-up wants another screen to put things on). */
bool lr_open(const char* rom, const char* stateDir) {
	config_defaults(&lrCfg);
	snprintf(lrCfg.stateDir, sizeof(lrCfg.stateDir), "%s", stateDir);
	snprintf(lrCfg.assetDir, sizeof(lrCfg.assetDir), "%s", stateDir);
	lrCfg.map = 1;
	cfg = &lrCfg;
	tallLiveAtStart = cfg->tallLive;
	debug = getenv("GBADS_DEBUG") != NULL;
	romPath = rom;
	nScreens = 2;
	nViews = 1;
	profile_set_rom(NULL, 0);
	if (!emu_open(&emu, rom, NULL, true)) return false;
	profile_set_rom(emu.gameCode, emu.romCrc);
	mc = NULL;
	gameTexAll = true;
	frameCount = 0;
	mapAnimStart = 0;
	menuOpen = false;
	{
		const MapProfile* gp = profile_find(emu.gameCode);
		pickProf = gp && gp->pick ? gp : NULL;
		snprintf(subCode, sizeof(subCode), "%.4s", emu.gameCode);
	}
	ss_setup(rom, subCode);
	gameTex[0] = SDL_CreateTexture(NULL, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, GBA_H);
	ss_textures();
	unsigned bc = (unsigned) strtoul(cfg->borderColor, NULL, 16);
	borderColor = (SDL_Color) { (bc >> 16) & 0xFF, (bc >> 8) & 0xFF, bc & 0xFF, 255 };
	lrLogoGen = logo_generation();
	logoSub = 1;
	{
		void lr_take_logo(const Config* c, const char* code); /* (libretro/shim.c) */
		lr_take_logo(cfg, emu.gameCode);
	}
	if (ui_load_logo(cfg, rom, emu.gameCode, logoTex, &logoW, &logoH, &logoPixel)) make_logo_canvas();
	lastRoomKey = seenRoomKey = 0;
	seenRoomFor = 0;
	sinceMapRequest = 1 << 20;
	lastFails = 0;
	forceMap = true, wasDark = false;
	refreshAt = -1;
	return true;
}

Emu* lr_emu(void) { return &emu; }
const char* lr_profile_title(void) { return mc ? mapcap_title(mc) : NULL; }
int lr_kind(void) { return (mc ? 1 : 0) | (hudProf ? 2 : 0) | (tallProf ? 4 : 0) | (snapProf ? 8 : 0) | (combatProf ? 16 : 0) | (logoOnly ? 32 : 0); }

/* A state was loaded: the second screen as of where it is now */
void lr_state_loaded(void) {
	forceMap = true;
	mapShownIdx = -1;
}

void lr_close(void) {
	twin_close();
	if (mc) mapcap_destroy(mc);
	mc = NULL;
	emu_close(&emu);
}

/* One frame: the game's (run_frame: the pinball table's copies too), then the second screen's part of
 * the frame loop. */
void lr_frame(uint32_t keys) {
	const char* rom = romPath;
	run_frame(keys);
	++frameCount;
	if (logo_generation() != lrLogoGen) {
		lrLogoGen = logo_generation();
		logo_refresh();
	}
	/* GENERATED: game_run_inner, from 「a compilation」 to the map refresh's end */
		/* ---- a compilation: the game running in it */
		if (pickProf && frameCount % 15 == 0) {
			const char* pc = pickProf->pick(&emu);
			if (!pc) pc = emu.gameCode;
			if (strncmp(pc, subCode, 4)) ss_switch(rom, pc);
		}

		/* ---- map refresh */
		if (mc) {
			++sinceMapRequest;
			/* the profile's sprite hiding, where it's only for some moments
			 * (hideWhen: Lady Sia's HUD in play, not its credits or pause
			 * screen); from the next frame drawn */
			if (hideWhenProf && memprobes_match(hideWhenProf->hideWhen, 2, &emu) != hideWhenOn) {
				hideWhenOn = !hideWhenOn;
				emu_hide_sprites(&emu, hideWhenOn ? hideWhenProf->hideSprites : NULL, hideWhenOn ? hideWhenN : 0);
				if (debug) fprintf(stderr, "frame %llu: sprite hiding %s\n", (unsigned long long) frameCount, hideWhenOn ? "on" : "off");
			}
			{
				/* (logoIf: the logo for now, SM4-11) */
				const MapProfile* lp = mapcap_profile(mc);
				bool lf = lp && ((lp->logoIf[0].addr && memprobes_match(lp->logoIf, 2, &emu)) ||
				                 (lp->logoIfOr[0].addr && memprobes_match(lp->logoIfOr, 2, &emu)));
				if (lf) logoHolding = 60;
				else if (logoHolding > 0) --logoHolding;
				lf = lf || logoHolding > 0;
				if (debug && lf != logoForced) fprintf(stderr, "frame %llu: logo only %s\n", (unsigned long long) frameCount, lf ? "on" : "off");
				logoForced = lf;
			}
			bool dark = frame_is_dark(emu.fb);
			if (wasDark && !dark) refreshAt = (long) frameCount + 6;
			wasDark = dark;
			if (refreshAt >= 0 && (long) frameCount >= refreshAt) {
				forceMap = true;
				refreshAt = -1;
				if (debug) fprintf(stderr, "frame %llu: room transition\n", (unsigned long long) frameCount);
			}
			if (mapShownIdx >= 0 && frameCount % 30 == 0 && mapcap_in_menu(mc, &emu)) {
				/* quit to the title: the map belongs to the old session */
				mapShownIdx = -1;
				mapInactive = false;
				forceMap = true;
				/* (always logged: what sends the second screen back to the logo) */
				fprintf(stderr, "frame %llu: second screen back to the logo (the profile's menu probes matched)\n", (unsigned long long) frameCount);
			}
			uint32_t rk = mapcap_room_key(mc, &emu);
			if (rk != seenRoomKey) seenRoomKey = rk, seenRoomFor = 0;
			else ++seenRoomFor;
			/* a change counts once it has held for 3 frames (a cursor
			 * gliding to its place, Mario walking a path: not each step);
			 * a capture still running for the old one is called off, so
			 * the new one starts as soon as it has stopped */
			bool roomChanged = rk && rk != lastRoomKey && seenRoomFor >= 2;
			if (roomChanged && mapcap_busy(mc)) mapcap_cancel(mc);
			/* Back off while captures keep failing (e.g. the game says "playing"
			 * but the map can't open: intro cutscenes, briefings). Each failed
			 * attempt emulates a few seconds in the background, which starves
			 * the game and its audio on slower devices. */
			const char* why = NULL;
			int fails = mapcap_failures(mc, &why);
			if (fails != lastFails) {
				if (debug && fails) fprintf(stderr, "frame %llu: map capture failed (%d in a row): %s\n",
				                            (unsigned long long) frameCount, fails, why ? why : "?");
				const MapProfile* dp = mapcap_profile(mc);
				if (fails && why && dp && dp->dimWhenUnavailable &&
				    (!strcmp(why, "map screen did not appear") || !strcmp(why, "map button did nothing"))) {
					mapInactive = true;
				}
				lastFails = fails;
			}
			/* quick "no map here" failures (A Link to the Past indoors) cost little
			 * and are expected, so they don't back off */
			bool cheapFail = why && !strcmp(why, "map button did nothing");
			/* (a profile's own interval, when it's quicker: Swordcraft Story) */
			const MapProfile* ip = mapcap_profile(mc);
			int every = ip && ip->interval > 0 && ip->interval < cfg->mapInterval ? ip->interval : cfg->mapInterval;
			int interval = cheapFail ? every : every << SDL_min(fails, 3);
			/* after failures, a room change still triggers a try, but no more
			 * often than the normal interval */
			bool due = sinceMapRequest >= interval ||
			           ((forceMap || roomChanged) && (!fails || sinceMapRequest >= every));
			if (due && !mapcap_busy(mc) && mapcap_live_ok(mc, &emu)) {
				if (mapcap_request(mc, &emu)) {
					sinceMapRequest = 0;
					lastRoomKey = rk;
					forceMap = false;
				}
			}
			bool fresh = mapcap_poll(mc, mapRes.serial, &mapRes);
			if (fresh && mapcap_in_menu(mc, &emu)) {
				/* a capture that finished after quitting to the title: drop it */
			} else if (fresh) {
				map_show_new(true);
			} else if (map_anim() && mapRes.nFrames > 1 && mapShownIdx >= 0) {
				int idx = map_anim_index();
				upload_map_frame(idx);
			}
			/* a profile's own live picture (Mega Man Battle Network's maps on
			 * the net), drawn every frame over whatever was captured; when it
			 * stops, the captured picture again */
			const MapProfile* lvp = mapcap_profile(mc);
			const uint32_t* lv = lvp && lvp->liveView && romPath ? lvp->liveView(&emu, romPath) : NULL;
			if (lv) {
				for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], lv, mapW * 4);
				if (mapShownIdx < 0) mapShownIdx = 0;
				if (!liveOn && debug) fprintf(stderr, "frame %llu: live view on\n", (unsigned long long) frameCount);
				liveOn = true;
			} else if (liveOn) {
				liveOn = false;
				if (debug) fprintf(stderr, "frame %llu: live view off\n", (unsigned long long) frameCount);
				if (mapRes.ok && map_px(mapShownIdx)) {
					overDirty = true;
					upload_map_frame(mapShownIdx);
				} else {
					mapShownIdx = -1;
				}
			}
		}

	/* GENERATED: game_run_inner, snapshots, the table, the HUD, fights */
		if (snapProf) update_snapshot();
		if (tallProf) update_tall();
		if (hudProf) update_hud();
		if (combatProf) {
			/* a fight: after a few frames of it, not on one odd frame */
			static int streak;
			combatInGame = combatProf->hud.on[0].addr && memprobes_match(combatProf->hud.on, 1, &emu);
			if ((combatInGame || combatProf->hud.logoAlways) && combatProf->hud.logoUnder && !combatMade) make_combat_canvas();
			bool c = memprobes_match(combatProf->combat, 2, &emu) ||
			         (combatProf->combatOr[0].addr && memprobes_match(combatProf->combatOr, 2, &emu));
			streak = c != combatOn ? streak + 1 : 0;
			if (streak >= 3) {
				combatOn = c;
				streak = 0;
				if (combatOn && !combatMade) make_combat_canvas();
				fprintf(stderr, "frame %llu: fight %s (the second screen's logo %s)\n", (unsigned long long) frameCount, combatOn ? "on" : "off",
				        combatOn ? "up" : "gone");
			}
		}
	/* The table's copies going wrong (a romhack whose code isn't where the profile patches it: 「Jumped to
	 * invalid address」 every frame): closed, the logo on the other screen, as GBA DS does for a ROM it
	 * can tell isn't the one (twin_open's camera-store check). */
	{
		extern volatile unsigned emu_log_errors;
		static unsigned errorsAt;
		static unsigned long long since;
		if (!twinOpen) {
			errorsAt = emu_log_errors, since = frameCount;
		} else if (frameCount - since >= 60) {
			if (emu_log_errors - errorsAt > 30) {
				fprintf(stderr, "live view: the copies keep failing (%u errors in a second): copies off\n", emu_log_errors - errorsAt);
				twin_close();
				tallLive = false;
			}
			errorsAt = emu_log_errors, since = frameCount;
		}
	}
	(void) rom;
}

/* The game's picture as the game screen shows it (game_run_inner: the mirror, the table's upper half) */
const uint32_t* lr_game_picture(bool* dim) {
	const uint32_t* shown = emu.fb;
	if (mirrorOn && mirrorHave) shown = mirrorHeld;
	if (tallLive) shown = cfg->swap ? tallBot : tallTop;
	*dim = hudProf && hudOn && hudAlt && hudProf->hud.alt.dimGame;
	return shown;
}

/* The second screen's picture as draw_game picks it: the fight's, the HUD's (at 2x), the map's, the
 * logo's; NULL for nothing (dark). *dimmed: the map greyed (no map here now). */
const uint32_t* lr_second_picture(int* w, int* h, bool* dimmed, uint32_t* ground) {
	*dimmed = false;
	*ground = 0xFF000000u | ((uint32_t) borderColor.b << 16) | ((uint32_t) borderColor.g << 8) | borderColor.r;
	bool logoBack = combatProf && combatProf->hud.logoUnder && mapShownIdx < 0 && (combatInGame || combatProf->hud.logoAlways);
	if (overAlone && mapShownIdx >= 0) logoBack = false;
	SDL_Texture* t = NULL;
	if (((combatOn && !(overAlone && mapShownIdx >= 0)) || logoBack) && combatTex[0]) {
		t = combatTex[0];
	} else if (hudShown && !logoForced) {
		t = hudTex[0];
	} else if (mapShownIdx >= 0 && !logoForced) {
		t = mapTex[0];
		*dimmed = !hudShown && mapInactive;
	} else if (logoCanvasTex[0]) {
		t = logoCanvasTex[0];
	}
	return lr_texture_pixels(t, w, h);
}

/* (游典) The second screen shows pages in turn (Fire Emblem's units, The Minish Cap's menus…):
 * whether it does now, and a tap on it — the next page, held for half a minute. */
bool lr_second_paged(void) {
	return mapRes.ok && mapRes.nFrames > 1 && mapRes.frameStep >= 60 && mapShownIdx >= 0;
}
void lr_second_tap(void) {
	if (!lr_second_paged()) return;
	pageHeld = (map_anim_index() + 1) % mapRes.nFrames;
	pageHeldUntil = frameCount + 30 * 60;
	upload_map_frame(pageHeld);
	fprintf(stderr, "frame %llu: second screen tapped, page %d of %d\n", (unsigned long long) frameCount, pageHeld + 1, mapRes.nFrames);
}
/* (游典) a tap asked for page `page` (Golden Sun's party row): shown and held as a tap's next page is */
void lr_second_show(int page) {
	if (!lr_second_paged() || page < 0 || page >= mapRes.nFrames) return;
	pageHeld = page;
	pageHeldUntil = frameCount + 30 * 60;
	upload_map_frame(pageHeld);
	fprintf(stderr, "frame %llu: second screen tapped, page %d of %d\n", (unsigned long long) frameCount, pageHeld + 1, mapRes.nFrames);
}
int lr_second_page(void) {
	return lr_second_paged() ? map_anim_index() : (mapShownIdx >= 0 ? 0 : -1);
}
/* (游典) a tap changed the game (an item equipped): the second screen's pictures taken again soon, the page
 * shown kept */
void lr_second_refresh(void) {
	forceMap = true;
}
