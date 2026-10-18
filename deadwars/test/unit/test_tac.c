// Dead Wars: The Last Boat - host unit tests for the tactical rules (src/tac_rules.c).
// Every test loads a map with tac_load() and pokes gs/tac/run directly. See Makefile.
#include "lb.h"
#include "unit.h"
#include <stdlib.h>
#include <string.h>

int checks, fails;
const char *test_name = "";

#define IDX(x, y) ((y) * MAP_MAX_W + (x))
#define NO_PATH 255
#define END_Z { 0xFF, 0, 0, 0, 0, 0 }

static u8 dist[MAP_MAX_W * MAP_MAX_H], prev[MAP_MAX_W * MAP_MAX_H];
static const u8 crew[ROSTER_MAX] = { 0, 1, 2, 3, 4, 5 };
enum { MARA, DOC, JUNE, HOLT, ROSA, TESS };

// ---------------------------------------------------------------- test maps
/* lab: x 0123456789AB
 *   0    ............
 *   1    .HH...SSS...     house A (1,1) door (1,2) S    shop (6,1) door (7,2) S
 *   2    .hH...SsS...
 *   3    ............
 *   4    ....w.......     wreck north of the cover spot (4,5)
 *   5    .....#......     fence east of it
 *   6    ............
 *   7    oo...f..^...     woods (5,7) (5,8), rubble (8,7)
 *   8    oo...f..HHGG     house B (8,8) door (8,9) S, garage (10,8) door (11,9) S: B and the garage touch
 *   9    oo......hHGg
 *  10    ..........xx     exits
 */
static const char *const lab_rows[] = {
	"............",
	".HH...SSS...",
	".hH...SsS...",
	"............",
	"....w.......",
	".....#......",
	"............",
	"oo...f..^...",
	"oo...f..HHGG",
	"oo......hHGg",
	"..........xx",
	NULL,
};
static const UnitPlacement no_z[] = { END_Z };
static const TacMission lab = { "Lab", "", lab_rows, no_z, OBJ_SCAVENGE, 0, 0, { 0, 0, 0 }, 3 };
static const TacMission lab_waves = { "Lab", "", lab_rows, no_z, OBJ_SCAVENGE, 0, 0, { 5, 10, 15 }, 3 };
static const TacMission lab_flood = { "Lab", "", lab_rows, no_z, OBJ_SCAVENGE, 0, 0, { 1, 0, 0 }, 200 };
static const TacMission lab_through = { "Lab", "", lab_rows, no_z, OBJ_GET_THROUGH, 2, 0, { 0, 0, 0 }, 3 };

static const char *const span_rows[] = {
	"o..~~~..x",
	"o..===..x",
	"o..~~~..x",
	"o..~~~...",
	NULL,
};
static const TacMission span = { "Span", "", span_rows, no_z, OBJ_BRIDGE, 2, 0, { 0, 0, 0 }, 3 };

// ---------------------------------------------------------------- helpers
static void person_init(Person *p, int cast)
{
	const CastInfo *c = &cast_info[cast];
	memset(p, 0, sizeof *p);
	p->cast = cast;
	p->alive = 1;
	p->grit = c->grit;
	p->aim = c->aim;
	p->speed = c->speed;
	p->nerve = c->nerve;
	memcpy(p->trait, c->trait, sizeof p->trait);
	memcpy(p->skill, c->skill, sizeof p->skill);
	p->weapon = c->weapon;
	p->armor = c->armor;
	p->sprite = c->sprite;
	p->he = c->he;
	p->hp = p->maxhp = 6 + c->grit;
}

static void run_init(void)
{
	memset(&run, 0, sizeof run);
	run.day = 5;
	run.difficulty = 1;   // Normal: the Horde meter grows x1
	run.nroster = CAST_COUNT;
	for (int i = 0; i < CAST_COUNT; i++)
		person_init(&run.roster[i], i);
	run.res[RES_AMMO] = 20;
	run.res[RES_PARTS] = 5;
}

static void load(const TacMission *m, int n, u32 seed)
{
	run_init();
	memset(&fx, 0, sizeof fx);
	gs.rng = seed;
	tac_load(m, crew, n);
}

// slot of roster person r (on the map or not), -1 if never deployed
static int slot(int r)
{
	for (int i = 0; i < MAX_UNITS; i++)
		if (tac.actor[i].person == r)
			return i;
	return -1;
}

static void put(int u, int x, int y)
{
	gs.units[u].x = x;
	gs.units[u].y = y;
	gs.units[u].inside = tac_in_bldg(x, y);
}

static void set_hp(int u, int hp)
{
	tac.actor[u].hp = gs.units[u].hp = hp;
}

// an aware (alerted, awake) zombie
static int zombie(int type, int x, int y)
{
	int z = tac_spawn_zombie(type, x, y, false);
	CHECK(z >= 0);
	if (z >= 0)
		tac.actor[z].alerted = 1;
	return z;
}

static int count_side(int side)
{
	int n = 0;
	for (int i = 0; i < MAX_UNITS; i++)
		if (gs.units[i].alive && gs.units[i].side == side)
			n++;
	return n;
}

static int carry_sum(int u)
{
	int s = 0;
	for (int r = 0; r < RES_COUNT; r++)
		s += tac.actor[u].carry[r];
	return s;
}

// last Chronicle record of an event, or NULL
static const LogRec *last_log(int event)
{
	for (int k = run.log_count - 1; k >= 0; k--) {
		const LogRec *r = &run.log[(run.log_head + k) % LOG_MAX];
		if (r->event == event)
			return r;
	}
	return NULL;
}

// actor[] parallel to gs.units[]: zombies are 0xFF with synced HP, people point at living roster entries
static void check_sync(void)
{
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		Actor *a = &tac.actor[i];
		if (!u->alive)
			continue;
		EQ(a->hp, u->hp);
		if (u->side == SIDE_HORDE) {
			EQ(a->person, 0xFF);
			CHECK(u->type >= FIRST_ZOMBIE && u->type < UT_COUNT);
			CHECK(u->hp > 0);
		} else {
			CHECK(a->person < run.nroster);
			CHECK(run.roster[a->person].alive);
			EQ(a->extracted, 0);
		}
		EQ(u->inside, tac_in_bldg(u->x, u->y));
	}
}

// people BFS from every deploy tile, ignoring units
static void reach_from_deploy(u8 *seen)
{
	static u16 q[MAP_MAX_W * MAP_MAX_H];
	int qh = 0, qt = 0;
	memset(seen, 0, MAP_MAX_W * MAP_MAX_H);
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++)
			if (tac.zone[y][x] & ZN_DEPLOY) {
				seen[IDX(x, y)] = 1;
				q[qt++] = x | y << 8;
			}
	while (qh < qt) {
		int x = q[qh] & 0xFF, y = q[qh++] >> 8;
		for (int k = 0; k < 4; k++) {
			int nx = x + TAC_DIRX(k), ny = y + TAC_DIRY(k);
			if (!tac_edge_open(x, y, k, false) || tac_move_cost(nx, ny, false) == IMPASSABLE || seen[IDX(nx, ny)])
				continue;
			seen[IDX(nx, ny)] = 1;
			q[qt++] = nx | ny << 8;
		}
	}
}

static int raw(const TacMission *m, int x, int y)
{
	return m->rows[y][x];
}

static int upper(int c)
{
	return c >= 'a' && c <= 'z' ? c - 32 : c;
}

// ---------------------------------------------------------------- tests
static void test_load_basics(void)
{
	test_name = "load";
	run_init();
	run.roster[JUNE].hp = 3;      // wounded: keeps her HP
	run.roster[ROSA].alive = 0;   // dead: not deployed
	static const u8 dep[] = { MARA, DOC, JUNE, DOC, ROSA, HOLT, 200 };
	gs.rng = 0xC0FFEE;
	tac_load(&lab, dep, 7);
	EQ(gs.rng, 0xC0FFEE);
	EQ(gs.w, 12);
	EQ(gs.h, 11);
	EQ(gs.day, 5);
	EQ(gs.night, 0);
	EQ(gs.side, SIDE_SURVIVOR);
	EQ(gs.fog[3][3], 1);
	EQ(gs.owner[3][3], SIDE_NEUTRAL);
	EQ(tac.m == &lab, 1);
	EQ(tac.nbldg, 4);
	EQ(tac_survivors_left(), 4);  // Mara, Doc, June, Holt (duplicate, dead and bad index skipped)
	static const u8 want[4][3] = { { MARA, 0, 7 }, { DOC, 1, 7 }, { JUNE, 0, 8 }, { HOLT, 1, 8 } };
	for (int i = 0; i < 4; i++) {
		int u = slot(want[i][0]);
		CHECK(u >= 0);
		if (u < 0)
			continue;
		EQ(gs.units[u].x, want[i][1]);
		EQ(gs.units[u].y, want[i][2]);
		EQ(gs.units[u].type, UT_SURVIVOR);
		EQ(gs.units[u].side, SIDE_SURVIVOR);
		EQ(tac.actor[u].hp, run.roster[want[i][0]].hp);
		EQ(tac.actor[u].maxhp, run.roster[want[i][0]].maxhp);
	}
	EQ(tac.actor[slot(JUNE)].hp, 3);
	EQ(gs.units[slot(JUNE)].hp, 3);
	EQ(slot(ROSA), -1);
	// terrain and zones
	EQ(gs.terrain[4][4], TT_WRECK);
	EQ(gs.terrain[5][5], TT_WALL);
	EQ(gs.terrain[7][5], TT_FOREST);
	EQ(gs.terrain[7][8], TT_HILL);
	EQ(gs.terrain[1][1], TT_HOUSE);
	EQ(gs.terrain[10][10], TT_ROAD);
	EQ(tac.zone[10][10], ZN_EXIT);
	EQ(tac.zone[7][0], ZN_DEPLOY);
	// buildings
	const Building *a = &tac.bldg[0], *s = &tac.bldg[1], *b = &tac.bldg[2], *g = &tac.bldg[3];
	EQ(a->kind, BK_HOUSE);  EQ(a->x, 1);  EQ(a->y, 1);  EQ(a->w, 2); EQ(a->h, 2);
	EQ(a->door_x, 1);  EQ(a->door_y, 2);  EQ(a->door_dir, 2);
	EQ(s->kind, BK_SHOP);   EQ(s->w, 3);  EQ(s->door_x, 7);  EQ(s->door_y, 2);  EQ(s->door_dir, 2);
	EQ(b->kind, BK_HOUSE);  EQ(b->door_x, 8);  EQ(b->door_y, 9);  EQ(b->door_dir, 2);
	EQ(g->kind, BK_GARAGE); EQ(g->x, 10); EQ(g->door_x, 11); EQ(g->door_y, 9);  EQ(g->door_dir, 2);
	EQ(tac.bmap[2][2], 1);
	EQ(tac.bmap[2][7], 2);
	EQ(tac.bmap[8][9], 3);
	EQ(tac.bmap[8][10], 4);
	EQ(tac.bmap[3][3], 0);
	EQ(tac_in_bldg(1, 1), true);
	EQ(tac_in_bldg(-1, 1), false);
	EQ(tac_in_bldg(3, 3), false);
	check_sync();
	// a zombie placed under a roof is inside
	int z = tac_spawn_zombie(UT_BRUTE, 2, 1, true);
	CHECK(z >= 0);
	EQ(gs.units[z].inside, 1);
	EQ(gs.units[z].dormant, 1);
	EQ(tac.actor[z].hp, 12);
	EQ(gs.units[z].hp, 12);
	EQ(tac_spawn_zombie(UT_WALKER, 2, 1, false), -1); // occupied
	EQ(tac_spawn_zombie(UT_SURVIVOR, 3, 3, false), -1); // not a zombie type
	EQ(tac_spawn_zombie(UT_WALKER, 12, 3, false), -1); // out of bounds
}

static void test_real_maps(void)
{
	static u8 seen[MAP_MAX_W * MAP_MAX_H];
	for (int mi = 0; mi < tac_mission_count; mi++) {
		const TacMission *m = &tac_missions[mi];
		test_name = m->title;
		load(m, CAST_COUNT, 1);
		int h = 0;
		while (m->rows[h])
			h++;
		EQ(gs.h, h);
		for (int y = 0; y < h; y++)
			if ((int)strlen(m->rows[y]) != gs.w)
				FAILF("%s: row %d is %d wide, row 0 is %d", m->title, y, (int)strlen(m->rows[y]), gs.w);
		// every building letter belongs to a building
		for (int y = 0; y < gs.h; y++)
			for (int x = 0; x < gs.w; x++)
				if (strchr("HSGPhsgp", raw(m, x, y)) && !tac.bmap[y][x])
					FAILF("%s: building tile (%d,%d) not in any building", m->title, x, y);
		for (int i = 0; i < tac.nbldg; i++) {
			const Building *b = &tac.bldg[i];
			int letter = "HSGP"[b->kind], doors = 0, n = 0;
			for (int y = 0; y < gs.h; y++)
				for (int x = 0; x < gs.w; x++)
					n += tac.bmap[y][x] == i + 1;
			if (n != b->w * b->h)
				FAILF("%s: building %d has %d tiles, footprint %dx%d", m->title, i, n, b->w, b->h);
			for (int y = b->y; y < b->y + b->h; y++)
				for (int x = b->x; x < b->x + b->w; x++) {
					if (upper(raw(m, x, y)) != letter || tac.bmap[y][x] != i + 1)
						FAILF("%s: building %d footprint broken at (%d,%d)", m->title, i, x, y);
					if (raw(m, x, y) == letter + 32) {
						doors++;
						if (x != b->door_x || y != b->door_y)
							FAILF("%s: building %d door not at (%d,%d)", m->title, i, x, y);
					}
				}
			// a full rectangle: no tile of the same letter touches it from outside
			for (int y = b->y - 1; y <= b->y + b->h; y++)
				for (int x = b->x - 1; x <= b->x + b->w; x++) {
					bool in = x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
					bool corner = (x == b->x - 1 || x == b->x + b->w) && (y == b->y - 1 || y == b->y + b->h);
					if (!in && !corner && x >= 0 && y >= 0 && x < gs.w && y < gs.h && upper(raw(m, x, y)) == letter)
						FAILF("%s: building %d at (%d,%d) %dx%d is not a full rectangle: '%c' at (%d,%d)",
						      m->title, i, b->x, b->y, b->w, b->h, raw(m, x, y), x, y);
				}
			if (doors != 1)
				FAILF("%s: building %d at (%d,%d) has %d doors", m->title, i, b->x, b->y, doors);
			int ox = b->door_x + TAC_DIRX(b->door_dir), oy = b->door_y + TAC_DIRY(b->door_dir);
			if (ox < 0 || oy < 0 || ox >= gs.w || oy >= gs.h || tac.bmap[oy][ox] ||
			    tac_move_cost(ox, oy, false) == IMPASSABLE || !tac_edge_open(b->door_x, b->door_y, b->door_dir, false))
				FAILF("%s: building %d door (%d,%d) opens onto a blocked tile (%d,%d)",
				      m->title, i, b->door_x, b->door_y, ox, oy);
		}
		// deploy points, exits, reachability
		int deploy = 0, exits = 0;
		reach_from_deploy(seen);
		for (int y = 0; y < gs.h; y++)
			for (int x = 0; x < gs.w; x++) {
				deploy += !!(tac.zone[y][x] & ZN_DEPLOY);
				if (tac.zone[y][x] & ZN_EXIT) {
					exits++;
					if (!seen[IDX(x, y)])
						FAILF("%s: exit (%d,%d) unreachable from the deploy area", m->title, x, y);
				}
			}
		for (int i = 0; i < tac.nbldg; i++) {
			const Building *b = &tac.bldg[i];
			int ox = b->door_x + TAC_DIRX(b->door_dir), oy = b->door_y + TAC_DIRY(b->door_dir);
			if (ox >= 0 && oy >= 0 && ox < gs.w && oy < gs.h && !seen[IDX(ox, oy)])
				FAILF("%s: building %d door unreachable from the deploy area", m->title, i);
		}
		CHECK(deploy >= 4);
		CHECK(exits >= 1 || m->objective == OBJ_BRIDGE); // the bridge is held, not left
		EQ(tac_survivors_left(), MIN(deploy, CAST_COUNT));
		// zombies: all placed, on tiles they can stand on, not on the caravan
		int nz = 0;
		for (const UnitPlacement *p = m->zombies; p->type != 0xFF; p++) {
			nz++;
			if (p->x >= gs.w || p->y >= gs.h || tac_move_cost(p->x, p->y, true) == IMPASSABLE)
				FAILF("%s: zombie at (%d,%d) on an impassable tile", m->title, p->x, p->y);
			else if (tac.zone[p->y][p->x] & ZN_DEPLOY)
				FAILF("%s: zombie at (%d,%d) on a deploy point", m->title, p->x, p->y);
			int u = unit_at(p->x, p->y);
			if (u < 0 || gs.units[u].side != SIDE_HORDE || gs.units[u].type != p->type)
				FAILF("%s: zombie at (%d,%d) not placed", m->title, p->x, p->y);
			else {
				EQ(gs.units[u].dormant, p->dormant);
				EQ(gs.units[u].inside, tac_in_bldg(p->x, p->y));
			}
		}
		EQ(count_side(SIDE_HORDE), nz);
		check_sync();
		if (m->objective == OBJ_BRIDGE) {
			int nb = 0;
			for (int y = 0; y < gs.h; y++)
				for (int x = 0; x < gs.w; x++)
					nb += !!(tac.zone[y][x] & ZN_BRIDGE);
			CHECK(nb > 0);
		}
	}
}

static void test_edges(void)
{
	test_name = "edges";
	load(&lab, 0, 1);
	// house A: door (1,2) opens south
	EQ(tac_edge_open(1, 2, 2, false), true);
	EQ(tac_edge_open(1, 3, 0, false), true);
	EQ(tac_edge_open(1, 3, 0, true), true);
	EQ(tac_edge_open(2, 2, 2, false), false);  // wall
	EQ(tac_edge_open(2, 3, 0, true), false);
	EQ(tac_edge_open(0, 1, 1, false), false);
	EQ(tac_edge_open(1, 2, 3, false), false);  // door tile, west side is a wall
	EQ(tac_edge_open(3, 1, 3, false), false);
	EQ(tac_edge_open(1, 1, 1, false), true);   // inside
	EQ(tac_edge_open(1, 1, 2, true), true);
	EQ(tac_edge_open(9, 8, 1, false), false);  // house B | garage
	EQ(tac_edge_open(10, 8, 3, true), false);
	EQ(tac_edge_open(3, 3, 1, false), true);   // open ground
	EQ(tac_edge_open(4, 5, 1, false), true);   // fences block by move cost, not by edge
	EQ(tac_edge_open(0, 0, 0, false), false);  // off the map
	EQ(tac_edge_open(11, 0, 1, false), false);
	EQ(tac_edge_open(-1, 0, 1, false), false);
	EQ(tac_edge_open(5, 5, 7, false), false);
	// barricade: people pass their own, zombies don't
	tac.bldg[0].door_hp = 6;
	EQ(tac_edge_open(1, 2, 2, false), true);
	EQ(tac_edge_open(1, 3, 0, false), true);
	EQ(tac_edge_open(1, 2, 2, true), false);
	EQ(tac_edge_open(1, 3, 0, true), false);
	// move costs
	EQ(tac_move_cost(0, 0, false), 1);
	EQ(tac_move_cost(1, 1, true), 1);
	EQ(tac_move_cost(10, 10, false), 1);
	EQ(tac_move_cost(5, 7, false), 2);
	EQ(tac_move_cost(8, 7, true), 2);
	EQ(tac_move_cost(4, 4, false), IMPASSABLE);
	EQ(tac_move_cost(5, 5, true), IMPASSABLE);
	EQ(tac_move_cost(12, 0, false), IMPASSABLE);
	EQ(tac_move_cost(0, -1, true), IMPASSABLE);
	load(&span, 0, 1);
	EQ(tac_move_cost(3, 0, false), IMPASSABLE); // river
	EQ(tac_move_cost(3, 0, true), IMPASSABLE);
	EQ(tac_move_cost(4, 1, true), 1);           // bridge
	EQ(tac.zone[1][4], ZN_BRIDGE);
}

static void test_paths(void)
{
	test_name = "paths";
	load(&lab, 2, 1);
	int mara = slot(MARA), doc = slot(DOC);
	EQ(person_move(&run.roster[MARA]), 4);
	put(mara, 1, 3);
	put(doc, 0, 0);
	tac_paths(mara, dist, prev);
	EQ(dist[IDX(1, 3)], 0);
	EQ(dist[IDX(1, 2)], 1);    // through the door
	EQ(prev[IDX(1, 2)], 0);
	EQ(dist[IDX(2, 2)], 2);    // not through the wall from (2,3)
	EQ(prev[IDX(2, 2)], 1);
	EQ(dist[IDX(2, 1)], 3);
	EQ(dist[IDX(4, 4)], NO_PATH); // wreck
	put(mara, 3, 7);
	int n = tac_paths(mara, dist, prev);
	CHECK(n > 1);
	EQ(dist[IDX(4, 7)], 1);
	EQ(dist[IDX(5, 7)], 3);    // woods cost 2
	EQ(dist[IDX(6, 7)], 4);
	EQ(dist[IDX(7, 7)], NO_PATH);
	EQ(dist[IDX(3, 3)], 4);
	EQ(dist[IDX(3, 2)], NO_PATH);
	int count = 0;
	for (int i = 0; i < MAP_MAX_W * MAP_MAX_H; i++)
		count += dist[i] != NO_PATH;
	EQ(n, count);
	// seen zombies block; unseen ones (fog or under a roof) don't, for planning
	put(mara, 3, 3);
	int z = zombie(UT_WALKER, 4, 3);
	gs.fog[3][4] = 0;
	tac_paths(mara, dist, prev);
	EQ(dist[IDX(4, 3)], NO_PATH);
	EQ(dist[IDX(5, 3)], 4);
	gs.fog[3][4] = 1;
	tac_paths(mara, dist, prev);
	EQ(dist[IDX(4, 3)], 1);
	EQ(dist[IDX(5, 3)], 2);
	tac_kill(z, -1);
	z = zombie(UT_WALKER, 1, 2);
	gs.fog[2][1] = 0;
	put(mara, 1, 3);
	EQ(gs.units[z].inside, 1);
	tac_paths(mara, dist, prev);
	EQ(dist[IDX(1, 2)], 1);
	gs.units[z].inside = 0;                   // shown (tac_fog: someone of ours is in there)
	tac_paths(mara, dist, prev);
	EQ(dist[IDX(1, 2)], NO_PATH);
	tac_kill(z, -1);
	// friends are passed through
	put(mara, 3, 3);
	put(doc, 4, 3);
	gs.fog[3][4] = 0;
	tac_paths(mara, dist, prev);
	EQ(dist[IDX(5, 3)], 2);
	// zombies: blocked by people and by barricades, +1 move at night
	put(doc, 0, 0);
	put(mara, 1, 3);
	z = zombie(UT_WALKER, 1, 4);
	tac_paths(z, dist, prev);
	EQ(dist[IDX(1, 3)], NO_PATH);
	put(mara, 0, 0);
	put(doc, 11, 0);
	tac_paths(z, dist, prev);
	EQ(dist[IDX(1, 2)], 2);
	EQ(dist[IDX(1, 1)], NO_PATH);
	gs.night = 1;
	tac_paths(z, dist, prev);
	EQ(dist[IDX(1, 1)], 3);
	tac.bldg[0].door_hp = 3;
	tac_paths(z, dist, prev);
	EQ(dist[IDX(1, 2)], NO_PATH);
	EQ(dist[IDX(1, 3)], 1);
	gs.night = 0;
	// people walk through their own barricade
	put(mara, 1, 3);
	tac_paths(mara, dist, prev);
	EQ(dist[IDX(1, 2)], 1);
	EQ(dist[IDX(1, 1)], 2);
	// downed: nowhere to go
	tac.actor[mara].down = 2;
	EQ(tac_paths(mara, dist, prev), 1);
	EQ(dist[IDX(1, 3)], 0);
	EQ(tac_paths(-1, dist, prev), 0);
	EQ(tac_paths(MAX_UNITS - 1, dist, prev), 0);
	check_sync();
}

static void test_cover(void)
{
	test_name = "cover";
	load(&lab, 0, 1);
	// the mockup: fence east, wreck north of (4,5)
	EQ(tac_cover(4, 5, 8, 5), 40);   // east attacker: fence
	EQ(tac_cover(4, 5, 8, 4), 40);
	EQ(tac_cover(4, 5, 4, 1), 20);   // north attacker: wreck
	EQ(tac_cover(4, 5, 5, 1), 20);
	EQ(tac_cover(4, 5, 2, 7), 0);    // south-west diagonal: flanked
	EQ(tac_cover(4, 5, 0, 5), 0);    // west
	EQ(tac_cover(4, 5, 4, 9), 0);    // south
	EQ(tac_cover(4, 5, 6, 3), 40);   // exact NE diagonal: best of fence and wreck
	EQ(tac_cover(4, 5, 2, 3), 20);   // exact NW diagonal: wreck
	EQ(tac_cover(4, 5, 4, 5), 0);
	// buildings
	EQ(tac_cover(2, 2, 2, 6), 40);   // inside vs outside: wall
	EQ(tac_cover(1, 2, 1, 6), 20);   // door side
	EQ(tac_cover(1, 2, 0, 6), 20);
	EQ(tac_cover(1, 1, 8, 1), 40);   // back of the house: the far wall still covers
	EQ(tac_cover(1, 1, 2, 2), 0);    // same building
	EQ(tac_cover(2, 2, 7, 2), 40);   // from another building
	EQ(tac_cover(0, 2, 5, 2), 40);   // outside, against the house wall
	EQ(tac_cover(3, 1, 0, 1), 40);
	EQ(tac_cover(3, 3, 3, 9), 0);
	EQ(tac_cover(-1, 3, 3, 9), 0);
}

static void test_los(void)
{
	test_name = "los";
	load(&lab, 0, 1);
	EQ(tac_los(3, 7, 7, 7), false);  // woods between
	EQ(tac_los(3, 7, 5, 7), true);   // woods endpoint
	EQ(tac_los(5, 6, 5, 9), false);
	EQ(tac_los(5, 7, 5, 8), true);
	EQ(tac_los(0, 1, 4, 1), false);  // through house A
	EQ(tac_los(1, 1, 4, 1), true);   // from inside it
	EQ(tac_los(4, 1, 1, 1), true);
	EQ(tac_los(0, 1, 1, 1), true);
	EQ(tac_los(3, 5, 7, 5), true);   // fences don't block
	EQ(tac_los(4, 2, 4, 6), true);   // wrecks don't block
	EQ(tac_los(2, 1, 7, 1), true);   // house A to the shop: only open ground between
	EQ(tac_los(0, 2, 9, 2), false);
	EQ(tac_los(3, 3, 3, 3), true);
	EQ(tac_los(3, 3, 12, 3), false);
	// symmetric
	int asym = 0;
	for (int i = 0; i < 4000; i++) {
		int x0 = rand() % 12, y0 = rand() % 11, x1 = rand() % 12, y1 = rand() % 11;
		asym += tac_los(x0, y0, x1, y1) != tac_los(x1, y1, x0, y0);
	}
	EQ(asym, 0);
}

static void test_sees(void)
{
	test_name = "sees";
	load(&lab, 2, 1);
	int mara = slot(MARA), doc = slot(DOC);
	put(doc, 0, 0);
	put(mara, 5, 7);                       // woods
	EQ(tac_hidden(mara), true);
	int z = zombie(UT_WALKER, 5, 4);
	EQ(tac_sees(z, mara), false);
	tac.actor[mara].noisy = 1;
	EQ(tac_hidden(mara), false);
	EQ(tac_sees(z, mara), true);           // fence at (5,5) doesn't block sight
	gs.night = 1;
	EQ(tac_sees(z, mara), false);          // sight 4 - 2
	gs.night = 0;
	tac.actor[mara].noisy = 0;
	put(z, 5, 6);
	EQ(tac_sees(z, mara), true);           // adjacent: always
	put(mara, 1, 1);
	put(z, 4, 1);
	EQ(tac_hidden(mara), true);            // under a roof
	EQ(tac_sees(z, mara), false);
	put(mara, 3, 3);
	EQ(tac_hidden(mara), false);
	put(z, 3, 7);
	EQ(tac_sees(z, mara), true);           // 4 tiles, open
	put(z, 3, 8);
	EQ(tac_sees(z, mara), false);          // 5 tiles
	int r = zombie(UT_RUNNER, 2, 7);
	put(z, 9, 3);
	put(r, 3, 8);
	EQ(tac_sees(r, mara), true);           // runners see 5
	gs.night = 1;
	put(r, 3, 6);
	EQ(tac_sees(r, mara), true);           // night: 5 - 2 = 3
	put(r, 3, 7);
	EQ(tac_sees(r, mara), false);
	gs.night = 0;
	EQ(tac_hidden(z), false);              // zombies are never "hidden"
	EQ(tac_sees(mara, z), false);          // only zombies see with tac_sees
}

static void test_weapons(void)
{
	test_name = "weapons";
	load(&lab, 6, 1);
	int mara = slot(MARA), doc = slot(DOC), holt = slot(HOLT), june = slot(JUNE);
	for (int i = 0; i < 6; i++)
		put(slot(i), i, 0);
	int z = zombie(UT_WALKER, 4, 5);
	int cover = -1;
	// Holt, rifle from the east at 4: 50 + 8*4 + 5*3 + 10 - 40 (fence) = 67
	put(holt, 8, 5);
	EQ(tac_weapon(holt, z), WP_RIFLE);
	EQ(tac_hit_chance(holt, z, &cover), 67);
	EQ(cover, 40);
	EQ(tac_damage(holt, z), 5);
	tac.actor[z].alerted = 0;               // unaware: +25 and +1 damage
	EQ(tac_hit_chance(holt, z, NULL), 92);
	EQ(tac_damage(holt, z), 6);
	tac.actor[z].alerted = 1;
	gs.units[z].dormant = 1;
	EQ(tac_hit_chance(holt, z, NULL), 92);
	gs.units[z].dormant = 0;
	put(holt, 8, 7);                        // dist 6: -20 range, +10 rubble; ex 4 > ey 2: fence, 57
	EQ(tac_hit_chance(holt, z, &cover), 57);
	EQ(cover, 40);
	put(holt, 4, 7);                        // flanked from the south: clamps at 95, +1 damage
	EQ(tac_hit_chance(holt, z, &cover), 95);
	EQ(cover, 0);
	EQ(tac_damage(holt, z), 6);
	put(holt, 4, 9);                        // night, dist 4: 50+32+15+10+15 - 10 = 112 -> 95
	gs.night = 1;
	EQ(tac_hit_chance(holt, z, NULL), 95);
	gs.night = 0;
	put(holt, 3, 5);                        // rifle min range 2: bash with it
	EQ(tac_weapon(holt, z), WP_FISTS);
	EQ(tac_hit_chance(holt, z, &cover), 60 + 6 * 2 + 5 * 1);
	EQ(cover, 0);
	EQ(tac_damage(holt, z), 1);
	put(holt, 11, 0);
	// Doc, pistol at point blank: ranged, cover ignored, flanked: 50 + 16 + 0 + 0 + 15 = 81
	put(doc, 4, 6);
	EQ(tac_weapon(doc, z), WP_PISTOL);
	EQ(tac_hit_chance(doc, z, &cover), 81);
	EQ(cover, 0);
	EQ(tac_damage(doc, z), 4);
	run.res[RES_AMMO] = 0;                  // no ammo: fists, 60 + 12 = 72
	EQ(tac_weapon(doc, z), WP_FISTS);
	EQ(tac_hit_chance(doc, z, NULL), 72);
	EQ(tac_damage(doc, z), 1);
	put(doc, 4, 8);
	EQ(tac_weapon(doc, z), -1);
	EQ(tac_hit_chance(doc, z, &cover), 0);
	EQ(tac_damage(doc, z), 0);
	run.res[RES_AMMO] = 20;
	EQ(tac_weapon(doc, z), WP_PISTOL);      // range 3 with ammo: one step past (4+1)/2, flanked
	EQ(tac_hit_chance(doc, z, &cover), 50 + 16 - 10 + 15);
	EQ(cover, 0);
	gs.night = 1;                           // and at night
	EQ(tac_hit_chance(doc, z, NULL), 50 + 16 - 10 - 10 + 15);
	gs.night = 0;
	put(doc, 4, 10);
	EQ(tac_weapon(doc, z), -1);             // out of range (5 > 4)
	put(doc, 1, 0);
	// June's crossbow needs no ammo; the woods block her line
	run.res[RES_AMMO] = 0;
	put(june, 4, 8);
	EQ(tac_weapon(june, z), WP_CROSSBOW);
	put(june, 6, 7);                        // the line (4,5)->(6,7) passes (5,6): open
	EQ(tac_weapon(june, z), WP_CROSSBOW);
	put(june, 5, 9);                        // in range (5), but the line passes the woods at (5,7)
	EQ(tac_los(5, 9, 4, 5), false);
	EQ(tac_weapon(june, z), -1);
	run.res[RES_AMMO] = 20;
	put(june, 2, 0);
	// clamp low: a raw shooter at range 7 into the fence at night: 50 + 10 - 40 - 30 - 10 = -20 -> 5
	run.roster[HOLT].aim = 0;
	run.roster[HOLT].skill[SK_FIREARMS] = 0;
	put(holt, 11, 5);
	gs.night = 1;
	EQ(tac_hit_chance(holt, z, &cover), 5);
	EQ(cover, 40);
	gs.night = 0;
	put(holt, 3, 0);
	// Mara, machete in melee: 60 + 18 + 10 = 88; unaware: 113 -> 95
	put(mara, 3, 5);
	EQ(tac_weapon(mara, z), WP_MACHETE);
	EQ(tac_hit_chance(mara, z, NULL), 88);
	EQ(tac_damage(mara, z), 4);
	tac.actor[z].alerted = 0;
	EQ(tac_hit_chance(mara, z, NULL), 95);
	EQ(tac_damage(mara, z), 5);
	tac.actor[z].alerted = 1;
	// zombie on Mara: 50 - 5*3 = 35; unseen: 60, +1 damage; on a downed person: 95
	EQ(tac_hit_chance(z, mara, &cover), 35);
	EQ(cover, 0);
	EQ(tac_damage(z, mara), 2);
	tac.actor[z].unseen = 1;
	EQ(tac_hit_chance(z, mara, NULL), 60);
	EQ(tac_damage(z, mara), 3);
	tac.actor[z].unseen = 0;
	tac.actor[mara].down = 2;
	EQ(tac_hit_chance(z, mara, NULL), 95);
	EQ(tac_weapon(mara, z), -1);            // downed people don't fight
	tac.actor[mara].down = 0;
	// walls: no melee through them (either way); a gun fires through a window, point blank: the
	// wall is the walker's only cover and it faces Doc, so no flank (N3): 50 + 16 = 66, 3 damage
	put(mara, 2, 1);
	put(doc, 2, 2);
	put(z, 3, 1);
	EQ(tac_weapon(mara, z), -1);
	EQ(tac_hit_chance(z, mara, NULL), 0);
	put(z, 3, 2);
	EQ(tac_weapon(doc, z), WP_PISTOL);
	EQ(tac_hit_chance(doc, z, &cover), 66);
	EQ(cover, 0);
	EQ(tac_flanked(doc, z), false);
	EQ(tac_damage(doc, z), 3);
	// the door: zombies bite through it until it's barricaded
	put(mara, 1, 2);
	put(z, 1, 3);
	EQ(tac_hit_chance(z, mara, NULL), 35);
	EQ(tac_weapon(mara, z), WP_MACHETE);
	tac.bldg[0].door_hp = 6;
	EQ(tac_hit_chance(z, mara, NULL), 0);
	EQ(tac_damage(z, mara), 0);
	EQ(tac_weapon(mara, z), -1);            // the barricade blocks melee both ways (8.4)
	EQ(tac_hit_chance(mara, z, NULL), 0);
	put(mara, 2, 1);
	put(doc, 1, 2);
	EQ(tac_weapon(doc, z), WP_PISTOL);      // a gun still fires out over it
	put(doc, 1, 3);                         // and from outside, at one on the door tile
	put(z, 1, 2);
	EQ(tac_weapon(doc, z), WP_PISTOL);
	run.res[RES_AMMO] = 0;
	EQ(tac_weapon(doc, z), -1);             // no fists through it either
	run.res[RES_AMMO] = 20;
	tac.bldg[0].door_hp = 0;
	EQ(tac_weapon(doc, z), WP_PISTOL);
	run.res[RES_AMMO] = 0;
	EQ(tac_weapon(doc, z), WP_FISTS);
	run.res[RES_AMMO] = 20;
	// nonsense pairs
	EQ(tac_hit_chance(mara, doc, NULL), 0);
	EQ(tac_hit_chance(z, z, NULL), 0);
	EQ(tac_weapon(z, mara), -1);
	EQ(tac_hit_chance(-1, z, NULL), 0);
	EQ(tac_hit_chance(z, MAX_UNITS, NULL), 0);
	memset(&fx, 0, sizeof fx);
	tac_attack(mara, doc);                  // can't: nothing happens
	tac_attack(z, z);
	EQ(fx.hits + fx.misses + fx.noises, 0);
}

// Repeat one attack from the same state (the RNG keeps running) and compare the hit rate.
static void roll_check(int a, int d, int n, int *infected)
{
	static GameState g0;
	static TacState t0;
	static RunState r0;
	int ch = tac_hit_chance(a, d, NULL), hits = 0, inf = 0;
	g0 = gs;
	t0 = tac;
	r0 = run;
	for (int i = 0; i < n; i++) {
		u32 rng = gs.rng;
		gs = g0;
		tac = t0;
		run = r0;
		gs.rng = rng;
		int h = fx.hits;
		tac_attack(a, d);
		if (fx.hits != h) {
			hits++;
			if (gs.units[d].side == SIDE_SURVIVOR && (run.roster[tac.actor[d].person].flags & PF_INFECTED))
				inf++;
		}
	}
	int pct10 = hits * 1000 / n; // tenths of a percent
	if (pct10 < (ch - 4) * 10 || pct10 > (ch + 4) * 10)
		FAILF("%d rolls at %d%%: %d.%d%% hit", n, ch, pct10 / 10, pct10 % 10);
	else
		checks++;
	gs = g0;
	tac = t0;
	run = r0;
	if (infected)
		*infected = hits ? inf * 1000 / hits : 0;
}

static void test_stats(void)
{
	test_name = "stats";
	load(&lab, 6, 0x5EED);
	for (int i = 0; i < 6; i++)
		put(slot(i), i, 0);
	int holt = slot(HOLT), mara = slot(MARA), tess = slot(TESS);
	int z = zombie(UT_BRUTE, 4, 5);
	put(holt, 8, 5);
	EQ(tac_hit_chance(holt, z, NULL), 67);
	roll_check(holt, z, 2000, NULL);
	put(holt, 3, 0);
	put(mara, 3, 5);
	EQ(tac_hit_chance(mara, z, NULL), 88);
	roll_check(mara, z, 2000, NULL);
	// a Walker on Tess (speed 5): 50 - 25 = 25; infection 30 - 3*2 = 24% of hits
	int w = zombie(UT_WALKER, 9, 9);
	put(tess, 9, 8);
	EQ(tac_hit_chance(w, tess, NULL), 25);
	set_hp(tess, 99);
	int inf;
	roll_check(w, tess, 4000, &inf);
	if (inf < 200 || inf > 280)
		FAILF("Walker on Tess: %d.%d%% of hits infected, want 24", inf / 10, inf % 10);
	// a Brute on Mara: 20 - 10 (jacket) - 12 (grit) < 5 -> 5%
	put(mara, 4, 6);
	set_hp(mara, 99);
	tac.actor[z].unseen = 1; // 55 - 15 + 25 = 65
	EQ(tac_hit_chance(z, mara, NULL), 65);
	roll_check(z, mara, 4000, &inf);
	if (inf < 30 || inf > 75)
		FAILF("Brute on Mara: %d.%d%% of hits infected, want 5", inf / 10, inf % 10);
	// a hit always infects at most once, and records the bite
	set_hp(tess, 99);
	for (int i = 0; i < 400 && !(run.roster[TESS].flags & PF_INFECTED); i++)
		tac_attack(w, tess);
	CHECK(run.roster[TESS].flags & PF_INFECTED);
	EQ(run.roster[TESS].turn_in, 3 + 1);
	const LogRec *r = last_log(LOG_BITTEN);
	CHECK(r && r->actor == TESS && r->day == 5);
	int nlog = run.log_count;
	for (int i = 0; i < 40; i++)
		tac_attack(w, tess);
	EQ(run.log_count, nlog);
	// the bite timer by difficulty (LAST_BOAT.md 16: +2 Easy, -1 Hard and Very Hard) is set at the
	// bite, by person_bitten(), which a storylet's `infect` uses too
	static const int bite_days[4] = { 2, 0, -1, -1 };
	for (int d = 0; d < 4; d++) {
		run.difficulty = d;
		run.roster[TESS].flags &= ~PF_INFECTED;
		for (int i = 0; i < 400 && !(run.roster[TESS].flags & PF_INFECTED); i++) {
			set_hp(tess, 99);
			tac_attack(w, tess);
		}
		CHECK(run.roster[TESS].flags & PF_INFECTED);
		EQ(run.roster[TESS].turn_in, 3 + 1 + bite_days[d]);
	}
	Person p0 = { .grit = 0 };
	person_bitten(&p0); // Very Hard, Grit 0: 3 - 1
	EQ(p0.turn_in, 2);
	EQ(p0.flags, PF_INFECTED);
	run.difficulty = 1;
}

static void test_attack_effects(void)
{
	test_name = "attack";
	load(&lab, 2, 7);
	int mara = slot(MARA), doc = slot(DOC);
	put(doc, 0, 0);
	int z = zombie(UT_BRUTE, 4, 5), far = zombie(UT_WALKER, 11, 0);
	gs.units[far].dormant = 1;
	tac.actor[far].alerted = 0;
	// Doc's pistol: ammo, noise 6, noisy, firearms XP
	put(doc, 4, 7);
	int ammo = run.res[RES_AMMO], xp = run.roster[DOC].skill_xp[SK_FIREARMS];
	memset(&fx, 0, sizeof fx);
	tac_attack(doc, z);
	EQ(run.res[RES_AMMO], ammo - 1);
	EQ(fx.hits + fx.misses, 1);
	EQ(fx.noises, 1);
	EQ(fx.nr, 6);
	EQ(tac.horde, 6);
	EQ(tac.actor[doc].noisy, 1);
	CHECK(run.roster[DOC].skill_xp[SK_FIREARMS] == xp + (fx.hits ? 2 : 1));
	EQ(tac.actor[far].alerted, 0);           // 14 tiles away
	// fists don't spend ammo; quiet weapons don't make you noisy
	tac.actor[doc].noisy = 0;
	run.res[RES_AMMO] = 0;
	put(doc, 4, 6);
	tac_attack(doc, z);
	EQ(run.res[RES_AMMO], 0);
	EQ(tac.actor[doc].noisy, 0);
	EQ(fx.nr, 1);
	run.res[RES_AMMO] = 20;
	// kill: Mara's machete until it drops
	put(doc, 0, 0);
	put(mara, 3, 5);
	set_hp(z, 4);
	for (int i = 0; i < 100 && gs.units[z].alive; i++)
		tac_attack(mara, z);
	EQ(gs.units[z].alive, 0);
	EQ(run.roster[MARA].kills, 1);
	EQ(run.roster[MARA].xp, 5);
	EQ(tac.actor[z].hp, 0);
	EQ(unit_at(4, 5), -1);
	EQ(count_side(SIDE_HORDE), 1);
	// a wounded zombie wakes up and knows where the blow came from
	int d = tac_spawn_zombie(UT_BRUTE, 4, 5, true);
	int hits = fx.hits;
	for (int i = 0; i < 100 && fx.hits == hits; i++)
		tac_attack(mara, d);
	EQ(gs.units[d].dormant, 0);
	EQ(tac.actor[d].alerted, 1);
	EQ(tac.actor[d].gx, 3);
	EQ(tac.actor[d].gy, 5);
	EQ(tac.actor[d].hp, 12 - 5);             // unaware: +1
	EQ(gs.units[d].hp, 12 - 5);
	// a quiet bolt from 4 tiles: noise 1 doesn't reach it, the wound does
	load(&lab, 3, 8);
	int june = slot(JUNE);
	put(slot(MARA), 0, 0);
	put(slot(DOC), 1, 0);
	put(june, 4, 9);
	d = tac_spawn_zombie(UT_BRUTE, 4, 5, true);
	hits = fx.hits;
	for (int i = 0; i < 100 && fx.hits == hits; i++)
		tac_attack(june, d);
	EQ(fx.nr, 1);
	EQ(tac.actor[june].noisy, 0);
	EQ(gs.units[d].dormant, 0);
	EQ(tac.actor[d].alerted, 1);
	EQ(tac.actor[d].gx, 4);
	EQ(tac.actor[d].gy, 9);
	EQ(tac.actor[d].hp, 12 - 4);             // crossbow 3, unaware +1
	check_sync();
}

static void test_noise(void)
{
	test_name = "noise";
	load(&lab_waves, 2, 99);
	int mara = slot(MARA), doc = slot(DOC);
	int z1 = tac_spawn_zombie(UT_WALKER, 6, 3, true);   // 3 from (6,6)
	int z2 = tac_spawn_zombie(UT_WALKER, 8, 4, true);   // 4
	int z3 = tac_spawn_zombie(UT_WALKER, 10, 5, true);  // 5
	tac_noise(6, 6, 4);
	EQ(tac.horde, 4);
	EQ(fx.noises, 1);
	EQ(gs.units[z1].dormant, 0);
	EQ(tac.actor[z1].alerted, 1);
	EQ(tac.actor[z1].gx, 6);
	EQ(tac.actor[z1].gy, 6);
	EQ(tac.actor[z2].alerted, 1);
	EQ(gs.units[z3].dormant, 1);
	EQ(tac.actor[z3].alerted, 0);
	EQ(count_side(SIDE_HORDE), 3);
	tac_noise(6, 6, 0);                                 // silence
	EQ(tac.horde, 4);
	EQ(fx.noises, 1);
	tac_noise(6, 6, 1);                                 // 5: first wave
	EQ(tac.wave, 1);
	EQ(fx.texts, 1);
	EQ(count_side(SIDE_HORDE), 6);
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		if (!u->alive || u->side != SIDE_HORDE || i == z1 || i == z2 || i == z3)
			continue;
		CHECK(u->x == 0 || u->y == 0 || u->x == gs.w - 1 || u->y == gs.h - 1);
		CHECK(tac_move_cost(u->x, u->y, true) != IMPASSABLE);
		CHECK(!tac_in_bldg(u->x, u->y));
		CHECK(!(tac.zone[u->y][u->x] & (ZN_DEPLOY | ZN_EXIT)));
		CHECK(abs(u->x - gs.units[mara].x) + abs(u->y - gs.units[mara].y) > 4);
		CHECK(abs(u->x - gs.units[doc].x) + abs(u->y - gs.units[doc].y) > 4);
		CHECK(u->type == UT_WALKER || u->type == UT_RUNNER);
		EQ(tac.actor[i].alerted, 1);
		EQ(tac.actor[i].gx, 6);
		EQ(tac.actor[i].gy, 6);
		EQ(u->moved, 1);
		EQ(u->dormant, 0);
	}
	tac_noise(6, 6, 20);                                // 25: waves 2 and 3 at once
	EQ(tac.wave, 3);
	EQ(fx.texts, 3);
	EQ(count_side(SIDE_HORDE), 12);
	tac_noise(6, 6, 30);                                // no more waves
	EQ(tac.wave, 3);
	EQ(count_side(SIDE_HORDE), 12);
	EQ(gs.units[z3].dormant, 0);
	for (int i = 0; i < 4; i++)
		tac_noise(6, 6, 30000);
	EQ(tac.horde, 65535);
	check_sync();
	// waves keep 5+ tiles from every person and off the caravan; a crowded edge spawns fewer
	load(&lab_waves, 6, 3);
	for (int i = 0; i < 6; i++)
		put(slot(i), 2 + 2 * (i % 3), 1 + 8 * (i / 3) - (i / 3)); // (2,1) (4,1) (6,1) (2,8) (4,8) (6,8)
	tac_noise(5, 5, 15);
	EQ(tac.wave, 3);
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		if (!u->alive || u->side != SIDE_HORDE)
			continue;
		for (int s = 0; s < 6; s++) {
			Unit *p = &gs.units[slot(s)];
			CHECK(abs(u->x - p->x) + abs(u->y - p->y) > 4);
		}
	}
	CHECK(count_side(SIDE_HORDE) <= 9);
	check_sync();
	// a huge wave fills exactly the legal edge tiles: passable, no roof, not the caravan or an exit,
	// 5+ from people
	load(&lab_flood, 2, 4);
	put(slot(MARA), 6, 4);
	put(slot(DOC), 6, 6);
	tac_noise(6, 5, 1);
	EQ(tac.wave, 1);
	EQ(fx.texts, 1);
	int want = 0;
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++) {
			if (x && y && x != gs.w - 1 && y != gs.h - 1)
				continue;
			bool legal = tac_move_cost(x, y, true) != IMPASSABLE && !tac_in_bldg(x, y) &&
			             !(tac.zone[y][x] & (ZN_DEPLOY | ZN_EXIT)) && abs(x - 6) + abs(y - 4) > 4 &&
			             abs(x - 6) + abs(y - 6) > 4;
			int u = unit_at(x, y);
			want += legal;
			if (legal != (u >= 0 && gs.units[u].side == SIDE_HORDE))
				FAILF("flood wave: edge tile (%d,%d) legal %d, zombie %d", x, y, legal, u >= 0);
		}
	CHECK(want > 10);
	EQ(count_side(SIDE_HORDE), want);
	check_sync();
}

static void test_downed(void)
{
	test_name = "downed";
	load(&lab, 2, 11);
	int mara = slot(MARA), doc = slot(DOC);
	put(mara, 4, 6);
	put(doc, 9, 3);
	int w = zombie(UT_WALKER, 4, 5);
	set_hp(mara, 1);
	for (int i = 0; i < 200 && !tac.actor[mara].down; i++)
		tac_attack(w, mara);
	EQ(tac.actor[mara].down, 2);
	EQ(tac.actor[mara].hp, 0);
	EQ(gs.units[mara].hp, 0);
	EQ(gs.units[mara].alive, 1);
	EQ(run.roster[MARA].alive, 1);
	EQ(tac_survivors_left(), 2);
	EQ(tac_paths(mara, dist, prev), 1);
	EQ(tac_can_search(mara), false);
	EQ(tac_barricade_target(mara), -1);
	put(mara, 0, 7);                          // on the caravan, but can't walk out
	tac_extract(mara);
	EQ(gs.units[mara].alive, 1);
	put(mara, 4, 6);
	tac_end_survivor_phase();
	EQ(tac.actor[mara].down, 1);
	EQ(gs.units[mara].alive, 1);
	EQ(tac.turn, 1);
	tac_end_survivor_phase();                 // bled out
	EQ(gs.units[mara].alive, 0);
	EQ(run.roster[MARA].alive, 0);
	EQ(run.roster[MARA].hp, 0);
	EQ(unit_at(4, 6), -1);                    // no zombie rises
	const LogRec *r = last_log(LOG_DEATH);
	CHECK(r && r->actor == MARA && r->arg == 0);
	EQ(tac.result, 0);                        // Doc is still out there
	EQ(tac_survivors_left(), 1);
	// a zombie on a downed person kills them, and they rise as a Walker next horde turn
	put(doc, 4, 6);
	set_hp(doc, 1);
	tac.actor[doc].carry[RES_FOOD] = 2;
	for (int i = 0; i < 200 && gs.units[doc].alive; i++)
		tac_attack(w, doc);
	EQ(gs.units[doc].alive, 0);
	EQ(run.roster[DOC].alive, 0);
	r = last_log(LOG_DEATH);
	CHECK(r && r->actor == DOC && r->arg == 1);
	int risen = unit_at(4, 6);
	CHECK(risen >= 0 && risen != doc && risen != mara);
	if (risen >= 0) {
		EQ(gs.units[risen].type, UT_WALKER);
		EQ(gs.units[risen].side, SIDE_HORDE);
		EQ(gs.units[risen].moved, 1);
		EQ(tac.actor[risen].moved, 1);
		EQ(tac.actor[risen].alerted, 1);
		EQ(tac.actor[risen].person, 0xFF);
		EQ(tac.actor[risen].hp, 4);
	}
	EQ(tac.actor[doc].person, DOC);           // the record stays in Doc's slot
	EQ(tac.result, 2);                        // nobody left, nobody out
	CHECK(tac.cache[6][4] >= CACHE_PACK);     // Doc's food lies under the risen Walker
	check_sync();
}

static void test_search(void)
{
	test_name = "search";
	for (int mi = 0; mi < tac_mission_count; mi++) {
		load(&tac_missions[mi], 1, 1234 + mi);
		int mara = slot(MARA), n = 0;
		for (int b = 0; b < tac.nbldg; b++) {
			const Building *bd = &tac.bldg[b];
			for (int y = bd->y; y < bd->y + bd->h; y++)
				for (int x = bd->x; x < bd->x + bd->w; x++) {
					put(mara, x, y);
					memset(tac.actor[mara].carry, 0, RES_COUNT);
					int horde = tac.horde, noises = fx.noises;
					CHECK(tac_can_search(mara));
					int got = tac_search(mara);
					if (got <= 0)
						FAILF("%s: search at (%d,%d) found nothing", tac_missions[mi].title, x, y);
					EQ(carry_sum(mara), got);
					CHECK(!tac_can_search(mara));
					EQ(tac_search(mara), 0);
					EQ(carry_sum(mara), got);
					EQ(tac.horde, horde);         // silent
					EQ(fx.noises, noises);
					n++;
				}
		}
		CHECK(n > 0);
		EQ(run.roster[MARA].skill_xp[SK_SCAVENGE], MIN(255, 2 * n));
	}
	// Main Street's car trunk sits on a wreck: searched from next to it, once
	load(&tac_missions[0], 1, 77);
	int mara = slot(MARA);
	EQ(tac.cache[4][16], CACHE_TRUNK);
	EQ(gs.terrain[4][16], TT_WRECK);
	put(mara, 15, 3);
	EQ(tac_can_search(mara), false);          // diagonal
	put(mara, 16, 5);
	EQ(tac_can_search(mara), true);
	int got = tac_search(mara);
	CHECK(got >= 1 && got <= 3);
	EQ(tac.cache[4][16], 0);
	EQ(tac_can_search(mara), false);
	// loot by building kind
	load(&lab, 1, 5);
	mara = slot(MARA);
	int food = 0, parts = 0, fuel = 0, other = 0;
	for (int i = 0; i < 400; i++) {
		tac.bldg[3].searched = 0;            // the garage, over and over
		put(mara, 10, 8);
		memset(tac.actor[mara].carry, 0, RES_COUNT);
		tac_search(mara);
		parts += tac.actor[mara].carry[RES_PARTS];
		fuel += tac.actor[mara].carry[RES_FUEL];
		other += tac.actor[mara].carry[RES_FOOD] + tac.actor[mara].carry[RES_MEDS] + tac.actor[mara].carry[RES_AMMO];
		tac.bldg[1].searched = 0;            // the shop
		put(mara, 6, 1);
		memset(tac.actor[mara].carry, 0, RES_COUNT);
		tac_search(mara);
		food += tac.actor[mara].carry[RES_FOOD];
		other += tac.actor[mara].carry[RES_AMMO] + tac.actor[mara].carry[RES_MEDS] + tac.actor[mara].carry[RES_FUEL];
	}
	CHECK(parts >= 400 && fuel > 0 && food >= 800);
	EQ(other, 0);
	// carry saturates
	tac.bldg[1].searched = 0;
	tac.actor[mara].carry[RES_FOOD] = 254;
	CHECK(tac_search(mara) >= 2);
	EQ(tac.actor[mara].carry[RES_FOOD], 255);
	// a dead friend's pack: all of it, once
	load(&lab, 2, 6);
	mara = slot(MARA);
	int doc = slot(DOC);
	put(mara, 3, 3);
	put(doc, 6, 6);
	tac.actor[doc].carry[RES_FOOD] = 2;
	tac.actor[doc].carry[RES_AMMO] = 1;
	int w = zombie(UT_WALKER, 6, 5);
	tac_kill(doc, w);
	int risen = unit_at(6, 6);
	CHECK(risen >= 0);
	int c = tac.cache[6][6];
	CHECK(c >= CACHE_PACK && c < CACHE_PACK + TAC_MAX_PACKS);
	if (c >= CACHE_PACK && c < CACHE_PACK + TAC_MAX_PACKS) {
		EQ(tac.pack[c - CACHE_PACK].used, 1);
		EQ(tac.pack[c - CACHE_PACK].res[RES_FOOD], 2);
	}
	EQ(carry_sum(doc), 0);
	put(mara, 6, 7);
	EQ(tac_can_search(mara), false);          // the risen Walker stands on it
	tac_kill(risen, mara);
	EQ(tac_can_search(mara), true);           // next to it now
	memset(tac.actor[mara].carry, 0, RES_COUNT);
	EQ(tac_search(mara), 3);
	EQ(tac.actor[mara].carry[RES_FOOD], 2);
	EQ(tac.actor[mara].carry[RES_AMMO], 1);
	EQ(tac.cache[6][6], 0);
	if (c >= CACHE_PACK && c < CACHE_PACK + TAC_MAX_PACKS)
		EQ(tac.pack[c - CACHE_PACK].used, 0);
	EQ(tac_can_search(mara), false);
	// a pack on your own tile, even on an unsearched building tile: the pack first
	put(mara, 3, 3);
	tac.actor[mara].carry[RES_FUEL] = 1;
	tac_kill(mara, -1);
	EQ(tac.cache[3][3] >= CACHE_PACK, 1);
	load(&lab, 2, 9);
	mara = slot(MARA);
	doc = slot(DOC);
	put(doc, 2, 1);
	tac.actor[doc].carry[RES_FUEL] = 2;
	tac_kill(doc, -1);
	EQ(unit_at(2, 1), -1);
	put(mara, 2, 1);
	EQ(tac_can_search(mara), true);
	memset(tac.actor[mara].carry, 0, RES_COUNT);
	EQ(tac_search(mara), 2);
	EQ(tac.actor[mara].carry[RES_FUEL], 2);
	EQ(tac.cache[1][2], 0);
	EQ(tac.bldg[0].searched, 0);              // the room itself is still there to search
	EQ(tac_can_search(mara), true);
	CHECK(tac_search(mara) > 0);
	EQ(tac.bldg[0].searched, 1 << 1);
	EQ(tac_can_search(mara), false);
	// nothing to search out in the open
	put(mara, 3, 3);
	EQ(tac_search(mara), 0);
	EQ(tac_can_search(-1), false);
}

static void test_barricade(void)
{
	test_name = "barricade";
	load(&lab, 1, 8);
	int mara = slot(MARA);
	put(mara, 2, 2);
	EQ(tac_barricade_target(mara), -1);
	put(mara, 2, 3);
	EQ(tac_barricade_target(mara), -1);
	put(mara, 1, 2);
	EQ(tac_barricade_target(mara), 0);       // inside, on the door
	put(mara, 1, 3);
	EQ(tac_barricade_target(mara), 0);       // outside the door
	run.res[RES_PARTS] = 0;
	EQ(tac_barricade_target(mara), -1);
	tac_barricade(mara);
	EQ(tac.bldg[0].door_hp, 0);
	run.res[RES_PARTS] = 2;
	tac_barricade(mara);
	EQ(tac.bldg[0].door_hp, 6);
	EQ(run.res[RES_PARTS], 1);
	EQ(tac.actor[mara].noisy, 1);
	EQ(tac.horde, 3);
	EQ(fx.noises, 1);
	EQ(fx.nr, 3);
	EQ(run.roster[MARA].skill_xp[SK_MECHANICS], 2);
	EQ(tac_barricade_target(mara), -1);      // already done
	put(mara, 8, 10);
	EQ(tac_barricade_target(mara), 2);       // house B from outside
	put(mara, 11, 9);
	EQ(tac_barricade_target(mara), 3);       // the garage from inside
	EQ(tac_barricade_target(-1), -1);
}

static void test_objectives(void)
{
	test_name = "scavenge";
	load(&lab, 3, 21);
	int mara = slot(MARA), doc = slot(DOC), june = slot(JUNE);
	tac.actor[mara].carry[RES_FOOD] = 3;
	set_hp(mara, 5);
	put(mara, 3, 3);
	tac_extract(mara);                        // not on a zone
	EQ(gs.units[mara].alive, 1);
	put(mara, 0, 7);
	tac_extract(mara);
	EQ(gs.units[mara].alive, 0);
	EQ(tac.actor[mara].extracted, 2);
	EQ(tac.loot[RES_FOOD], 3);
	EQ(carry_sum(mara), 0);
	EQ(run.roster[MARA].hp, 5);
	EQ(run.roster[MARA].alive, 1);
	EQ(tac.through, 0);
	EQ(tac.result, 0);
	put(doc, 10, 10);
	tac.actor[doc].carry[RES_MEDS] = 1;
	tac_extract(doc);
	EQ(tac.actor[doc].extracted, 1);
	EQ(tac.through, 1);
	EQ(tac.loot[RES_MEDS], 1);
	EQ(tac.result, 0);
	tac_kill(june, -1);
	EQ(tac.result, 1);                        // loot made it out
	EQ(tac_survivors_left(), 0);
	tac_extract(june);                        // dead: nothing
	EQ(tac.through, 1);

	// Main Street, everyone straight back to the caravan on turn 1: not a win (+1 morale for
	// nothing, a free way past the stop); with something in a pack it is
	for (int got = 0; got < 2; got++) {
		load(&tac_missions[0], 4, 31 + got);
		EQ(tac_missions[0].objective, OBJ_SCAVENGE);
		tac.actor[slot(DOC)].carry[RES_MEDS] = got;
		for (int r = 0; r < 4; r++)
			tac_extract(slot(r));             // they start on deploy tiles
		EQ(tac_survivors_left(), 0);
		EQ(tac.turn, 0);
		EQ(tac.loot[RES_MEDS], got);
		EQ(tac.result, got ? 1 : 2);
	}

	load(&lab, 2, 22);
	tac_kill(slot(MARA), -1);
	EQ(tac.result, 0);
	tac_kill(slot(DOC), -1);
	EQ(tac.result, 2);

	test_name = "get through";
	load(&lab_through, 3, 23);
	mara = slot(MARA), doc = slot(DOC), june = slot(JUNE);
	put(mara, 10, 10);
	tac_extract(mara);
	EQ(tac.through, 1);
	EQ(tac.result, 0);
	tac.actor[june].down = 1;                 // downed: carried out by the caravan
	set_hp(june, 0);
	tac.actor[june].carry[RES_PARTS] = 2;
	put(doc, 11, 10);
	tac_end_survivor_phase();                 // her last turn ran out: she bleeds out
	EQ(gs.units[june].alive, 0);
	EQ(run.roster[JUNE].alive, 0);
	EQ(tac.result, 0);

	load(&lab_through, 3, 24);
	mara = slot(MARA), doc = slot(DOC), june = slot(JUNE);
	put(mara, 10, 10);
	tac_extract(mara);
	tac.actor[june].down = 2;
	set_hp(june, 0);
	tac.actor[june].carry[RES_PARTS] = 2;
	put(doc, 11, 10);
	tac_extract(doc);
	EQ(tac.through, 2);
	EQ(tac.result, 1);
	EQ(gs.units[june].alive, 0);              // auto-extracted
	EQ(tac.actor[june].extracted, 2);
	EQ(tac.actor[june].down, 0);
	EQ(run.roster[JUNE].hp, 1);
	EQ(run.roster[JUNE].alive, 1);
	EQ(tac.loot[RES_PARTS], 2);
	EQ(tac_survivors_left(), 0);

	load(&lab_through, 2, 25);                // fail: one through, one dead
	put(slot(MARA), 10, 10);
	tac_extract(slot(MARA));
	tac_kill(slot(DOC), -1);
	EQ(tac.result, 2);

	load(&lab_through, 2, 26);                // fail: everyone turned back
	tac_extract(slot(MARA));
	tac_extract(slot(DOC));
	EQ(tac.through, 0);
	EQ(tac.result, 2);

	test_name = "bridge";
	load(&span, 3, 27);
	mara = slot(MARA), doc = slot(DOC), june = slot(JUNE);
	put(mara, 4, 1);
	tac_end_survivor_phase();
	EQ(tac.held, 1);
	EQ(tac.turn, 1);
	int z = zombie(UT_WALKER, 3, 1);
	tac_end_survivor_phase();
	EQ(tac.held, 1);                          // contested
	tac_kill(z, mara);
	tac.actor[mara].down = 2;                 // a downed person holds nothing
	set_hp(mara, 0);
	tac_end_survivor_phase();
	EQ(tac.held, 1);
	EQ(tac.actor[mara].down, 1);
	put(doc, 5, 1);
	tac_end_survivor_phase();                 // Mara bleeds out, Doc holds
	EQ(run.roster[MARA].alive, 0);
	EQ(tac.held, 2);
	EQ(tac.result, 1);
	EQ(tac.actor[doc].extracted, 2);
	EQ(tac.actor[june].extracted, 2);
	EQ(tac_survivors_left(), 0);
	tac_end_survivor_phase();                 // decided: nothing changes
	EQ(tac.result, 1);
	EQ(tac.held, 2);

	load(&span, 2, 28);                       // fail: retreat
	tac_extract(slot(MARA));
	EQ(tac.result, 0);
	tac_extract(slot(DOC));
	EQ(tac.result, 2);
	EQ(tac.actor[slot(DOC)].extracted, 2);
}

static void test_sync(void)
{
	test_name = "sync";
	load(&lab, 4, 31);
	int s[4];
	for (int i = 0; i < 4; i++) {
		s[i] = slot(i);
		EQ(s[i], i);                          // people first, in deploy order
	}
	int z[5];
	for (int i = 0; i < 5; i++)
		z[i] = zombie(UT_WALKER, 3 + i, 3);
	EQ(z[0], 4);
	tac_kill(z[1], -1);
	put(s[DOC], 7, 4);
	tac_kill(s[DOC], z[4]);                   // Doc rises into the free zombie slot, not his own
	int risen = unit_at(7, 4);
	EQ(risen, z[1]);
	EQ(tac.actor[s[DOC]].person, DOC);
	put(s[JUNE], 0, 9);
	tac.actor[s[JUNE]].carry[RES_AMMO] = 4;
	tac_extract(s[JUNE]);
	int nz = count_side(SIDE_HORDE), spawned = 0;
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++)
			if (tac_move_cost(x, y, true) != IMPASSABLE && unit_at(x, y) < 0 && tac_spawn_zombie(UT_WALKER, x, y, false) >= 0)
				spawned++;
	EQ(count_side(SIDE_HORDE), nz + spawned);
	EQ(nz + spawned, MAX_UNITS - 4);          // every slot but the four people's
	EQ(tac.actor[s[DOC]].person, DOC);
	EQ(tac.actor[s[JUNE]].person, JUNE);
	EQ(tac.actor[s[JUNE]].extracted, 2);
	EQ(tac.loot[RES_AMMO], 4);
	EQ(gs.units[s[DOC]].alive, 0);
	EQ(gs.units[s[JUNE]].alive, 0);
	int fx_ = -1, fy_ = -1;
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++)
			if (fx_ < 0 && tac_move_cost(x, y, true) != IMPASSABLE && unit_at(x, y) < 0) {
				fx_ = x;
				fy_ = y;
			}
	CHECK(fx_ >= 0);
	EQ(tac_spawn_zombie(UT_WALKER, fx_, fy_, false), -1); // no slot left
	check_sync();
	// kill half of them and refill: actors reset every time
	for (int i = 0; i < MAX_UNITS; i += 2)
		if (gs.units[i].alive && gs.units[i].side == SIDE_HORDE) {
			tac.actor[i].alerted = 1;
			tac.actor[i].unseen = 1;
			tac_kill(i, s[MARA]);
		}
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++)
			if (tac_move_cost(x, y, true) != IMPASSABLE && unit_at(x, y) < 0)
				tac_spawn_zombie(UT_RUNNER, x, y, true);
	for (int i = 0; i < MAX_UNITS; i++)
		if (gs.units[i].alive && gs.units[i].type == UT_RUNNER) {
			EQ(tac.actor[i].alerted, 0);
			EQ(tac.actor[i].unseen, 0);
			EQ(tac.actor[i].hp, 3);
		}
	check_sync();
	EQ(tac_survivors_left(), 2);
}

// SLICE_NUMBERS.md: flank only against cover facing elsewhere (N3), +1 Horde a round (N5), the
// Horde meter scaled by difficulty (LAST_BOAT.md 16), doors facing out by position (R4).
// Rounds of +1 until Main Street's first wave (12) with no noise at all, Easy..Very Hard
static int quiet_wave_round(int difficulty)
{
	load(&tac_missions[0], 4, 8);
	run.difficulty = difficulty;
	for (int r = 1; r <= 40; r++) {
		tac_end_survivor_phase();
		if (tac.wave)
			return r;
	}
	return 0;
}

static const char *const door_rows[] = {
	"..........",
	".hH..HhH..",   // door top-left of a 2x2 faces N; top-middle of a 3x2 faces N
	".HH..HHH..",
	"..........",
	".HH..HHh..",   // door top-right of a 3x2 faces N; no door letter: bottom-left, S
	".HH..HHH..",
	"..........",
	"oo........",
	"oo........",
	NULL,
};
static const TacMission doors = { "Doors", "", door_rows, no_z, OBJ_SCAVENGE, 0, 0, { 0, 0, 0 }, 3 };

static void test_slice_rules(void)
{
	test_name = "slice rules";
	// N3: Doc's pistol at range 3 on a walker in the open: no flank bonus (50 + 16 - 10 = 56), 3 dmg
	load(&lab, 6, 5);
	for (int i = 0; i < 6; i++)
		put(slot(i), i, 0);
	int doc = slot(DOC), z = zombie(UT_WALKER, 3, 7);  // (3,7): open on all four sides
	put(doc, 3, 4);
	int cover = -1;
	EQ(tac_hit_chance(doc, z, &cover), 50 + 16 - 10);
	EQ(cover, 0);
	EQ(tac_flanked(doc, z), false);
	EQ(tac_damage(doc, z), 3);
	tac.actor[z].alerted = 0;                          // unaware still adds +25, +1
	EQ(tac_hit_chance(doc, z, NULL), 50 + 16 - 10 + 25);
	EQ(tac_damage(doc, z), 4);
	tac.actor[z].alerted = 1;
	// a walker behind the wreck at (4,4), shot from the south: flanked (+15, +1)
	tac_kill(z, -1);
	z = zombie(UT_WALKER, 4, 5);
	put(doc, 4, 8);
	EQ(tac_flanked(doc, z), true);
	EQ(tac_hit_chance(doc, z, &cover), 50 + 16 - 10 + 15);
	EQ(tac_damage(doc, z), 4);
	put(doc, 4, 2);                                    // from the north: the wreck covers it
	EQ(tac_flanked(doc, z), false);
	EQ(tac_hit_chance(doc, z, &cover), 50 + 16 - 10 - 20);
	EQ(cover, 20);
	// point blank from the woods at (5,7): the walker's only cover is the woods edge Doc stands
	// behind, so no flank (cover is ignored point blank): 66, 3 damage
	tac_kill(z, -1);
	z = zombie(UT_WALKER, 6, 7);
	put(doc, 5, 7);
	EQ(tac_weapon(doc, z), WP_PISTOL);
	EQ(tac_flanked(doc, z), false);
	EQ(tac_hit_chance(doc, z, &cover), 50 + 16);
	EQ(cover, 0);
	EQ(tac_damage(doc, z), 3);
	put(doc, 6, 6);                                    // from the open north side: flanked, 81
	EQ(tac_flanked(doc, z), true);
	EQ(tac_hit_chance(doc, z, NULL), 50 + 16 + 15);
	EQ(tac_damage(doc, z), 4);

	// N5: +1 a round
	load(&lab_waves, 2, 6);
	tac_end_survivor_phase();
	EQ(tac.horde, 1);
	tac_end_survivor_phase();
	EQ(tac.horde, 2);
	tac_noise(6, 6, 3);
	EQ(tac.horde, 5);                                  // Normal: x1; wave 1 at 5
	EQ(tac.wave, 1);
	// every fill is scaled by difficulty, x0.75 x1 x1.25 x1.5, the quarters carried to the next
	// fill: the round tick, quiet melee (noise 1-2) and a barricade (3) all scale
	static const u8 scale4[4] = { 3, 4, 5, 6 };
	for (int d = 0; d < 4; d++) {
		load(&lab, 2, 6);
		run.difficulty = d;
		for (int r = 0; r < 4; r++)
			tac_end_survivor_phase();
		EQ(tac.horde, scale4[d]);                      // 4 rounds
		for (int k = 0; k < 4; k++)
			tac_noise(6, 6, 1);
		EQ(tac.horde, 2 * scale4[d]);                  // 4 knife strikes
		tac_noise(6, 6, 2);
		tac_noise(6, 6, 2);
		EQ(tac.horde, 3 * scale4[d]);                  // 2 machete strikes
		tac_noise(6, 6, 3);
		tac_noise(6, 6, 3);
		tac_noise(6, 6, 3);
		tac_noise(6, 6, 3);
		EQ(tac.horde, 6 * scale4[d]);                  // 4 barricades
	}
	load(&lab, 2, 6);
	run.difficulty = 0;
	tac_noise(6, 6, 1);
	EQ(tac.horde, 0);                                  // Easy: 0.75 of a point, kept
	tac_noise(6, 6, 1);
	EQ(tac.horde, 1);
	load(&lab, 2, 6);
	run.difficulty = 2;
	tac_noise(6, 6, 9);
	EQ(tac.horde, 11);                                 // Hard: a shotgun, 11.25
	// a quiet team on Main Street meets wave 1 in round 16 / 12 / 10 / 8 by difficulty
	EQ(quiet_wave_round(0), 16);
	EQ(quiet_wave_round(1), 12);
	EQ(quiet_wave_round(2), 10);
	EQ(quiet_wave_round(3), 8);
	// the round tick stops once the mission is decided, and with nobody left
	load(&lab_waves, 1, 6);
	tac_extract(slot(MARA));
	CHECK(tac.result != 0);
	int h = tac.horde;
	tac_end_survivor_phase();
	EQ(tac.horde, h);

	// R4: doors face out by their place in the footprint
	load(&doors, 4, 7);
	EQ(tac.nbldg, 4);
	for (int i = 0; i < tac.nbldg; i++) {
		const Building *b = &tac.bldg[i];
		int want = b->door_y == b->y + b->h - 1 ? 2 : 0;
		EQ(b->door_dir, want);
		CHECK(tac_edge_open(b->door_x, b->door_y, b->door_dir, false));
	}
	check_sync();
}

// LAST_BOAT.md 9.1: on a Get through map the waves come from behind (never nearer the exits than
// the person nearest them) while there is room; no wave lands on an exit.
static int exit_dist(int x, int y)
{
	int d = 999;
	for (int ey = 0; ey < gs.h; ey++)
		for (int ex = 0; ex < gs.w; ex++)
			if (tac.zone[ey][ex] & ZN_EXIT)
				d = MIN(d, abs(x - ex) + abs(y - ey));
	return d;
}

static int wave_check(const u8 (*team)[2], int nteam, u32 seed, bool behind)
{
	load(&tac_missions[2], 4, seed);                   // Hensley Farm: exits on the east edge
	for (int i = 0; i < MAX_UNITS; i++)
		if (gs.units[i].alive && gs.units[i].side == SIDE_HORDE)
			tac_kill(i, -1);
	int lead = 999;
	for (int i = 0; i < nteam; i++) {
		put(slot(i), team[i][0], team[i][1]);
		lead = MIN(lead, exit_dist(team[i][0], team[i][1]));
	}
	tac_noise(team[0][0], team[0][1], 10);             // a rifle shot: wave 1
	EQ(tac.wave, 1);
	int n = 0;
	for (int i = 0; i < MAX_UNITS; i++) {
		const Unit *u = &gs.units[i];
		if (!u->alive || u->side != SIDE_HORDE)
			continue;
		n++;
		if (tac.zone[u->y][u->x] & ZN_EXIT)
			FAILF("seed %u: a wave zombie on the exit (%d,%d)", seed, u->x, u->y);
		else if (behind && exit_dist(u->x, u->y) < lead)
			FAILF("seed %u: a wave zombie ahead at (%d,%d), %d from the exits, the team %d", seed, u->x,
			      u->y, exit_dist(u->x, u->y), lead);
		else
			checks++;
	}
	return n;
}

static void test_wave_sides(void)
{
	test_name = "wave sides";
	static const u8 mid[4][2] = { { 10, 4 }, { 11, 5 }, { 12, 4 }, { 11, 7 } };
	static const u8 start[4][2] = { { 0, 4 }, { 1, 1 }, { 1, 8 }, { 0, 5 } };
	for (u32 seed = 1; seed <= 200; seed++) {
		EQ(wave_check(mid, 4, seed, true), tac_missions[2].wave_size);
		// at the start every tile behind them is too close: the wave still comes
		EQ(wave_check(start, 4, seed, false), tac_missions[2].wave_size);
	}
	// other objectives keep waves off the exits too (Main Street's are on the west edge)
	for (u32 seed = 1; seed <= 50; seed++) {
		load(&tac_missions[0], 2, seed);
		put(slot(MARA), 10, 6);
		put(slot(DOC), 12, 6);
		tac_noise(10, 6, 12);
		EQ(tac.wave, 1);
		for (int i = 0; i < MAX_UNITS; i++)
			if (gs.units[i].alive && gs.units[i].side == SIDE_HORDE)
				CHECK(!(tac.zone[gs.units[i].y][gs.units[i].x] & ZN_EXIT));
	}
}

int main(void)
{
	srand(1);
	test_load_basics();
	test_real_maps();
	test_edges();
	test_paths();
	test_cover();
	test_los();
	test_sees();
	test_weapons();
	test_stats();
	test_attack_effects();
	test_noise();
	test_downed();
	test_search();
	test_barricade();
	test_objectives();
	test_sync();
	test_slice_rules();
	test_wave_sides();
	printf("tac rules: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
