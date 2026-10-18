// Dead Wars - battle flow: player turn, movement, actions, combat, turn cycle
#include "game.h"
#include "video.h"
#include "perf.h"
#include <string.h>

#define IDX(x, y) ((y) * MAP_MAX_W + (x))
#define NO_PATH 255

static u8 dist[MAP_MAX_W * MAP_MAX_H], prev[MAP_MAX_W * MAP_MAX_H];
static const s8 dirx[4] = { 0, 1, 0, -1 }, diry[4] = { -1, 0, 1, 0 };

int unit_move_points(const Unit *u)
{
	int mp = unit_info[u->type].move;
	if (u->side == SIDE_HORDE && gs.night)
		mp++;
	return mp;
}

// Can unit u stop on (x, y)? Empty tile, its own tile, or boarding an empty friendly bus.
bool can_end_on(int ui, int x, int y)
{
	int o = unit_at(x, y);
	if (o < 0 || o == ui)
		return true;
	Unit *u = &gs.units[ui], *t = &gs.units[o];
	return t->side == u->side && t->type == UT_BUS && !t->cargo && (unit_info[u->type].flags & UF_INFANTRY);
}

// Dijkstra over the map. Enemy units block (except zombies hidden by fog when it is
// the player moving: they ambush instead). Returns number of reachable tiles.
int find_paths(int ui, u8 *d, u8 *p)
{
	Unit *u = &gs.units[ui];
	int mt = unit_info[u->type].movetype, mp = unit_move_points(u);
	static u16 queue[MAP_MAX_W * MAP_MAX_H * 4] EWRAM_BSS;
	PERF_T0(t0);
	memset(d, NO_PATH, MAP_MAX_W * MAP_MAX_H);
	memset(p, NO_PATH, MAP_MAX_W * MAP_MAX_H);
	occ_begin();
	int qh = 0, qt = 0, n = 0;
	d[IDX(u->x, u->y)] = 0;
	queue[qt++] = u->x | u->y << 8; // packed x,y: no division on the GBA
	while (qh < qt) {
		int cx = queue[qh] & 0xFF, cy = queue[qh++] >> 8, c = IDX(cx, cy);
		for (int k = 0; k < 4; k++) {
			int nx = cx + dirx[k], ny = cy + diry[k];
			if (nx < 0 || ny < 0 || nx >= gs.w || ny >= gs.h)
				continue;
			int cost = move_cost(mt, gs.terrain[ny][nx]);
			if (cost == IMPASSABLE)
				continue;
			int nd = d[c] + cost;
			if (nd > mp || nd >= d[IDX(nx, ny)])
				continue;
			int o = unit_at(nx, ny);
			if (o >= 0 && gs.units[o].side != u->side &&
			    !(u->side == SIDE_SURVIVOR && gs.fog[ny][nx]))
				continue;
			d[IDX(nx, ny)] = nd;
			p[IDX(nx, ny)] = k;
			if (qt < (int)(sizeof queue / sizeof queue[0]))
				queue[qt++] = nx | ny << 8;
		}
	}
	occ_end();
	for (int i = 0; i < MAP_MAX_W * MAP_MAX_H; i++)
		if (d[i] != NO_PATH)
			n++;
	PERF_ADD(paths, t0);
	return n;
}

int build_path(const u8 *p, int sx, int sy, int tx, int ty, u16 *path)
{
	u16 rev[64];
	int n = 0, x = tx, y = ty;
	while ((x != sx || y != sy) && n < 64) {
		rev[n++] = x | (y << 8);
		int k = p[IDX(x, y)];
		if (k == NO_PATH)
			break;
		x -= dirx[k];
		y -= diry[k];
	}
	for (int i = 0; i < n; i++)
		path[i] = rev[n - 1 - i];
	return n;
}

void unit_move_anim(int ui, const u16 *path, int len)
{
	Unit *u = &gs.units[ui];
	bool visible = !(u->side == SIDE_HORDE && gs.fog[u->y][u->x]);
	spr.anim_unit = ui;
	spr.anim_px = u->x * 16;
	spr.anim_py = u->y * 16;
	for (int i = 0; i < len; i++) {
		int tx = (path[i] & 0xFF) * 16, ty = (path[i] >> 8) * 16;
		bool vis_next = !(u->side == SIDE_HORDE && gs.fog[path[i] >> 8][path[i] & 0xFF]);
		if (visible || vis_next) {
			// holding B fast-forwards the horde's turn
			int step = (u->side == SIDE_HORDE && (keys_held & KEY_B)) ? 16 : profile.anim_speed ? 2 : 4;
			while (spr.anim_px != tx || spr.anim_py != ty) {
				if (spr.anim_px < tx) spr.anim_px += step;
				if (spr.anim_px > tx) spr.anim_px -= step;
				if (spr.anim_py < ty) spr.anim_py += step;
				if (spr.anim_py > ty) spr.anim_py -= step;
				if (u->side == SIDE_HORDE)
					camera_follow(spr.anim_px / 16, spr.anim_py / 16, false);
				frame();
			}
		}
		spr.anim_px = tx;
		spr.anim_py = ty;
		visible = vis_next;
	}
	if (len) {
		u->x = path[len - 1] & 0xFF;
		u->y = path[len - 1] >> 8;
		u->capture = 20;
	}
	spr.anim_unit = -1;
}

bool in_range(const Unit *u, int x, int y, int fx, int fy)
{
	int d = ABS(x - fx) + ABS(y - fy);
	return d >= unit_info[u->type].minrange && d <= unit_info[u->type].maxrange;
}

static void popup(int x, int y, int value)
{
	spr.popup_x = x * 16;
	spr.popup_y = y * 16;
	spr.popup_value = MIN(value, 99);
	spr.popup_timer = 40;
}

void kill_unit(int i, int killer)
{
	Unit *u = &gs.units[i];
	int x = u->x, y = u->y, type = u->type, side = u->side;
	u->alive = 0;
	if (u->cargo) {
		gs.units[u->cargo - 1].alive = 0;
		if (side == SIDE_SURVIVOR)
			gs.losses++;
	}
	if (side == SIDE_SURVIVOR)
		gs.losses++;
	else
		gs.kills++;
	// infected: survivors killed by zombies rise again
	if (side == SIDE_SURVIVOR && (unit_info[type].flags & UF_INFANTRY) && killer >= 0 &&
	    gs.units[killer].side == SIDE_HORDE) {
		int z = spawn_unit(UT_WALKER, SIDE_HORDE, x, y, 80);
		if (z >= 0)
			gs.units[z].moved = 1;
		turn_risen++;
	}
	// destroyed vehicles leave wrecks on open ground
	if ((unit_info[type].flags & UF_VEHICLE) && (gs.terrain[y][x] == TT_PLAIN || gs.terrain[y][x] == TT_ROAD)) {
		gs.terrain[y][x] = TT_WRECK;
		map_redraw_tile(x, y);
		for (int k = 0; k < 4; k++)
			if (x + dirx[k] >= 0 && y + diry[k] >= 0 && x + dirx[k] < gs.w && y + diry[k] < gs.h)
				map_redraw_tile(x + dirx[k], y + diry[k]);
	}
	// bloaters burst, hurting everything adjacent (never below 1HP)
	if (unit_info[type].flags & UF_EXPLODES) {
		for (int k = 0; k < 4; k++) {
			int o = unit_at(x + dirx[k], y + diry[k]);
			if (o >= 0)
				gs.units[o].hp = gs.units[o].hp > 20 ? gs.units[o].hp - 20 : 1;
		}
		wait_frames(8);
	}
}

static void hit(int a, int d, int dmg)
{
	Unit *du = &gs.units[d];
	spr.flash_unit = d;
	popup(du->x, du->y, (dmg + 9) / 10);
	wait_frames((keys_held & KEY_B) && gs.side == SIDE_HORDE ? 4 : profile.anim_speed ? 24 : 12);
	spr.flash_unit = -1;
	if (dmg >= du->hp) {
		du->hp = 0;
		kill_unit(d, a);
	} else {
		du->hp -= dmg;
	}
}

void attack(int a, int d)
{
	Unit *au = &gs.units[a], *du = &gs.units[d];
	int dmg = damage_estimate(au, du, true);
	hit(a, d, dmg);
	// counterattack: defender alive, direct, adjacent
	if (du->alive && ABS(au->x - du->x) + ABS(au->y - du->y) == 1 &&
	    !(unit_info[du->type].flags & UF_INDIRECT) && unit_info[du->type].maxrange >= 1 &&
	    base_damage[du->type][au->type]) {
		dmg = damage_estimate(du, au, true);
		hit(d, a, dmg);
	}
	au->moved = 1;
}

// ---------------------------------------------------------------- player turn

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
			if (gs.terrain[y][x] == tt)
				n++;
	return n;
}

void check_victory(void)
{
	const Mission *m = &missions[gs.mission];
	if (gs.over)
		return;
	bool has_workshop = false, safehouse_lost = false;
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++) {
			if (gs.terrain[y][x] == TT_WORKSHOP && gs.owner[y][x] == SIDE_SURVIVOR)
				has_workshop = true;
			if (gs.terrain[y][x] == TT_SAFEHOUSE && gs.owner[y][x] == SIDE_HORDE)
				safehouse_lost = true;
		}
	if (safehouse_lost || (count_side(SIDE_SURVIVOR) == 0 && (!has_workshop || gs.funds < 1000) &&
	                       !(m->goal == GOAL_EVAC && gs.evacuated >= m->evac_needed))) {
		gs.over = 2;
		return;
	}
	switch (m->goal) {
	case GOAL_ROUT:
		if (count_side(SIDE_HORDE) == 0 && count_terrain(TT_NEST) == 0)
			gs.over = 1;
		break;
	case GOAL_NESTS:
		if (count_terrain(TT_NEST) == 0)
			gs.over = 1;
		break;
	case GOAL_EVAC:
		if (gs.evacuated >= m->evac_needed)
			gs.over = 1;
		else if (m->goal_days && gs.day > m->goal_days)
			gs.over = 2;
		break;
	case GOAL_SURVIVE:
		if (gs.day > m->goal_days)
			gs.over = 1;
		break;
	}
}

void info_panel(void)
{
	int sx = gs.cur_x - gs.cam_x;
	int px = sx < 8 ? 20 : 0;
	text_clear_rect(0, 15, 30, 5);
	int tt = gs.terrain[gs.cur_y][gs.cur_x];
	text_window(px, 15, 10, 5);
	if (gs.fog[gs.cur_y][gs.cur_x] && gs.night)
		text_put(px + 1, 16, "Fog", BGPAL_TEXT_GREY);
	else
		text_put(px + 1, 16, terrain_info[tt].name, BGPAL_UI);
	text_printf(px + 1, 17, BGPAL_TEXT_YEL, "Def %d", terrain_info[tt].defense);
	int o = unit_at(gs.cur_x, gs.cur_y);
	if (o >= 0 && !(gs.units[o].side == SIDE_HORDE && gs.fog[gs.cur_y][gs.cur_x])) {
		Unit *u = &gs.units[o];
		text_printf(px + 1, 18, u->side == SIDE_SURVIVOR ? BGPAL_TEXT_BLUE : BGPAL_TEXT_RED, "%-8s", unit_info[u->type].name);
		text_printf(px + 6, 17, BGPAL_UI, "%2dHP", DISP_HP(u->hp));
	} else if (terrain_info[tt].building && gs.owner[gs.cur_y][gs.cur_x] != SIDE_NEUTRAL) {
		text_put(px + 1, 18, gs.owner[gs.cur_y][gs.cur_x] == SIDE_SURVIVOR ? "Ours" : "Overrun",
		         gs.owner[gs.cur_y][gs.cur_x] == SIDE_SURVIVOR ? BGPAL_TEXT_BLUE : BGPAL_TEXT_RED);
	} else if (tt == TT_HOUSE && !gs.looted[gs.cur_y][gs.cur_x]) {
		text_put(px + 1, 18, "Unsearched", BGPAL_TEXT_GREY);
	}
	// day / funds in the opposite top corner
	int fx = sx < 8 ? 20 : 0;
	text_clear_rect(0, 0, 30, 3);
	text_window(fx, 0, 10, 3);
	text_printf(fx + 1, 1, gs.night ? BGPAL_TEXT_BLUE : BGPAL_TEXT_YEL, "D%-2d $%ld", gs.day, (long)gs.funds);
}

static void show_ranges(int ui)
{
	Unit *u = &gs.units[ui];
	static u8 d2[MAP_MAX_W * MAP_MAX_H] EWRAM_BSS, p2[MAP_MAX_W * MAP_MAX_H] EWRAM_BSS;
	find_paths(ui, d2, p2);
	overlay_clear();
	bool indirect = unit_info[u->type].flags & UF_INDIRECT;
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++) {
			if (d2[IDX(x, y)] != NO_PATH)
				overlay_set(x, y, OV_MOVE);
		}
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++) {
			if (d2[IDX(x, y)] != NO_PATH)
				continue;
			bool hits = false;
			for (int sy = 0; sy < gs.h && !hits; sy++)
				for (int sx = 0; sx < gs.w && !hits; sx++) {
					if (indirect ? (sx != u->x || sy != u->y) : d2[IDX(sx, sy)] == NO_PATH)
						continue;
					if (in_range(u, sx, sy, x, y))
						hits = true;
				}
			if (hits && unit_info[u->type].maxrange)
				overlay_set(x, y, OV_ATTACK);
		}
	text_clear();
	while (!(keys_down & (KEY_A | KEY_B)))
		frame();
	overlay_clear();
}

static int list_targets(int ui, int fx, int fy, int *targets)
{
	Unit *u = &gs.units[ui];
	int n = 0;
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *t = &gs.units[i];
		if (!t->alive || t->carried || t->side == u->side || gs.fog[t->y][t->x] ||
		    !base_damage[u->type][t->type])
			continue;
		if (in_range(u, fx, fy, t->x, t->y))
			targets[n++] = i;
	}
	return n;
}

// Choose a target with the cursor. Returns unit index or -1.
static int pick_target(int ui, const int *targets, int n)
{
	int sel = 0;
	spr.cursor_show = 1;
	for (;;) {
		Unit *t = &gs.units[targets[sel]];
		gs.cur_x = t->x;
		gs.cur_y = t->y;
		camera_follow(t->x, t->y, true);
		forecast_draw(ui, targets[sel]);
		frame();
		if (keys_rep & (KEY_RIGHT | KEY_DOWN | KEY_R))
			sel = (sel + 1) % n;
		if (keys_rep & (KEY_LEFT | KEY_UP | KEY_L))
			sel = (sel + n - 1) % n;
		if (keys_down & KEY_A) {
			text_clear();
			return targets[sel];
		}
		if (keys_down & KEY_B) {
			text_clear();
			return -1;
		}
	}
}

static void search_house(int ui)
{
	Unit *u = &gs.units[ui];
	gs.looted[u->y][u->x] = 1;
	map_redraw_tile(u->x, u->y);
	u32 r = rng_next() % 100;
	if (r < 55) {
		int amount = 1000 + (rng_next() % 3) * 500;
		gs.funds += amount;
		text_clear();
		banner("Supplies found!", amount >= 2000 ? "+$2000" : amount >= 1500 ? "+$1500" : "+$1000", 60);
	} else if (r < 80) {
		for (int k = 0; k < 4; k++) {
			int x = u->x + dirx[k], y = u->y + diry[k];
			if (x < 0 || y < 0 || x >= gs.w || y >= gs.h || unit_at(x, y) >= 0 ||
			    move_cost(MOVE_FOOT, gs.terrain[y][x]) == IMPASSABLE)
				continue;
			int n = spawn_unit(UT_SURVIVOR, SIDE_SURVIVOR, x, y, 40 + (rng_next() % 5) * 10);
			if (n >= 0)
				gs.units[n].moved = 1;
			banner("A survivor joins!", "They were hiding inside.", 60);
			return;
		}
		gs.funds += 500;
		banner("Scraps found", "+$500", 60);
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
				banner("It's a trap!", "Something was waiting inside...", 60);
				attack(z, ui);
				return;
			}
		}
		banner("Empty", "Nothing left here.", 45);
	}
}

static void do_capture(int ui)
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
			banner("Nest burned!", NULL, 50);
		} else {
			gs.owner[u->y][u->x] = SIDE_SURVIVOR;
			map_redraw_tile(u->x, u->y);
			fog_update();
			banner("Secured!", NULL, 40);
		}
	} else {
		u->capture = need;
	}
}

static bool drop_cargo(int ui)
{
	Unit *bus = &gs.units[ui];
	int c = bus->cargo - 1;
	int opts[4], n = 0;
	for (int k = 0; k < 4; k++) {
		int x = bus->x + dirx[k], y = bus->y + diry[k];
		if (x < 0 || y < 0 || x >= gs.w || y >= gs.h || unit_at(x, y) >= 0 ||
		    move_cost(unit_info[gs.units[c].type].movetype, gs.terrain[y][x]) == IMPASSABLE)
			continue;
		opts[n++] = k;
	}
	if (!n)
		return false;
	int sel = 0;
	for (;;) {
		overlay_clear();
		for (int i = 0; i < n; i++)
			overlay_set(bus->x + dirx[opts[i]], bus->y + diry[opts[i]], OV_MOVE);
		gs.cur_x = bus->x + dirx[opts[sel]];
		gs.cur_y = bus->y + diry[opts[sel]];
		frame();
		if (keys_rep & (KEY_RIGHT | KEY_DOWN))
			sel = (sel + 1) % n;
		if (keys_rep & (KEY_LEFT | KEY_UP))
			sel = (sel + n - 1) % n;
		if (keys_down & KEY_B) {
			overlay_clear();
			return false;
		}
		if (keys_down & KEY_A)
			break;
	}
	overlay_clear();
	Unit *cu = &gs.units[c];
	cu->carried = 0;
	cu->x = gs.cur_x;
	cu->y = gs.cur_y;
	cu->moved = 1;
	cu->capture = 20;
	bus->cargo = 0;
	return true;
}

enum { ACT_FIRE, ACT_CAPTURE, ACT_SEARCH, ACT_LOAD, ACT_DROP, ACT_EVAC, ACT_WAIT, ACT_COUNT };
static const char *const act_names[ACT_COUNT] = { "Fire", "Secure", "Search", "Board", "Drop", "Evac", "Wait" };

// After moving: pick an action. Returns false if the player backed out (undo move).
static bool action_menu(int ui, bool moved_this_turn)
{
	Unit *u = &gs.units[ui];
	int targets[MAX_UNITS];
	for (;;) {
		const char *items[ACT_COUNT];
		int ids[ACT_COUNT], n = 0;
		int other = -1;
		for (int i = 0; i < MAX_UNITS; i++)
			if (i != ui && gs.units[i].alive && !gs.units[i].carried && gs.units[i].x == u->x && gs.units[i].y == u->y)
				other = i;
		const UnitInfo *inf = &unit_info[u->type];
		int tt = gs.terrain[u->y][u->x];
		int nt = (other < 0 && (!(inf->flags & UF_INDIRECT) || !moved_this_turn)) ? list_targets(ui, u->x, u->y, targets) : 0;
		if (other >= 0) {
			ids[n++] = ACT_LOAD;
		} else {
			if (nt)
				ids[n++] = ACT_FIRE;
			if ((inf->flags & UF_INFANTRY) &&
			    ((terrain_info[tt].building && gs.owner[u->y][u->x] != SIDE_SURVIVOR) || tt == TT_NEST))
				ids[n++] = ACT_CAPTURE;
			if ((inf->flags & UF_INFANTRY) && tt == TT_HOUSE && !gs.looted[u->y][u->x])
				ids[n++] = ACT_SEARCH;
			if (u->cargo)
				ids[n++] = ACT_DROP;
			if (tt == TT_HELIPAD && missions[gs.mission].goal == GOAL_EVAC)
				ids[n++] = ACT_EVAC;
			ids[n++] = ACT_WAIT;
		}
		for (int i = 0; i < n; i++)
			items[i] = (ids[i] == ACT_CAPTURE && tt == TT_NEST) ? "Burn" : act_names[ids[i]];
		int sx = u->x - gs.cam_x;
		int choice = text_menu(sx < 8 ? 21 : 1, 3, 8, items, NULL, n, 0);
		if (choice < 0)
			return false;
		switch (ids[choice]) {
		case ACT_FIRE: {
			int t = pick_target(ui, targets, nt);
			if (t < 0)
				continue;
			spr.cursor_show = 0;
			attack(ui, t);
			break;
		}
		case ACT_CAPTURE:
			do_capture(ui);
			break;
		case ACT_SEARCH:
			search_house(ui);
			break;
		case ACT_LOAD:
			gs.units[other].cargo = ui + 1;
			u->carried = 1;
			break;
		case ACT_DROP:
			if (!drop_cargo(ui))
				continue;
			break;
		case ACT_EVAC:
			gs.evacuated += 1 + (u->cargo ? 1 : 0);
			if (u->cargo)
				gs.units[u->cargo - 1].alive = 0;
			u->alive = 0;
			banner("Evacuated!", NULL, 40);
			break;
		}
		u->moved = 1;
		return true;
	}
}

static void build_menu(int x, int y)
{
	static const u8 buildable[] = { UT_SURVIVOR, UT_HEAVY, UT_MEDIC, UT_SNIPER, UT_PICKUP, UT_BUS, UT_ARMOR };
	char lines[7][20];
	const char *items[7];
	u8 en[7];
	for (int i = 0; i < 7; i++) {
		const UnitInfo *inf = &unit_info[buildable[i]];
		int len = strlen(inf->name);
		memset(lines[i], ' ', 19);
		memcpy(lines[i], inf->name, len);
		lines[i][19] = 0;
		int c = inf->cost, p = 13;
		lines[i][p++] = '$';
		char num[8];
		int nn = 0;
		do { num[nn++] = '0' + c % 10; c /= 10; } while (c);
		while (nn)
			lines[i][p++] = num[--nn];
		lines[i][p] = 0;
		items[i] = lines[i];
		en[i] = gs.funds >= inf->cost;
	}
	text_clear();
	text_window(1, 12, 28, 3);
	text_printf(2, 13, BGPAL_TEXT_YEL, "Supplies $%ld", (long)gs.funds);
	int c = text_menu(4, 1, 22, items, en, 7, 0);
	text_clear();
	if (c < 0)
		return;
	int i = spawn_unit(buildable[c], SIDE_SURVIVOR, x, y, 100);
	if (i >= 0) {
		gs.units[i].moved = 1;
		gs.funds -= unit_info[buildable[c]].cost;
		fog_update();
	}
}

static void objective_screen(void)
{
	const Mission *m = &missions[gs.mission];
	text_clear();
	text_window(1, 2, 28, 12);
	text_put(3, 3, m->title, BGPAL_TEXT_YEL);
	text_put(3, 5, m->objective, BGPAL_UI);
	text_printf(3, 8, BGPAL_UI, "Day %d %s", gs.day, gs.night ? "(Night)" : "(Day)");
	text_printf(3, 9, BGPAL_UI, "Survivors %d  Zombies %d", count_side(SIDE_SURVIVOR), count_side(SIDE_HORDE));
	text_printf(3, 10, BGPAL_UI, "Nests left %d", count_terrain(TT_NEST));
	if (m->goal == GOAL_EVAC)
		text_printf(3, 11, BGPAL_TEXT_BLUE, "Evacuated %d/%d", gs.evacuated, m->evac_needed);
	text_put(3, 12, "Night: zombies +1 move", BGPAL_TEXT_GREY);
	while (!(keys_down & (KEY_A | KEY_B)))
		frame();
	text_clear();
}

static void options_menu(void)
{
	static const char *const items[2] = { "Speed: Fast", "Speed: Normal" };
	int c = text_menu(8, 5, 15, items, NULL, 2, profile.anim_speed);
	if (c >= 0) {
		profile.anim_speed = c;
		profile_save();
	}
}

void start_side_turn(int side)
{
	gs.side = side;
	for (int i = 0; i < MAX_UNITS; i++)
		if (gs.units[i].side == side)
			gs.units[i].moved = 0;
	if (side != SIDE_SURVIVOR)
		return;
	const Mission *m = &missions[gs.mission];
	bool was_night = gs.night;
	gs.night = m->night_every == 1 || (m->night_every > 1 && gs.day % m->night_every == 0);
	if (was_night != gs.night)
		palettes_apply();
	fog_update();
	// income
	u32 income = 0;
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++)
			if (gs.owner[y][x] == SIDE_SURVIVOR)
				income += terrain_info[gs.terrain[y][x]].income;
	gs.funds += income * m->income_scale / 100;
	// healing on owned buildings, medics heal neighbours
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		if (!u->alive || u->carried || u->side != SIDE_SURVIVOR)
			continue;
		int tt = gs.terrain[u->y][u->x];
		bool inf = unit_info[u->type].flags & UF_INFANTRY;
		if (gs.owner[u->y][u->x] == SIDE_SURVIVOR &&
		    ((inf && tt != TT_WORKSHOP) || (!inf && tt == TT_WORKSHOP)))
			u->hp = MIN(100, u->hp + (tt == TT_HOSPITAL ? 30 : 20));
		if (unit_info[u->type].flags & UF_HEALER)
			for (int k = 0; k < 4; k++) {
				int o = unit_at(u->x + dirx[k], u->y + diry[k]);
				if (o >= 0 && gs.units[o].side == SIDE_SURVIVOR)
					gs.units[o].hp = MIN(100, gs.units[o].hp + 10);
			}
	}
}

// Main player-turn loop. Returns when the player ends the turn or the game ends.
// Returns false if the player suspended / quit.
static bool player_turn(void)
{
	int next_unit = 0;
	spr.cursor_show = 1;
	info_panel();
	for (;;) {
		frame();
		check_victory();
		if (gs.over)
			return true;
		if (keys_rep & KEY_DIR) {
			int nx = gs.cur_x + ((keys_rep & KEY_RIGHT) ? 1 : 0) - ((keys_rep & KEY_LEFT) ? 1 : 0);
			int ny = gs.cur_y + ((keys_rep & KEY_DOWN) ? 1 : 0) - ((keys_rep & KEY_UP) ? 1 : 0);
			gs.cur_x = MAX(0, MIN(nx, gs.w - 1));
			gs.cur_y = MAX(0, MIN(ny, gs.h - 1));
			camera_follow(gs.cur_x, gs.cur_y, true);
			info_panel();
		}
		if (keys_down & (KEY_SELECT | KEY_L)) {
			for (int k = 0; k < MAX_UNITS; k++) {
				int i = (next_unit + k) % MAX_UNITS;
				Unit *u = &gs.units[i];
				if (u->alive && !u->carried && u->side == SIDE_SURVIVOR && !u->moved) {
					gs.cur_x = u->x;
					gs.cur_y = u->y;
					next_unit = i + 1;
					camera_follow(u->x, u->y, true);
					info_panel();
					break;
				}
			}
		}
		if (keys_down & KEY_START)
			goto menu;
		if (keys_down & KEY_R) {
			info_card();
			info_panel();
			continue;
		}
		if (!(keys_down & KEY_A))
			continue;
		int ui = unit_at(gs.cur_x, gs.cur_y);
		if (ui >= 0 && gs.units[ui].side == SIDE_HORDE && !gs.fog[gs.cur_y][gs.cur_x]) {
			show_ranges(ui);
			info_panel();
			continue;
		}
		if (ui >= 0 && gs.units[ui].side == SIDE_SURVIVOR && !gs.units[ui].moved) {
			Unit *u = &gs.units[ui];
			int ox = u->x, oy = u->y;
			find_paths(ui, dist, prev);
			text_clear();
			for (int y = 0; y < gs.h; y++)
				for (int x = 0; x < gs.w; x++)
					if (dist[IDX(x, y)] != NO_PATH)
						overlay_set(x, y, OV_MOVE);
			for (;;) {
				frame();
				if (keys_rep & KEY_DIR) {
					int nx = gs.cur_x + ((keys_rep & KEY_RIGHT) ? 1 : 0) - ((keys_rep & KEY_LEFT) ? 1 : 0);
					int ny = gs.cur_y + ((keys_rep & KEY_DOWN) ? 1 : 0) - ((keys_rep & KEY_UP) ? 1 : 0);
					gs.cur_x = MAX(0, MIN(nx, gs.w - 1));
					gs.cur_y = MAX(0, MIN(ny, gs.h - 1));
					path_arrow_update(dist, prev, ox, oy);
					camera_follow(gs.cur_x, gs.cur_y, true);
				}
				if (keys_down & KEY_B) {
					path_arrow_clear();
					overlay_clear();
					gs.cur_x = ox;
					gs.cur_y = oy;
					camera_follow(ox, oy, true);
					break;
				}
				if (!(keys_down & KEY_A))
					continue;
				int tx = gs.cur_x, ty = gs.cur_y;
				int o = unit_at(tx, ty);
				if (dist[IDX(tx, ty)] == NO_PATH ||
				    (o >= 0 && gs.units[o].side == SIDE_SURVIVOR && !can_end_on(ui, tx, ty)))
					continue;
				path_arrow_clear();
				overlay_clear();
				u16 path[64];
				int len = build_path(prev, ox, oy, tx, ty, path);
				// ambush: stop before a zombie hidden in the fog
				bool ambushed = false;
				for (int i = 0; i < len; i++) {
					int hz = unit_at(path[i] & 0xFF, path[i] >> 8);
					if (hz >= 0 && gs.units[hz].side == SIDE_HORDE) {
						len = i;
						ambushed = true;
						break;
					}
				}
				spr.cursor_show = 0;
				unit_move_anim(ui, path, len);
				fog_update();
				if (ambushed) {
					banner("Ambush!", "Zombies in the dark!", 60);
					u->moved = 1;
					spr.cursor_show = 1;
					break;
				}
				if (!action_menu(ui, len > 0)) {
					// undo the move
					u->x = ox;
					u->y = oy;
					fog_update();
					spr.cursor_show = 1;
					for (int y = 0; y < gs.h; y++)
						for (int x = 0; x < gs.w; x++)
							if (dist[IDX(x, y)] != NO_PATH)
								overlay_set(x, y, OV_MOVE);
					path_arrow_update(dist, prev, ox, oy);
					continue;
				}
				fog_update();
				spr.cursor_show = 1;
				if (u->alive) {
					gs.cur_x = u->x;
					gs.cur_y = u->y;
				}
				break;
			}
			info_panel();
			continue;
		}
		if (ui < 0 && gs.terrain[gs.cur_y][gs.cur_x] == TT_WORKSHOP &&
		    gs.owner[gs.cur_y][gs.cur_x] == SIDE_SURVIVOR) {
			build_menu(gs.cur_x, gs.cur_y);
			info_panel();
			continue;
		}
	menu:;
		static const char *const items[5] = { "End Day", "Objective", "Options", "Suspend", "Cancel" };
		text_clear();
		int c = text_menu(9, 4, 12, items, NULL, 5, 0);
		if (c == 0) {
			text_clear();
			spr.cursor_show = 0;
			return true;
		} else if (c == 1) {
			objective_screen();
		} else if (c == 2) {
			options_menu();
		} else if (c == 3) {
			suspend_save();
			text_clear();
			banner("Game suspended.", "Continue from the title.", 90);
			return false;
		}
		info_panel();
	}
}

static void results(void)
{
	const Mission *m = &missions[gs.mission];
	spr.cursor_show = 0;
	text_clear();
	if (gs.over == 1) {
		banner("MISSION COMPLETE", m->title, 120);
		dialogue(m->outro);
		if (profile.unlocked < gs.mission + 2 && gs.mission + 1 < mission_count)
			profile.unlocked = gs.mission + 2;
		if (!profile.best_days[gs.mission] || gs.day < profile.best_days[gs.mission])
			profile.best_days[gs.mission] = gs.day;
		profile_save();
	} else {
		banner("MISSION FAILED", "The dead walk on...", 150);
	}
	text_clear();
	text_window(5, 4, 20, 9);
	text_put(7, 5, gs.over == 1 ? "Victory" : "Defeat", gs.over == 1 ? BGPAL_TEXT_YEL : BGPAL_TEXT_RED);
	text_printf(7, 7, BGPAL_UI, "Days     %d", gs.day);
	text_printf(7, 8, BGPAL_UI, "Zombies  %d", gs.kills);
	text_printf(7, 9, BGPAL_UI, "Lost     %d", gs.losses);
	text_put(7, 11, "Press A", BGPAL_TEXT_GREY);
	while (!(keys_down & KEY_A))
		frame();
	text_clear();
}

void battle_run(int mission)
{
	bool resumed = mission < 0;
	if (resumed) {
		if (!suspend_load())
			return;
	} else {
		map_load(mission);
#ifdef PERF
		perf_stress();
#endif
	}
	REG_DISPCNT = DCNT_BLANK;
	video_load_game_gfx();
	text_clear();
	map_draw();
	overlay_clear();
	spr.map_visible = 1;
	spr.cursor_show = 0;
	camera_follow(gs.cur_x, gs.cur_y, false);
	if (!resumed) {
		dialogue(missions[mission].intro);
		start_side_turn(SIDE_SURVIVOR);
		banner(missions[gs.mission].title, missions[gs.mission].objective, 120);
	} else {
		fog_update();
		map_draw();
		suspend_clear();
	}
	for (;;) {
		if (!player_turn())
			goto out;
		check_victory();
		if (gs.over)
			break;
		text_clear();
		summary_mark_horde();
		horde_turn();
		check_victory();
		if (gs.over)
			break;
		gs.day++;
		summary_mark_upkeep();
		start_side_turn(SIDE_SURVIVOR);
		check_victory();
		if (gs.over)
			break;
		char l1[24];
		const Mission *m = &missions[gs.mission];
		int left = m->goal_days ? m->goal_days - gs.day + 1 : 0;
		l1[0] = 'D'; l1[1] = 'a'; l1[2] = 'y'; l1[3] = ' ';
		int p = 4, d = gs.day;
		if (d >= 10) l1[p++] = '0' + d / 10;
		l1[p++] = '0' + d % 10;
		l1[p] = 0;
		if (gs.night)
			memcpy(l1 + p, " - Night", 9);
		static char l2[28];
		if (left > 0 && (m->goal == GOAL_SURVIVE || m->goal == GOAL_EVAC)) {
			memcpy(l2, "Days left: ", 11);
			int q = 11;
			if (left >= 10) l2[q++] = '0' + left / 10;
			l2[q++] = '0' + left % 10;
			l2[q] = 0;
		} else {
			memcpy(l2, gs.night ? "They're hunting." : "Survivors' turn", 17);
		}
		camera_follow(gs.cur_x, gs.cur_y, false);
		summary_show(l1, l2, 120);
	}
	results();
out:
	spr.map_visible = 0;
	spr.cursor_show = 0;
}
