// Dead Wars - AW2-style battle scene: survivors on the left, the horde on the
// right, one figure per displayed HP, HP counting down, counter, result.
// Art contract: docs/ASSET_SPEC.md "Battle scene".
#include "game.h"
#include "video.h"
#include "sound.h"

// VRAM while the scene is up (restored on close; the map lives elsewhere)
#define SCENE_BG_CBB   2
#define SCENE_BG_TILE0 128   // CBB2 tiles 0..127 hold SB_TEXT/SB_WINDOW
#define SCENE_BG_SB    26
#define OBJT_FIG       320   // 2 sides x BF_FRAMES x 16 tiles
#define OBJT_FX        448   // BFX_COUNT x 4 frames x 16 tiles
#define PANEL_ROWS     13    // backdrop rows; HP windows below

// figure slots (top-left of the 32x32) for the left, right-facing panel, back to front
static const u8 slots[10][2] = {
	{ 10, 50 }, { 40, 48 }, { 70, 50 }, { 24, 58 }, { 54, 57 },
	{ 84, 58 }, { 6, 66 }, { 36, 66 }, { 66, 67 }, { 20, 72 },
};

typedef struct {
	int type, side;
	int hp;           // shown HP, 0..100, counts down
	int pose;         // BF_* frame, or -1 to idle
	s8 lunge, shake;
	bool flash, risen;
	int fx, fx_t;     // effect on this side's figures (-1 none), frame timer
} Side;

static Side sides[2]; // [0] left = survivors, [1] right = horde
bool scene_on;
static bool skip;

static void vcopy(volatile u16 *dst, const void *src, int bytes)
{
	const u16 *s = src;
	for (int i = 0; i < bytes / 2; i++)
		dst[i] = s[i];
}

static void scene_wait(int n)
{
	while (n-- > 0 && !skip) {
		frame();
		if (keys_down & KEY_A)
			skip = true;
	}
}

static int backdrop_for(int tt)
{
	switch (tt) {
	case TT_FOREST: return BB_WOODS;
	case TT_RUINS: case TT_WALL: case TT_WRECK: case TT_HILL: return BB_RUINS;
	case TT_HOUSE: case TT_WORKSHOP: case TT_SAFEHOUSE: case TT_HOSPITAL: case TT_RADIO: return BB_CITY;
	case TT_NEST: return BB_NEST;
	default: return BB_PLAIN;
	}
}

// Backdrops are drawn for the right panel; the left one is mirrored.
static void draw_backdrop(int panel, int tt)
{
	const u16 (*m)[BATTLE_BG_W] = battle_bg_map[backdrop_for(tt)];
	volatile u16 *sb = SCREENBLOCK(SCENE_BG_SB);
	for (int y = 0; y < PANEL_ROWS; y++)
		for (int x = 0; x < BATTLE_BG_W; x++) {
			u16 e = panel ? m[y][x] : m[y][BATTLE_BG_W - 1 - x] ^ SE_HFLIP;
			sb[y * 32 + panel * 15 + x] = (SCENE_BG_TILE0 + (e & 0x3FF)) | (e & (SE_HFLIP | SE_VFLIP)) |
			                              SE_PAL(BGPAL_TERRAIN);
		}
}

static void draw_panel(int s)
{
	const Side *sd = &sides[s];
	int x = s * 15, hp = DISP_HP(sd->hp);
	text_put(x + 2, 15, unit_info[sd->type].name, sd->side == SIDE_SURVIVOR ? BGPAL_TEXT_BLUE : BGPAL_TEXT_RED);
	text_printf(x + 2, 17, hp > 3 ? BGPAL_TEXT_YEL : BGPAL_TEXT_RED, "%2d HP", hp);
}

static void load_figure(int s)
{
	int t = sides[s].type < UG_COUNT ? sides[s].type : UG_WALKER;
	vcopy(OBJ_VRAM + (OBJT_FIG + s * BF_FRAMES * 16) * 16, battle_fig_gfx[t], sizeof battle_fig_gfx[t]);
}

static void slot_xy(int s, int k, int *x, int *y)
{
	*x = slots[k][0];
	*y = slots[k][1];
	if (s)
		*x = 240 - 32 - *x;
}

// Called from build_oam while the scene is up. Front figures go first (on top).
void scene_oam(void)
{
	for (int s = 0; s < 2; s++) {
		const Side *sd = &sides[s];
		int n = sd->risen ? 3 : DISP_HP(sd->hp), dir = s ? -1 : 1;
		// effect frames on the first few figures
		if (sd->fx >= 0 && sd->fx_t < 32)
			for (int k = 9; k >= 10 - MIN(n, 3); k--) {
				int x, y;
				slot_xy(s, k, &x, &y);
				oam_add((y & 0xFF) | ATTR0_SQUARE, (x & 0x1FF) | ATTR1_SIZE32 | (s ? ATTR1_HFLIP : 0),
				        (OBJT_FX + (sd->fx * 4 + sd->fx_t / 8) * 16) | ATTR2_PRIO(0) |
				        ATTR2_PAL(battle_fx_pal[sd->fx] ? OBJPAL_HORDE : OBJPAL_CURSOR));
			}
		int pal = sd->flash && (frame_count & 4) ? OBJPAL_FLASH :
		          sd->side == SIDE_SURVIVOR ? OBJPAL_SURVIVOR : OBJPAL_HORDE;
		int pose = sd->pose >= 0 ? sd->pose : BF_IDLE0 + (int)((frame_count >> 4) & 1);
		for (int k = 9; k >= 10 - n; k--) {
			int x, y;
			slot_xy(s, k, &x, &y);
			x += (sd->lunge + sd->shake) * dir;
			oam_add((y & 0xFF) | ATTR0_SQUARE, (x & 0x1FF) | ATTR1_SIZE32 | (s ? ATTR1_HFLIP : 0),
			        (OBJT_FIG + (s * BF_FRAMES + pose) * 16) | ATTR2_PRIO(0) | ATTR2_PAL(pal));
		}
	}
}

static void play_fx(Side *sd, int fx)
{
	sd->fx = fx;
	for (sd->fx_t = 0; sd->fx_t < 32 && !skip; sd->fx_t++)
		scene_wait(1);
	sd->fx = -1;
}

// One side strikes the other: wind-up, fire with effect, flash, HP counts down to `to`.
static void strike(int from, int to_side, int to)
{
	Side *a = &sides[from], *d = &sides[to_side];
	a->pose = BF_ATK0;
	scene_wait(8);
	a->pose = BF_ATK1;
	a->lunge = 4;
	sfx(a->type == UT_SPITTER ? SFX_ACID : a->side == SIDE_HORDE ? SFX_BITE : SFX_SHOT);
	if (a->side == SIDE_SURVIVOR)
		a->fx = BFX_MUZZLE, a->fx_t = 0;
	d->fx = a->type == UT_SPITTER ? BFX_ACID : a->side == SIDE_HORDE ? BFX_SLASH : BFX_SPARK;
	d->flash = true;
	for (d->fx_t = 0; d->fx_t < 32 && !skip; d->fx_t++) {
		a->fx_t = d->fx_t;
		d->shake = d->fx_t < 16 ? ((d->fx_t & 2) ? 2 : -2) : 0;
		scene_wait(1);
	}
	a->fx = d->fx = -1;
	a->pose = -1;
	a->lunge = d->shake = 0;
	d->flash = false;
	while (d->hp > to) {
		d->hp = MAX(to, d->hp - 10);
		draw_panel(to_side);
		scene_wait(5);
	}
	d->hp = to;
	draw_panel(to_side);
	scene_wait(10);
}

void battle_scene(int a, int d)
{
	Unit *au = &gs.units[a], *du = &gs.units[d];
	// resolve combat first, with the same rng order as attack()
	int dmg = damage_estimate(au, du, true);
	int dhp = dmg >= du->hp ? 0 : du->hp - dmg, ahp = au->hp;
	bool countered = false;
	if (dhp) {
		Unit hurt = *du;
		hurt.hp = dhp;
		if (can_counter(au, &hurt)) {
			countered = true;
			int c = damage_estimate(&hurt, au, true);
			ahp = c >= au->hp ? 0 : au->hp - c;
		}
	}
	bool infect = !dhp && du->side == SIDE_SURVIVOR && (unit_info[du->type].flags & UF_INFANTRY) &&
	              au->side == SIDE_HORDE;

	// open: own BG2 setup and VRAM, map hidden
	int sa = au->side == SIDE_SURVIVOR ? 0 : 1, sd = !sa;
	sides[sa] = (Side){ .type = au->type, .side = au->side, .hp = au->hp, .pose = -1, .fx = -1 };
	sides[sd] = (Side){ .type = du->type, .side = du->side, .hp = du->hp, .pose = -1, .fx = -1 };
	skip = false;
	u16 dispcnt = REG_DISPCNT, bg2cnt = REG_BGCNT(2);
	REG_DISPCNT = dispcnt & ~(DCNT_BG1 | DCNT_BG2);
	text_clear();
	spr.map_visible = 0;
	vcopy(CHARBLOCK(SCENE_BG_CBB) + SCENE_BG_TILE0 * 16, battle_bg_tiles, sizeof battle_bg_tiles);
	vcopy(OBJ_VRAM + OBJT_FX * 16, battle_fx_gfx, sizeof battle_fx_gfx);
	load_figure(0);
	load_figure(1);
	draw_backdrop(sa, gs.terrain[au->y][au->x]);
	draw_backdrop(sd, gs.terrain[du->y][du->x]);
	REG_BGCNT(2) = BG_PRIO(3) | BG_CBB(SCENE_BG_CBB) | BG_SBB(SCENE_BG_SB) | BG_SIZE_256;
	REG_BGHOFS(2) = REG_BGVOFS(2) = 0;
	text_window(0, 13, 15, 7);
	text_window(15, 13, 15, 7);
	draw_panel(0);
	draw_panel(1);
	scene_on = true;
	REG_DISPCNT = (dispcnt & ~DCNT_BG1) | DCNT_BG2;
	scene_wait(20);

	strike(sa, sd, dhp);
	if (countered)
		strike(sd, sa, ahp);
	// result beat
	int dead = !dhp ? sd : !ahp ? sa : -1;
	if (infect) {
		Side *s = &sides[sd];
		sfx(SFX_INFECT);
		text_put(sd * 15 + 2, 18, "Rises again", BGPAL_TEXT_RED);
		s->type = UT_WALKER;
		s->side = SIDE_HORDE;
		load_figure(sd);
		s->risen = true;
		play_fx(s, BFX_RISE);
		scene_wait(30);
	} else if (dead >= 0) {
		sfx(SFX_DESTROY);
		text_put(dead * 15 + 2, 18, "Destroyed", BGPAL_TEXT_GREY);
		scene_wait(30);
	}
	scene_wait(20);

	// close and apply, so map effects (explosion, rising, bursts) play on the map
	scene_on = false;
	REG_DISPCNT = dispcnt & ~(DCNT_BG1 | DCNT_BG2);
	text_clear();
	REG_BGCNT(2) = bg2cnt;
	spr.map_visible = 1;
	frame(); // scroll registers back before showing the map
	REG_DISPCNT = dispcnt;
	if (!dhp) {
		du->hp = 0;
		kill_unit(d, a);
	} else {
		du->hp = dhp;
		if (!ahp) {
			au->hp = 0;
			kill_unit(a, d);
		} else {
			au->hp = ahp;
		}
	}
}

// Use the scene for visible fights when the setting is on (not while B
// fast-forwards the horde).
bool scene_wanted(int a, int d)
{
	const Unit *au = &gs.units[a], *du = &gs.units[d];
	if (tac_mode || profile.battle_off || (gs.side == SIDE_HORDE && (keys_held & KEY_B)))
		return false;
	return !gs.fog[au->y][au->x] || !gs.fog[du->y][du->x];
}
