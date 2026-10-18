// Dead Wars - map loading, drawing, fog, camera, combat math
#include "game.h"
#include "video.h"
#include <string.h>

GameState gs EWRAM_BSS;

/* Map legend (one char per tile):
 *  .  plain      f  woods     ^  rubble     ~  river     =  bridge
 *  r  ruins      #  barricade w  wreck      -  road      @  helipad
 *  n  nest
 *  buildings: lower case = neutral, upper case = survivors, other = horde
 *  h/H/q house   k/K/j workshop   s/S/v safehouse   p/P/b hospital   x/X/y radio
 */
static void legend(char c, u8 *tt, u8 *own)
{
	*own = SIDE_NEUTRAL;
	switch (c) {
	case 'f': *tt = TT_FOREST; return;
	case '^': *tt = TT_HILL; return;
	case '~': *tt = TT_WATER; return;
	case '=': *tt = TT_BRIDGE; return;
	case 'r': *tt = TT_RUINS; return;
	case '#': *tt = TT_WALL; return;
	case 'w': *tt = TT_WRECK; return;
	case '-': *tt = TT_ROAD; return;
	case '@': *tt = TT_HELIPAD; return;
	case 'n': *tt = TT_NEST; return;
	case 'H': *own = SIDE_SURVIVOR; /* fallthrough */
	case 'h': *tt = TT_HOUSE; return;
	case 'q': *own = SIDE_HORDE; *tt = TT_HOUSE; return;
	case 'K': *own = SIDE_SURVIVOR; /* fallthrough */
	case 'k': *tt = TT_WORKSHOP; return;
	case 'j': *own = SIDE_HORDE; *tt = TT_WORKSHOP; return;
	case 'S': *own = SIDE_SURVIVOR; /* fallthrough */
	case 's': *tt = TT_SAFEHOUSE; return;
	case 'v': *own = SIDE_HORDE; *tt = TT_SAFEHOUSE; return;
	case 'P': *own = SIDE_SURVIVOR; /* fallthrough */
	case 'p': *tt = TT_HOSPITAL; return;
	case 'b': *own = SIDE_HORDE; *tt = TT_HOSPITAL; return;
	case 'X': *own = SIDE_SURVIVOR; /* fallthrough */
	case 'x': *tt = TT_RADIO; return;
	case 'y': *own = SIDE_HORDE; *tt = TT_RADIO; return;
	default: *tt = TT_PLAIN; return;
	}
}

u32 rng_next(void)
{
	u32 x = gs.rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return gs.rng = x;
}

int spawn_unit(int type, int side, int x, int y, int hp)
{
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		if (u->alive)
			continue;
		memset(u, 0, sizeof *u);
		u->type = type;
		u->side = side;
		u->x = x;
		u->y = y;
		u->hp = hp;
		u->capture = 20;
		u->alive = 1;
		return i;
	}
	return -1;
}

void map_load(int m)
{
	const Mission *ms = &missions[m];
	u32 seed = gs.rng ? gs.rng : 0x1234567 + frame_count;
	memset(&gs, 0, sizeof gs);
	gs.rng = seed | 1;
	gs.mission = m;
	gs.w = strlen(ms->rows[0]);
	gs.h = ms->h;
	gs.funds = ms->funds;
	gs.day = 1;
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++)
			legend(ms->rows[y][x], &gs.terrain[y][x], &gs.owner[y][x]);
	for (const UnitPlacement *p = ms->units; p->type != 0xFF; p++) {
		int i = spawn_unit(p->type, p->side, p->x, p->y, p->hp ? p->hp : 100);
		if (i >= 0)
			gs.units[i].dormant = p->dormant;
	}
	// start cursor on the first survivor unit
	for (int i = 0; i < MAX_UNITS; i++)
		if (gs.units[i].alive && gs.units[i].side == SIDE_SURVIVOR) {
			gs.cur_x = gs.units[i].x;
			gs.cur_y = gs.units[i].y;
			break;
		}
}

int terrain_at(int x, int y)
{
	if (x < 0 || y < 0 || x >= gs.w || y >= gs.h)
		return TT_WALL;
	return gs.terrain[y][x];
}

int move_cost(int movetype, int tt)
{
	return terrain_info[tt].cost[movetype];
}

int unit_at(int x, int y)
{
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		if (u->alive && !u->carried && u->x == x && u->y == y)
			return i;
	}
	return -1;
}

static bool roadlike(int x, int y)
{
	int t = terrain_at(x, y);
	return x >= 0 && y >= 0 && x < gs.w && y < gs.h && (t == TT_ROAD || t == TT_BRIDGE);
}

static int metatile_for(int x, int y)
{
	switch (gs.terrain[y][x]) {
	case TT_FOREST: return MT_FOREST;
	case TT_HILL: return MT_HILL;
	case TT_WATER: return MT_WATER;
	case TT_BRIDGE: return (terrain_at(x - 1, y) == TT_WATER || terrain_at(x + 1, y) == TT_WATER) &&
	                       terrain_at(x, y - 1) != TT_WATER ? MT_BRIDGE_V : MT_BRIDGE_H;
	case TT_RUINS: return MT_RUINS;
	case TT_WALL: return MT_WALL;
	case TT_WRECK: return MT_WRECK;
	case TT_ROAD:
		return MT_ROAD0 + (roadlike(x, y - 1) ? 1 : 0) + (roadlike(x + 1, y) ? 2 : 0) +
		       (roadlike(x, y + 1) ? 4 : 0) + (roadlike(x - 1, y) ? 8 : 0);
	case TT_HOUSE: return gs.looted[y][x] ? MT_HOUSE_LOOTED : MT_HOUSE;
	case TT_WORKSHOP: return MT_WORKSHOP;
	case TT_SAFEHOUSE: return MT_SAFEHOUSE;
	case TT_HOSPITAL: return MT_HOSPITAL;
	case TT_NEST: return MT_NEST;
	case TT_RADIO: return MT_RADIO;
	case TT_HELIPAD: return MT_HELIPAD;
	default: return MT_PLAIN;
	}
}

static inline int se_index(int tx, int ty)
{
	return (ty >> 5) * 2048 + (tx >> 5) * 1024 + (ty & 31) * 32 + (tx & 31);
}

void map_redraw_tile(int x, int y)
{
	volatile u16 *sb = SCREENBLOCK(SB_TERRAIN);
	int mt = metatile_for(x, y);
	int team = gs.owner[y][x] == SIDE_SURVIVOR ? 1 : gs.owner[y][x] == SIDE_HORDE ? 2 : 0;
	int pal = BGPAL_TERRAIN + team + (gs.fog[y][x] ? 3 : 0);
	for (int i = 0; i < 4; i++) {
		u16 e = terrain_meta[mt][i];
		e = (TILE_TERRAIN + (e & 0x3FF)) | (e & (SE_HFLIP | SE_VFLIP)) | SE_PAL(pal);
		sb[se_index(x * 2 + (i & 1), y * 2 + (i >> 1))] = e;
	}
}

void map_draw(void)
{
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++)
			map_redraw_tile(x, y);
}

void overlay_set(int x, int y, int kind)
{
	volatile u16 *sb = SCREENBLOCK(SB_OVERLAY);
	for (int i = 0; i < 4; i++) {
		u16 e = kind ? ((TILE_OVERLAY + (overlay_meta[kind][i] & 0x3FF)) |
		                (overlay_meta[kind][i] & (SE_HFLIP | SE_VFLIP)) | SE_PAL(BGPAL_OVERLAY)) : 0;
		sb[se_index(x * 2 + (i & 1), y * 2 + (i >> 1))] = e;
	}
}

void overlay_clear(void)
{
	volatile u16 *sb = SCREENBLOCK(SB_OVERLAY);
	for (int i = 0; i < 4 * 1024; i++)
		sb[i] = 0;
}

// Fog of war: only at night. Survivors see within vision range of their units
// and on/next to buildings they own.
void fog_update(void)
{
	static u8 prev[MAP_MAX_H][MAP_MAX_W] EWRAM_BSS;
	memcpy(prev, gs.fog, sizeof prev);
	u8 f = gs.night ? 1 : 0;
	memset(gs.fog, f, sizeof gs.fog);
	if (f) {
		for (int i = 0; i < MAX_UNITS; i++) {
			Unit *u = &gs.units[i];
			if (!u->alive || u->carried || u->side != SIDE_SURVIVOR)
				continue;
			int v = unit_info[u->type].vision;
			if (gs.terrain[u->y][u->x] == TT_HILL && (unit_info[u->type].flags & UF_INFANTRY))
				v += 2;
			for (int dy = -v; dy <= v; dy++)
				for (int dx = -v; dx <= v; dx++) {
					int x = u->x + dx, y = u->y + dy;
					if (ABS(dx) + ABS(dy) > v || x < 0 || y < 0 || x >= gs.w || y >= gs.h)
						continue;
					// woods hide what's inside unless adjacent
					if (gs.terrain[y][x] == TT_FOREST && ABS(dx) + ABS(dy) > 1)
						continue;
					gs.fog[y][x] = 0;
				}
		}
		for (int y = 0; y < gs.h; y++)
			for (int x = 0; x < gs.w; x++)
				if (gs.owner[y][x] == SIDE_SURVIVOR) {
					gs.fog[y][x] = 0;
					if (gs.terrain[y][x] == TT_RADIO) // radio towers light up a wide area
						for (int dy = -3; dy <= 3; dy++)
							for (int dx = -3; dx <= 3; dx++)
								if (ABS(dx) + ABS(dy) <= 3 && x + dx >= 0 && y + dy >= 0 &&
								    x + dx < gs.w && y + dy < gs.h)
									gs.fog[y + dy][x + dx] = 0;
				}
	}
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++)
			if (prev[y][x] != gs.fog[y][x])
				map_redraw_tile(x, y);
}

static void cam_target(int x, int y, int *tx, int *ty)
{
	int cx = gs.cam_x, cy = gs.cam_y;
	if (x < cx + 2) cx = x - 2;
	if (x > cx + SCREEN_TW - 3) cx = x - SCREEN_TW + 3;
	if (y < cy + 2) cy = y - 2;
	if (y > cy + SCREEN_TH - 3) cy = y - SCREEN_TH + 3;
	cx = MAX(0, MIN(cx, gs.w - SCREEN_TW));
	cy = MAX(0, MIN(cy, gs.h - SCREEN_TH));
	*tx = cx;
	*ty = cy;
}

void camera_apply(void)
{
	cam_px = gs.cam_x * 16;
	cam_py = gs.cam_y * 16;
}

void camera_follow(int x, int y, bool smooth)
{
	int tx, ty;
	cam_target(x, y, &tx, &ty);
	if (smooth) {
		int px = tx * 16, py = ty * 16;
		while (cam_px != px || cam_py != py) {
			int step = profile.anim_speed ? 4 : 8;
			if (cam_px < px) cam_px = MIN(px, cam_px + step);
			if (cam_px > px) cam_px = MAX(px, cam_px - step);
			if (cam_py < py) cam_py = MIN(py, cam_py + step);
			if (cam_py > py) cam_py = MAX(py, cam_py - step);
			frame();
		}
	}
	gs.cam_x = tx;
	gs.cam_y = ty;
	camera_apply();
}

// AW-style damage: base * attacker HP/10 * (100 - stars*defender HP)/100, plus small luck.
int damage_estimate(const Unit *a, const Unit *d, bool luck)
{
	int base = base_damage[a->type][d->type];
	if (!base)
		return 0;
	int ahp = DISP_HP(a->hp), dhp = DISP_HP(d->hp);
	int boost = 100;
	if (a->side == SIDE_HORDE && gs.night)
		boost += 15;
	int stars = terrain_info[gs.terrain[d->y][d->x]].defense;
	if (unit_info[d->type].flags & UF_VEHICLE)
		stars = stars > 2 ? 2 : stars; // vehicles can't dig into buildings as well
	int dmg = base * boost / 100;
	if (luck)
		dmg += rng_next() % 10;
	dmg = dmg * ahp / 10;
	dmg = dmg * (100 - stars * dhp) / 100;
	return MAX(dmg, 0);
}
