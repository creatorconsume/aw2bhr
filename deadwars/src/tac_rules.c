// Dead Wars: The Last Boat - tactical rules (LAST_BOAT.md section 8). No graphics: feedback goes
// through tac_fx_*. Host-testable (test/unit). tac.c runs the flow, tac_ai.c the horde turn.
//
// Slots: tac.actor[i] is parallel to gs.units[i]. A deployed person's slot is never reused during
// a mission (zombies only spawn into slots with actor.person == 0xFF), so actor[] keeps every
// deployed person's record (extracted, down, carry) after the unit leaves the map.
#include "lb.h"
#include <string.h>

TacState tac EWRAM_BSS;

#define IDX(x, y) ((y) * MAP_MAX_W + (x))
#define NO_PATH 255
#define RING (MAP_MAX_W * MAP_MAX_H) // power of two
#define DOOR_HP 6

static const s8 dirx[4] = { 0, 1, 0, -1 }, diry[4] = { -1, 0, 1, 0 };

static inline bool inb(int x, int y)
{
	return (unsigned)x < (unsigned)gs.w && (unsigned)y < (unsigned)gs.h;
}

static inline int mdist(int x0, int y0, int x1, int y1)
{
	return ABS(x0 - x1) + ABS(y0 - y1);
}

static inline int udist(int a, int b)
{
	return mdist(gs.units[a].x, gs.units[a].y, gs.units[b].x, gs.units[b].y);
}

static bool live(int i, int side)
{
	return (unsigned)i < MAX_UNITS && gs.units[i].alive && gs.units[i].side == side;
}

static Person *person_of(int i)
{
	return &run.roster[tac.actor[i].person < ROSTER_MAX ? tac.actor[i].person : 0];
}

static inline void set_hp(int i, int hp)
{
	tac.actor[i].hp = gs.units[i].hp = hp;
}

static inline u8 sat8(int v)
{
	return v > 255 ? 255 : v;
}

// direction k from (x0,y0) to an orthogonal neighbour (x1,y1), or -1
static int dir_to(int x0, int y0, int x1, int y1)
{
	for (int k = 0; k < 4; k++)
		if (x0 + dirx[k] == x1 && y0 + diry[k] == y1)
			return k;
	return -1;
}

static bool door_edge(const Building *b, int x, int y, int k)
{
	return b->door_x == x && b->door_y == y && b->door_dir == k;
}

// adjacent and the shared edge is open for the attacker (melee)
static bool can_reach(int a, int d, bool zombie)
{
	Unit *ua = &gs.units[a], *ud = &gs.units[d];
	int k = dir_to(ua->x, ua->y, ud->x, ud->y);
	return k >= 0 && tac_edge_open(ua->x, ua->y, k, zombie);
}

static void wake(int z, int x, int y)
{
	Actor *ac = &tac.actor[z];
	gs.units[z].dormant = 0;
	ac->alerted = 1;
	ac->gx = x;
	ac->gy = y;
}

// ---------------------------------------------------------------- loading
static u8 bchar[MAP_MAX_H][MAP_MAX_W] EWRAM_BSS; // building letter (upper case), | 0x80 = door

static int bkind(int c)
{
	return c == 'S' ? BK_SHOP : c == 'G' ? BK_GARAGE : c == 'P' ? BK_PHARMACY : BK_HOUSE;
}

static void legend(int x, int y, int c)
{
	u8 *tt = &gs.terrain[y][x];
	switch (c) {
	case 'f': *tt = TT_FOREST; break;
	case '^': *tt = TT_HILL; break;
	case '~': *tt = TT_WATER; break;
	case '=': *tt = TT_BRIDGE; tac.zone[y][x] = ZN_BRIDGE; break;
	case 'w': *tt = TT_WRECK; break;
	case '#': *tt = TT_WALL; break;
	case '-': *tt = TT_ROAD; break;
	case 'r': *tt = TT_RUINS; break;
	case 'n': *tt = TT_NEST; break;
	case 'c': *tt = TT_WRECK; tac.cache[y][x] = CACHE_TRUNK; break;
	case 'x': *tt = TT_ROAD; tac.zone[y][x] = ZN_EXIT; break;
	case 'X': *tt = TT_PLAIN; tac.zone[y][x] = ZN_EXIT; break;
	case 'o': *tt = TT_PLAIN; tac.zone[y][x] = ZN_DEPLOY; break;
	case 'H': case 'S': case 'G': case 'P':
		*tt = TT_HOUSE; bchar[y][x] = c; break;
	case 'h': case 's': case 'g': case 'p':
		*tt = TT_HOUSE; bchar[y][x] = (c - 32) | 0x80; break;
	default: *tt = TT_PLAIN; break;
	}
}

static bool bfree(int x, int y, int c)
{
	return inb(x, y) && (bchar[y][x] & 0x7F) == c && !tac.bmap[y][x];
}

// Greedy rectangle from the top-left tile: as wide as the letter runs, then as tall as whole
// rows of it continue (footprint <= 8 tiles: one searched bit per tile).
static void add_building(int x, int y)
{
	int c = bchar[y][x] & 0x7F, w = 1, h = 1;
	if (tac.nbldg >= TAC_MAX_BLDG) {
		gs.terrain[y][x] = TT_WALL;
		bchar[y][x] = 0;
		return;
	}
	while (w < 8 && bfree(x + w, y, c))
		w++;
	for (;;) {
		if ((h + 1) * w > 8)
			break;
		int i = 0;
		while (i < w && bfree(x + i, y + h, c))
			i++;
		if (i < w)
			break;
		h++;
	}
	Building *b = &tac.bldg[tac.nbldg++];
	b->x = x;
	b->y = y;
	b->w = w;
	b->h = h;
	b->kind = bkind(c);
	b->door_x = 0xFF;
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++) {
			tac.bmap[y + j][x + i] = tac.nbldg;
			if ((bchar[y + j][x + i] & 0x80) && b->door_x == 0xFF) {
				b->door_x = x + i;
				b->door_y = y + j;
			}
		}
	if (b->door_x == 0xFF) { // no door marked: bottom-left tile
		b->door_x = x;
		b->door_y = y + h - 1;
	}
	// the door faces out by its place in the footprint (bottom row S, top row N, then the
	// sides), else the first outside neighbour in the order S E W N
	int rx = b->door_x - x, ry = b->door_y - y;
	int want = ry == h - 1 ? 2 : !ry ? 0 : !rx ? 3 : rx == w - 1 ? 1 : -1;
	static const u8 order[4] = { 2, 1, 3, 0 }; // S E W N
	b->door_dir = 2;
	for (int i = -1; i < 4; i++) {
		int k = i < 0 ? want : order[i], nx = b->door_x + dirx[k & 3], ny = b->door_y + diry[k & 3];
		if (k >= 0 && inb(nx, ny) && tac.bmap[ny][nx] != tac.nbldg) {
			b->door_dir = k;
			break;
		}
	}
}

// Next free deploy point at or after the cursor (row-major); moves the cursor past it.
static bool next_deploy(int *cx, int *cy, int *ox, int *oy)
{
	for (int y = *cy, x = *cx; y < gs.h; y++, x = 0)
		for (; x < gs.w; x++)
			if ((tac.zone[y][x] & ZN_DEPLOY) && unit_at(x, y) < 0 && tac_move_cost(x, y, false) != IMPASSABLE) {
				*ox = x;
				*oy = y;
				*cx = x + 1;
				*cy = y;
				return true;
			}
	*cy = gs.h;
	return false;
}

void tac_load(const TacMission *m, const u8 *deploy, int n)
{
	u32 seed = gs.rng ? gs.rng : (0x1234567u + frame_count) | 1;
	memset(&gs, 0, sizeof gs);
	memset(&tac, 0, sizeof tac);
	memset(bchar, 0, sizeof bchar);
	gs.rng = seed;
	tac.m = m;
	for (int i = 0; i < MAX_UNITS; i++)
		tac.actor[i].person = 0xFF;
	memset(gs.owner, SIDE_NEUTRAL, sizeof gs.owner);
	memset(gs.fog, 1, sizeof gs.fog);
	gs.night = m->night;
	gs.side = SIDE_SURVIVOR;
	gs.day = run.day;

	int w = m->rows[0] ? (int)strlen(m->rows[0]) : 0, h = 0;
	gs.w = MIN(w, MAP_MAX_W);
	while (h < MAP_MAX_H && m->rows[h])
		h++;
	gs.h = h;
	for (int y = 0; y < gs.h; y++) {
		const char *r = m->rows[y];
		bool end = false;
		for (int x = 0; x < gs.w; x++) {
			end = end || !r[x]; // short row: the rest is plain
			legend(x, y, end ? '.' : r[x]);
		}
	}
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++)
			if (bchar[y][x] && !tac.bmap[y][x])
				add_building(x, y);

	// people on the deploy points, row-major
	int cx = 0, cy = 0;
	bool first = true;
	for (int i = 0; i < n; i++) {
		int r = deploy[i];
		if (r >= ROSTER_MAX || r >= run.nroster || !run.roster[r].alive)
			continue;
		bool dup = false;
		for (int j = 0; j < MAX_UNITS; j++)
			dup = dup || tac.actor[j].person == r;
		if (dup)
			continue;
		int x, y;
		if (!next_deploy(&cx, &cy, &x, &y))
			break;
		Person *p = &run.roster[r];
		int hp = p->hp ? p->hp : 1, maxhp = MAX(p->maxhp, hp);
		int u = spawn_unit(UT_SURVIVOR, SIDE_SURVIVOR, x, y, hp);
		if (u < 0)
			break;
		Actor *ac = &tac.actor[u];
		memset(ac, 0, sizeof *ac);
		ac->person = r;
		ac->hp = hp;
		ac->maxhp = maxhp;
		gs.units[u].inside = tac.bmap[y][x] != 0;
		if (first) {
			gs.cur_x = x;
			gs.cur_y = y;
			first = false;
		}
	}
	for (const UnitPlacement *p = m->zombies; p && p->type != 0xFF; p++)
		tac_spawn_zombie(p->type, p->x, p->y, p->dormant);
}

// ---------------------------------------------------------------- map queries
bool tac_in_bldg(int x, int y)
{
	return inb(x, y) && tac.bmap[y][x];
}

bool tac_edge_open(int x, int y, int dir, bool zombie)
{
	if ((unsigned)dir > 3)
		return false;
	int nx = x + dirx[dir], ny = y + diry[dir];
	if (!inb(x, y) || !inb(nx, ny))
		return false;
	int b0 = tac.bmap[y][x], b1 = tac.bmap[ny][nx];
	if (b0 == b1)
		return true;
	if (b0 && b1)
		return false;
	const Building *b;
	if (b0) {
		b = &tac.bldg[b0 - 1];
		if (!door_edge(b, x, y, dir))
			return false;
	} else {
		b = &tac.bldg[b1 - 1];
		if (!door_edge(b, nx, ny, (dir + 2) & 3))
			return false;
	}
	return !(zombie && b->door_hp);
}

int tac_move_cost(int x, int y, bool zombie)
{
	if (!inb(x, y))
		return IMPASSABLE;
	switch (gs.terrain[y][x]) {
	case TT_FOREST: case TT_HILL: case TT_RUINS:
		return 2;
	case TT_WATER: case TT_WRECK: case TT_WALL:
		return IMPASSABLE;
	default:
		return 1;
	}
}

static int move_points(int ui)
{
	Unit *u = &gs.units[ui];
	if (u->side == SIDE_SURVIVOR)
		return tac.actor[ui].down ? 0 : person_move(person_of(ui));
	return zombie_info[u->type].move + (gs.night ? 1 : 0);
}

// Does the unit on (x, y) stop `side` from passing? Survivors walk into zombies they can't see
// (fogged, or under a roof: inside is tac_fog()'s draw flag).
static bool blocks(int side, int x, int y)
{
	int o = unit_at(x, y);
	if (o < 0 || gs.units[o].side == side)
		return false;
	return side != SIDE_SURVIVOR || (!gs.fog[y][x] && !gs.units[o].inside);
}

// Weighted search (SPFA, each tile queued at most once at a time) up to the move points.
int tac_paths(int ui, u8 *d, u8 *p)
{
	static u16 ring[RING] EWRAM_BSS;
	static u8 inq[MAP_MAX_W * MAP_MAX_H] EWRAM_BSS;
	memset(d, NO_PATH, MAP_MAX_W * MAP_MAX_H);
	memset(p, NO_PATH, MAP_MAX_W * MAP_MAX_H);
	if ((unsigned)ui >= MAX_UNITS || !gs.units[ui].alive)
		return 0;
	Unit *u = &gs.units[ui];
	bool z = u->side == SIDE_HORDE;
	int mp = move_points(ui), qh = 0, qn = 1, n = 0;
	memset(inq, 0, sizeof inq);
	occ_begin();
	d[IDX(u->x, u->y)] = 0;
	ring[0] = u->x | u->y << 8;
	inq[IDX(u->x, u->y)] = 1;
	while (qn) {
		int c = ring[qh], cx = c & 0xFF, cy = c >> 8, ci = IDX(cx, cy);
		qh = (qh + 1) & (RING - 1);
		qn--;
		inq[ci] = 0;
		for (int k = 0; k < 4; k++) {
			if (!tac_edge_open(cx, cy, k, z))
				continue;
			int nx = cx + dirx[k], ny = cy + diry[k], ni = IDX(nx, ny);
			int cost = tac_move_cost(nx, ny, z);
			if (cost == IMPASSABLE)
				continue;
			int nd = d[ci] + cost;
			if (nd > mp || nd >= d[ni] || blocks(u->side, nx, ny))
				continue;
			d[ni] = nd;
			p[ni] = k;
			if (!inq[ni]) {
				inq[ni] = 1;
				ring[(qh + qn) & (RING - 1)] = nx | ny << 8;
				qn++;
			}
		}
	}
	occ_end();
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++)
			if (d[IDX(x, y)] != NO_PATH)
				n++;
	return n;
}

// Cover of the edge on side k of (x, y). Inside a building every side is its wall (40) except
// the door edge (20), even where the next tile is the same building: the outer wall faces out.
static int edge_cover(int x, int y, int k)
{
	int b = tac.bmap[y][x], nx = x + dirx[k], ny = y + diry[k];
	if (b)
		return door_edge(&tac.bldg[b - 1], x, y, k) ? 20 : 40;
	if (!inb(nx, ny))
		return 0;
	if (tac.bmap[ny][nx])
		return 40;
	switch (gs.terrain[ny][nx]) {
	case TT_WALL:
		return 40;
	case TT_WRECK: case TT_FOREST: case TT_RUINS: case TT_HILL:
		return 20;
	default:
		return 0;
	}
}

int tac_edge_cover(int x, int y, int dir)
{
	return inb(x, y) && (unsigned)dir < 4 ? edge_cover(x, y, dir) : 0;
}

int tac_cover(int dx, int dy, int ax, int ay)
{
	if (!inb(dx, dy) || !inb(ax, ay))
		return 0;
	int bd = tac.bmap[dy][dx], ex = ax - dx, ey = ay - dy, c = 0;
	if ((bd && bd == tac.bmap[ay][ax]) || (!ex && !ey))
		return 0;
	if (ABS(ex) >= ABS(ey))
		c = edge_cover(dx, dy, ex > 0 ? 1 : 3);
	if (ABS(ey) >= ABS(ex))
		c = MAX(c, edge_cover(dx, dy, ey > 0 ? 2 : 0));
	return c;
}

// Bresenham between tile centres, always traced from the smaller endpoint so it is symmetric.
bool tac_los(int x0, int y0, int x1, int y1)
{
	if (!inb(x0, y0) || !inb(x1, y1))
		return false;
	if (x0 == x1 && y0 == y1)
		return true;
	if (x1 < x0 || (x1 == x0 && y1 < y0)) {
		int t = x0; x0 = x1; x1 = t;
		t = y0; y0 = y1; y1 = t;
	}
	int b0 = tac.bmap[y0][x0], b1 = tac.bmap[y1][x1];
	int dx = ABS(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -ABS(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy, x = x0, y = y0;
	for (;;) {
		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y += sy;
		}
		if (x == x1 && y == y1)
			return true;
		int b = tac.bmap[y][x];
		if ((b && b != b0 && b != b1) || gs.terrain[y][x] == TT_FOREST)
			return false;
	}
}

bool tac_hidden(int ui)
{
	if (!live(ui, SIDE_SURVIVOR) || tac.actor[ui].noisy)
		return false;
	Unit *u = &gs.units[ui];
	return tac.bmap[u->y][u->x] || gs.terrain[u->y][u->x] == TT_FOREST;
}

bool tac_sees(int z, int t)
{
	if (!live(z, SIDE_HORDE) || (unsigned)t >= MAX_UNITS || !gs.units[t].alive)
		return false;
	int dist = udist(z, t);
	if (dist <= 1)
		return true;
	if (tac_hidden(t))
		return false;
	int s = zombie_info[gs.units[z].type].sight - (gs.night ? 2 : 0);
	s = MAX(s, 2);
	return dist <= s && tac_los(gs.units[z].x, gs.units[z].y, gs.units[t].x, gs.units[t].y);
}

// ---------------------------------------------------------------- combat
int tac_weapon(int a, int d)
{
	if (!live(a, SIDE_SURVIVOR) || !live(d, SIDE_HORDE) || tac.actor[a].down)
		return -1;
	int w = person_of(a)->weapon, dist = udist(a, d);
	if (w >= WP_COUNT)
		w = WP_FISTS;
	const WeaponInfo *wi = &weapon_info[w];
	bool reach = dist == 1 && can_reach(a, d, true); // a barricade blocks melee both ways (8.4)
	if (dist >= wi->minr && dist <= wi->maxr && run.res[RES_AMMO] >= wi->ammo &&
	    (wi->skill == SK_MELEE ? reach : tac_los(gs.units[a].x, gs.units[a].y, gs.units[d].x, gs.units[d].y)))
		return w;
	return reach ? WP_FISTS : -1;
}

// Does (x, y) have cover on any edge? A target in the open can't be flanked.
static bool has_cover(int x, int y)
{
	for (int k = 0; k < 4; k++)
		if (edge_cover(x, y, k))
			return true;
	return false;
}

// Hit chance (0 = can't attack), damage, the cover used and whether it is a flank shot.
static int atk_calc(int a, int d, int *cover_out, int *dmg_out, bool *flank_out)
{
	int ch = 0, cover = 0, dmg = 0;
	bool ok = false, flank = false;
	if (live(a, SIDE_SURVIVOR)) {
		int w = tac_weapon(a, d);
		if (w >= 0) {
			ok = true;
			const WeaponInfo *wi = &weapon_info[w];
			const Person *p = person_of(a);
			Unit *ua = &gs.units[a], *ud = &gs.units[d];
			int dist = udist(a, d);
			bool ranged = dist > 1 || wi->skill == SK_FIREARMS;
			bool unaware = ud->dormant || !tac.actor[d].alerted;
			if (ranged) {
				int face = tac_cover(ud->x, ud->y, ua->x, ua->y);
				if (dist > 1) // point blank ignores cover
					cover = face;
				int rp = dist - ((wi->maxr + 1) >> 1);
				ch = 50 + 8 * p->aim + 5 * p->skill[wi->skill] + wi->acc - cover;
				if (rp > 0)
					ch -= 10 * rp;
				if (gs.night && dist > 2)
					ch -= 10;
				flank = !face && has_cover(ud->x, ud->y); // its cover faces the other way
				if (flank)
					ch += 15;
				if (gs.terrain[ua->y][ua->x] == TT_HILL)
					ch += 10;
			} else {
				ch = 60 + 6 * p->speed + 5 * p->skill[wi->skill] + wi->acc;
			}
			if (unaware)
				ch += 25;
			dmg = wi->dmg + (flank || unaware ? 1 : 0);
		}
	} else if (live(a, SIDE_HORDE) && live(d, SIDE_SURVIVOR) && can_reach(a, d, true)) {
		const ZombieInfo *zi = &zombie_info[gs.units[a].type];
		ok = true;
		bool unseen = tac.actor[a].unseen;
		ch = tac.actor[d].down ? 95 : zi->hit - 5 * person_of(d)->speed + (unseen ? 25 : 0);
		dmg = zi->dmg + (unseen ? 1 : 0);
	}
	if (cover_out)
		*cover_out = cover;
	if (dmg_out)
		*dmg_out = dmg;
	if (flank_out)
		*flank_out = flank;
	if (!ok)
		return 0;
	return ch < 5 ? 5 : ch > 95 ? 95 : ch;
}

int tac_hit_chance(int a, int d, int *cover_out)
{
	return atk_calc(a, d, cover_out, NULL, NULL);
}

int tac_damage(int a, int d)
{
	int dmg;
	atk_calc(a, d, NULL, &dmg, NULL);
	return dmg;
}

bool tac_flanked(int a, int d)
{
	bool f;
	return atk_calc(a, d, NULL, NULL, &f) && f;
}

static void infect(int t, int z)
{
	Person *p = person_of(t);
	int c = zombie_info[gs.units[z].type].infect;
	if (!c || (p->flags & PF_INFECTED))
		return;
	c -= armor_info[p->armor < AR_COUNT ? p->armor : 0].infect_cut + 3 * p->grit;
	c = MAX(c, 5);
	if ((int)(rng_next() % 100) < c) {
		person_bitten(p);
		chronicle(LOG_BITTEN, tac.actor[t].person, 0);
	}
}

void tac_attack(int a, int d)
{
	int dmg, ch = atk_calc(a, d, NULL, &dmg, NULL);
	if (!ch)
		return;
	int ax = gs.units[a].x, ay = gs.units[a].y;
	bool hit = (int)(rng_next() % 100) < ch;
	Actor *t = &tac.actor[d];
	if (gs.units[a].side == SIDE_SURVIVOR) {
		const WeaponInfo *wi = &weapon_info[tac_weapon(a, d)];
		Person *p = person_of(a);
		run.res[RES_AMMO] -= MIN(run.res[RES_AMMO], wi->ammo);
		skill_use(p, wi->skill, hit ? 2 : 1);
		if (wi->noise >= 3)
			tac.actor[a].noisy = 1;
		if (hit) {
			tac_fx_hit(d, dmg);
			if (t->hp <= dmg) {
				set_hp(d, 0);
				p->kills = sat8(p->kills + 1);
				p->xp = p->xp > 65530 ? 65535 : p->xp + 5;
				tac_kill(d, a);
			} else {
				set_hp(d, t->hp - dmg);
				wake(d, ax, ay); // a wounded zombie knows where it came from
			}
		} else {
			tac_fx_miss(d);
		}
		if (wi->noise)
			tac_noise(ax, ay, wi->noise);
		return;
	}
	if (hit) {
		tac_fx_hit(d, dmg);
		if (t->down) {
			tac_kill(d, a);
		} else {
			infect(d, a);
			if (t->hp <= dmg) {
				set_hp(d, 0);
				t->down = 2;
			} else {
				set_hp(d, t->hp - dmg);
			}
		}
	} else {
		tac_fx_miss(d);
	}
	tac_noise(ax, ay, 2);
}

static void drop_pack(int i, int x, int y)
{
	Actor *ac = &tac.actor[i];
	int sum = 0, slot = -1;
	for (int r = 0; r < RES_COUNT; r++)
		sum += ac->carry[r];
	if (!sum)
		return;
	int c = tac.cache[y][x];
	if (c >= CACHE_PACK && c < CACHE_PACK + TAC_MAX_PACKS && tac.pack[c - CACHE_PACK].used)
		slot = c - CACHE_PACK; // merge into the pack already lying here
	for (int k = 0; k < TAC_MAX_PACKS && slot < 0; k++)
		if (!tac.pack[k].used)
			slot = k;
	if (slot < 0 || (c && c != CACHE_PACK + slot))
		return; // no room: the loot is lost
	TacPack *pk = &tac.pack[slot];
	if (!pk->used)
		memset(pk, 0, sizeof *pk);
	pk->x = x;
	pk->y = y;
	pk->used = 1;
	for (int r = 0; r < RES_COUNT; r++) {
		pk->res[r] = sat8(pk->res[r] + ac->carry[r]);
		ac->carry[r] = 0;
	}
	tac.cache[y][x] = CACHE_PACK + slot;
}

void tac_kill(int i, int killer)
{
	if ((unsigned)i >= MAX_UNITS || !gs.units[i].alive)
		return;
	Unit *u = &gs.units[i];
	if (u->side != SIDE_SURVIVOR) {
		set_hp(i, 0);
		u->alive = 0;
		if (gs.kills < 0xFFFF)
			gs.kills++;
		return;
	}
	int x = u->x, y = u->y;
	bool by_zombie = live(killer, SIDE_HORDE);
	Person *p = person_of(i);
	p->alive = 0;
	p->hp = 0;
	set_hp(i, 0);
	tac.actor[i].down = 0;
	chronicle(LOG_DEATH, tac.actor[i].person, by_zombie ? 1 : 0);
	drop_pack(i, x, y);
	u->alive = 0;
	if (gs.losses < 0xFFFF)
		gs.losses++;
	if (by_zombie) { // they rise, and join the horde from its next turn
		int z = tac_spawn_zombie(UT_WALKER, x, y, false);
		if (z >= 0) {
			gs.units[z].moved = tac.actor[z].moved = 1;
			wake(z, x, y);
		}
	}
	tac_check_objective();
}

// ---------------------------------------------------------------- noise and waves
#define MAX_EXITS 16

// Manhattan distance from (x, y) to the nearest of the exit tiles (ex, ey)
static int exit_dist(int x, int y, const u8 *ex, const u8 *ey, int ne)
{
	int d = 255;
	for (int i = 0; i < ne; i++)
		d = MIN(d, mdist(x, y, ex[i], ey[i]));
	return d;
}

// Waves come from the map edges, 5+ tiles from everyone, never on the caravan or an exit. Get
// through: from behind (no nearer the exits than the person nearest them), while there is room.
static void spawn_wave(int nx, int ny)
{
	static u16 cand[2 * (MAP_MAX_W + MAP_MAX_H)] EWRAM_BSS;
	u8 sx[MAX_UNITS], sy[MAX_UNITS], ex[MAX_EXITS], ey[MAX_EXITS];
	int ns = 0, n = 0, ne = 0;
	for (int i = 0; i < MAX_UNITS; i++)
		if (live(i, SIDE_SURVIVOR)) {
			sx[ns] = gs.units[i].x;
			sy[ns++] = gs.units[i].y;
		}
	occ_begin();
	for (int y = 0; y < gs.h; y++) {
		bool row_edge = y == 0 || y == gs.h - 1;
		for (int x = 0; x < gs.w; x += row_edge || x == gs.w - 1 ? 1 : gs.w - 1) {
			if (tac_move_cost(x, y, true) == IMPASSABLE || tac.bmap[y][x] ||
			    (tac.zone[y][x] & (ZN_DEPLOY | ZN_EXIT)) || unit_at(x, y) >= 0)
				continue;
			bool far = true;
			for (int s = 0; s < ns && far; s++)
				far = mdist(x, y, sx[s], sy[s]) > 4;
			if (far)
				cand[n++] = x | y << 8;
		}
	}
	occ_end();
	int nb = n; // cand[0..nb) are drawn first
	if (tac.m->objective == OBJ_GET_THROUGH) {
		for (int y = 0; y < gs.h; y++)
			for (int x = 0; x < gs.w && ne < MAX_EXITS; x++)
				if (tac.zone[y][x] & ZN_EXIT) {
					ex[ne] = x;
					ey[ne++] = y;
				}
		int lead = 255;
		for (int s = 0; s < ns; s++)
			lead = MIN(lead, exit_dist(sx[s], sy[s], ex, ey, ne));
		nb = 0;
		for (int j = 0; j < n; j++)
			if (exit_dist(cand[j] & 0xFF, cand[j] >> 8, ex, ey, ne) >= lead) {
				u16 t = cand[nb];
				cand[nb++] = cand[j];
				cand[j] = t;
			}
	}
	for (int k = 0; k < tac.m->wave_size && n > 0; k++) {
		int j = rng_next() % (nb ? nb : n), c = cand[j];
		if (j < nb) {
			cand[j] = cand[--nb];
			cand[nb] = cand[--n];
		} else {
			cand[j] = cand[--n];
		}
		int z = tac_spawn_zombie(rng_next() % 3 ? UT_WALKER : UT_RUNNER, c & 0xFF, c >> 8, false);
		if (z >= 0) {
			wake(z, nx, ny);
			gs.units[z].moved = tac.actor[z].moved = 1; // acts from the next horde turn
		}
	}
}

// Fill the Horde meter by n, scaled by difficulty (x0.75 x1 x1.25 x1.5; the quarters carry over to
// the next fill, so a +1 round tick and quiet melee scale too); each threshold crossed brings a
// wave heading for (x, y).
static void horde_add(int n, int x, int y)
{
	static const char *const wave_text[3] = { "Wave 1 of 3", "Wave 2 of 3", "Wave 3 of 3" };
	static const u8 scale[4] = { 3, 4, 5, 6 };
	int q = n * scale[run.difficulty & 3] + tac.horde_q;
	tac.horde_q = q & 3;
	tac.horde = MIN(tac.horde + (q >> 2), 0xFFFF);
	while (tac.wave < 3 && tac.m->waves[tac.wave] && tac.horde >= tac.m->waves[tac.wave]) {
		spawn_wave(x, y);
		tac_fx_text("The horde is coming", wave_text[tac.wave]);
		tac.wave++;
	}
}

void tac_noise(int x, int y, int r)
{
	if (r <= 0 || !tac.m)
		return;
	for (int i = 0; i < MAX_UNITS; i++) {
		const Unit *u = &gs.units[i];
		if (!live(i, SIDE_HORDE) || mdist(x, y, u->x, u->y) > r)
			continue;
		if (u->x == x && u->y == y && tac.actor[i].alerted)
			continue; // its own bite or bash: it keeps the goal it is after
		wake(i, x, y);
	}
	tac_fx_noise(x, y, r);
	horde_add(r, x, y);
}

// ---------------------------------------------------------------- searching, doors, leaving
// What ui can search: 1 = the building tile it stands on, 2 = a cache on its tile or next to it
// (car trunks sit on wrecks, which nobody can stand on).
static int search_spot(int ui, int *sx, int *sy)
{
	if (!live(ui, SIDE_SURVIVOR) || tac.actor[ui].down)
		return 0;
	int x = gs.units[ui].x, y = gs.units[ui].y, b = tac.bmap[y][x];
	*sx = x;
	*sy = y;
	if (tac.cache[y][x])
		return 2;
	if (b) {
		const Building *bd = &tac.bldg[b - 1];
		if (!(bd->searched & (1 << ((y - bd->y) * bd->w + x - bd->x))))
			return 1;
	}
	for (int k = 0; k < 4; k++) {
		int nx = x + dirx[k], ny = y + diry[k];
		if (inb(nx, ny) && tac.cache[ny][nx] && unit_at(nx, ny) < 0 && tac_edge_open(x, y, k, false)) {
			*sx = nx;
			*sy = ny;
			return 2;
		}
	}
	return 0;
}

bool tac_can_search(int ui)
{
	int x, y;
	return search_spot(ui, &x, &y) != 0;
}

// Loot table of a building kind (BK_*) or BK_COUNT for a car trunk. Returns the first resource.
static int roll_loot(int kind, u8 *got)
{
	switch (kind) {
	case BK_HOUSE:
		got[RES_FOOD] = 1 + (rng_next() & 1);
		if (rng_next() % 100 < 30)
			got[RES_AMMO] = 1 + (rng_next() & 1);
		if (rng_next() % 100 < 20)
			got[RES_MEDS] = 1;
		return RES_FOOD;
	case BK_SHOP:
		got[RES_FOOD] = 2 + (rng_next() & 1);
		if (rng_next() % 100 < 30)
			got[RES_PARTS] = 1;
		return RES_FOOD;
	case BK_GARAGE:
		got[RES_PARTS] = 1 + (rng_next() & 1);
		if (rng_next() % 100 < 50)
			got[RES_FUEL] = 1 + (rng_next() & 1);
		return RES_PARTS;
	case BK_PHARMACY:
		got[RES_MEDS] = 1 + (rng_next() & 1);
		if (rng_next() % 100 < 25)
			got[RES_AMMO] = 1;
		return RES_MEDS;
	default: {
		static const u8 trunk_res[3] = { RES_FUEL, RES_PARTS, RES_AMMO }, trunk_n[3] = { 1, 1, 2 };
		int k = rng_next() % 3;
		got[trunk_res[k]] = trunk_n[k];
		return trunk_res[k];
	}
	}
}

int tac_search(int ui)
{
	int x, y, what = search_spot(ui, &x, &y);
	if (!what)
		return 0;
	Person *p = person_of(ui);
	u8 got[RES_COUNT] = { 0 };
	int c = tac.cache[y][x];
	if (what == 2 && c >= CACHE_PACK) { // a dead friend's pack: all of it, nothing more
		TacPack *pk = &tac.pack[(c - CACHE_PACK) & (TAC_MAX_PACKS - 1)];
		memcpy(got, pk->res, sizeof got);
		memset(pk, 0, sizeof *pk);
	} else {
		int first;
		if (what == 2) {
			first = roll_loot(BK_COUNT, got);
		} else {
			Building *b = &tac.bldg[tac.bmap[y][x] - 1];
			b->searched |= 1 << ((y - b->y) * b->w + x - b->x);
			first = roll_loot(b->kind, got);
		}
		if ((int)(rng_next() % 10) < p->skill[SK_SCAVENGE])
			got[first]++;
	}
	if (what == 2)
		tac.cache[y][x] = 0;
	int sum = 0;
	for (int r = 0; r < RES_COUNT; r++) {
		tac.actor[ui].carry[r] = sat8(tac.actor[ui].carry[r] + got[r]);
		sum += got[r];
	}
	skill_use(p, SK_SCAVENGE, 2);
	return sum;
}

int tac_barricade_target(int ui)
{
	if (!live(ui, SIDE_SURVIVOR) || tac.actor[ui].down || run.res[RES_PARTS] < 1)
		return -1;
	int x = gs.units[ui].x, y = gs.units[ui].y;
	for (int i = 0; i < tac.nbldg; i++) {
		const Building *b = &tac.bldg[i];
		if (b->door_hp)
			continue;
		if ((x == b->door_x && y == b->door_y) ||
		    (x == b->door_x + dirx[b->door_dir] && y == b->door_y + diry[b->door_dir]))
			return i;
	}
	return -1;
}

void tac_barricade(int ui)
{
	int b = tac_barricade_target(ui);
	if (b < 0)
		return;
	tac.bldg[b].door_hp = DOOR_HP;
	run.res[RES_PARTS]--;
	tac.actor[ui].noisy = 1;
	skill_use(person_of(ui), SK_MECHANICS, 2);
	tac_noise(gs.units[ui].x, gs.units[ui].y, 3);
}

// Take a person off the map with their loot (how: 1 exit, 2 caravan).
static void extract_unit(int i, int how)
{
	Actor *ac = &tac.actor[i];
	for (int r = 0; r < RES_COUNT; r++) {
		tac.loot[r] = MIN(tac.loot[r] + ac->carry[r], 0xFFFF);
		ac->carry[r] = 0;
	}
	if (ac->down || !ac->hp) { // carried out
		ac->down = 0;
		set_hp(i, MAX(ac->hp, 1));
	}
	person_of(i)->hp = ac->hp;
	ac->extracted = how;
	gs.units[i].alive = 0;
}

void tac_extract(int ui)
{
	if (!live(ui, SIDE_SURVIVOR) || tac.actor[ui].down)
		return;
	int z = tac.zone[gs.units[ui].y][gs.units[ui].x];
	if (!(z & (ZN_EXIT | ZN_DEPLOY)))
		return;
	if (z & ZN_EXIT)
		tac.through = sat8(tac.through + 1);
	extract_unit(ui, z & ZN_EXIT ? 1 : 2);
	tac_check_objective();
}

// ---------------------------------------------------------------- turn end and objectives
static bool bridge_held(void)
{
	bool ours = false;
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		if (!u->alive || !(tac.zone[u->y][u->x] & ZN_BRIDGE))
			continue;
		if (u->side == SIDE_HORDE)
			return false;
		if (u->side == SIDE_SURVIVOR && !tac.actor[i].down)
			ours = true;
	}
	return ours;
}

void tac_end_survivor_phase(void)
{
	for (int i = 0; i < MAX_UNITS; i++)
		if (live(i, SIDE_SURVIVOR) && tac.actor[i].down && !--tac.actor[i].down)
			tac_kill(i, -1); // bled out
	if (tac.m && tac.m->objective == OBJ_BRIDGE && !tac.result && bridge_held())
		tac.held = sat8(tac.held + 1);
	tac.turn = sat8(tac.turn + 1);
	tac_check_objective();
	// the longer you stay, the worse it gets: +1 a round; a wave heads for the first of us
	for (int i = 0; i < MAX_UNITS && tac.m && !tac.result; i++)
		if (live(i, SIDE_SURVIVOR)) {
			horde_add(1, gs.units[i].x, gs.units[i].y);
			break;
		}
}

void tac_check_objective(void)
{
	if (tac.result || !tac.m)
		return;
	const TacMission *m = tac.m;
	if ((m->objective == OBJ_BRIDGE && tac.held >= m->need) ||
	    (m->objective == OBJ_GET_THROUGH && tac.through >= m->need)) {
		tac.result = 1;
		for (int i = 0; i < MAX_UNITS; i++) // the caravan picks everyone up
			if (live(i, SIDE_SURVIVOR))
				extract_unit(i, 2);
		return;
	}
	if (tac_survivors_left())
		return;
	int got = 0; // a scavenge run is won by what comes out (only the people who left carry it out)
	for (int r = 0; r < RES_COUNT; r++)
		got += tac.loot[r];
	tac.result = m->objective == OBJ_SCAVENGE && got ? 1 : 2;
}

int tac_spawn_zombie(int type, int x, int y, bool dormant)
{
	if (type < FIRST_ZOMBIE || type >= UT_COUNT || !inb(x, y) || unit_at(x, y) >= 0)
		return -1;
	int i = 0;
	while (i < MAX_UNITS && (gs.units[i].alive || tac.actor[i].person != 0xFF))
		i++; // free slot that never held a person
	if (i >= MAX_UNITS)
		return -1;
	Unit *u = &gs.units[i];
	Actor *ac = &tac.actor[i];
	memset(u, 0, sizeof *u);
	memset(ac, 0, sizeof *ac);
	u->type = type;
	u->side = SIDE_HORDE;
	u->x = x;
	u->y = y;
	u->capture = 20;
	u->alive = 1;
	u->dormant = dormant;
	u->inside = tac.bmap[y][x] != 0;
	ac->person = 0xFF;
	ac->gx = x;
	ac->gy = y;
	ac->maxhp = zombie_info[type].hp;
	set_hp(i, ac->maxhp);
	return i;
}

int tac_survivors_left(void)
{
	int n = 0;
	for (int i = 0; i < MAX_UNITS; i++)
		if (live(i, SIDE_SURVIVOR))
			n++;
	return n;
}
