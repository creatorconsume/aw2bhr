// Dead Wars: The Last Boat - tactical map graphics: roofs, door and status icons, fog of war, HUD
#include "lb.h"
#include "lbart.h"
#include "video.h"
#include <string.h>

static const char *const bk_names[BK_COUNT] = { "House", "Shop", "Garage", "Pharmacy" };

// noise pulse, started by tac_fx_noise (tac.c)
static s16 pulse_x, pulse_y;
static u8 pulse_timer;
#define PULSE_FRAMES 40

// tac_hidden() per unit, refreshed by tac_fog(): too slow to ask every frame
static u8 hid[MAX_UNITS];

static bool on_map(int i)
{
	const Unit *u = &gs.units[i];
	return u->alive && !u->carried && u->side == SIDE_SURVIVOR && !tac.actor[i].extracted;
}

// Is unit i drawn for the player? Zombies hide in the fog and under roofs.
static bool shown(int i)
{
	const Unit *u = &gs.units[i];
	return u->side != SIDE_HORDE || (!gs.fog[u->y][u->x] && !u->inside);
}

// ROM data is read bytewise so the generated arrays need no alignment; VRAM gets u16 writes only.
static void copy_bytes16(volatile u16 *dst, const u8 *src, int bytes)
{
	for (int i = 0; i < bytes / 2; i++)
		dst[i] = src[2 * i] | src[2 * i + 1] << 8;
}

void tac_gfx_load(void)
{
	copy_bytes16(OBJ_VRAM + OBJT_LBPERSON * 16, &lbart_person_gfx[0][0][0], sizeof lbart_person_gfx);
	copy_bytes16(OBJ_VRAM + OBJT_LBICON * 16, &lbart_icon_gfx[0][0], sizeof lbart_icon_gfx);
	int n = MIN(lbart_bg_tile_count, LBART_BG_TILE_COUNT_MAX);
	if (n > 0)
		copy_bytes16(CHARBLOCK(0) + LBART_BG_TILE_BASE * 16, lbart_bg_tiles, n * 32);
	pulse_timer = 0;
	memset(hid, 0, sizeof hid);
}

// tac_fx_noise() hook: blink the noise icon on (x, y) for a moment.
void tac_gfx_pulse(int x, int y)
{
	pulse_x = x;
	pulse_y = y;
	pulse_timer = PULSE_FRAMES;
}

static inline int se_index(int tx, int ty)
{
	return (ty >> 5) * 2048 + (tx >> 5) * 1024 + (ty & 31) * 32 + (tx & 31);
}

// Roof metatile for building tiles: the sign on the top-left, a 9-slice for the rest.
bool tac_redraw_tile(int x, int y)
{
	if ((unsigned)x >= MAP_MAX_W || (unsigned)y >= MAP_MAX_H)
		return false;
	int bi = tac.bmap[y][x] - 1;
	if (bi < 0 || bi >= tac.nbldg)
		return false;
	const Building *b = &tac.bldg[bi];
	int rx = x - b->x, ry = y - b->y;
	if (rx < 0 || ry < 0 || rx >= b->w || ry >= b->h)
		return false;
	int r;
	if (!rx && !ry)
		r = LBR_SIGN_HOUSE + (b->kind < BK_COUNT ? b->kind : BK_HOUSE);
	else
		r = (ry == 0 ? 0 : ry == b->h - 1 ? 6 : 3) + (rx == 0 ? 0 : rx == b->w - 1 ? 2 : 1);
	volatile u16 *sb = SCREENBLOCK(SB_TERRAIN);
	int pal = BGPAL_TERRAIN + (gs.fog[y][x] ? 3 : 0);
	for (int i = 0; i < 4; i++) {
		u16 e = lbart_roof_meta[r][i];
		e = (LBART_BG_TILE_BASE + (e & 0x3FF)) | (e & (SE_HFLIP | SE_VFLIP)) | SE_PAL(pal);
		sb[se_index(x * 2 + (i & 1), y * 2 + (i >> 1))] = e;
	}
	return true;
}

// People use the generated portraits-on-the-map sprites; zombies keep unit_gfx.
int tac_unit_tile(int i, int frame)
{
	const Unit *u = &gs.units[i];
	frame &= 1;
	if (u->side == SIDE_SURVIVOR) {
		int p = tac.actor[i].person, s = p < ROSTER_MAX ? run.roster[p].sprite : LBG_MAN;
		if (s >= LBG_COUNT)
			s = LBG_MAN;
		return OBJT_LBPERSON + (s * 2 + frame) * 4;
	}
	int t = u->type < UT_COUNT ? u->type : UT_WALKER;
	return OBJT_UNITS + (t * 2 + frame) * 4;
}

static void icon(int px, int py, int id, u16 flip, int prio)
{
	px -= cam_px;
	py -= cam_py;
	if (px <= -16 || px >= 240 || py <= -16 || py >= 160)
		return;
	oam_add((py & 0xFF) | ATTR0_SQUARE, (px & 0x1FF) | ATTR1_SIZE16 | flip,
	        (OBJT_LBICON + id * 4) | ATTR2_PRIO(prio) | ATTR2_PAL(OBJPAL_CURSOR));
}

static bool cache_open(int x, int y)
{
	int c = tac.cache[y][x];
	if (c == CACHE_TRUNK)
		return true;
	return c >= CACHE_PACK && c < CACHE_PACK + TAC_MAX_PACKS && tac.pack[c - CACHE_PACK].used; // a dropped pack
}

// build_oam() hook, after the units: map icons under them (same priority, later in OAM),
// status icons and the noise pulse over them.
void tac_oam(void)
{
	// doors, on the outside edge of the door tile
	for (int i = 0; i < tac.nbldg; i++) {
		const Building *b = &tac.bldg[i];
		int d = b->door_dir & 3, x = b->door_x, y = b->door_y, ox = x + TAC_DIRX(d), oy = y + TAC_DIRY(d);
		if (x >= gs.w || y >= gs.h)
			continue;
		bool out_seen = ox >= 0 && oy >= 0 && ox < gs.w && oy < gs.h && !gs.fog[oy][ox];
		if (gs.fog[y][x] && !out_seen)
			continue;
		int id = ((d & 1) ? LBI_DOOR_E : LBI_DOOR_S) + (b->door_hp ? 2 : 0);
		u16 flip = d == 0 ? ATTR1_VFLIP : d == 3 ? ATTR1_HFLIP : 0;
		icon(x * 16 + TAC_DIRX(d) * 8, y * 16 + TAC_DIRY(d) * 8, id, flip, 2);
	}
	// caches and exits: only the tiles on screen
	int tx0 = cam_px >> 4, ty0 = cam_py >> 4;
	int tx1 = MIN(tx0 + SCREEN_TW, gs.w - 1), ty1 = MIN(ty0 + SCREEN_TH, gs.h - 1);
	for (int y = ty0; y <= ty1; y++)
		for (int x = tx0; x <= tx1; x++) {
			if (gs.fog[y][x])
				continue;
			if (tac.cache[y][x] && cache_open(x, y))
				icon(x * 16, y * 16, LBI_CACHE, 0, 2);
			if (tac.zone[y][x] & ZN_EXIT)
				icon(x * 16, y * 16, LBI_EXIT, 0, 2);
		}
	// one status per person: downed > infected > hidden
	for (int i = 0; i < MAX_UNITS; i++) {
		if (!on_map(i) || gs.units[i].inside)
			continue;
		const Actor *a = &tac.actor[i];
		int id = -1;
		if (a->down)
			id = LBI_DOWNED;
		else if (a->person < ROSTER_MAX && (run.roster[a->person].flags & PF_INFECTED))
			id = LBI_INFECTED;
		else if (hid[i])
			id = LBI_HIDDEN;
		if (id < 0)
			continue;
		bool moving = i == spr.anim_unit;
		icon(moving ? spr.anim_px : gs.units[i].x * 16, moving ? spr.anim_py : gs.units[i].y * 16, id, 0, 1);
	}
	if (pulse_timer) {
		pulse_timer--;
		if ((pulse_timer & 4) && pulse_x >= 0 && pulse_y >= 0 && pulse_x < gs.w && pulse_y < gs.h &&
		    !gs.fog[pulse_y][pulse_x])
			icon(pulse_x * 16, pulse_y * 16, LBI_NOISE, 0, 1);
	}
}

// ---------------------------------------------------------------- fog of war

// Visible: within sight of a survivor with line of sight, and every tile of a building
// someone is inside. Zombies in an empty building stay under its roof.
void tac_fog(void)
{
	static u8 vis[MAP_MAX_H][MAP_MAX_W] EWRAM_BSS;
	u8 occupied[TAC_MAX_BLDG];
	memset(vis, 0, sizeof vis);
	memset(occupied, 0, sizeof occupied);
	int base = gs.night ? 3 : 7;
	for (int i = 0; i < MAX_UNITS; i++) {
		if (!on_map(i))
			continue;
		const Unit *u = &gs.units[i];
		int ux = u->x, uy = u->y;
		if (ux >= gs.w || uy >= gs.h)
			continue;
		vis[uy][ux] = 1;
		int bi = tac.bmap[uy][ux] - 1;
		if (bi >= 0 && bi < tac.nbldg) {
			const Building *b = &tac.bldg[bi];
			occupied[bi] = 1;
			for (int y = b->y; y < b->y + b->h && y < gs.h; y++)
				for (int x = b->x; x < b->x + b->w && x < gs.w; x++)
					vis[y][x] = 1;
		}
		int s = base + (gs.terrain[uy][ux] == TT_HILL ? 2 : 0);
		int y0 = MAX(0, uy - s), y1 = MIN(gs.h - 1, uy + s);
		for (int y = y0; y <= y1; y++) {
			int r = s - ABS(y - uy);
			int x0 = MAX(0, ux - r), x1 = MIN(gs.w - 1, ux + r);
			for (int x = x0; x <= x1; x++)
				if (!vis[y][x] && tac_los(ux, uy, x, y))
					vis[y][x] = 1;
		}
	}
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++) {
			u8 f = !vis[y][x];
			if (gs.fog[y][x] != f) {
				gs.fog[y][x] = f;
				map_redraw_tile(x, y);
			}
		}
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		hid[i] = 0;
		if (!u->alive)
			continue;
		if (u->side == SIDE_HORDE) {
			int bi = u->x < MAP_MAX_W && u->y < MAP_MAX_H ? tac.bmap[u->y][u->x] - 1 : -1;
			u->inside = bi >= 0 && bi < TAC_MAX_BLDG && !occupied[bi];
		} else if (on_map(i)) {
			u->inside = 0;
			hid[i] = tac_hidden(i);
		}
	}
}

// ---------------------------------------------------------------- HUD

#define HUD_W 14 // panels sit on the side away from the cursor: 0..13 or 16..29

static int hud_x(void)
{
	return gs.cur_x - gs.cam_x < 8 ? 30 - HUD_W : 0;
}

static void hud_top(int px)
{
	const TacMission *m = tac.m;
	text_window(px, 0, HUD_W, 5);
	text_printf(px + 1, 1, gs.night ? BGPAL_TEXT_BLUE : BGPAL_TEXT_YEL, "T%d", tac.turn + 1); // tac.turn counts finished turns
	text_printf(px + 5, 1, run.res[RES_AMMO] ? BGPAL_UI : BGPAL_TEXT_RED, "Ammo %d", MIN(run.res[RES_AMMO], 999));
	// horde meter: progress toward the next wave
	int fill = 6, w = tac.wave;
	if (m && w < 3 && m->waves[w]) {
		int lo = w ? m->waves[w - 1] : 0, hi = m->waves[w];
		fill = hi > lo ? (tac.horde - lo) * 6 / (hi - lo) : 6;
		fill = MAX(0, MIN(fill, 6));
	}
	text_put(px + 1, 2, "Horde", w ? BGPAL_TEXT_RED : BGPAL_UI);
	for (int i = 0; i < 6; i++)
		text_put(px + 7 + i, 2, "=", i < fill ? BGPAL_TEXT_RED : BGPAL_TEXT_GREY);
	// objective progress and waves so far
	if (m && m->objective == OBJ_BRIDGE)
		text_printf(px + 1, 3, BGPAL_TEXT_BLUE, "Held %d/%d", tac.held, m->need);
	else if (m && m->objective == OBJ_GET_THROUGH)
		text_printf(px + 1, 3, BGPAL_TEXT_BLUE, "Out %d/%d", tac.through, m->need);
	else {
		int loot = 0;
		for (int r = 0; r < RES_COUNT; r++)
			loot += tac.loot[r];
		for (int i = 0; i < MAX_UNITS; i++)
			if (on_map(i))
				for (int r = 0; r < RES_COUNT; r++)
					loot += tac.actor[i].carry[r];
		text_printf(px + 1, 3, BGPAL_TEXT_BLUE, "Loot %d", MIN(loot, 999));
	}
	text_printf(px + 10, 3, w ? BGPAL_TEXT_RED : BGPAL_TEXT_GREY, "W%d", MIN(w, 9));
}

static const char *status_word(int i, int *pal)
{
	const Actor *a = &tac.actor[i];
	*pal = BGPAL_TEXT_RED;
	if (a->down)
		return "Downed";
	if (a->person < ROSTER_MAX && (run.roster[a->person].flags & PF_INFECTED))
		return "Infected";
	*pal = BGPAL_TEXT_BLUE;
	if (hid[i])
		return "Hidden";
	*pal = BGPAL_TEXT_GREY;
	if (gs.side != SIDE_SURVIVOR)
		return "";
	return !a->moved && !a->acted ? "Ready" : !a->acted ? "Can act" : !a->moved ? "Can move" : "Done";
}

static void hud_unit(int px, int o)
{
	const Unit *u = &gs.units[o];
	const Actor *a = &tac.actor[o];
	if (u->side == SIDE_SURVIVOR) {
		const Person *p = a->person < ROSTER_MAX ? &run.roster[a->person] : NULL;
		text_printf(px + 1, 16, BGPAL_TEXT_BLUE, "%.7s", p ? person_name(p) : "?");
		text_printf(px + 8, 16, BGPAL_UI, "%2d/%d", u->hp, a->maxhp);
		if (p && p->weapon < WP_COUNT)
			text_put(px + 1, 17, weapon_info[p->weapon].name, BGPAL_UI);
		int pal;
		const char *s = status_word(o, &pal);
		text_put(px + 1, 18, s, pal);
		if (a->down)
			text_printf(px + 8, 18, BGPAL_TEXT_RED, "%dt", a->down);
		return;
	}
	text_printf(px + 1, 16, BGPAL_TEXT_RED, "%.7s", unit_info[u->type < UT_COUNT ? u->type : UT_WALKER].name);
	text_printf(px + 8, 16, BGPAL_UI, "%2d/%d", u->hp, a->maxhp);
	if (u->dormant)
		text_put(px + 1, 17, "Dormant", BGPAL_TEXT_GREY);
	text_put(px + 1, 18, terrain_info[gs.terrain[u->y][u->x]].name, BGPAL_TEXT_GREY);
}

static void hud_tile(int px, int x, int y)
{
	int bi = tac.bmap[y][x] - 1;
	const Building *b = bi >= 0 && bi < tac.nbldg ? &tac.bldg[bi] : NULL;
	int fog = gs.fog[y][x];
	text_put(px + 1, 16, b ? bk_names[b->kind < BK_COUNT ? b->kind : 0] : terrain_info[gs.terrain[y][x]].name,
	         fog ? BGPAL_TEXT_GREY : BGPAL_UI);
	u8 z = tac.zone[y][x];
	if (z & ZN_EXIT)
		text_put(px + 1, 17, "Exit", BGPAL_TEXT_BLUE);
	else if (z & ZN_DEPLOY)
		text_put(px + 1, 17, "Caravan", BGPAL_TEXT_BLUE);
	else if (b && b->door_hp)
		text_printf(px + 1, 17, BGPAL_TEXT_YEL, "Barricade %d", b->door_hp);
	else if (b) {
		int n = 0, t = MIN(b->w * b->h, 8);
		for (int i = 0; i < t; i++)
			n += (b->searched >> i) & 1;
		text_printf(px + 1, 17, BGPAL_TEXT_GREY, "Searched %d/%d", n, t);
	} else if (tac.cache[y][x] && cache_open(x, y))
		text_put(px + 1, 17, tac.cache[y][x] == CACHE_TRUNK ? "Car trunk" : "Backpack", BGPAL_TEXT_YEL);
	else if (z & ZN_BRIDGE)
		text_put(px + 1, 17, "Bridge", BGPAL_TEXT_BLUE);
	// directional cover: grey none, yellow half, blue full
	static const char nesw[4][2] = { "N", "E", "S", "W" };
	text_put(px + 1, 18, "Cover", BGPAL_UI);
	for (int k = 0; k < 4; k++) {
		int c = tac_edge_cover(x, y, k);
		text_put(px + 7 + k, 18, nesw[k], c >= 40 ? BGPAL_TEXT_BLUE : c > 0 ? BGPAL_TEXT_YEL : BGPAL_TEXT_GREY);
	}
}

// Top panel: turn, ammo, horde meter, objective. Bottom panel: what the cursor is on.
void tac_hud(void)
{
	int px = hud_x(), x = gs.cur_x, y = gs.cur_y;
	text_clear_rect(0, 0, 30, 5);
	text_clear_rect(0, 15, 30, 5);
	hud_top(px);
	if (x < 0 || y < 0 || x >= gs.w || y >= gs.h)
		return;
	text_window(px, 15, HUD_W, 5);
	int o = unit_at(x, y);
	if (o >= 0 && shown(o) && (gs.units[o].side == SIDE_HORDE || on_map(o)))
		hud_unit(px, o);
	else
		hud_tile(px, x, y);
}
