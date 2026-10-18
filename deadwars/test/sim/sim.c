// Dead Wars - headless balance simulator.
//
// Runs the real rules (map.c, battle.c, ai.c, data.c, missions.c compiled for the
// host) with a scripted "competent player" bot in place of battle.c's interactive
// player_turn(). The horde side is the real horde_turn().
//
// Player actions that are static in battle.c are mirrored here line for line:
//   sim_search_house  <- battle.c search_house()
//   sim_do_capture    <- battle.c do_capture()
//   sim_move          <- the move + ambush part of battle.c player_turn()
//   sim_act           <- battle.c action_menu() (Fire/Secure/Search/Board/Evac/Wait)
//   sim_build         <- battle.c build_menu() (minus the menu)
//   count_side        <- battle.c count_side()
// If any of those change in battle.c, update the mirror.
#include "game.h"
#include "video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>

#define IDX(x, y) ((y) * MAP_MAX_W + (x))
#define NO_PATH 255
#define NTILES (MAP_MAX_W * MAP_MAX_H)
#define DAY_CAP 30            // a mission still running on this day counts as a timeout
#define FRAME_CAP 400000      // per horde turn; more means the AI is stuck

extern u32 sim_frames;
extern int sim_verbose;
static const s8 dirx[4] = { 0, 1, 0, -1 }, diry[4] = { -1, 0, 1, 0 };
static const Mission *M;
static jmp_buf watchdog;
static int cur_seed, cur_mission;

static bool inb(int x, int y) { return x >= 0 && y >= 0 && x < gs.w && y < gs.h; }

static int count_side(int side)
{
	int n = 0;
	for (int i = 0; i < MAX_UNITS; i++)
		if (gs.units[i].alive && gs.units[i].side == side)
			n++;
	return n;
}

static int count_terrain(int tt)
{
	int n = 0;
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++)
			n += gs.terrain[y][x] == tt;
	return n;
}

// ------------------------------------------------------------ mirrored player actions

static struct { int searches, supplies, recruits, ambushes, traps, built, evacs, burned; } st;

static void sim_search_house(int ui)
{
	Unit *u = &gs.units[ui];
	gs.looted[u->y][u->x] = 1;
	map_redraw_tile(u->x, u->y);
	st.searches++;
	u32 r = rng_next() % 100;
	if (r < 55) {
		int amount = 1000 + (rng_next() % 3) * 500;
		gs.funds += amount;
		st.supplies += amount;
	} else if (r < 80) {
		for (int k = 0; k < 4; k++) {
			int x = u->x + dirx[k], y = u->y + diry[k];
			if (x < 0 || y < 0 || x >= gs.w || y >= gs.h || unit_at(x, y) >= 0 ||
			    move_cost(MOVE_FOOT, gs.terrain[y][x]) == IMPASSABLE)
				continue;
			int n = spawn_unit(UT_SURVIVOR, SIDE_SURVIVOR, x, y, 40 + (rng_next() % 5) * 10);
			if (n >= 0)
				gs.units[n].moved = 1;
			st.recruits++;
			return;
		}
		gs.funds += 500;
		st.supplies += 500;
	} else {
		for (int k = 0; k < 4; k++) {
			int x = u->x + dirx[k], y = u->y + diry[k];
			if (x < 0 || y < 0 || x >= gs.w || y >= gs.h || unit_at(x, y) >= 0 ||
			    move_cost(MOVE_ZOMBIE, gs.terrain[y][x]) == IMPASSABLE)
				continue;
			int z = spawn_unit(UT_WALKER, SIDE_HORDE, x, y, 60);
			if (z >= 0) {
				gs.units[z].moved = 1;
				fog_update();
				st.traps++;
				attack(z, ui);
				return;
			}
		}
	}
}

static void sim_do_capture(int ui)
{
	Unit *u = &gs.units[ui];
	int need = u->capture;
	need -= DISP_HP(u->hp);
	if (need <= 0) {
		u->capture = 20;
		if (gs.terrain[u->y][u->x] == TT_NEST) {
			gs.terrain[u->y][u->x] = TT_RUINS;
			gs.owner[u->y][u->x] = SIDE_NEUTRAL;
			map_redraw_tile(u->x, u->y);
			st.burned++;
			if (sim_verbose)
				printf("    nest burned at %d,%d\n", u->x, u->y);
		} else {
			gs.owner[u->y][u->x] = SIDE_SURVIVOR;
			map_redraw_tile(u->x, u->y);
			fog_update();
		}
	} else {
		u->capture = need;
	}
}

// Move along the shortest path; stops short at a zombie hidden in the fog.
// Returns false when ambushed (the unit's turn is over).
static bool sim_move(int ui, int tx, int ty)
{
	static u8 d[NTILES], p[NTILES];
	Unit *u = &gs.units[ui];
	find_paths(ui, d, p);
	u16 path[64];
	int len = build_path(p, u->x, u->y, tx, ty, path);
	bool ambushed = false;
	for (int i = 0; i < len; i++) {
		int hz = unit_at(path[i] & 0xFF, path[i] >> 8);
		if (hz >= 0 && gs.units[hz].side == SIDE_HORDE) {
			len = i;
			ambushed = true;
			break;
		}
	}
	unit_move_anim(ui, path, len);
	fog_update();
	if (ambushed) {
		u->moved = 1;
		st.ambushes++;
		if (sim_verbose)
			printf("    AMBUSH! stopped at %d,%d\n", u->x, u->y);
		return false;
	}
	return true;
}

static void sim_build(int x, int y, int type)
{
	int i = spawn_unit(type, SIDE_SURVIVOR, x, y, 100);
	if (i >= 0) {
		gs.units[i].moved = 1;
		gs.funds -= unit_info[type].cost;
		fog_update();
		st.built++;
	}
}

static int guard_ui = -1; // unit told to stay home this turn (objective missions)

// bot tuning knobs (per tile-turn of distance to the objective, and % of damage value)
static int W_HOME = 250, W_HUNT = 400, W_HELI = 3000, W_NEST = 1200, W_NEST_DAY = 150, W_RISK = 60, W_FOCUS = 40, W_BLOCK = 4, W_ESCORT = 1, W_EVAC_ATK = 50, W_GUARD = 2500, W_GROUP = 50;

// ------------------------------------------------------------ bot: threat model

static u8 zhit[MAX_UNITS][NTILES]; // zombie z could attack this tile on its next turn
static u8 zawake[MAX_UNITS];
static u8 zprey[MAX_UNITS];        // how many of our units zombie z could bite right now
static int guard_bonus;            // value of standing on our safehouse this turn

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

// Weighted BFS from the marked tiles for a move type. Tiles with a zombie on them
// cost `blocked` extra (they have to be cut through), friendly units are ignored.
static void dist_field_ex(const u8 *mark, int movetype, u16 *out, int blocked)
{
	static u8 horde[NTILES];
	memset(horde, 0, sizeof horde);
	if (blocked)
		for (int i = 0; i < MAX_UNITS; i++)
			if (gs.units[i].alive && gs.units[i].side == SIDE_HORDE)
				horde[IDX(gs.units[i].x, gs.units[i].y)] = 1;
	static u16 q[NTILES * 8];
	int qh = 0, qt = 0;
	for (int i = 0; i < NTILES; i++) {
		out[i] = mark[i] ? 0 : 0xFFFF;
		if (mark[i])
			q[qt++] = i;
	}
	while (qh < qt) {
		int c = q[qh++], cx = c % MAP_MAX_W, cy = c / MAP_MAX_W;
		for (int k = 0; k < 4; k++) {
			int nx = cx + dirx[k], ny = cy + diry[k];
			if (!inb(nx, ny))
				continue;
			int cost = move_cost(movetype, gs.terrain[ny][nx]);
			if (cost == IMPASSABLE)
				continue;
			// cost of entering c from n is the terrain of c, but symmetric enough here
			u16 nd = out[c] + cost + (horde[IDX(nx, ny)] ? blocked : 0);
			if (nd < out[IDX(nx, ny)] && qt < NTILES * 8) {
				out[IDX(nx, ny)] = nd;
				q[qt++] = IDX(nx, ny);
			}
		}
	}
}

static void dist_field(const u8 *mark, int movetype, u16 *out) { dist_field_ex(mark, movetype, out, 0); }

static void scan_horde(void)
{
	static u8 d[NTILES], p[NTILES];
	for (int z = 0; z < MAX_UNITS; z++) {
		Unit *u = &gs.units[z];
		memset(zhit[z], 0, NTILES);
		if (!u->alive || u->carried || u->side != SIDE_HORDE)
			continue;
		zawake[z] = !u->dormant || gs.night || u->hp < 100 || nearest_survivor(u->x, u->y) <= 4;
		const UnitInfo *inf = &unit_info[u->type];
		if (inf->flags & UF_INDIRECT) {
			for (int y = 0; y < gs.h; y++)
				for (int x = 0; x < gs.w; x++)
					if (in_range(u, u->x, u->y, x, y))
						zhit[z][IDX(x, y)] = 1;
			continue;
		}
		find_paths(z, d, p);
		for (int y = 0; y < gs.h; y++)
			for (int x = 0; x < gs.w; x++) {
				if (d[IDX(x, y)] == NO_PATH)
					continue;
				int o = unit_at(x, y);
				if (o >= 0 && o != z && gs.units[o].side == SIDE_HORDE)
					continue;
				for (int k = 0; k < 4; k++)
					if (inb(x + dirx[k], y + diry[k]))
						zhit[z][IDX(x + dirx[k], y + diry[k])] = 1;
			}
	}
	for (int z = 0; z < MAX_UNITS; z++) {
		zprey[z] = 0;
		for (int i = 0; i < MAX_UNITS; i++) {
			Unit *u = &gs.units[i];
			if (u->alive && !u->carried && u->side == SIDE_SURVIVOR && zhit[z][IDX(u->x, u->y)])
				zprey[z]++;
		}
	}
	// guard the safehouse when zombies are within two turns of it
	u8 mark[NTILES] = { 0 };
	static u16 f[NTILES];
	bool any = false;
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++)
			if (gs.terrain[y][x] == TT_SAFEHOUSE && gs.owner[y][x] == SIDE_SURVIVOR)
				mark[IDX(x, y)] = any = 1;
	guard_bonus = 0;
	if (any) {
		dist_field(mark, MOVE_ZOMBIE, f);
		int turns = 99;
		for (int z = 0; z < MAX_UNITS; z++) {
			Unit *u = &gs.units[z];
			if (u->alive && u->side == SIDE_HORDE && f[IDX(u->x, u->y)] != 0xFFFF)
				turns = MIN(turns, (f[IDX(u->x, u->y)] + unit_move_points(u) - 1) / unit_move_points(u));
		}
		guard_bonus = turns <= 1 ? 4000 : turns <= 2 ? 1200 : 0;
	}
}

// Damage unit u could take standing on (x, y) during the next horde turn.
// Returns the worst case (everything that can reach focuses on it, limited by open
// sides) and stores in *share the expected case (each zombie splits its attention
// among all the units it could bite).
static int threat_at(int ui, const Unit *u, int x, int y, int skip, int *share)
{
	int mel[MAX_UNITS], nm = 0, ranged = 0;
	*share = 0;
	// friends that haven't moved yet and are close can follow us in and split the bites
	int backup = 0;
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *f = &gs.units[i];
		if (i != ui && f->alive && !f->carried && f->side == SIDE_SURVIVOR && !f->moved &&
		    !(unit_info[f->type].flags & UF_TRANSPORT) && ABS(f->x - x) + ABS(f->y - y) <= unit_info[f->type].move + 1)
			backup++;
	}
	Unit tmp = *u;
	tmp.x = x;
	tmp.y = y;
	for (int z = 0; z < MAX_UNITS; z++) {
		Unit *zu = &gs.units[z];
		if (z == skip || !zu->alive || zu->side != SIDE_HORDE || !zhit[z][IDX(x, y)])
			continue;
		if (!zawake[z] && ABS(zu->x - x) + ABS(zu->y - y) > 4)
			continue;
		int dmg = damage_estimate(zu, &tmp, false) + 4 * DISP_HP(zu->hp) / 10;
		*share += dmg * 100 / (100 * (zprey[z] + (zhit[z][IDX(u->x, u->y)] ? 0 : 1)) + backup * W_GROUP);
		if (unit_info[zu->type].flags & UF_INDIRECT)
			ranged += dmg;
		else
			mel[nm++] = dmg;
	}
	// only as many biters as there are open sides
	int open = 0;
	for (int k = 0; k < 4; k++) {
		int nx = x + dirx[k], ny = y + diry[k];
		if (!inb(nx, ny) || move_cost(MOVE_ZOMBIE, gs.terrain[ny][nx]) == IMPASSABLE)
			continue;
		int o = unit_at(nx, ny);
		if (o < 0 || o == ui || gs.units[o].side == SIDE_HORDE)
			open++;
	}
	for (int i = 0; i < nm; i++) // partial selection sort, descending
		for (int j = i + 1; j < nm; j++)
			if (mel[j] > mel[i]) {
				int t = mel[i]; mel[i] = mel[j]; mel[j] = t;
			}
	int sum = ranged;
	for (int i = 0; i < nm && i < MIN(open, 3); i++)
		sum += mel[i];
	return sum;
}

// ------------------------------------------------------------ bot: objectives

enum { R_HOME, R_NEST, R_HELI, R_HUNT, R_COUNT };
static u16 goal_f[R_COUNT][MOVE_COUNT][NTILES];
static bool goal_ok[R_COUNT];

static void pick_guard(void);

static void build_goals(void)
{
	static u8 mark[R_COUNT][NTILES];
	memset(mark, 0, sizeof mark);
	memset(goal_ok, 0, sizeof goal_ok);
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++) {
			int tt = gs.terrain[y][x];
			if (tt == TT_SAFEHOUSE && gs.owner[y][x] == SIDE_SURVIVOR)
				mark[R_HOME][IDX(x, y)] = goal_ok[R_HOME] = 1;
			if (tt == TT_NEST)
				mark[R_NEST][IDX(x, y)] = goal_ok[R_NEST] = 1;
			if (tt == TT_HELIPAD)
				mark[R_HELI][IDX(x, y)] = goal_ok[R_HELI] = 1;
		}
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		if (u->alive && u->side == SIDE_HORDE && !gs.fog[u->y][u->x])
			mark[R_HUNT][IDX(u->x, u->y)] = goal_ok[R_HUNT] = 1;
	}
	for (int r = 0; r < R_COUNT; r++)
		if (goal_ok[r])
			for (int mt = 0; mt < MOVE_COUNT; mt++)
				dist_field_ex(mark[r], mt, goal_f[r][mt], r == R_HUNT ? 0 : W_BLOCK);
	pick_guard();
}


static void pick_guard(void)
{
	guard_ui = -1;
	if (M->goal == GOAL_SURVIVE || !goal_ok[R_HOME] || count_side(SIDE_SURVIVOR) < 4)
		return;
	int best = 0xFFFF;
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		if (!u->alive || u->carried || u->side != SIDE_SURVIVOR || (unit_info[u->type].flags & UF_TRANSPORT) ||
		    u->type == UT_PICKUP || u->type == UT_SNIPER || u->type == UT_MEDIC)
			continue;
		int d = goal_f[R_HOME][unit_info[u->type].movetype][IDX(u->x, u->y)];
		if (u->type == UT_SURVIVOR)
			d += 2; // rather a Heavy or Armor
		if (d < best) {
			best = d;
			guard_ui = i;
		}
	}
}

static int role_of(const Unit *u)
{
	bool inf = unit_info[u->type].flags & UF_INFANTRY;
	if (guard_ui >= 0 && u == &gs.units[guard_ui])
		return R_HOME;
	switch (M->goal) {
	case GOAL_EVAC:
		return R_HELI;
	case GOAL_NESTS:
	case GOAL_ROUT:
		// push on the nests as one group; mop up stragglers once they're gone
		if (goal_ok[R_NEST] && (inf || W_ESCORT))
			return R_NEST;
		return goal_ok[R_HUNT] ? R_HUNT : R_NEST;
	default:
		return R_HOME;
	}
}


static int goal_cost(const Unit *u, int x, int y)
{
	int r = role_of(u);
	if (!goal_ok[r])
		return 0;
	int d = goal_f[r][unit_info[u->type].movetype][IDX(x, y)];
	if (d == 0xFFFF)
		d = 60;
	int mp = unit_move_points(u);
	if (guard_ui >= 0 && u == &gs.units[guard_ui])
		return W_GUARD * d / mp; // the guard sits on the safehouse
	switch (r) {
	case R_HOME: return W_HOME * MAX(0, d - 2) / mp;
	case R_HUNT: return W_HUNT * MAX(0, d - 1) / mp;
	case R_HELI: return (u->cargo ? W_HELI * 3 / 2 : W_HELI) * d / mp;
	default:     return (W_NEST + W_NEST_DAY * gs.day) * d / mp;
	}
}

static int unit_value(const Unit *u)
{
	int v = unit_info[u->type].cost + 500;  // every body counts
	if (unit_info[u->type].flags & UF_INFANTRY)
		v += 1000;                          // and gets back up as a Walker
	if (u->cargo)
		v += unit_value(&gs.units[u->cargo - 1]);
	return v;
}

static int building_value(int x, int y)
{
	switch (gs.terrain[y][x]) {
	case TT_NEST: return (M->goal == GOAL_NESTS || M->goal == GOAL_ROUT) ? 9000 : 3000;
	case TT_WORKSHOP: return 3500;
	case TT_SAFEHOUSE: return 6000;
	case TT_HOSPITAL: return 2000;
	case TT_RADIO: return 1500;
	default: return 1000;
	}
}

static int position_score(int ui, const Unit *u, int x, int y, int skip, int hp)
{
	int val = unit_value(u), s = 0;
	const UnitInfo *inf = &unit_info[u->type];
	bool infantry = inf->flags & UF_INFANTRY;
	int tt = gs.terrain[y][x];
	s -= goal_cost(u, x, y);
	int stars = terrain_info[tt].defense;
	if (inf->flags & UF_VEHICLE)
		stars = MIN(stars, 2);
	s += stars * 40;
	if (gs.owner[y][x] == SIDE_SURVIVOR && hp < 100 &&
	    ((infantry && tt != TT_WORKSHOP) || (!infantry && tt == TT_WORKSHOP)))
		s += MIN(100 - hp, tt == TT_HOSPITAL ? 30 : 20) * val / 100 * (hp < 50 ? 3 : 1);
	if (inf->flags & UF_HEALER)
		for (int k = 0; k < 4; k++) {
			int o = unit_at(x + dirx[k], y + diry[k]);
			if (o >= 0 && o != ui && gs.units[o].side == SIDE_SURVIVOR && gs.units[o].hp <= 90)
				s += 10 * unit_value(&gs.units[o]) / 100;
		}
	if (tt == TT_SAFEHOUSE && gs.owner[y][x] == SIDE_SURVIVOR && !(inf->flags & UF_TRANSPORT))
		s += guard_bonus;
	if (tt == TT_WORKSHOP && gs.owner[y][x] == SIDE_SURVIVOR && gs.funds >= 1000)
		s -= 300; // don't block our own recruiting
	Unit tmp = *u;
	tmp.hp = hp;
	int share, th = threat_at(ui, &tmp, x, y, skip, &share);
	// chance of dying: ramps up as the expected damage nears our HP, and jumps if
	// everything in reach could gang up on us
	int pdie = MAX(0, MIN(100, (share * 100 / MAX(hp, 1) - 60) * 100 / 80));
	if (th >= hp)
		pdie = MAX(pdie, W_FOCUS);
	s -= pdie * val / 100 + MIN(share, hp) * val / 100 * W_RISK / 100;
	return s;
}

// ------------------------------------------------------------ bot: planning

enum { A_WAIT, A_FIRE, A_CAPTURE, A_SEARCH, A_BOARD, A_EVAC };
static const char *const act_name[] = { "Wait", "Fire", "Secure", "Search", "Board", "Evac" };
typedef struct { int gain, score, ui, x, y, act, target; } Plan;

static int attack_score(int ui, int x, int y, int t)
{
	Unit *u = &gs.units[ui], *tu = &gs.units[t];
	Unit a = *u;
	a.x = x;
	a.y = y;
	int dmg = damage_estimate(&a, tu, false) + 4 * DISP_HP(u->hp) / 10;
	int tv = unit_info[tu->type].cost;
	bool kill = dmg >= tu->hp;
	int s = MIN(dmg, tu->hp) * tv / 100, hp_after = u->hp;
	bool adj = ABS(x - tu->x) + ABS(y - tu->y) == 1;
	if (kill) {
		s += tv / 2 + 800;
		if (unit_info[tu->type].flags & UF_EXPLODES) {
			for (int k = 0; k < 4; k++) {
				int nx = tu->x + dirx[k], ny = tu->y + diry[k];
				int o = (nx == x && ny == y) ? ui : unit_at(nx, ny);
				if (o >= 0 && gs.units[o].side == SIDE_SURVIVOR && (o != ui || adj))
					s -= 20 * unit_value(&gs.units[o]) / 100;
			}
			if (adj)
				hp_after = MAX(1, hp_after - 20);
		}
	} else if (adj && !(unit_info[tu->type].flags & UF_INDIRECT) && base_damage[tu->type][u->type]) {
		Unit d2 = *tu;
		d2.hp -= dmg;
		int c = damage_estimate(&d2, &a, false) + 4 * DISP_HP(d2.hp) / 10;
		if (c >= hp_after)
			return s - unit_value(u) - 2000;
		s -= c * unit_value(u) / 100;
		hp_after -= c;
	}
	if (gs.owner[tu->y][tu->x] == SIDE_SURVIVOR && terrain_info[gs.terrain[tu->y][tu->x]].building)
		s += gs.terrain[tu->y][tu->x] == TT_SAFEHOUSE ? 8000 : 1500;
	if (kill)
		s += zprey[t] * 300; // it was about to bite one of us
	if (role_of(u) == R_HELI)
		s = s * W_EVAC_ATK / 100; // the evac runners shouldn't stop for every zombie
	// judge the spot with the target as it will be after the hit
	int old = tu->hp;
	if (!kill)
		tu->hp -= dmg;
	s += position_score(ui, u, x, y, kill ? t : -1, hp_after);
	tu->hp = old;
	return s;
}

static void consider(Plan *best, int base, int score, int ui, int x, int y, int act, int target)
{
	if (score - base > best->gain || best->ui < 0) {
		best->gain = score - base;
		best->score = score;
		best->ui = ui;
		best->x = x;
		best->y = y;
		best->act = act;
		best->target = target;
	}
}

static void plan_unit(int ui, Plan *best)
{
	static u8 d[NTILES], p[NTILES];
	Unit *u = &gs.units[ui];
	const UnitInfo *inf = &unit_info[u->type];
	bool infantry = inf->flags & UF_INFANTRY;
	find_paths(ui, d, p);
	int base = position_score(ui, u, u->x, u->y, -1, u->hp);
	Plan mine = { .ui = -1 };
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++) {
			if (d[IDX(x, y)] == NO_PATH)
				continue;
			int o = unit_at(x, y);
			if (o >= 0 && gs.units[o].side == SIDE_HORDE)
				o = -1; // hidden in the fog: the player can't know it's there
			bool moved = x != u->x || y != u->y;
			if (o >= 0 && o != ui) {
				if (!can_end_on(ui, x, y))
					continue;
				// boarding an empty bus that can still drive this turn
				if (M->goal == GOAL_EVAC && !gs.units[o].moved) {
					int s = 2500 - goal_cost(u, x, y) * 3 / 4;
					consider(&mine, base, s, ui, x, y, A_BOARD, o);
				}
				continue;
			}
			int tt = gs.terrain[y][x];
			if (tt == TT_HELIPAD && M->goal == GOAL_EVAC) {
				consider(&mine, base, 40000 * (u->cargo ? 2 : 1), ui, x, y, A_EVAC, -1);
				continue;
			}
			int pos = position_score(ui, u, x, y, -1, u->hp);
			consider(&mine, base, pos, ui, x, y, A_WAIT, -1);
			if (!(inf->flags & UF_INDIRECT) || !moved)
				for (int t = 0; t < MAX_UNITS; t++) {
					Unit *tu = &gs.units[t];
					if (!tu->alive || tu->carried || tu->side == u->side || gs.fog[tu->y][tu->x] ||
					    !base_damage[u->type][tu->type] || !in_range(u, x, y, tu->x, tu->y))
						continue;
					consider(&mine, base, attack_score(ui, x, y, t), ui, x, y, A_FIRE, t);
				}
			if (infantry && ((terrain_info[tt].building && gs.owner[y][x] != SIDE_SURVIVOR) || tt == TT_NEST)) {
				int need = (moved ? 20 : u->capture) - DISP_HP(u->hp);
				int bv = building_value(x, y);
				int bonus = need <= 0 ? bv : bv * (20 - need) / 40;
				consider(&mine, base, pos + bonus, ui, x, y, A_CAPTURE, -1);
			}
			if (infantry && tt == TT_HOUSE && !gs.looted[y][x])
				consider(&mine, base, pos + 900, ui, x, y, A_SEARCH, -1);
		}
	if (sim_verbose >= 3 && mine.ui >= 0) {
		// debug: score of every reachable tile for this unit (Wait only)
		printf("    [%s %d,%d base %d] best %s %d,%d gain %d\n", unit_info[u->type].name, u->x, u->y, base,
		       act_name[mine.act], mine.x, mine.y, mine.gain);
		for (int y = 0; y < gs.h; y++)
			for (int x = 0; x < gs.w; x++)
				if (d[IDX(x, y)] != NO_PATH && can_end_on(ui, x, y)) {
					Unit tmp = *u;
					int sh, th = threat_at(ui, &tmp, x, y, -1, &sh);
					printf("      %2d,%2d pos %6d goal %6d threat %3d share %3d\n", x, y,
					       position_score(ui, u, x, y, -1, u->hp), goal_cost(u, x, y), th, sh);
				}
	}
	if (mine.ui >= 0 && (best->ui < 0 || mine.gain > best->gain))
		*best = mine;
}

static void sim_act(const Plan *pl)
{
	Unit *u = &gs.units[pl->ui];
	if (sim_verbose)
		printf("  D%d %-8s %2d,%2d -> %2d,%2d %s%s%s\n", gs.day, unit_info[u->type].name, u->x, u->y, pl->x, pl->y,
		       act_name[pl->act], pl->target >= 0 && pl->act == A_FIRE ? " " : "",
		       pl->target >= 0 && pl->act == A_FIRE ? unit_info[gs.units[pl->target].type].name : "");
	if (sim_verbose >= 2)
		printf("      gain %d score %d role %d goal %d -> %d\n", pl->gain, pl->score, role_of(u),
		       goal_cost(u, u->x, u->y), goal_cost(u, pl->x, pl->y));
	if (!sim_move(pl->ui, pl->x, pl->y))
		return;
	switch (pl->act) {
	case A_FIRE:
		attack(pl->ui, pl->target);
		break;
	case A_CAPTURE:
		sim_do_capture(pl->ui);
		break;
	case A_SEARCH:
		sim_search_house(pl->ui);
		break;
	case A_BOARD:
		gs.units[pl->target].cargo = pl->ui + 1;
		u->carried = 1;
		break;
	case A_EVAC:
		gs.evacuated += 1 + (u->cargo ? 1 : 0);
		st.evacs += 1 + (u->cargo ? 1 : 0);
		if (u->cargo)
			gs.units[u->cargo - 1].alive = 0;
		u->alive = 0;
		break;
	}
	u->moved = 1;
	fog_update();
}

static void recruit(void)
{
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++) {
			if (gs.terrain[y][x] != TT_WORKSHOP || gs.owner[y][x] != SIDE_SURVIVOR || unit_at(x, y) >= 0)
				continue;
			int left = M->goal_days ? M->goal_days - gs.day : 99;
			int type = -1;
			if (M->goal == GOAL_EVAC && gs.funds >= 4000 && left >= 3)
				type = UT_PICKUP;
			else if (gs.funds >= 7000)
				type = UT_ARMOR;
			else if (gs.funds >= 3000)
				type = UT_HEAVY;
			else if (gs.funds >= 1000 && count_side(SIDE_SURVIVOR) < 4)
				type = UT_SURVIVOR;
			if (type < 0 || (M->goal == GOAL_SURVIVE && left < 1))
				continue;
			if (sim_verbose)
				printf("  D%d build %s\n", gs.day, unit_info[type].name);
			sim_build(x, y, type);
		}
}

static void bot_turn(void)
{
	build_goals();
	for (int step = 0; step < MAX_UNITS * 2; step++) {
		check_victory();
		if (gs.over)
			return;
		scan_horde();
		Plan best = { .ui = -1 };
		for (int i = 0; i < MAX_UNITS; i++) {
			Unit *u = &gs.units[i];
			if (u->alive && !u->carried && u->side == SIDE_SURVIVOR && !u->moved)
				plan_unit(i, &best);
		}
		if (best.ui < 0)
			break;
		sim_act(&best);
	}
	recruit();
}

// ------------------------------------------------------------ game loop

static void dump_map(void)
{
	static const char tch[TT_COUNT] = { '.', 'f', '^', '~', '=', 'r', '#', 'w', '-', 'h', 'k', 's', 'p', 'n', 'x', '@' };
	static const char uch[UT_COUNT] = { 'S', 'H', 'M', 'N', 'P', 'B', 'A', 'w', 'r', 'b', 's', 'x' };
	printf("  Day %d%s  funds %u  survivors %d  zombies %d  nests %d\n", gs.day, gs.night ? " NIGHT" : "",
	       (unsigned)gs.funds, count_side(SIDE_SURVIVOR), count_side(SIDE_HORDE), count_terrain(TT_NEST));
	for (int y = 0; y < gs.h; y++) {
		printf("   ");
		for (int x = 0; x < gs.w; x++) {
			int o = unit_at(x, y);
			char c = tch[gs.terrain[y][x]];
			if (terrain_info[gs.terrain[y][x]].building && gs.owner[y][x] == SIDE_SURVIVOR)
				c = c - 'a' + 'A';
			if (o >= 0)
				c = uch[gs.units[o].type];
			putchar(c);
			putchar(o >= 0 ? '0' + DISP_HP(gs.units[o].hp) % 10 : ' ');
		}
		putchar('\n');
	}
}

enum { END_WIN, END_OVERRUN, END_WIPED, END_DEADLINE, END_TIMEOUT, END_STALL, END_COUNT };
static const char *const end_name[END_COUNT] = { "win", "overrun", "wiped", "deadline", "timeout", "STALL" };

typedef struct { int end, day, kills, losses, peak_horde, survivors, burned, evacs; } Result;

static void on_alarm(int sig)
{
	printf("\nSTALL: mission %d seed %d hung without drawing frames (compute loop)\n", cur_mission + 1, cur_seed);
	fflush(stdout);
	_exit(3);
}

static Result play(int m, int seed)
{
	Result r = { 0 };
	cur_mission = m;
	cur_seed = seed;
	M = &missions[m];
	memset(&st, 0, sizeof st);
	memset(&gs, 0, sizeof gs);
	gs.rng = 0x9E3779B9u * (u32)(seed + 1);
	map_load(m);
	start_side_turn(SIDE_SURVIVOR);
	alarm(20);
	static int peak;
	peak = 0;
	if (setjmp(watchdog)) {
		printf("STALL: mission %d seed %d day %d: %s turn exceeded %d frames\n", m + 1, seed, gs.day,
		       gs.side == SIDE_HORDE ? "horde" : "player", FRAME_CAP);
		r.end = END_STALL;
		goto done;
	}
	for (;;) {
		peak = MAX(peak, count_side(SIDE_HORDE));
		if (sim_verbose)
			dump_map();
		sim_frames = 0;
		bot_turn();
		check_victory();
		if (gs.over)
			break;
		sim_frames = 0;
		horde_turn();
		check_victory();
		if (gs.over)
			break;
		gs.day++;
		start_side_turn(SIDE_SURVIVOR);
		check_victory();
		if (gs.over)
			break;
		if (gs.day > DAY_CAP) {
			r.end = END_TIMEOUT;
			goto done;
		}
	}
	if (gs.over == 1) {
		r.end = END_WIN;
	} else {
		bool lost_safehouse = false;
		for (int y = 0; y < gs.h; y++)
			for (int x = 0; x < gs.w; x++)
				if (gs.terrain[y][x] == TT_SAFEHOUSE && gs.owner[y][x] == SIDE_HORDE)
					lost_safehouse = true;
		r.end = lost_safehouse ? END_OVERRUN : count_side(SIDE_SURVIVOR) == 0 ? END_WIPED : END_DEADLINE;
	}
done:
	alarm(0);
	r.peak_horde = peak;
	r.burned = st.burned;
	r.evacs = st.evacs;
	r.day = gs.day;
	r.kills = gs.kills;
	r.losses = gs.losses;
	r.survivors = count_side(SIDE_SURVIVOR);
	if (sim_verbose) {
		dump_map();
		printf("  => %s on day %d (kills %d, losses %d, searches %d, ambushes %d, built %d)\n", end_name[r.end],
		       r.day, r.kills, r.losses, st.searches, st.ambushes, st.built);
	}
	return r;
}

// called from stubs.c frame()/wait_frames() via sim_frames; checked here per frame
void sim_check_watchdog(void)
{
	if (sim_frames > FRAME_CAP)
		longjmp(watchdog, 1);
}

int main(int argc, char **argv)
{
	int only = -1, seeds = 40, first = 1;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-m") && i + 1 < argc)
			only = atoi(argv[++i]) - 1;
		else if (!strcmp(argv[i], "-n") && i + 1 < argc)
			seeds = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-s") && i + 1 < argc)
			first = atoi(argv[++i]);
		else if (!strncmp(argv[i], "-W", 2) && strchr(argv[i], '=')) {
			static const struct { const char *n; int *v; } knobs[] = {
				{ "home", &W_HOME }, { "hunt", &W_HUNT }, { "heli", &W_HELI }, { "nest", &W_NEST },
				{ "nestday", &W_NEST_DAY }, { "risk", &W_RISK }, { "focus", &W_FOCUS }, { "block", &W_BLOCK }, { "escort", &W_ESCORT }, { "evacatk", &W_EVAC_ATK }, { "guard", &W_GUARD }, { "group", &W_GROUP },
			};
			for (unsigned k = 0; k < sizeof knobs / sizeof knobs[0]; k++)
				if (!strncmp(argv[i] + 2, knobs[k].n, strlen(knobs[k].n)) && argv[i][2 + strlen(knobs[k].n)] == '=')
					*knobs[k].v = atoi(strchr(argv[i], '=') + 1);
		} else if (!strcmp(argv[i], "-v"))
			sim_verbose++;
		else {
			fprintf(stderr, "usage: %s [-m mission 1..%d] [-n seeds] [-s first_seed] [-v[-v]] [-Wknob=N]\n", argv[0], mission_count);
			return 1;
		}
	}
	signal(SIGALRM, on_alarm);
	profile_load();
	keys_held = KEY_B; // fast-forward the horde (fewer animation frames)
	printf("%-16s %5s  %-40s %5s %5s %5s %5s %5s\n", "mission", "win%", "outcomes", "days", "kills", "lost", "peakZ", "goal");
	for (int m = 0; m < mission_count; m++) {
		if (only >= 0 && m != only)
			continue;
		int ends[END_COUNT] = { 0 }, wins = 0;
		double days = 0, wdays = 0, kills = 0, losses = 0, peak = 0, prog = 0;
		for (int s = first; s < first + seeds; s++) {
			Result r = play(m, s);
			ends[r.end]++;
			wins += r.end == END_WIN;
			days += r.day;
			if (r.end == END_WIN)
				wdays += r.day;
			kills += r.kills;
			losses += r.losses;
			peak += r.peak_horde;
			prog += missions[m].goal == GOAL_EVAC ? r.evacs : r.burned;
			if (sim_verbose)
				printf("seed %d: %s day %d\n", s, end_name[r.end], r.day);
		}
		char out[96];
		int p = 0;
		for (int e = 0; e < END_COUNT; e++)
			if (ends[e])
				p += snprintf(out + p, sizeof out - p, "%s %d  ", end_name[e], ends[e]);
		out[p] = 0;
		printf("%d %-14s %4.0f%%  %-40s %5.1f %5.1f %5.1f %5.1f %5.1f\n", m + 1, missions[m].title, 100.0 * wins / seeds,
		       out, days / seeds, kills / seeds, losses / seeds, peak / seeds, prog / seeds);
		fflush(stdout);
	}
	return 0;
}
