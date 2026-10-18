// Dead Wars - player-facing info: unit/terrain card (R), damage forecast,
// movement path arrow, turn-start summary
#include "game.h"
#include "video.h"
#include <string.h>

#define IDX(x, y) ((y) * MAP_MAX_W + (x))
#define NO_PATH 255

static const char *const movetype_names[MOVE_COUNT] = { "Foot", "Boots", "Tires", "Treads", "Shamble" };

// Word-wrap s into lines of width w starting at (x, y). Returns lines used.
static int text_wrap(int x, int y, int w, const char *s, int pal)
{
	char line[32];
	int rows = 0;
	while (*s) {
		int n = strlen(s), cut = n;
		if (n > w)
			for (cut = w; cut > 0 && s[cut] != ' '; cut--)
				;
		if (cut <= 0)
			cut = MIN(n, w);
		memcpy(line, s, cut);
		line[cut] = 0;
		text_put(x, y + rows++, line, pal);
		s += cut;
		while (*s == ' ')
			s++;
	}
	return rows;
}

static void wait_close(void)
{
	do
		frame();
	while (!(keys_down & (KEY_A | KEY_B | KEY_R)));
	text_clear();
}

// ---------------------------------------------------------------- R: info card

static void terrain_card(int x, int y)
{
	int tt = gs.terrain[y][x];
	const TerrainInfo *t = &terrain_info[tt];
	text_clear();
	text_window(3, 3, 24, 13);
	text_put(5, 4, t->name, BGPAL_TEXT_YEL);
	text_printf(5, 6, BGPAL_UI, "Defense %d", t->defense);
	if (t->building) {
		int o = gs.owner[y][x];
		text_put(16, 6, o == SIDE_SURVIVOR ? "Ours" : o == SIDE_HORDE ? "Overrun" : "Neutral",
		         o == SIDE_SURVIVOR ? BGPAL_TEXT_BLUE : o == SIDE_HORDE ? BGPAL_TEXT_RED : BGPAL_TEXT_GREY);
		text_printf(5, 7, BGPAL_UI, "Income $%d", t->income);
	}
	text_put(5, 9, "Move cost", BGPAL_TEXT_GREY);
	for (int m = 0; m < MOVE_COUNT; m++) {
		int c = t->cost[m];
		text_put(5, 10 + m, movetype_names[m], BGPAL_UI);
		if (c == IMPASSABLE)
			text_put(14, 10 + m, "-", BGPAL_TEXT_GREY);
		else
			text_putn(14, 10 + m, c, BGPAL_UI);
	}
	wait_close();
}

static void unit_card(int ui)
{
	const Unit *u = &gs.units[ui];
	const UnitInfo *inf = &unit_info[u->type];
	bool ours = u->side == SIDE_SURVIVOR;
	text_clear();
	text_window(0, 0, 30, 20);
	text_put(2, 1, inf->name, ours ? BGPAL_TEXT_BLUE : BGPAL_TEXT_RED);
	text_printf(20, 1, BGPAL_UI, "HP %2d/10", DISP_HP(u->hp));
	text_wrap(2, 2, 26, inf->desc, BGPAL_TEXT_GREY);
	text_printf(2, 4, BGPAL_UI, "Move %d %s", unit_move_points(u), movetype_names[inf->movetype]);
	text_printf(18, 4, BGPAL_UI, "Vision %d", inf->vision);
	if (!inf->maxrange)
		text_put(2, 5, "No attack", BGPAL_UI);
	else if (inf->minrange == inf->maxrange)
		text_printf(2, 5, BGPAL_UI, "Range %d", inf->maxrange);
	else
		text_printf(2, 5, BGPAL_UI, "Range %d-%d", inf->minrange, inf->maxrange);
	text_printf(18, 5, BGPAL_UI, "Cost $%d", inf->cost);
	// traits
	char traits[48] = "";
	if ((inf->flags & UF_INFANTRY) && ours) strcat(traits, "Captures ");
	if (inf->flags & UF_INDIRECT) strcat(traits, "No move+fire ");
	if (inf->flags & UF_HEALER) strcat(traits, "Heals ");
	if (inf->flags & UF_TRANSPORT) strcat(traits, "Carries 1 ");
	if (inf->flags & UF_EXPLODES) strcat(traits, "Bursts ");
	text_put(2, 6, traits, BGPAL_TEXT_YEL);
	if ((inf->flags & UF_INFANTRY) && ours)
		text_put(2, 7, "Rises as a Walker if bitten", BGPAL_TEXT_RED);
	// base damage both ways against every enemy type, at full HP
	text_put(2, 8, "vs", BGPAL_TEXT_GREY);
	text_put(14, 8, "Deals", BGPAL_TEXT_GREY);
	text_put(21, 8, "Takes", BGPAL_TEXT_GREY);
	int first = ours ? FIRST_ZOMBIE : 0, last = ours ? UT_COUNT : FIRST_ZOMBIE;
	for (int e = first, row = 9; e < last; e++, row++) {
		int deal = base_damage[u->type][e], take = base_damage[e][u->type];
		text_put(2, row, unit_info[e].name, ours ? BGPAL_TEXT_RED : BGPAL_TEXT_BLUE);
		if (deal)
			text_printf(14, row, BGPAL_UI, "%3d%%", deal);
		else
			text_put(16, row, "-", BGPAL_TEXT_GREY);
		if (take)
			text_printf(21, row, BGPAL_UI, "%3d%%", take);
		else
			text_put(23, row, "-", BGPAL_TEXT_GREY);
	}
	int tt = gs.terrain[u->y][u->x];
	text_printf(2, 17, BGPAL_TEXT_GREY, "On %s, defense %d", terrain_info[tt].name, terrain_info[tt].defense);
	wait_close();
}

// Info card for whatever is under the cursor (zombies hidden by fog stay hidden).
void info_card(void)
{
	int x = gs.cur_x, y = gs.cur_y, o = unit_at(x, y);
	if (o >= 0 && !(gs.units[o].side == SIDE_HORDE && gs.fog[y][x]))
		unit_card(o);
	else
		terrain_card(x, y);
}

// ---------------------------------------------------------------- forecast

// Damage window while choosing a target: our hit and their counter, as luck ranges.
void forecast_draw(int a, int d)
{
	const Unit *au = &gs.units[a], *du = &gs.units[d];
	int lo = damage_calc(au, du, 0), hi = damage_calc(au, du, 9);
	int px = (du->x - gs.cam_x) < 8 ? 17 : 1;
	text_clear();
	text_window(px, 1, 12, 5);
	text_put(px + 1, 2, unit_info[du->type].name, BGPAL_TEXT_RED);
	text_printf(px + 1, 3, BGPAL_TEXT_YEL, "Hit %d-%d%%", MIN(lo, 100), MIN(hi, 100));
	bool counters = ABS(au->x - du->x) + ABS(au->y - du->y) == 1 &&
	                !(unit_info[du->type].flags & UF_INDIRECT) && unit_info[du->type].maxrange >= 1 &&
	                base_damage[du->type][au->type];
	if (lo >= du->hp) {
		text_put(px + 1, 4, "Destroys", BGPAL_TEXT_BLUE);
	} else if (!counters) {
		text_put(px + 1, 4, "No counter", BGPAL_TEXT_BLUE);
	} else {
		// weakest counter after our best roll, strongest after our worst
		Unit hurt = *du;
		hurt.hp = du->hp - MIN(hi, du->hp - 1);
		int clo = hi >= du->hp ? 0 : damage_calc(&hurt, au, 0);
		hurt.hp = du->hp - lo;
		int chi = damage_calc(&hurt, au, 9);
		text_printf(px + 1, 4, BGPAL_TEXT_RED, "Back %d-%d%%", MIN(clo, 100), MIN(chi, 100));
	}
}

// ---------------------------------------------------------------- path arrow

#define ARROW_MAX 24
static u16 arrow_path[ARROW_MAX];
static u8 arrow_dir[ARROW_MAX]; // direction stepped into each tile
static int arrow_len;
static u8 arrow_sx, arrow_sy;

enum { PIECE_STRAIGHT_H, PIECE_STRAIGHT_V, PIECE_CORNER, PIECE_HEAD_R, PIECE_HEAD_D, PIECE_COUNT };

// Shape of each 16x16 piece. Bands may run past the tile edge so they get no
// outline where they join the next piece. V pieces are the H ones transposed;
// the corner joins right and down.
static bool piece_has(int piece, int x, int y)
{
	if (piece == PIECE_STRAIGHT_V || piece == PIECE_HEAD_D) {
		int t = x; x = y; y = t;
		piece = piece == PIECE_STRAIGHT_V ? PIECE_STRAIGHT_H : PIECE_HEAD_R;
	}
	bool hband = y >= 5 && y <= 10, vband = x >= 5 && x <= 10;
	switch (piece) {
	case PIECE_STRAIGHT_H: return hband;
	case PIECE_CORNER: return (hband && x >= 5) || (vband && y >= 5);
	default: // head pointing right
		if (hband && x < 8)
			return true;
		return x >= 7 && x <= 14 && ABS(y * 2 - 15) <= 2 * (14 - x) - 1;
	}
}

void path_arrow_load_gfx(void)
{
	static const u16 pal[4] = { 0, RGB15(5, 2, 2), RGB15(31, 8, 5), RGB15(31, 24, 18) };
	for (int i = 0; i < 4; i++)
		PAL_OBJ[16 * OBJPAL_ARROW + i] = pal[i];
	for (int p = 0; p < PIECE_COUNT; p++)
		for (int y = 0; y < 16; y++)
			for (int x = 0; x < 16; x += 4) {
				u16 row = 0;
				for (int i = 0; i < 4; i++) {
					int px = x + i, c = 0;
					if (piece_has(p, px, y)) {
						bool edge = !piece_has(p, px - 1, y) || !piece_has(p, px + 1, y) ||
						            !piece_has(p, px, y - 1) || !piece_has(p, px, y + 1);
						c = edge ? 1 : (y == 6 || px == 6) ? 3 : 2; // outline, highlight, fill
					}
					row |= c << (i * 4);
				}
				// 1D mapping: TL, TR, BL, BR 8x8 tiles, 16 u16 per tile
				int tile = OBJT_ARROW + p * 4 + (y >= 8 ? 2 : 0) + (x >= 8 ? 1 : 0);
				OBJ_VRAM[tile * 16 + (y & 7) * 2 + ((x & 7) >> 2)] = row;
			}
}

void path_arrow_clear(void)
{
	arrow_len = 0;
}

// Show the route from (sx, sy) to the cursor using find_paths() output.
void path_arrow_update(const u8 *dist, const u8 *prev, int sx, int sy)
{
	u16 path[64];
	arrow_len = 0;
	if (dist[IDX(gs.cur_x, gs.cur_y)] == NO_PATH)
		return;
	int len = build_path(prev, sx, sy, gs.cur_x, gs.cur_y, path);
	if (len > ARROW_MAX)
		return;
	int px = sx, py = sy;
	for (int i = 0; i < len; i++) {
		int x = path[i] & 0xFF, y = path[i] >> 8;
		arrow_dir[i] = x > px ? 1 : x < px ? 3 : y > py ? 2 : 0;
		arrow_path[i] = path[i];
		px = x;
		py = y;
	}
	arrow_sx = sx;
	arrow_sy = sy;
	arrow_len = len;
}

// Called from build_oam after the units, so the arrow sits under them.
void path_arrow_oam(void)
{
	for (int i = 0; i < arrow_len; i++) {
		int x = (arrow_path[i] & 0xFF) * 16 - cam_px, y = (arrow_path[i] >> 8) * 16 - cam_py;
		if (x <= -16 || x >= 240 || y <= -16 || y >= 160)
			continue;
		int din = arrow_dir[i], piece, flip = 0;
		if (i == arrow_len - 1) {
			piece = (din & 1) ? PIECE_HEAD_R : PIECE_HEAD_D;
			if (din == 3) flip = ATTR1_HFLIP;
			if (din == 0) flip = ATTR1_VFLIP;
		} else if (arrow_dir[i + 1] == din) {
			piece = (din & 1) ? PIECE_STRAIGHT_H : PIECE_STRAIGHT_V;
		} else {
			// sides touched: where we came from and where we go next
			int from = (din + 2) & 3, to = arrow_dir[i + 1];
			piece = PIECE_CORNER;
			if (from == 3 || to == 3) flip |= ATTR1_HFLIP;
			if (from == 0 || to == 0) flip |= ATTR1_VFLIP;
		}
		oam_add((y & 0xFF) | ATTR0_SQUARE, (x & 0x1FF) | ATTR1_SIZE16 | flip,
		        (OBJT_ARROW + piece * 4) | ATTR2_PRIO(2) | ATTR2_PAL(OBJPAL_ARROW));
	}
}

// ---------------------------------------------------------------- turn summary

u8 turn_risen; // survivors turned by bites since summary_mark_horde()
static u16 losses0;
static u32 funds0;
static int hp0;

static int survivor_hp(void)
{
	int n = 0;
	for (int i = 0; i < MAX_UNITS; i++) {
		const Unit *u = &gs.units[i];
		if (u->alive && !u->carried && u->side == SIDE_SURVIVOR)
			n += DISP_HP(u->hp);
	}
	return n;
}

// before the horde moves
void summary_mark_horde(void)
{
	turn_risen = 0;
	losses0 = gs.losses;
}

// right before start_side_turn(SIDE_SURVIVOR) pays income and heals
void summary_mark_upkeep(void)
{
	funds0 = gs.funds;
	hp0 = survivor_hp();
}

// Day banner plus what happened overnight. Closes after `frames` or on A/B.
void summary_show(const char *l1, const char *l2, int frames)
{
	int income = gs.funds - funds0, healed = survivor_hp() - hp0, lost = gs.losses - losses0;
	const int x = 6, y = 5, w = 18, h = 9;
	text_window(x, y, w, h);
	text_put(x + (w - strlen(l1)) / 2, y + 1, l1, BGPAL_TEXT_YEL);
	text_put(x + (w - strlen(l2)) / 2, y + 2, l2, BGPAL_UI);
	text_put(x + 2, y + 4, "Supplies", BGPAL_UI);
	text_printf(x + 11, y + 4, income ? BGPAL_TEXT_BLUE : BGPAL_TEXT_GREY, "+$%d", income);
	text_put(x + 2, y + 5, "Healed", BGPAL_UI);
	text_printf(x + 11, y + 5, healed ? BGPAL_TEXT_BLUE : BGPAL_TEXT_GREY, "+%dHP", healed);
	text_put(x + 2, y + 6, "Lost", BGPAL_UI);
	text_printf(x + 11, y + 6, lost ? BGPAL_TEXT_RED : BGPAL_TEXT_GREY, "%d", lost);
	text_put(x + 2, y + 7, "Turned", BGPAL_UI);
	text_printf(x + 11, y + 7, turn_risen ? BGPAL_TEXT_RED : BGPAL_TEXT_GREY, "%d", turn_risen);
	for (int i = 0; i < frames; i++) {
		frame();
		if (keys_down & (KEY_A | KEY_B))
			break;
	}
	text_clear_rect(x, y, w, h);
}
