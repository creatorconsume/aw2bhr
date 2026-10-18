// Dead Wars - AW2-style battle scene: survivors on the left, the horde on the
// right, one figure per displayed HP, HP bars counting down, counter, result.
// Placeholder art: the map sprites at 2x through an affine matrix.
#include "game.h"
#include "video.h"
#include "sound.h"
#include <string.h>

#define FIG_PER_ROW 5
#define GROUND_ROW  10   // first text row of the ground strip (4 rows tall)

typedef struct {
	int unit, type, side;
	int hp;              // shown HP, 0..100, counts down
	s8 lunge, shake;     // pixel offsets this frame
	bool flash, raw;     // flash palette / draw even at 0 HP (a riser)
	s16 rise;            // riser: pixels still below ground
} Side;

static Side sides[2]; // [0] left = survivors, [1] right = horde
bool scene_on;
static bool skip;

static void scene_wait(int n)
{
	while (n-- > 0 && !skip) {
		frame();
		if (keys_down & KEY_A)
			skip = true;
	}
}

// Ground strip: the unit's own terrain tile repeated under its half of the screen.
static void draw_ground(int half, int x, int y)
{
	int mt = metatile_for(x, y);
	int team = gs.owner[y][x] == SIDE_SURVIVOR ? 1 : gs.owner[y][x] == SIDE_HORDE ? 2 : 0;
	volatile u16 *sb = SCREENBLOCK(SB_TEXT);
	for (int ty = 0; ty < 4; ty++)
		for (int tx = 0; tx < 15; tx++) {
			u16 e = terrain_meta[mt][(ty & 1) * 2 + ((tx + half) & 1)];
			sb[(GROUND_ROW + ty) * 32 + half * 15 + tx] =
				(TILE_TERRAIN + (e & 0x3FF)) | (e & (SE_HFLIP | SE_VFLIP)) | SE_PAL(BGPAL_TERRAIN + team);
		}
}

static void draw_panel(int s)
{
	const Side *sd = &sides[s];
	int x = s * 15, pal = sd->side == SIDE_SURVIVOR ? BGPAL_TEXT_BLUE : BGPAL_TEXT_RED;
	int hp = DISP_HP(sd->hp);
	text_put(x + 1, 15, unit_info[sd->type].name, pal);
	text_printf(x + 9, 15, BGPAL_UI, "HP%3d", hp);
	char bar[11];
	for (int i = 0; i < 10; i++)
		bar[i] = i < hp ? '=' : '-';
	bar[10] = 0;
	text_put(x + 2, 17, bar, hp > 3 ? BGPAL_TEXT_YEL : BGPAL_TEXT_RED);
}

// Called from build_oam while the scene is up.
void scene_oam(void)
{
	int fr = (frame_count >> 3) & 1;
	for (int s = 0; s < 2; s++) {
		const Side *sd = &sides[s];
		int n = sd->raw ? 1 : DISP_HP(sd->hp);
		int pal = sd->flash && (frame_count & 4) ? OBJPAL_FLASH : sd->side == SIDE_SURVIVOR ? OBJPAL_SURVIVOR : OBJPAL_HORDE;
		for (int k = 0; k < n; k++) {
			int col = k % FIG_PER_ROW, row = k / FIG_PER_ROW;
			int fx = 4 + col * 20 + row * 10 + sd->lunge + sd->shake;
			int fy = (row ? 44 : 58) + (sd->raw ? sd->rise : 0);
			if (s)
				fx = 240 - 32 - fx;
			// affine (0x100) + double size (0x200): 16x16 drawn 32x32; matrix 0 faces right, 1 left
			oam_add((fy & 0xFF) | 0x300, (fx & 0x1FF) | (s << 9) | ATTR1_SIZE16,
			        (OBJT_UNITS + (sd->type * 2 + fr) * 4) | ATTR2_PRIO(0) | ATTR2_PAL(pal));
		}
	}
	oam_affine(0, 0x80, 0, 0, 0x80);
	oam_affine(1, -0x80, 0, 0, 0x80);
}

// One side strikes the other: lunge, flash, HP counts down to `to`.
static void strike(int from, int to_side, int to)
{
	Side *a = &sides[from], *d = &sides[to_side];
	sfx(a->type == UT_SPITTER ? SFX_ACID : a->side == SIDE_HORDE ? SFX_BITE : SFX_SHOT);
	for (int i = 0; i < 16 && !skip; i++) {
		a->lunge = i < 8 ? i : 16 - i; // mirrored for the right side: always toward the middle
		d->flash = i >= 6;
		d->shake = i >= 6 ? ((i & 2) ? 2 : -2) : 0;
		scene_wait(1);
	}
	a->lunge = 0;
	d->shake = 0;
	while (d->hp > to) {
		d->hp = MAX(to, d->hp - 10);
		draw_panel(to_side);
		scene_wait(5);
	}
	d->hp = to;
	d->flash = false;
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

	// open
	int sa = au->side == SIDE_SURVIVOR ? 0 : 1, sd = !sa;
	memset(sides, 0, sizeof sides);
	sides[sa] = (Side){ .unit = a, .type = au->type, .side = au->side, .hp = au->hp };
	sides[sd] = (Side){ .unit = d, .type = du->type, .side = du->side, .hp = du->hp };
	skip = false;
	u16 dispcnt = REG_DISPCNT;
	text_clear();
	REG_DISPCNT = dispcnt & ~(DCNT_BG1 | DCNT_BG2);
	spr.map_visible = 0;
	scene_on = true;
	draw_ground(sa, au->x, au->y);
	draw_ground(sd, du->x, du->y);
	text_window(0, 14, 15, 6);
	text_window(15, 14, 15, 6);
	draw_panel(0);
	draw_panel(1);
	scene_wait(20);

	strike(sa, sd, dhp);
	if (countered)
		strike(sd, sa, ahp);
	// result beat
	int dead = !dhp ? sd : !ahp ? sa : -1;
	if (infect) {
		text_put(sd * 15 + 2, 18, "Rises again...", BGPAL_TEXT_RED);
		Side *s = &sides[sd];
		s->type = UT_WALKER;
		s->side = SIDE_HORDE;
		s->raw = true;
		sfx(SFX_INFECT);
		for (s->rise = 32; s->rise > 0 && !skip; s->rise--)
			scene_wait(1);
		s->rise = 0;
		scene_wait(30);
	} else if (dead >= 0) {
		sfx(SFX_DESTROY);
		text_put(dead * 15 + 2, 18, "Destroyed", BGPAL_TEXT_GREY);
		scene_wait(30);
	}
	scene_wait(20);

	// close and apply, so map effects (explosion, rising, bursts) play on the map
	scene_on = false;
	text_clear();
	REG_DISPCNT = dispcnt;
	spr.map_visible = 1;
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
	if (profile.battle_off || (gs.side == SIDE_HORDE && (keys_held & KEY_B)))
		return false;
	return !gs.fog[au->y][au->x] || !gs.fog[du->y][du->x];
}
