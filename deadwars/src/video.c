// Dead Wars - video setup, palettes, sprite manager, input, frame loop
#include "game.h"
#include "video.h"
#include "perf.h"
#include <string.h>

u16 keys_down, keys_held, keys_rep;
u32 frame_count;
static u16 rep_timer;

// sprite state shared with gameplay code
SpriteState spr = { .cursor_show = 0, .hide_unit = -1, .anim_unit = -1, .flash_unit = -1,
                    .portrait = -1, .pointer_show = 0 };
s16 cam_px, cam_py;

static u16 shadow_oam[128 * 4] __attribute__((aligned(4)));
static int oam_n;

static void copy16(volatile u16 *dst, const void *src, int bytes)
{
	const u16 *s = src;
	for (int i = 0; i < bytes / 2; i++)
		dst[i] = s[i];
}

static void fill16(volatile u16 *dst, u16 v, int count)
{
	for (int i = 0; i < count; i++)
		dst[i] = v;
}

static u16 blend(u16 c, int r2, int g2, int b2, int t) // t/16 toward (r2,g2,b2)
{
	int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
	r += (r2 - r) * t / 16; g += (g2 - g) * t / 16; b += (b2 - b) * t / 16;
	return RGB15(r, g, b);
}

static u16 grey(u16 c, int dark)
{
	int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
	int l = (r * 5 + g * 9 + b * 2) / 16;
	l = l * dark / 16;
	return RGB15(l, l, l);
}

// team colors for buildings (light, mid, dark)
static const u16 team_cols[3][3] = {
	{ RGB15(26, 26, 24), RGB15(18, 18, 17), RGB15(10, 10, 10) }, // neutral (overwritten from art)
	{ RGB15(14, 22, 31), RGB15(5, 12, 28), RGB15(2, 5, 16) },     // survivors: blue
	{ RGB15(31, 12, 10), RGB15(22, 3, 4), RGB15(11, 1, 3) },      // horde: blood red
};

// Build all palettes. Night tints terrain/units blue-dark; fog darkens further.
void palettes_apply(void)
{
	bool night = gs.night;
	for (int team = 0; team < 3; team++) {
		for (int fogged = 0; fogged < 2; fogged++) {
			volatile u16 *p = PAL_BG + 16 * (BGPAL_TERRAIN + team + fogged * 3);
			for (int i = 0; i < 16; i++) {
				u16 c = pal_terrain[i];
				if (i >= 13 && team > 0)
					c = team_cols[team][i - 13];
				if (night)
					c = blend(c, 2, 3, 10, 7);
				if (fogged)
					c = blend(c, 0, 0, 4, night ? 7 : 6);
				p[i] = c;
			}
		}
	}
	for (int i = 0; i < 16; i++) {
		u16 s = pal_survivor[i], h = pal_horde[i];
		if (night) {
			s = blend(s, 4, 6, 14, 3);
			h = blend(h, 4, 6, 14, 3);
		}
		PAL_OBJ[16 * OBJPAL_SURVIVOR + i] = s;
		PAL_OBJ[16 * OBJPAL_HORDE + i] = h;
		PAL_OBJ[16 * OBJPAL_SURVIVOR_DONE + i] = grey(pal_survivor[i], 12);
		PAL_OBJ[16 * OBJPAL_HORDE_DONE + i] = grey(pal_horde[i], 12);
		PAL_OBJ[16 * OBJPAL_FLASH + i] = i ? RGB15(31, 31, 31) : 0;
	}
	// backdrop: dark
	PAL_BG[0] = night ? RGB15(1, 1, 4) : RGB15(2, 3, 2);
}

static void load_ui_palettes(void)
{
	copy16(PAL_BG + 16 * BGPAL_UI, pal_ui, 32);
	static const u16 text_cols[4] = { RGB15(31, 9, 8), RGB15(31, 27, 6), RGB15(12, 20, 31), RGB15(18, 18, 18) };
	for (int k = 0; k < 4; k++) {
		copy16(PAL_BG + 16 * (BGPAL_TEXT_RED + k), pal_ui, 32);
		PAL_BG[16 * (BGPAL_TEXT_RED + k) + 1] = text_cols[k];
	}
	copy16(PAL_BG + 16 * BGPAL_OVERLAY, pal_overlay, 32);
	copy16(PAL_OBJ + 16 * OBJPAL_CURSOR, pal_cursor, 32);
	for (int p = 0; p < PORTRAIT_COUNT; p++)
		copy16(PAL_OBJ + 16 * (OBJPAL_PORTRAIT + p), pal_portrait[p], 32);
}

void video_load_game_gfx(void)
{
	// BG tiles in charblock 0
	copy16(CHARBLOCK(0) + TILE_FONT * 16, font_tiles, sizeof(font_tiles));
	copy16(CHARBLOCK(0) + TILE_WINDOW * 16, window_tiles, sizeof(window_tiles));
	copy16(CHARBLOCK(0) + TILE_OVERLAY * 16, overlay_tiles, OVERLAY_TILE_COUNT * 32);
	copy16(CHARBLOCK(0) + TILE_TERRAIN * 16, terrain_tiles, TERRAIN_TILE_COUNT * 32);
	// OBJ tiles
	copy16(OBJ_VRAM + OBJT_UNITS * 16, unit_gfx, sizeof(unit_gfx));
	copy16(OBJ_VRAM + OBJT_CURSOR * 16, cursor_gfx, sizeof(cursor_gfx));
	copy16(OBJ_VRAM + OBJT_DIGITS * 16, digit_gfx, sizeof(digit_gfx));
	copy16(OBJ_VRAM + OBJT_POINTER * 16, pointer_gfx, sizeof(pointer_gfx));
	copy16(OBJ_VRAM + OBJT_PORTRAIT * 16, portrait_gfx, sizeof(portrait_gfx));
	copy16(OBJ_VRAM + OBJT_EXPLODE * 16, explode_gfx, sizeof(explode_gfx));
	load_ui_palettes();
	palettes_apply();
	path_arrow_load_gfx();

	fill16(SCREENBLOCK(SB_TERRAIN), 0, 4 * 1024);
	fill16(SCREENBLOCK(SB_OVERLAY), 0, 4 * 1024);
	fill16(SCREENBLOCK(SB_TEXT), 0, 1024);
	fill16(SCREENBLOCK(SB_WINDOW), 0, 1024);

	REG_BGCNT(0) = BG_PRIO(0) | BG_CBB(0) | BG_SBB(SB_TEXT) | BG_SIZE_256;
	REG_BGCNT(3) = BG_PRIO(0) | BG_CBB(0) | BG_SBB(SB_WINDOW) | BG_SIZE_256;
	REG_BGCNT(1) = BG_PRIO(2) | BG_CBB(0) | BG_SBB(SB_OVERLAY) | BG_SIZE_512;
	REG_BGCNT(2) = BG_PRIO(3) | BG_CBB(0) | BG_SBB(SB_TERRAIN) | BG_SIZE_512;
	for (int i = 0; i < 4; i++)
		REG_BGHOFS(i) = REG_BGVOFS(i) = 0;
	REG_BLDCNT = 0x0002 | 0x0040 | 0x0400; // BG1 over BG2, alpha
	REG_BLDALPHA = 9 | (8 << 8);
	REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ_1D | DCNT_BG0 | DCNT_BG1 | DCNT_BG2 | DCNT_BG3 | DCNT_OBJ;
}

void video_show_title(void)
{
	REG_DISPCNT = DCNT_BLANK;
	copy16(CHARBLOCK(1), title_tiles, TITLE_TILE_COUNT * 32);
	copy16(PAL_BG + 16 * BGPAL_TITLE, pal_title, 32);
	volatile u16 *sb = SCREENBLOCK(SB_TITLE);
	fill16(sb, 0, 1024);
	for (int y = 0; y < 20; y++)
		for (int x = 0; x < 30; x++)
			sb[y * 32 + x] = title_map[y][x] | SE_PAL(BGPAL_TITLE);
	copy16(CHARBLOCK(0) + TILE_FONT * 16, font_tiles, sizeof(font_tiles));
	copy16(CHARBLOCK(0) + TILE_WINDOW * 16, window_tiles, sizeof(window_tiles));
	copy16(OBJ_VRAM + OBJT_POINTER * 16, pointer_gfx, sizeof(pointer_gfx));
	copy16(OBJ_VRAM + OBJT_PORTRAIT * 16, portrait_gfx, sizeof(portrait_gfx));
	load_ui_palettes();
	fill16(SCREENBLOCK(SB_TEXT), 0, 1024);
	fill16(SCREENBLOCK(SB_WINDOW), 0, 1024);
	REG_BGCNT(0) = BG_PRIO(0) | BG_CBB(0) | BG_SBB(SB_TEXT) | BG_SIZE_256;
	REG_BGCNT(3) = BG_PRIO(0) | BG_CBB(0) | BG_SBB(SB_WINDOW) | BG_SIZE_256;
	REG_BGCNT(2) = BG_PRIO(3) | BG_CBB(1) | BG_SBB(SB_TITLE) | BG_SIZE_256;
	for (int i = 0; i < 4; i++)
		REG_BGHOFS(i) = REG_BGVOFS(i) = 0;
	REG_BLDCNT = 0;
	REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ_1D | DCNT_BG0 | DCNT_BG2 | DCNT_BG3 | DCNT_OBJ;
}

void video_init(void)
{
	REG_DISPCNT = DCNT_BLANK;
	REG_WAITCNT = 0x4317; // faster ROM access + prefetch
	REG_DISPSTAT = DSTAT_VBL_IRQ;
	REG_IE = IRQ_VBLANK;
	REG_IME = 1;
	for (int i = 0; i < 128; i++)
		shadow_oam[i * 4] = ATTR0_HIDE;
}

void oam_add(u16 a0, u16 a1, u16 a2)
{
	if (oam_n >= 128)
		return;
	shadow_oam[oam_n * 4 + 0] = a0;
	shadow_oam[oam_n * 4 + 1] = a1;
	shadow_oam[oam_n * 4 + 2] = a2;
	oam_n++;
}

static void oam_sprite(int x, int y, int size, int tile, int pal, int prio, bool hflip)
{
	if (x <= -32 || x >= 240 || y <= -32 || y >= 160)
		return;
	oam_add((y & 0xFF) | ATTR0_SQUARE, (x & 0x1FF) | size | (hflip ? ATTR1_HFLIP : 0),
	        tile | ATTR2_PRIO(prio) | ATTR2_PAL(pal));
}

static void build_oam(void)
{
	oam_n = 0;
	if (spr.pointer_show)
		oam_sprite(spr.pointer_x, spr.pointer_y, ATTR1_SIZE8, OBJT_POINTER, OBJPAL_CURSOR, 0, false);
	if (spr.portrait >= 0)
		oam_sprite(spr.portrait_x, spr.portrait_y, ATTR1_SIZE32, OBJT_PORTRAIT + spr.portrait * 16,
		           OBJPAL_PORTRAIT + spr.portrait, 0, spr.portrait_flip);
	if (!spr.map_visible)
		goto done;
	if (spr.cursor_show)
		oam_sprite(gs.cur_x * 16 - cam_px, gs.cur_y * 16 - cam_py, ATTR1_SIZE16,
		           OBJT_CURSOR + ((frame_count >> 4) & 1) * 4, OBJPAL_CURSOR, 1, false);
	if (spr.popup_timer > 0) {
		// floating damage number (digits)
		int v = spr.popup_value, x = spr.popup_x - cam_px + 4, y = spr.popup_y - cam_py - (40 - spr.popup_timer) / 4;
		if (v >= 10) {
			oam_sprite(x - 4, y, ATTR1_SIZE8, OBJT_DIGITS + v / 10, OBJPAL_CURSOR, 0, false);
			oam_sprite(x + 3, y, ATTR1_SIZE8, OBJT_DIGITS + v % 10, OBJPAL_CURSOR, 0, false);
		} else
			oam_sprite(x, y, ATTR1_SIZE8, OBJT_DIGITS + v, OBJPAL_CURSOR, 0, false);
		spr.popup_timer--;
	}
	if (spr.boom_timer > 0) {
		// map explosion: 4 frames of 8 ticks, drawn over the units
		int f = (BOOM_FRAMES - spr.boom_timer) / 8;
		oam_sprite(spr.boom_x - cam_px, spr.boom_y - cam_py, ATTR1_SIZE16, OBJT_EXPLODE + f * 4,
		           OBJPAL_CURSOR, 1, false);
		spr.boom_timer--;
	}
	// AW2-style idle: every unit steps through its frames in lockstep, about 5 times a second
	int af = (frame_count / 12) & 1;
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		if (!u->alive || u->carried || i == spr.hide_unit)
			continue;
		int px, py, fr = u->dormant ? 0 : af;
		bool flip = u->side == SIDE_HORDE;
		if (i == spr.anim_unit) {
			// walking: quick step cycle, facing the direction of travel
			px = spr.anim_px;
			py = spr.anim_py;
			fr = (frame_count >> 2) & 1;
			if (spr.anim_face)
				flip = spr.anim_face < 0;
		} else {
			if (u->side == SIDE_HORDE && gs.fog[u->y][u->x])
				continue;
			px = u->x * 16;
			py = u->y * 16;
		}
		px -= cam_px;
		py -= cam_py;
		if (px <= -16 || px >= 240 || py <= -16 || py >= 160)
			continue;
		int pal = u->side == SIDE_SURVIVOR ? OBJPAL_SURVIVOR : OBJPAL_HORDE;
		if (u->moved && u->side == gs.side)
			pal = u->side == SIDE_SURVIVOR ? OBJPAL_SURVIVOR_DONE : OBJPAL_HORDE_DONE;
		if (i == spr.flash_unit) {
			px += (frame_count & 2) ? 1 : -1; // hit shake
			if (frame_count & 4)
				pal = OBJPAL_FLASH;
		}
		int hp = DISP_HP(u->hp);
		if (hp < 10)
			oam_sprite(px + 9, py + 9, ATTR1_SIZE8, OBJT_DIGITS + hp, OBJPAL_CURSOR, 2, false);
		if (u->cargo)
			oam_sprite(px, py + 9, ATTR1_SIZE8, OBJT_DIGITS + 1, OBJPAL_CURSOR, 2, false);
		if (u->capture < 20)
			oam_sprite(px, py, ATTR1_SIZE8, OBJT_POINTER, OBJPAL_CURSOR, 2, false);
		oam_sprite(px, py, ATTR1_SIZE16, OBJT_UNITS + (u->type * 2 + fr) * 4, pal, 2, flip);
	}
	path_arrow_oam();
done:
	for (int i = oam_n; i < 128; i++)
		shadow_oam[i * 4] = ATTR0_HIDE;
}

void frame(void)
{
	build_oam();
#ifdef PERF
	static u32 last;
	if (last) {
		u32 c = perf_now() - last;
		perf_add(&perf.frame_work, last);
		if (c > 280896 - 83776) // longer than the visible part of a frame
			perf.dropped++;
		if (perf.turn_frame_max && c > perf.turn_frame_max)
			perf.turn_frame_max = c;
	}
#endif
	VBlankIntrWait();
#ifdef PERF
	last = perf_now();
#endif
	copy16(OAM, shadow_oam, sizeof(shadow_oam));
	if (spr.map_visible) {
		REG_BGHOFS(1) = REG_BGHOFS(2) = cam_px;
		REG_BGVOFS(1) = REG_BGVOFS(2) = cam_py;
	}
	frame_count++;
	input_poll();
}

void wait_frames(int n)
{
	while (n-- > 0)
		frame();
}

void input_poll(void)
{
	u16 k = ~REG_KEYINPUT & 0x3FF;
	keys_down = k & ~keys_held;
	keys_held = k;
	keys_rep = keys_down;
	if (k & KEY_DIR) {
		if (keys_down & KEY_DIR)
			rep_timer = 12;
		else if (--rep_timer == 0) {
			keys_rep |= k & KEY_DIR;
			rep_timer = 3;
		}
	}
}
