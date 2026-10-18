// Dead Wars: The Last Boat - tactical mission flow: survivor phase UI, actions, feedback hooks, results
#include "lb.h"
#include "video.h"
#include "sound.h"
#include <stdio.h>
#include <string.h>

#define IDX(x, y) ((y) * MAP_MAX_W + (x))
#define NO_PATH 255

bool tac_mode;

static u8 dist[MAP_MAX_W * MAP_MAX_H] EWRAM_BSS, prev[MAP_MAX_W * MAP_MAX_H] EWRAM_BSS;
static u8 fog0[MAP_MAX_H][MAP_MAX_W] EWRAM_BSS;
static bool fx_ready;   // the map is on screen: the fx hooks may draw and wait
static s8 music_level;
static u8 next_unit;
// the deployed people: roster index, unit index (never reused in a mission), last state shown
static u8 dep_n, dep_person[ROSTER_MAX], dep_unit[ROSTER_MAX], dep_state[ROSTER_MAX];
#define PS_DEAD 1
#define PS_DOWN 2
#define PS_BITTEN 4

static const char *const res_names[RES_COUNT] = { "Food", "Fuel", "Ammo", "Meds", "Parts" };
static const char *const bk_names[BK_COUNT] = { "House", "Shop", "Garage", "Pharmacy" };
static const char *const dir_names[4] = { "North", "East", "South", "West" };

// ---------------------------------------------------------------- helpers

static bool on_map(int i)
{
	const Unit *u = &gs.units[i];
	return u->alive && !u->carried && u->side == SIDE_SURVIVOR && !tac.actor[i].extracted;
}

static bool shown(int i)
{
	const Unit *u = &gs.units[i];
	return u->side != SIDE_HORDE || (!gs.fog[u->y][u->x] && !u->inside);
}

static bool ready(int i)
{
	const Actor *a = &tac.actor[i];
	return on_map(i) && !a->down && (!a->moved || !a->acted);
}

static Person *person_of(int i)
{
	int p = tac.actor[i].person;
	return p < ROSTER_MAX ? &run.roster[p] : NULL;
}

static const char *name_of(int i)
{
	const Person *p = person_of(i);
	return p ? person_name(p) : "Someone";
}

// done people (moved and acted, or downed) are drawn grey by build_oam
static void refresh_grey(void)
{
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		if (u->alive && u->side == SIDE_SURVIVOR) {
			const Actor *a = &tac.actor[i];
			u->moved = a->down || (a->moved && a->acted);
		}
	}
}

static bool fast_fwd(void)
{
	return gs.side == SIDE_HORDE && (keys_held & KEY_B);
}

static int wrap(int x, int y, int w, int rows, const char *s, int pal)
{
	char line[32];
	int n = 0;
	w = MIN(w, 30);
	while (s && *s && n < rows) {
		int len = strlen(s), cut = len;
		if (len > w)
			for (cut = w; cut > 0 && s[cut] != ' '; cut--)
				;
		if (cut <= 0)
			cut = MIN(len, w);
		memcpy(line, s, cut);
		line[cut] = 0;
		text_put(x, y + n++, line, pal);
		s += cut;
		while (*s == ' ')
			s++;
	}
	return n;
}

static void wait_close(void)
{
	do
		frame();
	while (!(keys_down & (KEY_A | KEY_B | KEY_R | KEY_START)));
	text_clear();
}

static bool cursor_keys(void)
{
	if (!(keys_rep & KEY_DIR))
		return false;
	int nx = gs.cur_x + ((keys_rep & KEY_RIGHT) ? 1 : 0) - ((keys_rep & KEY_LEFT) ? 1 : 0);
	int ny = gs.cur_y + ((keys_rep & KEY_DOWN) ? 1 : 0) - ((keys_rep & KEY_UP) ? 1 : 0);
	gs.cur_x = MAX(0, MIN(nx, gs.w - 1));
	gs.cur_y = MAX(0, MIN(ny, gs.h - 1));
	sfx(SFX_CURSOR);
	camera_follow(gs.cur_x, gs.cur_y, true);
	return true;
}

static bool jump_next(void)
{
	for (int k = 0; k < MAX_UNITS; k++) {
		int i = next_unit + k;
		if (i >= MAX_UNITS)
			i -= MAX_UNITS;
		if (gs.units[i].side == SIDE_SURVIVOR && ready(i)) {
			gs.cur_x = gs.units[i].x;
			gs.cur_y = gs.units[i].y;
			next_unit = i + 1 < MAX_UNITS ? i + 1 : 0;
			camera_follow(gs.cur_x, gs.cur_y, true);
			return true;
		}
	}
	return false;
}

// Horde music: waves so far, +1 past half of the next threshold.
void tac_music_update(void)
{
	const TacMission *m = tac.m;
	int lvl = MIN(tac.wave, 3);
	if (m && tac.wave < 3 && m->waves[tac.wave] && tac.horde * 2 >= m->waves[tac.wave])
		lvl++;
	lvl = MIN(lvl, 3);
	if (lvl != music_level) {
		music_level = lvl;
		sound_horde_level(lvl);
	}
}

static int person_state(int k)
{
	const Person *p = &run.roster[dep_person[k]];
	const Actor *a = &tac.actor[dep_unit[k]];
	return (!p->alive ? PS_DEAD : 0) | (a->down ? PS_DOWN : 0) | ((p->flags & PF_INFECTED) ? PS_BITTEN : 0);
}

// The rules don't announce what happens to our people: down, bitten, dead. Show each once; one
// bite can down and infect together (both are told), a death says it all.
void tac_people_notice(void)
{
	char l1[24];
	for (int k = 0; k < dep_n; k++) {
		int s = person_state(k), was = dep_state[k], i = dep_unit[k], fresh = s & ~was;
		const char *name = person_name(&run.roster[dep_person[k]]);
		dep_state[k] = s;
		if (fresh & PS_DEAD) {
			int o = unit_at(gs.units[i].x, gs.units[i].y);
			bool rises = o >= 0 && gs.units[o].side == SIDE_HORDE;
			sfx(rises ? SFX_INFECT : SFX_DESTROY);
			snprintf(l1, sizeof l1, "%.9s is dead", name);
			tac_fx_text(l1, rises ? "And rises again." : "Bled out.");
			continue;
		}
		if (s & PS_DEAD)
			continue;
		if (fresh & PS_DOWN) {
			snprintf(l1, sizeof l1, "%.9s is down!", name);
			tac_fx_text(l1, "Help them within 2 turns.");
		}
		if (fresh & PS_BITTEN) {
			sfx(SFX_INFECT);
			snprintf(l1, sizeof l1, "%.9s was bitten", name);
			tac_fx_text(l1, "Infected.");
		}
	}
}

// ---------------------------------------------------------------- feedback hooks (tac_rules.c)

void tac_fx_hit(int d, int dmg)
{
	if (!fx_ready || d < 0 || d >= MAX_UNITS)
		return;
	const Unit *u = &gs.units[d];
	sfx(u->side == SIDE_SURVIVOR ? SFX_BITE : SFX_SHOT);
	spr.popup_x = u->x * 16;
	spr.popup_y = u->y * 16;
	spr.popup_value = MAX(0, MIN(dmg, 99));
	spr.popup_timer = 40;
	spr.flash_unit = d;
	wait_frames(fast_fwd() ? 4 : profile.anim_speed ? 24 : 12);
	spr.flash_unit = -1;
}

// "Miss" just above (or below) the target for half a second, never on a window; the text
// cells underneath come back after.
void tac_fx_miss(int d)
{
	if (!fx_ready || d < 0 || d >= MAX_UNITS)
		return;
	const Unit *u = &gs.units[d];
	int tx = ((u->x * 16 - cam_px) >> 3) - 1, ty0 = (u->y * 16 - cam_py) >> 3, ty = -1;
	tx = MAX(0, MIN(tx, 26));
	for (int k = 0; k < 2 && ty < 0; k++) {
		int y = k ? ty0 + 2 : ty0 - 1;
		bool free = y >= 0 && y < 20;
		for (int i = 0; i < 4 && free; i++)
			free = !SCREENBLOCK(SB_WINDOW)[y * 32 + tx + i];
		if (free)
			ty = y;
	}
	sfx(SFX_EMPTY);
	if (ty < 0) {
		wait_frames(fast_fwd() ? 8 : 30);
		return;
	}
	volatile u16 *row = SCREENBLOCK(SB_TEXT) + ty * 32 + tx;
	u16 keep[4];
	for (int i = 0; i < 4; i++)
		keep[i] = row[i];
	text_put(tx, ty, "Miss", BGPAL_TEXT_YEL);
	wait_frames(fast_fwd() ? 8 : 30);
	for (int i = 0; i < 4; i++)
		row[i] = keep[i];
}

void tac_fx_text(const char *l1, const char *l2)
{
	if (!fx_ready || !l1)
		return;
	banner(l1, l2, fast_fwd() ? 30 : 75);
}

void tac_fx_noise(int x, int y, int radius)
{
	if (fx_ready)
		tac_gfx_pulse(x, y);
}

// ---------------------------------------------------------------- info cards (R)

static void person_card(int i)
{
	const Unit *u = &gs.units[i];
	const Actor *a = &tac.actor[i];
	const Person *p = person_of(i);
	text_clear();
	text_window(0, 0, 30, 20);
	if (!p) {
		text_put(2, 1, "Unknown", BGPAL_TEXT_GREY);
		wait_close();
		return;
	}
	text_put(2, 1, person_name(p), BGPAL_TEXT_YEL);
	text_printf(20, 1, BGPAL_UI, "HP %2d/%d", u->hp, a->maxhp);
	text_put(2, 2, p->cast < CAST_COUNT ? cast_info[p->cast].bg : "Survivor", BGPAL_TEXT_GREY);
	if (p->weapon < WP_COUNT) {
		const WeaponInfo *w = &weapon_info[p->weapon];
		text_put(2, 3, w->name, BGPAL_UI);
		if (w->minr == w->maxr)
			text_printf(12, 3, BGPAL_UI, "Dmg %d Rng %d", w->dmg, w->maxr);
		else
			text_printf(12, 3, BGPAL_UI, "Dmg %d Rng %d-%d", w->dmg, w->minr, w->maxr);
	}
	text_put(2, 4, p->armor < AR_COUNT ? armor_info[p->armor].name : "-", BGPAL_UI);
	text_printf(12, 4, BGPAL_TEXT_GREY, "Move %d", person_move(p));
	text_printf(2, 6, BGPAL_UI, "Grit  %d", p->grit);
	text_printf(12, 6, BGPAL_UI, "Aim   %d", p->aim);
	text_printf(2, 7, BGPAL_UI, "Speed %d", p->speed);
	text_printf(12, 7, BGPAL_UI, "Nerve %d", p->nerve);
	text_printf(20, 6, BGPAL_TEXT_GREY, "Kills %d", p->kills);
	if (p->wounds)
		text_printf(20, 7, BGPAL_TEXT_RED, "Wounds %d", p->wounds);
	text_put(2, 9, "Skills", BGPAL_TEXT_GREY);
	for (int s = 0; s < SK_COUNT; s++)
		text_printf(s & 1 ? 16 : 2, 10 + (s >> 1), BGPAL_UI, "%-9s %d", skill_names[s], p->skill[s]);
	char buf[32];
	snprintf(buf, sizeof buf, "%s%s%s", p->trait[0] < TR_COUNT ? trait_names[p->trait[0]] : "",
	         p->trait[0] && p->trait[1] ? ", " : "", p->trait[1] < TR_COUNT ? trait_names[p->trait[1]] : "");
	text_put(2, 15, "Traits", BGPAL_TEXT_GREY);
	text_put(9, 15, buf, BGPAL_TEXT_YEL);
	int row = 16;
	if (a->down)
		text_printf(2, row++, BGPAL_TEXT_RED, "Downed: %d turn%s to live", a->down, a->down == 1 ? "" : "s");
	if (p->flags & PF_INFECTED)
		text_printf(2, row++, BGPAL_TEXT_RED, "Infected: turns in %d day%s", p->turn_in, p->turn_in == 1 ? "" : "s");
	if (row < 18 && tac_hidden(i))
		text_put(2, row++, "Hidden from the dead", BGPAL_TEXT_BLUE);
	// carried loot, as much as fits on one line
	int len = snprintf(buf, sizeof buf, "Carrying");
	bool any = false;
	for (int r = 0; r < RES_COUNT && len < 22; r++)
		if (a->carry[r]) {
			len += snprintf(buf + len, sizeof buf - len, " %s %d", res_names[r], a->carry[r]);
			any = true;
		}
	if (row < 19 && any) {
		buf[27] = 0;
		text_put(2, row, buf, BGPAL_UI);
	}
	wait_close();
}

static void zombie_card(int i)
{
	const Unit *u = &gs.units[i];
	const Actor *a = &tac.actor[i];
	int t = u->type < UT_COUNT ? u->type : UT_WALKER;
	const ZombieInfo *z = &zombie_info[t];
	text_clear();
	text_window(3, 3, 24, 12);
	text_put(5, 4, unit_info[t].name, BGPAL_TEXT_RED);
	text_printf(17, 4, BGPAL_UI, "HP %d/%d", u->hp, a->maxhp);
	text_printf(5, 6, BGPAL_UI, "Move %d   Sight %d", z->move, z->sight);
	text_printf(5, 7, BGPAL_UI, "Hearing %d", z->hearing);
	text_printf(5, 8, BGPAL_UI, "Hit %d%%   Dmg %d", z->hit, z->dmg);
	if (z->infect)
		text_printf(5, 9, BGPAL_TEXT_RED, "Bites infect %d%%", z->infect);
	text_put(5, 11, u->dormant ? "Dormant" : a->alerted ? "Alerted" : "Wandering",
	         u->dormant ? BGPAL_TEXT_GREY : a->alerted ? BGPAL_TEXT_RED : BGPAL_TEXT_YEL);
	text_printf(5, 12, BGPAL_TEXT_GREY, "On %s", terrain_info[gs.terrain[u->y][u->x]].name);
	wait_close();
}

static void tile_card(int x, int y)
{
	int bi = tac.bmap[y][x] - 1;
	const Building *b = bi >= 0 && bi < tac.nbldg ? &tac.bldg[bi] : NULL;
	text_clear();
	text_window(2, 1, 26, 18);
	text_put(4, 2, b ? bk_names[b->kind < BK_COUNT ? b->kind : 0] : terrain_info[gs.terrain[y][x]].name, BGPAL_TEXT_YEL);
	if (gs.fog[y][x])
		text_put(15, 2, "(unseen)", BGPAL_TEXT_GREY);
	int row = 3;
	if (b) {
		int n = 0, t = MIN(b->w * b->h, 8);
		for (int i = 0; i < t; i++)
			n += (b->searched >> i) & 1;
		text_printf(4, row++, BGPAL_UI, "Searched %d of %d", n, t);
		if (b->door_hp)
			text_printf(4, row++, BGPAL_TEXT_YEL, "Door barricaded (%d)", b->door_hp);
		else
			text_printf(4, row++, BGPAL_UI, "Door: %s side", dir_names[b->door_dir & 3]);
	}
	u8 z = tac.zone[y][x];
	if (z & ZN_EXIT)
		text_put(4, row++, "Exit: leave here", BGPAL_TEXT_BLUE);
	if (z & ZN_DEPLOY)
		text_put(4, row++, "Caravan: retreat here", BGPAL_TEXT_BLUE);
	if (z & ZN_BRIDGE)
		text_put(4, row++, "Bridge: hold it", BGPAL_TEXT_BLUE);
	int c = tac.cache[y][x];
	if (c == CACHE_TRUNK)
		text_put(4, row++, "A car trunk to search", BGPAL_TEXT_YEL);
	else if (c >= CACHE_PACK && c < CACHE_PACK + TAC_MAX_PACKS && tac.pack[c - CACHE_PACK].used)
		text_put(4, row++, "A backpack to search", BGPAL_TEXT_YEL);
	row = MAX(row + 1, 9);
	int mp = tac_move_cost(x, y, false), mz = tac_move_cost(x, y, true);
	text_put(4, row, "Move cost", BGPAL_TEXT_GREY);
	if (mp == IMPASSABLE)
		text_put(14, row, "people -", BGPAL_UI);
	else
		text_printf(14, row, BGPAL_UI, "people %d", mp);
	row++;
	if (mz == IMPASSABLE)
		text_put(14, row, "dead   -", BGPAL_UI);
	else
		text_printf(14, row, BGPAL_UI, "dead   %d", mz);
	row += 2;
	text_put(4, row++, "Cover from fire", BGPAL_TEXT_GREY);
	for (int k = 0; k < 4; k++) {
		int cv = tac_edge_cover(x, y, k); // against shots from that side
		text_put(5, row, dir_names[k], BGPAL_UI);
		text_put(12, row++, cv >= 40 ? "full (-40)" : cv > 0 ? "half (-20)" : "none",
		         cv >= 40 ? BGPAL_TEXT_BLUE : cv > 0 ? BGPAL_TEXT_YEL : BGPAL_TEXT_GREY);
	}
	wait_close();
}

static void tac_card(int x, int y)
{
	int o = unit_at(x, y);
	if (o >= 0 && shown(o) && on_map(o))
		person_card(o);
	else if (o >= 0 && shown(o) && gs.units[o].side == SIDE_HORDE)
		zombie_card(o);
	else
		tile_card(x, y);
}

// ---------------------------------------------------------------- objective, results

static int loot_carried(void)
{
	int n = 0;
	for (int i = 0; i < MAX_UNITS; i++)
		if (on_map(i))
			for (int r = 0; r < RES_COUNT; r++)
				n += tac.actor[i].carry[r];
	return n;
}

static void objective_screen(void)
{
	const TacMission *m = tac.m;
	text_clear();
	text_window(1, 1, 28, 17);
	text_put(3, 2, m->title, BGPAL_TEXT_YEL);
	wrap(3, 4, 24, 3, m->brief, BGPAL_UI);
	if (m->objective == OBJ_BRIDGE)
		text_printf(3, 8, BGPAL_TEXT_BLUE, "Held %d of %d turns", tac.held, m->need);
	else if (m->objective == OBJ_GET_THROUGH)
		text_printf(3, 8, BGPAL_TEXT_BLUE, "Through %d of %d", tac.through, m->need);
	else {
		int out = 0;
		for (int r = 0; r < RES_COUNT; r++)
			out += tac.loot[r];
		text_printf(3, 8, BGPAL_TEXT_BLUE, "Loot out %d, carried %d", out, loot_carried());
	}
	text_printf(3, 10, gs.night ? BGPAL_TEXT_BLUE : BGPAL_UI, "Turn %d%s", tac.turn + 1, gs.night ? "  Night" : "");
	if (tac.wave < 3 && m->waves[tac.wave])
		text_printf(3, 11, BGPAL_TEXT_RED, "Horde %d (wave at %d)", tac.horde, m->waves[tac.wave]);
	else
		text_printf(3, 11, BGPAL_TEXT_RED, "Horde %d", tac.horde);
	text_printf(3, 12, BGPAL_UI, "Waves %d of 3", MIN(tac.wave, 3));
	text_printf(3, 13, BGPAL_UI, "People on the map %d", tac_survivors_left());
	text_put(3, 15, "Noise fills the meter.", BGPAL_TEXT_GREY);
	wait_close();
}

static void briefing(void)
{
	const TacMission *m = tac.m;
	text_clear();
	text_window(2, 4, 26, 11);
	text_put(4, 5, m->title, BGPAL_TEXT_YEL);
	wrap(4, 7, 22, 4, m->brief, BGPAL_UI);
	if (m->night)
		text_put(4, 12, "Night: you see little.", BGPAL_TEXT_BLUE);
	text_put(4, 13, "Press A", BGPAL_TEXT_GREY);
	for (int i = 0; i < 600; i++) {
		frame();
		if (keys_down & (KEY_A | KEY_B | KEY_START))
			break;
	}
	text_clear();
}

static void results(void)
{
	const TacMission *m = tac.m;
	bool won = tac.result == 1;
	spr.cursor_show = 0;
	text_clear();
	text_window(0, 0, 30, 20);
	text_put(2, 1, m->title, BGPAL_TEXT_YEL);
	text_put(2, 2, won ? "Success" : "Failed", won ? BGPAL_TEXT_BLUE : BGPAL_TEXT_RED);
	int row = 4;
	for (int k = 0; k < dep_n && row < 12; k++, row++) {
		const Person *ps = &run.roster[dep_person[k]];
		int out = tac.actor[dep_unit[k]].extracted; // 1 exit, 2 caravan (retreat, or picked up on success)
		text_put(2, row, person_name(ps), BGPAL_UI);
		if (!ps->alive)
			text_put(12, row, "\x7f Dead", BGPAL_TEXT_RED);
		else if (out == 1)
			text_put(12, row, "Got out", BGPAL_TEXT_BLUE);
		else if (out)
			text_put(12, row, won ? "Made it" : "Retreated", BGPAL_TEXT_BLUE);
		else
			text_put(12, row, won ? "Made it" : "Left behind", won ? BGPAL_TEXT_BLUE : BGPAL_TEXT_RED);
		if (ps->alive && (ps->flags & PF_INFECTED))
			text_put(23, row, "Bitten", BGPAL_TEXT_RED);
	}
	row++;
	text_put(2, row++, "Brought back", BGPAL_TEXT_GREY);
	for (int r = 0; r < RES_COUNT; r++)
		text_printf(r < 3 ? 3 + r * 9 : 3 + (r - 3) * 9, row + (r >= 3), tac.loot[r] ? BGPAL_UI : BGPAL_TEXT_GREY,
		            "%s %d", res_names[r], MIN(tac.loot[r], 999));
	text_put(2, 18, "Press A", BGPAL_TEXT_GREY);
	do
		frame();
	while (!(keys_down & (KEY_A | KEY_START)));
	text_clear();
}

// ---------------------------------------------------------------- actions

enum { AC_ATTACK, AC_SEARCH, AC_BARRICADE, AC_HELP, AC_LEAVE, AC_WAIT, AC_COUNT };
static const char *const ac_names[AC_COUNT] = { "Attack", "Search", "Barricade", "Help", "Leave", "Wait" };
// action menu modes: B goes back to the move / undoes the move / can't undo / just closes
enum { AM_FRESH, AM_MOVED, AM_LOCKED, AM_LATER };

static int list_targets(int ui, u8 *t)
{
	int n = 0;
	for (int i = 0; i < MAX_UNITS; i++) {
		const Unit *z = &gs.units[i];
		if (z->alive && z->side == SIDE_HORDE && shown(i) && tac_weapon(ui, i) >= 0)
			t[n++] = i;
	}
	return n;
}

static void forecast(int ui, int t)
{
	const Unit *tu = &gs.units[t];
	int w = tac_weapon(ui, t), cover = 0;
	int hit = tac_hit_chance(ui, t, &cover), dmg = tac_damage(ui, t);
	int x = 6, y = tu->y - gs.cam_y < 5 ? 12 : 0;
	text_clear();
	text_window(x, y, 18, 8);
	text_put(x + 1, y + 1, unit_info[tu->type < UT_COUNT ? tu->type : UT_WALKER].name, BGPAL_TEXT_RED);
	text_printf(x + 9, y + 1, BGPAL_UI, "HP %d/%d", tu->hp, tac.actor[t].maxhp);
	text_printf(x + 1, y + 2, BGPAL_TEXT_YEL, "Hit %d%%", hit);
	text_printf(x + 9, y + 2, BGPAL_TEXT_YEL, "Dmg %d", dmg);
	// same tests as the rules (atk_calc): ranged = shooting or not adjacent
	const Unit *au = &gs.units[ui];
	bool ranged = ABS(au->x - tu->x) + ABS(au->y - tu->y) > 1 || (w >= 0 && weapon_info[w].skill == SK_FIREARMS);
	bool flank = tac_flanked(ui, t);
	if (ranged)
		text_put(x + 1, y + 3, flank ? "FLANKED" : cover >= 40 ? "Full cover" : cover > 0 ? "Half cover" : "In the open",
		         flank ? BGPAL_TEXT_RED : cover > 0 ? BGPAL_TEXT_BLUE : BGPAL_TEXT_GREY);
	else
		text_put(x + 1, y + 3, "Melee", BGPAL_UI);
	if (tu->dormant || !tac.actor[t].alerted)
		text_put(x + 1, y + 4, "Unaware", BGPAL_TEXT_YEL);
	if (w >= 0) {
		const WeaponInfo *wi = &weapon_info[w];
		text_put(x + 1, y + 5, wi->name, BGPAL_TEXT_GREY);
		if (wi->ammo)
			text_printf(x + 9, y + 5, run.res[RES_AMMO] ? BGPAL_UI : BGPAL_TEXT_RED, "Ammo %d", MIN(run.res[RES_AMMO], 999));
		text_printf(x + 1, y + 6, wi->noise >= 6 ? BGPAL_TEXT_RED : BGPAL_TEXT_GREY, "Noise %d", wi->noise);
	}
}

// Choose a target with the D-pad. Returns a unit index or -1.
static int pick_target(int ui, const u8 *targets, int n)
{
	int sel = 0, drawn = -1;
	spr.cursor_show = 1;
	for (;;) {
		if (sel != drawn) {
			const Unit *t = &gs.units[targets[sel]];
			gs.cur_x = t->x;
			gs.cur_y = t->y;
			camera_follow(t->x, t->y, true);
			forecast(ui, targets[sel]);
			drawn = sel;
		}
		frame();
		if (keys_rep & (KEY_RIGHT | KEY_DOWN | KEY_R)) {
			sel = sel + 1 < n ? sel + 1 : 0;
			sfx(SFX_CURSOR);
		}
		if (keys_rep & (KEY_LEFT | KEY_UP | KEY_L)) {
			sel = sel ? sel - 1 : n - 1;
			sfx(SFX_CURSOR);
		}
		if (keys_down & (KEY_A | KEY_B)) {
			bool ok = keys_down & KEY_A;
			sfx(ok ? SFX_CONFIRM : SFX_CANCEL);
			text_clear();
			spr.cursor_show = 0;
			return ok ? targets[sel] : -1;
		}
	}
}

static void do_attack(int ui, int t)
{
	Unit *tu = &gs.units[t];
	int x0 = tu->x, y0 = tu->y, side0 = tu->side;
	camera_follow(x0, y0, false);
	tac_attack(ui, t);
	// the rules have no kill hook: the map explosion when a zombie drops (its slot may be reused already)
	if (side0 == SIDE_HORDE && (!tu->alive || tu->side != side0 || tu->x != x0 || tu->y != y0)) {
		sfx(SFX_DESTROY);
		spr.boom_x = x0 * 16;
		spr.boom_y = y0 * 16;
		spr.boom_timer = BOOM_FRAMES;
		wait_frames(BOOM_FRAMES);
	}
}

// "Food 2 Ammo 1": what changed in the carry since `before`
static int loot_line(char *buf, int size, const u8 *before, const u8 *after)
{
	int len = 0;
	buf[0] = 0;
	for (int r = 0; r < RES_COUNT; r++)
		if (after[r] > before[r] && len < size - 10)
			len += snprintf(buf + len, size - len, "%s%s %d", len ? " " : "", res_names[r], after[r] - before[r]);
	return len;
}

static void do_search(int ui)
{
	u8 before[RES_COUNT];
	char l2[28];
	memcpy(before, tac.actor[ui].carry, sizeof before);
	tac_search(ui);
	if (loot_line(l2, 27, before, tac.actor[ui].carry)) {
		sfx(SFX_FOUND);
		banner("Found", l2, 50);
	} else {
		sfx(SFX_EMPTY);
		banner("Nothing useful", NULL, 40);
	}
}

static void do_barricade(int ui)
{
	int b = tac_barricade_target(ui);
	tac_barricade(ui);
	if (b >= 0 && tac.bldg[b].door_hp) {
		sfx(SFX_BUILD);
		banner("Door barricaded", "Parts -1", 40);
	}
}

static void do_leave(int ui)
{
	char l1[24];
	sfx(SFX_EVAC);
	tac_extract(ui);
	int how = tac.actor[ui].extracted;
	if (!how)
		return;
	snprintf(l1, sizeof l1, how == 1 ? "%.9s got out" : "%.9s fell back", name_of(ui));
	banner(l1, how == 1 ? "Through the exit." : "Back at the caravan.", 50);
}

// a downed survivor next to ui, across an open edge
static int downed_next_to(int ui)
{
	const Unit *u = &gs.units[ui];
	for (int k = 0; k < 4; k++) {
		int x = u->x + TAC_DIRX(k), y = u->y + TAC_DIRY(k);
		if (x < 0 || y < 0 || x >= gs.w || y >= gs.h || !tac_edge_open(u->x, u->y, k, false))
			continue;
		int o = unit_at(x, y);
		if (o >= 0 && o != ui && on_map(o) && tac.actor[o].down)
			return o;
	}
	return -1;
}

// Back on their feet with 1 HP; they need the rest of the turn to get steady.
static void do_help(int ui, int o)
{
	Actor *d = &tac.actor[o];
	Person *p = person_of(ui);
	char l2[24];
	d->down = 0;
	d->hp = 1;
	gs.units[o].hp = 1;
	d->moved = d->acted = 1;
	if (p)
		skill_use(p, SK_MEDICINE, 2);
	sfx(SFX_HEAL);
	snprintf(l2, sizeof l2, "%.9s is up again.", name_of(o));
	banner("Back on their feet", l2, 50);
}

static bool confirm_leave(int ui)
{
	static const char *const yn[2] = { "Leave", "Stay" };
	const Unit *u = &gs.units[ui];
	text_clear();
	text_window(3, 3, 24, 4);
	text_put(5, 4, name_of(ui), BGPAL_TEXT_YEL);
	text_put(5, 5, (tac.zone[u->y][u->x] & ZN_EXIT) ? "Out by the exit?" : "Back to the caravan?", BGPAL_UI);
	int c = text_menu(11, 8, 9, yn, NULL, 2, 1);
	text_clear();
	return c == 0;
}

static void after_action(int ui)
{
	tac_fog();
	tac_check_objective();
	refresh_grey();
	tac_music_update();
	tac_people_notice();
	if (on_map(ui)) {
		gs.cur_x = gs.units[ui].x;
		gs.cur_y = gs.units[ui].y;
	}
}

// After a move (or instead of one). Returns true if the person acted.
static bool action_menu(int ui, int mode)
{
	Unit *u = &gs.units[ui];
	Actor *a = &tac.actor[ui];
	u8 targets[MAX_UNITS];
	for (;;) {
		if (!on_map(ui) || a->down || a->acted)
			return false;
		const char *items[AC_COUNT];
		u8 ids[AC_COUNT], en[AC_COUNT];
		int n = 0, nt = list_targets(ui, targets), helpee = downed_next_to(ui);
		if (nt)
			ids[n++] = AC_ATTACK;
		if (tac_can_search(ui))
			ids[n++] = AC_SEARCH;
		if (tac_barricade_target(ui) >= 0)
			ids[n++] = AC_BARRICADE;
		if (helpee >= 0)
			ids[n++] = AC_HELP;
		if (u->x < gs.w && u->y < gs.h && (tac.zone[u->y][u->x] & (ZN_EXIT | ZN_DEPLOY)))
			ids[n++] = AC_LEAVE;
		ids[n++] = AC_WAIT;
		for (int i = 0; i < n; i++) {
			items[i] = ac_names[ids[i]];
			en[i] = ids[i] != AC_BARRICADE || run.res[RES_PARTS] >= 1;
		}
		gs.cur_x = u->x;
		gs.cur_y = u->y;
		tac_hud();
		int c = text_menu(u->x - gs.cam_x < 8 ? 18 : 1, 5, 12, items, en, n, 0);
		if (c < 0) {
			if (mode == AM_LOCKED) {
				sfx(SFX_BUZZ);
				banner("No going back", "You've seen too much.", 50);
			}
			return false;
		}
		switch (ids[c]) {
		case AC_ATTACK: {
			int t = pick_target(ui, targets, nt);
			if (t < 0)
				continue;
			do_attack(ui, t);
			break;
		}
		case AC_SEARCH:
			do_search(ui);
			break;
		case AC_BARRICADE:
			do_barricade(ui);
			break;
		case AC_HELP:
			do_help(ui, helpee);
			break;
		case AC_LEAVE:
			if (!confirm_leave(ui))
				continue;
			do_leave(ui);
			break;
		case AC_WAIT:
			a->moved = 1;
			break;
		}
		a->acted = 1;
		after_action(ui);
		return true;
	}
}

// ---------------------------------------------------------------- moving

static void show_moves(void)
{
	overlay_clear();
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++)
			if (dist[IDX(x, y)] != NO_PATH)
				overlay_set(x, y, OV_MOVE);
}

static bool revealed(void)
{
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++)
			if (fog0[y][x] && !gs.fog[y][x])
				return true;
	return false;
}

// A zombie the player couldn't see was on the way: it gets a free bite if it can reach.
static void ambush(int ui, int z)
{
	const Unit *u = &gs.units[ui], *zu = &gs.units[z];
	int dx = zu->x - u->x, dy = zu->y - u->y;
	int k = dy < 0 ? 0 : dx > 0 ? 1 : dy > 0 ? 2 : 3;
	sfx(SFX_AMBUSH);
	camera_follow(u->x, u->y, false);
	if (ABS(dx) + ABS(dy) != 1 || !tac_edge_open(u->x, u->y, k, true)) {
		banner("Something moves", "It was in your way.", 50);
		return;
	}
	banner("Ambush!", "Something was waiting.", 50);
	tac.actor[z].unseen = 1;
	tac_attack(z, ui);
	tac_fog();
	tac_check_objective();
	refresh_grey();
	tac_music_update();
	tac_people_notice();
}

// Walk ui to (tx, ty). Returns true if B may still undo the move.
static bool do_move(int ui, int tx, int ty)
{
	Unit *u = &gs.units[ui];
	u16 path[64];
	int len = build_path(prev, u->x, u->y, tx, ty, path), amb = -1;
	// stop before the first zombie on the way (seen ones block in tac_paths, so it was unseen)
	for (int i = 0; i < len; i++) {
		int z = unit_at(path[i] & 0xFF, path[i] >> 8);
		if (z >= 0 && gs.units[z].side == SIDE_HORDE) {
			amb = shown(z) ? -1 : z;
			len = i;
			break;
		}
	}
	// never end on someone else's tile
	while (len > 0) {
		int o = unit_at(path[len - 1] & 0xFF, path[len - 1] >> 8);
		if (o < 0 || o == ui)
			break;
		len--;
	}
	// ending in a building nobody of ours is in lifts its roof: what's inside is seen
	int ex = len ? path[len - 1] & 0xFF : u->x, ey = len ? path[len - 1] >> 8 : u->y, b = tac.bmap[ey][ex];
	bool lifted = b != 0;
	u8 seen0[MAX_UNITS];
	for (int i = 0; i < MAX_UNITS; i++) {
		lifted = lifted && !(on_map(i) && tac.bmap[gs.units[i].y][gs.units[i].x] == b);
		seen0[i] = shown(i);
	}
	spr.cursor_show = 0;
	memcpy(fog0, gs.fog, sizeof fog0);
	unit_move_anim(ui, path, len);
	tac.actor[ui].moved = 1;
	tac_fog();
	bool undo = !revealed() && !lifted;
	for (int i = 0; i < MAX_UNITS; i++) // and any zombie that came into view
		if (gs.units[i].alive && gs.units[i].side == SIDE_HORDE && shown(i) && !seen0[i])
			undo = false;
	if (amb >= 0) {
		undo = false;
		ambush(ui, amb);
	}
	refresh_grey();
	gs.cur_x = u->x;
	gs.cur_y = u->y;
	return undo;
}

// A on a ready person: pick a tile (or their own tile to act in place), then act.
static void activate(int ui)
{
	Unit *u = &gs.units[ui];
	Actor *a = &tac.actor[ui];
	if (a->moved) {
		sfx(SFX_CONFIRM);
		action_menu(ui, AM_LATER);
		return;
	}
	int ox = u->x, oy = u->y;
	sfx(SFX_CONFIRM);
	tac_paths(ui, dist, prev);
	dist[IDX(ox, oy)] = 0;
	show_moves();
	path_arrow_update(dist, prev, ox, oy);
	for (;;) {
		frame();
		if (tac.result)
			break;
		if (cursor_keys()) {
			path_arrow_update(dist, prev, ox, oy);
			tac_hud();
		}
		if (keys_down & KEY_R) {
			tac_card(gs.cur_x, gs.cur_y);
			tac_hud();
			continue;
		}
		if (keys_down & KEY_B) {
			sfx(SFX_CANCEL);
			break;
		}
		if (!(keys_down & KEY_A))
			continue;
		int tx = gs.cur_x, ty = gs.cur_y, o = unit_at(tx, ty);
		if (dist[IDX(tx, ty)] == NO_PATH || (o >= 0 && o != ui && shown(o)) || (tx == ox && ty == oy && a->acted)) {
			sfx(SFX_BUZZ);
			continue;
		}
		path_arrow_clear();
		overlay_clear();
		if (tx == ox && ty == oy) {
			spr.cursor_show = 0;
			bool acted = action_menu(ui, AM_FRESH);
			spr.cursor_show = 1;
			if (acted)
				return;
			show_moves();
			path_arrow_update(dist, prev, ox, oy);
			tac_hud();
			continue;
		}
		bool undo = do_move(ui, tx, ty);
		if (!tac.result && on_map(ui) && !a->down && !a->acted) {
			bool acted = action_menu(ui, undo ? AM_MOVED : AM_LOCKED);
			if (!acted && undo && on_map(ui)) {
				// undo the move
				u->x = ox;
				u->y = oy;
				a->moved = 0;
				tac_fog();
				refresh_grey();
				gs.cur_x = ox;
				gs.cur_y = oy;
				camera_follow(ox, oy, true);
				spr.cursor_show = 1;
				show_moves();
				path_arrow_update(dist, prev, ox, oy);
				tac_hud();
				continue;
			}
		}
		spr.cursor_show = 1;
		return;
	}
	// cancelled
	path_arrow_clear();
	overlay_clear();
	gs.cur_x = ox;
	gs.cur_y = oy;
	camera_follow(ox, oy, true);
}

// ---------------------------------------------------------------- survivor phase

// Returns true for End Turn.
static bool start_menu(void)
{
	static const char *const items[4] = { "End Turn", "Objective", "Options", "Cancel" };
	text_clear();
	int c = text_menu(9, 5, 12, items, NULL, 4, 0);
	text_clear();
	if (c == 1)
		objective_screen();
	else if (c == 2)
		options_screen(5, 5);
	return c == 0;
}

static void phase_start(void)
{
	char l1[16];
	gs.side = SIDE_SURVIVOR;
	for (int i = 0; i < MAX_UNITS; i++)
		if (gs.units[i].alive && gs.units[i].side == SIDE_SURVIVOR) {
			Actor *a = &tac.actor[i];
			a->noisy = 0;
			a->moved = 0;
			a->acted = 0;
		}
	refresh_grey();
	tac_fog();
	text_clear();
	next_unit = 0;
	jump_next();
	snprintf(l1, sizeof l1, "Turn %d", tac.turn + 1); // tac.turn counts finished turns
	banner(l1, gs.night ? "Night. Keep quiet." : "Survivors' turn", 50);
}

static void survivor_phase(void)
{
	phase_start();
	spr.cursor_show = 1;
	tac_hud();
	while (!tac.result) {
		frame();
		if (cursor_keys()) {
			tac_hud();
			continue;
		}
		if (keys_down & (KEY_SELECT | KEY_L)) {
			if (!jump_next())
				sfx(SFX_BUZZ);
			tac_hud();
			continue;
		}
		if (keys_down & KEY_R) {
			tac_card(gs.cur_x, gs.cur_y);
			tac_hud();
			continue;
		}
		bool menu = keys_down & KEY_START;
		if (keys_down & KEY_A) {
			int o = unit_at(gs.cur_x, gs.cur_y);
			if (o >= 0 && gs.units[o].side == SIDE_SURVIVOR && ready(o)) {
				activate(o);
				tac_hud();
				continue;
			}
			if (o >= 0 && shown(o)) {
				tac_card(gs.cur_x, gs.cur_y);
				tac_hud();
				continue;
			}
			menu = true;
		}
		if (menu) {
			if (start_menu())
				break;
			tac_hud();
		}
	}
	spr.cursor_show = 0;
	path_arrow_clear();
	overlay_clear();
	text_clear();
}

// Nobody left on the map: the rules must have ended the mission; never loop forever.
static bool mission_over(void)
{
	if (!tac.result && !tac_survivors_left()) {
		tac_check_objective();
		if (!tac.result)
			tac.result = 2;
	}
	return tac.result != 0;
}

int tac_run(const TacMission *m, const u8 *deploy, int n)
{
	n = MAX(0, MIN(n, ROSTER_MAX));
	tac_mode = true;
	fx_ready = false;
	tac_load(m, deploy, n);
	camera_follow(gs.cur_x, gs.cur_y, false); // snap: cam_px/cam_py still hold the last map's scroll
	REG_DISPCNT = DCNT_BLANK;
	video_load_game_gfx();
	u16 dispcnt = REG_DISPCNT;
	REG_DISPCNT = DCNT_BLANK; // stay blank until the map is drawn
	tac_gfx_load();
	text_clear();
	overlay_clear();
	path_arrow_clear();
	spr.cursor_show = spr.pointer_show = 0;
	spr.hide_unit = spr.anim_unit = spr.flash_unit = spr.portrait = -1;
	spr.popup_timer = spr.boom_timer = 0;
	spr.anim_face = 0;
	map_draw();
	spr.map_visible = 1;
	tac_fog();
	REG_DISPCNT = dispcnt;
	// who went in, for the results
	dep_n = 0;
	for (int k = 0; k < n; k++)
		for (int i = 0; i < MAX_UNITS; i++)
			if (gs.units[i].alive && gs.units[i].side == SIDE_SURVIVOR && tac.actor[i].person == deploy[k]) {
				dep_person[dep_n] = deploy[k];
				dep_unit[dep_n] = i;
				dep_state[dep_n] = person_state(dep_n); // bitten before the mission: no notice
				dep_n++;
				break;
			}
	next_unit = 0;
	if (jump_next())
		camera_follow(gs.cur_x, gs.cur_y, false);
	fx_ready = true;
	music_level = -1;
	tac_music_update();
	briefing();
	while (!mission_over()) {
		survivor_phase();
		if (mission_over())
			break;
		tac_end_survivor_phase();
		tac_fog();
		tac_music_update();
		tac_people_notice();
		if (mission_over())
			break;
		tac_horde_turn();
	}
	gs.over = tac.result; // victory / defeat jingle under the results
	results();
	gs.over = 0;
	for (int r = 0; r < RES_COUNT; r++)
		run.res[r] = MIN(65535, run.res[r] + tac.loot[r]);
	fx_ready = false;
	tac_mode = false;
	spr.map_visible = 0;
	spr.cursor_show = 0;
	path_arrow_clear();
	overlay_clear();
	text_clear();
	return tac.result;
}
