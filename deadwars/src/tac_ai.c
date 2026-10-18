// Dead Wars: The Last Boat - horde turn: waking, hunting by sight, noise goals, breaking doors
#include "lb.h"
#include "video.h"
#include "sound.h"
#include <string.h>

#define IDX(x, y) ((y) * MAP_MAX_W + (x))
#define FAR 0xFFFF
#define FIELD_SLOTS 4
#define QMASK (MAP_MAX_W * MAP_MAX_H - 1) // ring queue: each tile is queued at most once

static u8 cost_map[MAP_MAX_H][MAP_MAX_W] EWRAM_BSS; // zombie cost to step onto the tile
static u8 edge_map[MAP_MAX_H][MAP_MAX_W] EWRAM_BSS; // bit k: a zombie may step out across edge k
static u16 field[FIELD_SLOTS][MAP_MAX_W * MAP_MAX_H] EWRAM_BSS; // walking cost to a goal
static u16 queue[MAP_MAX_W * MAP_MAX_H] EWRAM_BSS;
static u8 inq[MAP_MAX_W * MAP_MAX_H] EWRAM_BSS;
static struct { u8 x, y, valid; u16 used; } fkey[FIELD_SLOTS];
static u16 fclock;
static bool heavy; // a field was built since the last frame

static bool on_map(int i)
{
	const Unit *u = &gs.units[i];
	return u->alive && !u->carried && u->side == SIDE_SURVIVOR && !tac.actor[i].extracted;
}

static int bldg_at(int x, int y)
{
	if (x < 0 || y < 0 || x >= gs.w || y >= gs.h)
		return -1;
	int b = tac.bmap[y][x] - 1;
	return b < tac.nbldg ? b : -1;
}

static bool bldg_occupied(int b)
{
	for (int i = 0; i < MAX_UNITS; i++)
		if (on_map(i) && bldg_at(gs.units[i].x, gs.units[i].y) == b)
			return true;
	return false;
}

// What the player sees: not fogged, and not under the roof of a building nobody of ours is in.
static bool tile_shown(int x, int y)
{
	if (gs.fog[y][x])
		return false;
	int b = bldg_at(x, y);
	return b < 0 || bldg_occupied(b);
}

static bool shown(int z)
{
	return !gs.fog[gs.units[z].y][gs.units[z].x] && !gs.units[z].inside;
}

// ---------------------------------------------------------------- pacing (as ai.c)

static bool paced(void)
{
	return profile.anim_speed && !(keys_held & KEY_B);
}

static void beat(int frames)
{
	while (frames-- > 0 && paced())
		frame();
}

static void point_at(int x, int y, int frames)
{
	if (!paced())
		return;
	gs.cur_x = x;
	gs.cur_y = y;
	camera_follow(x, y, true);
	tac_hud();
	spr.cursor_show = 1;
	beat(frames);
	spr.cursor_show = 0;
}

// ---------------------------------------------------------------- flow fields

static void edges_at(int x, int y)
{
	u8 e = 0;
	for (int k = 0; k < 4; k++) {
		int nx = x + TAC_DIRX(k), ny = y + TAC_DIRY(k);
		if (nx >= 0 && ny >= 0 && nx < gs.w && ny < gs.h && tac_edge_open(x, y, k, true))
			e |= 1 << k;
	}
	edge_map[y][x] = e;
}

// Terrain and edges for the whole turn, a few rows per frame so no frame runs long.
static void map_costs(void)
{
	for (int y = 0; y < gs.h; y++) {
		for (int x = 0; x < gs.w; x++) {
			int c = tac_move_cost(x, y, true);
			cost_map[y][x] = c <= 0 ? 1 : c >= IMPASSABLE ? IMPASSABLE : c;
			edges_at(x, y);
		}
		if ((y & 7) == 7)
			frame();
	}
	memset(fkey, 0, sizeof fkey);
	fclock = 0;
}

// Cost for a zombie to walk from any tile to (gx, gy); cached per goal for the turn.
static const u16 *field_to(int gx, int gy)
{
	int s;
	for (s = 0; s < FIELD_SLOTS; s++)
		if (fkey[s].valid && fkey[s].x == gx && fkey[s].y == gy) {
			fkey[s].used = ++fclock;
			return field[s];
		}
	int slot = 0;
	for (s = 0; s < FIELD_SLOTS; s++) {
		if (!fkey[s].valid) {
			slot = s;
			break;
		}
		if (fkey[s].used < fkey[slot].used)
			slot = s;
	}
	u16 *f = field[slot];
	memset(f, 0xFF, sizeof field[0]);
	memset(inq, 0, sizeof inq);
	int qh = 0, qt = 0, cnt = 1;
	f[IDX(gx, gy)] = 0;
	queue[qt++] = gx | gy << 8; // packed x,y: no division on the GBA
	inq[IDX(gx, gy)] = 1;
	while (cnt) {
		int cx = queue[qh] & 0xFF, cy = queue[qh] >> 8, c = IDX(cx, cy);
		qh = (qh + 1) & QMASK;
		cnt--;
		inq[c] = 0;
		// the goal itself may be a survivor's tile a zombie never enters: count it as 1
		u16 nd = f[c] + ((cx == gx && cy == gy) ? 1 : cost_map[cy][cx]);
		for (int k = 0; k < 4; k++) {
			int nx = cx + TAC_DIRX(k), ny = cy + TAC_DIRY(k), n = IDX(nx, ny);
			if (nx < 0 || ny < 0 || nx >= gs.w || ny >= gs.h || cost_map[ny][nx] == IMPASSABLE)
				continue;
			// a zombie on n steps onto c across n's edge facing back
			if (!(edge_map[ny][nx] & (1 << ((k + 2) & 3))) || nd >= f[n])
				continue;
			f[n] = nd;
			if (!inq[n]) {
				inq[n] = 1;
				queue[qt] = nx | ny << 8;
				qt = (qt + 1) & QMASK;
				cnt++;
			}
		}
	}
	fkey[slot].x = gx;
	fkey[slot].y = gy;
	fkey[slot].valid = 1;
	fkey[slot].used = ++fclock;
	heavy = true;
	return f;
}

static int dir_to(int x, int y, int tx, int ty)
{
	return ty < y ? 0 : tx > x ? 1 : ty > y ? 2 : 3;
}

// next to (tx, ty) with the edge between open
static bool adjacent(int x, int y, int tx, int ty)
{
	return ABS(tx - x) + ABS(ty - y) == 1 && (edge_map[y][x] & (1 << dir_to(x, y, tx, ty)));
}

// Downhill on the field onto a free tile we can afford; -1 if none.
static int step_toward(const u16 *f, int x, int y, int mp)
{
	int best = -1, k0 = rng_next() & 3;
	u16 bf = f[IDX(x, y)];
	for (int j = 0; j < 4; j++) {
		int k = (k0 + j) & 3;
		if (!(edge_map[y][x] & (1 << k)))
			continue;
		int nx = x + TAC_DIRX(k), ny = y + TAC_DIRY(k), c = cost_map[ny][nx];
		if (c == IMPASSABLE || c > mp || f[IDX(nx, ny)] >= bf || unit_at(nx, ny) >= 0)
			continue;
		bf = f[IDX(nx, ny)];
		best = k;
	}
	return best;
}

// A barricaded door between the zombie and its goal: returns the building and where to stand
// to hit it (inside on the door tile to break out, outside the door to break in), or -1.
static int door_block(int zx, int zy, int gx, int gy, int *sx, int *sy)
{
	int zb = bldg_at(zx, zy), gb = bldg_at(gx, gy);
	if (zb >= 0 && zb != gb && tac.bldg[zb].door_hp) {
		*sx = tac.bldg[zb].door_x;
		*sy = tac.bldg[zb].door_y;
		return zb;
	}
	if (gb >= 0 && gb != zb && tac.bldg[gb].door_hp) {
		const Building *b = &tac.bldg[gb];
		int d = b->door_dir & 3, ox = b->door_x + TAC_DIRX(d), oy = b->door_y + TAC_DIRY(d);
		if (ox < 0 || oy < 0 || ox >= gs.w || oy >= gs.h || cost_map[oy][ox] == IMPASSABLE)
			return -1;
		*sx = ox;
		*sy = oy;
		return gb;
	}
	return -1;
}

// ---------------------------------------------------------------- actions

// Hidden all the way: no animation, no camera. Otherwise walk it like ai.c.
static void walk(int z, const u16 *path, int len)
{
	Unit *zu = &gs.units[z];
	if (len <= 0)
		return;
	bool start = tile_shown(zu->x, zu->y), seen = start;
	for (int i = 0; i < len && !seen; i++)
		seen = tile_shown(path[i] & 0xFF, path[i] >> 8);
	if (!seen) {
		zu->x = path[len - 1] & 0xFF;
		zu->y = path[len - 1] >> 8;
	} else {
		if (start)
			point_at(zu->x, zu->y, 20);
		zu->inside = 0; // drawn while it walks; set again below
		unit_move_anim(z, path, len);
		beat(8);
	}
	int b = bldg_at(zu->x, zu->y);
	zu->inside = b >= 0 && !bldg_occupied(b);
}

static void wander(int z)
{
	Unit *zu = &gs.units[z];
	if (rng_next() & 3) // 25%
		return;
	int k = rng_next() & 3, nx = zu->x + TAC_DIRX(k), ny = zu->y + TAC_DIRY(k);
	if (!(edge_map[zu->y][zu->x] & (1 << k)) || cost_map[ny][nx] == IMPASSABLE || unit_at(nx, ny) >= 0)
		return;
	u16 p = nx | ny << 8;
	walk(z, &p, 1);
}

static void bite(int z, int t)
{
	const Unit *tu = &gs.units[t];
	point_at(tu->x, tu->y, 16);
	camera_follow(tu->x, tu->y, false);
	tac_attack(z, t);
	tac_fog();
	tac_check_objective();
	tac_hud();
	tac_music_update();
	tac_people_notice();
	beat(12);
}

static void bash_door(int z, int bi)
{
	Unit *zu = &gs.units[z];
	Building *b = &tac.bldg[bi];
	int type = zu->type < UT_COUNT ? zu->type : UT_WALKER, dmg = MAX(1, zombie_info[type].door_dmg);
	bool seen = tile_shown(zu->x, zu->y) || tile_shown(b->door_x, b->door_y) || bldg_occupied(bi);
	if (seen) {
		point_at(zu->x, zu->y, 12);
		camera_follow(zu->x, zu->y, false);
	}
	b->door_hp = b->door_hp > dmg ? b->door_hp - dmg : 0;
	sfx(b->door_hp ? SFX_BITE : SFX_DESTROY);
	if (seen) {
		spr.popup_x = b->door_x * 16;
		spr.popup_y = b->door_y * 16;
		spr.popup_value = MIN(dmg, 99);
		spr.popup_timer = 40;
		spr.flash_unit = shown(z) ? z : -1;
		wait_frames((keys_held & KEY_B) ? 4 : profile.anim_speed ? 24 : 12);
		spr.flash_unit = -1;
	}
	tac_noise(zu->x, zu->y, 3);
	if (!b->door_hp) {
		// the door edge opens: refresh both sides, the cached fields are stale
		int d = b->door_dir & 3, ox = b->door_x + TAC_DIRX(d), oy = b->door_y + TAC_DIRY(d);
		edges_at(b->door_x, b->door_y);
		if (ox >= 0 && oy >= 0 && ox < gs.w && oy < gs.h)
			edges_at(ox, oy);
		memset(fkey, 0, sizeof fkey);
		if (seen)
			banner("The door gives way!", NULL, (keys_held & KEY_B) ? 30 : 60);
	}
	tac_hud();
	tac_music_update();
}

// One zombie's activation: see -> hunt, heard -> go, else maybe wander. Move, then bite or bash.
static void act(int z)
{
	Unit *zu = &gs.units[z];
	Actor *za = &tac.actor[z];
	int type = zu->type < UT_COUNT ? zu->type : UT_WALKER;
	zu->moved = 1;
	int t = -1, best = 999;
	for (int i = 0; i < MAX_UNITS; i++) {
		if (!on_map(i))
			continue;
		int d = ABS(gs.units[i].x - zu->x) + ABS(gs.units[i].y - zu->y);
		if (d < best && tac_sees(z, i)) {
			best = d;
			t = i;
		}
	}
	int gx, gy;
	if (t >= 0) {
		gx = gs.units[t].x;
		gy = gs.units[t].y;
		za->alerted = 1; // remembers where it saw them
		za->gx = gx;
		za->gy = gy;
	} else if (za->alerted && za->gx < gs.w && za->gy < gs.h) {
		gx = za->gx;
		gy = za->gy;
		if (zu->x == gx && zu->y == gy) {
			za->alerted = 0;
			wander(z);
			return;
		}
	} else {
		za->alerted = 0;
		wander(z);
		return;
	}
	if (t >= 0 && adjacent(zu->x, zu->y, gx, gy)) {
		bite(z, t);
		return;
	}
	const u16 *f = field_to(gx, gy);
	int sx = -1, sy = -1, door = -1;
	if (f[IDX(zu->x, zu->y)] == FAR) {
		door = door_block(zu->x, zu->y, gx, gy, &sx, &sy);
		if (door < 0) { // no way there: give up on it
			za->alerted = 0;
			wander(z);
			return;
		}
		if (zu->x == sx && zu->y == sy) {
			bash_door(z, door);
			return;
		}
		f = field_to(sx, sy);
		if (f[IDX(zu->x, zu->y)] == FAR) {
			wander(z);
			return;
		}
	}
	int mp = zombie_info[type].move + (gs.night ? 1 : 0);
	u16 path[16];
	int len = 0, cx = zu->x, cy = zu->y;
	occ_begin();
	while (len < 16 && mp > 0) {
		if (door >= 0 ? (cx == sx && cy == sy) : t >= 0 ? adjacent(cx, cy, gx, gy) : (cx == gx && cy == gy))
			break;
		int k = step_toward(f, cx, cy, mp);
		if (k < 0)
			break;
		cx += TAC_DIRX(k);
		cy += TAC_DIRY(k);
		mp -= cost_map[cy][cx];
		path[len++] = cx | cy << 8;
	}
	occ_end();
	walk(z, path, len);
	if (door >= 0) {
		if (zu->x == sx && zu->y == sy)
			bash_door(z, door);
	} else if (t >= 0) {
		if (adjacent(zu->x, zu->y, gx, gy))
			bite(z, t);
	} else if (ABS(zu->x - gx) + ABS(zu->y - gy) <= 1) {
		za->alerted = 0; // reached the noise: nothing here
	}
}

void tac_horde_turn(void)
{
	u8 order[MAX_UNITS];
	u16 key[MAX_UNITS];
	int n = 0, cx = gs.cur_x, cy = gs.cur_y;
	gs.side = SIDE_HORDE;
	spr.cursor_show = 0;
	text_clear();
	banner("The horde", gs.night ? "They hunt in the dark." : "The dead are moving...", 40);
	map_costs();
	tac_hud();
	for (int i = 0; i < MAX_UNITS; i++) {
		Unit *u = &gs.units[i];
		if (!u->alive || u->side != SIDE_HORDE)
			continue;
		u->moved = 0;
		tac.actor[i].unseen = gs.fog[u->y][u->x] || u->inside;
		// sleepers wake when someone stands close in plain view
		if (u->dormant)
			for (int s = 0; s < MAX_UNITS; s++)
				if (on_map(s) && ABS(gs.units[s].x - u->x) + ABS(gs.units[s].y - u->y) <= 2 &&
				    tac_los(u->x, u->y, gs.units[s].x, gs.units[s].y)) {
					u->dormant = 0;
					break;
				}
	}
	// closest to the living first, so the front moves before the ones behind it
	for (int i = 0; i < MAX_UNITS; i++) {
		const Unit *u = &gs.units[i];
		if (!u->alive || u->side != SIDE_HORDE || u->dormant)
			continue;
		int d = 999;
		for (int s = 0; s < MAX_UNITS; s++)
			if (on_map(s))
				d = MIN(d, ABS(gs.units[s].x - u->x) + ABS(gs.units[s].y - u->y));
		int j = n++;
		while (j > 0 && key[j - 1] > d) {
			key[j] = key[j - 1];
			order[j] = order[j - 1];
			j--;
		}
		key[j] = d;
		order[j] = i;
	}
	int quiet = 0;
	heavy = false;
	for (int j = 0; j < n && !tac.result; j++) {
		int z = order[j];
		const Unit *u = &gs.units[z];
		if (!u->alive || u->side != SIDE_HORDE || u->dormant || u->moved)
			continue;
		u32 f0 = frame_count;
		act(z);
		// zombies in the fog act without frames: yield now and then so sound and input keep up
		if (frame_count != f0)
			quiet = 0, heavy = false;
		else if (heavy || ++quiet >= 4) {
			frame();
			quiet = 0;
			heavy = false;
		}
	}
	for (int i = 0; i < MAX_UNITS; i++)
		gs.units[i].moved = 0;
	gs.side = SIDE_SURVIVOR;
	gs.cur_x = cx;
	gs.cur_y = cy;
	spr.cursor_show = 0;
	tac_fog();
}
