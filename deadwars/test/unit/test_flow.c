// Dead Wars: The Last Boat - host tests of the horde turn (tac_ai.c) and the mission flow (tac.c).
// tac.c is included so its static helpers (do_move, the people notices) can be called directly;
// the UI it calls (text, menus, banners, the path arrow) is stubbed below. See Makefile.
#include "../../src/tac.c"
#include "unit.h"

int checks, fails;
const char *test_name = "";

// ---------------------------------------------------------------- UI stand-ins
static char banner_l1[8][32];
static int banners;

void banner(const char *l1, const char *l2, int frames)
{
	if (banners < 8)
		snprintf(banner_l1[banners], sizeof banner_l1[0], "%s", l1 ? l1 : "");
	banners++;
}

void text_clear(void) {}
void text_put(int x, int y, const char *s, int pal) {}
void text_window(int x, int y, int w, int h) {}
void text_clear_rect(int x, int y, int w, int h) {}
void text_printf(int x, int y, int pal, const char *fmt, ...) {}
int text_menu(int x, int y, int w, const char *const *items, const u8 *enabled, int count, int start) { return -1; }
void oam_add(u16 a0, u16 a1, u16 a2) {}
void path_arrow_clear(void) {}
void path_arrow_update(const u8 *d, const u8 *p, int sx, int sy) {}
void video_load_game_gfx(void) {}

void unit_move_anim(int ui, const u16 *path, int len)
{
	if (len > 0) {
		gs.units[ui].x = path[len - 1] & 0xFF;
		gs.units[ui].y = path[len - 1] >> 8;
	}
}

// battle.c's, which the host build doesn't link
int build_path(const u8 *p, int sx, int sy, int tx, int ty, u16 *path)
{
	static const s8 bx[4] = { 0, 1, 0, -1 }, by[4] = { -1, 0, 1, 0 };
	u16 rev[64];
	int n = 0, x = tx, y = ty;
	while ((x != sx || y != sy) && n < 64) {
		rev[n++] = x | (y << 8);
		int k = p[IDX(x, y)];
		if (k == NO_PATH)
			break;
		x -= bx[k];
		y -= by[k];
	}
	for (int i = 0; i < n; i++)
		path[i] = rev[n - 1 - i];
	return n;
}

// ---------------------------------------------------------------- helpers
enum { MARA, DOC, JUNE, HOLT };
static const u8 crew[4] = { MARA, DOC, JUNE, HOLT };

static void load(const TacMission *m, int n, u32 seed)
{
	memset(&run, 0, sizeof run);
	run.day = 5;
	run.difficulty = 1;
	run.nroster = 4;
	for (int i = 0; i < 4; i++) {
		const CastInfo *c = &cast_info[i];
		Person *p = &run.roster[i];
		p->cast = i;
		p->alive = 1;
		p->grit = c->grit;
		p->aim = c->aim;
		p->speed = c->speed;
		p->nerve = c->nerve;
		memcpy(p->trait, c->trait, sizeof p->trait);
		memcpy(p->skill, c->skill, sizeof p->skill);
		p->weapon = c->weapon;
		p->armor = c->armor;
		p->hp = p->maxhp = 6 + c->grit;
	}
	run.res[RES_AMMO] = 20;
	run.res[RES_PARTS] = 5;
	gs.rng = seed;
	tac_load(m, crew, n);
	banners = 0;
}

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
}

// an aware walker heading for (gx, gy)
static int walker(int x, int y, int gx, int gy)
{
	int z = tac_spawn_zombie(UT_WALKER, x, y, false);
	CHECK(z >= 0);
	if (z >= 0) {
		tac.actor[z].alerted = 1;
		tac.actor[z].gx = gx;
		tac.actor[z].gy = gy;
	}
	return z;
}

/* yard: x 0123456789
 *   2    ...HH.....     a house (3,2)-(4,3), its door (3,3) opens S onto (3,4)
 *   3    ...hH.....
 *   7    oooo......
 */
static const char *const yard_rows[] = {
	"..........",
	"..........",
	"...HH.....",
	"...hH.....",
	"..........",
	"..........",
	"..........",
	"oooo......",
	NULL,
};
static const UnitPlacement no_z[] = { { 0xFF, 0, 0, 0, 0, 0 } };
static const TacMission yard = { "Yard", "", yard_rows, no_z, OBJ_SCAVENGE, 0, 0, { 0, 0, 0 }, 3 };

// ---------------------------------------------------------------- the horde turn
// A barricaded door is broken (6 HP, a walker does 1): the basher's own noise doesn't make it
// forget the quiet person inside it was after.
static void test_door_bash(void)
{
	test_name = "door bash";
	for (u32 seed = 1; seed <= 5; seed++) {
		load(&yard, 1, seed * 7919);
		int mara = slot(MARA);
		put(mara, 4, 2);                       // inside, off the door tile, quiet
		Building *b = &tac.bldg[tac.bmap[3][3] - 1];
		b->door_hp = 6;
		int z = walker(3, 6, 4, 2);            // heard the barricading
		tac_fog();
		int turns = 0;
		while (b->door_hp && turns < 8) {
			tac_horde_turn();
			turns++;
			if (b->door_hp && !tac.actor[z].alerted)
				break;
		}
		EQ(b->door_hp, 0);
		CHECK(turns <= 6);                     // walk up and bash, then one bash a turn
		EQ(tac.actor[z].alerted, 1);
		EQ(tac.actor[z].gx, 4);                // it still knows where Mara is
		EQ(tac.actor[z].gy, 2);
	}
}

// A walker heading for a noise inside the house stops by the east wall on its way to the door:
// that isn't reaching the noise.
static void test_noise_through_wall(void)
{
	test_name = "noise through a wall";
	int forgot = 0;
	for (u32 seed = 1; seed <= 20; seed++) {
		load(&yard, 1, seed * 7919);
		put(slot(MARA), 0, 7);                 // far away, out of its sight
		int z = walker(5, 0, 4, 2);            // a shot from (4,2)
		tac_fog();
		tac_horde_turn();
		forgot += !tac.actor[z].alerted;
	}
	EQ(forgot, 0);
	// it does give up on reaching the noise across an open edge
	load(&yard, 1, 3);
	put(slot(MARA), 0, 7);
	int z = walker(8, 2, 5, 2);
	tac_fog();
	tac_horde_turn();
	EQ(tac.actor[z].alerted, 0);
}

// A walker next to Doc in the open, with Mara behind the house wall on its other side: it bites
// Doc whatever their slots, and doesn't walk off to the door.
static void test_bite_now(void)
{
	test_name = "bite now";
	static const struct { u8 dx, dy, zx, zy; } cases[] = {
		{ 6, 2, 5, 2 },                        // Doc east of it
		{ 5, 3, 5, 2 },                        // Doc south of it, on its only way round
		{ 7, 2, 6, 2 },                        // Doc two tiles east: a step and a bite
	};
	for (int c = 0; c < 3; c++)
		for (int order = 0; order < 2; order++) {
			int bites = 0, n = 0;
			for (u32 seed = 1; seed <= 20; seed++, n++) {
				load(&yard, 2, seed * 7919);
				int in = order ? slot(DOC) : slot(MARA), out = order ? slot(MARA) : slot(DOC);
				put(in, 4, 2);                 // inside, next to the zombie through the wall
				put(out, cases[c].dx, cases[c].dy);
				int hp_in = tac.actor[in].hp;
				int z = walker(5, 2, 5, 2);
				tac_fog();
				tac_horde_turn();
				// a bite makes noise 2, hit or miss; nothing else does here
				if (tac.horde == 2 && gs.units[z].x == cases[c].zx && gs.units[z].y == cases[c].zy &&
				    tac.actor[in].hp == hp_in)
					bites++;
			}
			if (bites != n)
				FAILF("case %d order %d: bit the person in the open %d of %d times", c, order, bites, n);
			else
				checks++;
		}
}

// ---------------------------------------------------------------- moving (tac.c)
// Stepping onto a door lifts the roof: whatever is inside is seen, so the move can't be undone
// even when no fogged tile cleared (the interior is in sight from outside).
static void test_undo_roof(void)
{
	test_name = "undo under a roof";
	const TacMission *m = &tac_missions[0];  // Main Street: a dormant walker in the house at (15,1)
	load(m, 3, 11);
	int mara = slot(MARA), june = slot(JUNE), doc = slot(DOC);
	put(mara, 15, 3);
	put(june, 13, 1);
	put(doc, 17, 1);
	tac_fog();
	int z = unit_at(15, 1);
	CHECK(z >= 0 && gs.units[z].side == SIDE_HORDE && gs.units[z].dormant);
	if (z < 0)
		return;
	EQ(shown(z), false);
	tac_paths(mara, dist, prev);
	CHECK(dist[IDX(15, 2)] != NO_PATH);
	bool undo = do_move(mara, 15, 2);
	EQ(gs.units[mara].x, 15);
	EQ(gs.units[mara].y, 2);
	EQ(revealed(), false);                   // no fog cleared: only the roof came off
	EQ(shown(z), true);
	EQ(undo, false);
	// an empty house tells as much: in the west house at (3,1) (door (3,2), facing S)
	load(m, 3, 11);
	mara = slot(MARA);
	june = slot(JUNE);
	doc = slot(DOC);
	put(mara, 3, 3);
	put(june, 1, 2);
	put(doc, 5, 2);
	tac_fog();
	tac_paths(mara, dist, prev);
	CHECK(dist[IDX(3, 2)] != NO_PATH);
	undo = do_move(mara, 3, 2);
	EQ(revealed(), false);
	EQ(undo, false);
	// a step in the open that shows nothing new can still be taken back
	load(m, 3, 11);
	mara = slot(MARA);
	june = slot(JUNE);
	doc = slot(DOC);
	put(mara, 3, 4);
	put(june, 1, 2);
	put(doc, 5, 2);
	tac_fog();
	tac_paths(mara, dist, prev);
	undo = do_move(mara, 3, 3);
	EQ(revealed(), false);
	EQ(undo, true);
}

// ---------------------------------------------------------------- people notices (tac.c)
static void notice_setup(int who)
{
	dep_n = 1;
	dep_person[0] = who;
	dep_unit[0] = slot(who);
	dep_state[0] = person_state(0);
	fx_ready = true;
	banners = 0;
}

static void test_notices(void)
{
	test_name = "people notices";
	// one bite downs and infects June: both are told, the downing first
	load(&yard, 3, 21);
	int june = slot(JUNE);
	notice_setup(JUNE);
	tac.actor[june].down = 2;
	run.roster[JUNE].flags |= PF_INFECTED;
	tac_people_notice();
	EQ(banners, 2);
	CHECK(!strcmp(banner_l1[0], "June is down!"));
	CHECK(!strcmp(banner_l1[1], "June was bitten"));
	banners = 0;
	tac_people_notice();                     // once each
	EQ(banners, 0);
	tac.actor[june].down = 0;                // helped up: still bitten, nothing new
	tac_people_notice();
	EQ(banners, 0);
	// bitten and killed at once: death says it all
	load(&yard, 3, 22);
	june = slot(JUNE);
	notice_setup(JUNE);
	run.roster[JUNE].flags |= PF_INFECTED;
	run.roster[JUNE].alive = 0;
	tac_people_notice();
	EQ(banners, 1);
	CHECK(!strcmp(banner_l1[0], "June is dead"));
	tac_people_notice();
	EQ(banners, 1);
	// just bitten
	load(&yard, 3, 23);
	notice_setup(JUNE);
	run.roster[JUNE].flags |= PF_INFECTED;
	tac_people_notice();
	EQ(banners, 1);
	CHECK(!strcmp(banner_l1[0], "June was bitten"));
	fx_ready = false;
	dep_n = 0;
}

// ---------------------------------------------------------------- mission start
// cam_px / cam_py still hold the last map's scroll (a Hensley Farm east exit leaves 160 px): the
// new map must come up on its deploy point, not glide there from the old offset on screen.
extern void (*frame_hook)(void); // stubs.c
static int cam_frames, cam_off;

static void cam_watch(void)
{
	if (spr.map_visible) {
		cam_frames++;
		if (cam_px != gs.cam_x * 16 || cam_py != gs.cam_y * 16)
			cam_off++;
	}
	tac.result = 2;    // end it after the briefing
	keys_down = KEY_A; // past the briefing and the results
}

static void test_start_camera(void)
{
	test_name = "mission start camera";
	const TacMission *m = &tac_missions[1]; // Miller's Bridge: deploys on the west edge
	load(m, 4, 31);
	cam_px = 160;
	cam_py = 32;
	cam_frames = cam_off = 0;
	frame_hook = cam_watch;
	tac_run(m, crew, 4);
	frame_hook = NULL;
	keys_down = 0;
	CHECK(cam_frames > 0);
	EQ(cam_off, 0);
	EQ(cam_px, gs.cam_x * 16);
	EQ(cam_py, gs.cam_y * 16);
}

int main(void)
{
	test_door_bash();
	test_noise_through_wall();
	test_bite_now();
	test_undo_roof();
	test_notices();
	test_start_camera();
	printf("tac flow: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
