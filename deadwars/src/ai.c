// Dead Wars - horde AI: spawning, waking, hunting, overrunning buildings
#include "game.h"
#include "video.h"
#include <string.h>

#define IDX(x, y) ((y) * MAP_MAX_W + (x))
#define NO_PATH 255

static u8 dist[MAP_MAX_W * MAP_MAX_H], prev[MAP_MAX_W * MAP_MAX_H];
static u16 field[MAP_MAX_W * MAP_MAX_H]; // walking distance to the nearest prey
static const s8 dirx[4] = { 0, 1, 0, -1 }, diry[4] = { -1, 0, 1, 0 };

static bool capturable(int x, int y)
{
	int tt = gs.terrain[y][x];
	return terrain_info[tt].building && gs.owner[y][x] != SIDE_HORDE;
}

// Multi-source BFS (weighted by zombie move cost) from all survivors and the
// buildings they own. Zombies are drawn to the living.
static void build_field(void)
{
	static u16 queue[MAP_MAX_W * MAP_MAX_H * 4] EWRAM_BSS;
	int qh = 0, qt = 0;
	for (int i = 0; i < MAP_MAX_W * MAP_MAX_H; i++)
		field[i] = 0xFFFF;
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		if (u->alive && !u->carried && u->side == SIDE_SURVIVOR) {
			field[IDX(u->x, u->y)] = 0;
			queue[qt++] = IDX(u->x, u->y);
		}
	}
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++)
			if (gs.owner[y][x] == SIDE_SURVIVOR && field[IDX(x, y)]) {
				field[IDX(x, y)] = gs.terrain[y][x] == TT_SAFEHOUSE ? 0 : 2;
				queue[qt++] = IDX(x, y);
			}
	while (qh < qt) {
		int c = queue[qh++], cx = c % MAP_MAX_W, cy = c / MAP_MAX_W;
		for (int k = 0; k < 4; k++) {
			int nx = cx + dirx[k], ny = cy + diry[k];
			if (nx < 0 || ny < 0 || nx >= gs.w || ny >= gs.h)
				continue;
			int cost = move_cost(MOVE_ZOMBIE, gs.terrain[ny][nx]);
			if (cost == IMPASSABLE)
				continue;
			u16 nd = field[c] + cost;
			if (nd < field[IDX(nx, ny)]) {
				field[IDX(nx, ny)] = nd;
				if (qt < (int)(sizeof queue / sizeof queue[0]))
					queue[qt++] = IDX(nx, ny);
			}
		}
	}
}

// Returns how many zombies were actually placed.
static int spawn_at_nests(int count, bool night)
{
	int total = 0;
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++) {
			if (gs.terrain[y][x] != TT_NEST)
				continue;
			int made = 0;
			// spawn on the nest, then spill onto adjacent tiles
			for (int k = -1; k < 4 && made < count; k++) {
				int sx = k < 0 ? x : x + dirx[k], sy = k < 0 ? y : y + diry[k];
				if (sx < 0 || sy < 0 || sx >= gs.w || sy >= gs.h || unit_at(sx, sy) >= 0 ||
				    move_cost(MOVE_ZOMBIE, gs.terrain[sy][sx]) == IMPASSABLE)
					continue;
				u32 r = rng_next() % 100;
				int type = UT_WALKER;
				if (night ? r < 45 : r < 20) type = UT_RUNNER;
				else if (r > 92) type = UT_BLOATER;
				else if (r > 86 && gs.day >= 4) type = UT_SPITTER;
				else if (r > 82 && gs.day >= 6) type = UT_BRUTE;
				int i = spawn_unit(type, SIDE_HORDE, sx, sy, 100);
				if (i < 0)
					return total;
				gs.units[i].moved = 1; // fresh spawns act next turn
				made++;
				total++;
			}
		}
	return total;
}

static int nearest_survivor(int x, int y)
{
	int best = 999;
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		if (u->alive && !u->carried && u->side == SIDE_SURVIVOR)
			best = MIN(best, ABS(u->x - x) + ABS(u->y - y));
	}
	return best;
}

// Score attacking target t from (x, y). Higher is better.
static int attack_score(int z, int t, int x, int y)
{
	Unit *zu = &gs.units[z], *tu = &gs.units[t];
	Unit tmp = *zu;
	tmp.x = x;
	tmp.y = y;
	int dmg = damage_estimate(&tmp, tu, false);
	if (!dmg)
		return -1;
	int value = MIN(dmg, tu->hp) * unit_info[tu->type].cost / 100;
	if (dmg >= tu->hp) {
		value += unit_info[tu->type].cost / 2;
		if (unit_info[tu->type].flags & UF_INFANTRY)
			value += 1500; // a new zombie joins the horde
	}
	// expected counterattack
	if (ABS(x - tu->x) + ABS(y - tu->y) == 1 && dmg < tu->hp && !(unit_info[tu->type].flags & UF_INDIRECT)) {
		Unit hurt = *tu;
		hurt.hp -= dmg;
		int counter = damage_estimate(&hurt, &tmp, false);
		value -= MIN(counter, zu->hp) * unit_info[zu->type].cost / 200;
	}
	value += terrain_info[gs.terrain[y][x]].defense * 20;
	return value;
}

static void act(int z)
{
	Unit *zu = &gs.units[z];
	bool indirect = unit_info[zu->type].flags & UF_INDIRECT;
	find_paths(z, dist, prev);
	int best = 0, bx = -1, by = -1, bt = -1;
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++) {
			if (dist[IDX(x, y)] == NO_PATH || !can_end_on(z, x, y))
				continue;
			if (indirect && (x != zu->x || y != zu->y))
				continue;
			for (int t = 0; t < MAX_UNITS; t++) {
				Unit *tu = &gs.units[t];
				if (!tu->alive || tu->carried || tu->side != SIDE_SURVIVOR || !in_range(zu, x, y, tu->x, tu->y))
					continue;
				int s = attack_score(z, t, x, y);
				if (s > best) {
					best = s;
					bx = x;
					by = y;
					bt = t;
				}
			}
		}
	if (bt < 0) {
		// nothing to bite: overrun the building we're on, or shamble toward the living
		if (capturable(zu->x, zu->y) && zu->capture < 20)
			bx = zu->x, by = zu->y;
		else {
			int bestf = 0xFFFF;
			for (int y = 0; y < gs.h; y++)
				for (int x = 0; x < gs.w; x++) {
					if (dist[IDX(x, y)] == NO_PATH || !can_end_on(z, x, y) || (unit_at(x, y) >= 0 && unit_at(x, y) != z))
						continue;
					int f = field[IDX(x, y)];
					if (capturable(x, y))
						f -= 3; // zombies love to swarm buildings
					if (f < bestf || (f == bestf && (rng_next() & 1))) {
						bestf = f;
						bx = x;
						by = y;
					}
				}
		}
	}
	if (bx < 0) {
		zu->moved = 1;
		return;
	}
	u16 path[64];
	int len = build_path(prev, zu->x, zu->y, bx, by, path);
	unit_move_anim(z, path, len);
	if (bt >= 0) {
		camera_follow(gs.units[bt].x, gs.units[bt].y, false);
		attack(z, bt);
		fog_update();
	} else if (capturable(zu->x, zu->y)) {
		int need = zu->capture - DISP_HP(zu->hp);
		if (need <= 0) {
			gs.owner[zu->y][zu->x] = SIDE_HORDE;
			zu->capture = 20;
			map_redraw_tile(zu->x, zu->y);
			fog_update();
		} else {
			zu->capture = need;
		}
	}
	zu->moved = 1;
}

void horde_turn(void)
{
	const Mission *m = &missions[gs.mission];
	start_side_turn(SIDE_HORDE);
	text_clear();
	banner("The Horde", gs.night ? "They hunt in the dark." : "The dead are moving...", 45);

	// nests breed
	if (m->spawn_every && gs.day % m->spawn_every == 0)
		spawn_at_nests(1 + (gs.night ? m->spawn_night_extra : 0), gs.night);
	if (m->day_wave && gs.day == m->day_wave && spawn_at_nests(m->wave_size, gs.night))
		banner("A huge wave!", "The nests boil over.", 60);
	fog_update();

	// wake sleepers near the living, or anyone hurt, or everyone at night
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		if (u->alive && u->side == SIDE_HORDE && u->dormant &&
		    (gs.night || u->hp < 100 || nearest_survivor(u->x, u->y) <= 4))
			u->dormant = 0;
	}

	build_field();
	// act in order: spitters first (they can't move and fire), then closest to prey first
	for (int pass = 0; pass < 2; pass++) {
		for (;;) {
			int pick = -1, pd = 9999;
			for (int i = 0; i < MAX_UNITS; i++) {
				Unit *u = &gs.units[i];
				if (!u->alive || u->side != SIDE_HORDE || u->moved || u->dormant)
					continue;
				if (pass == 0 && !(unit_info[u->type].flags & UF_INDIRECT))
					continue;
				int d = field[IDX(u->x, u->y)];
				if (d < pd) {
					pd = d;
					pick = i;
				}
			}
			if (pick < 0)
				break;
			act(pick);
			check_victory();
			if (gs.over)
				return;
		}
	}
	for (int i = 0; i < MAX_UNITS; i++)
		if (gs.units[i].side == SIDE_HORDE)
			gs.units[i].moved = 0;
}
