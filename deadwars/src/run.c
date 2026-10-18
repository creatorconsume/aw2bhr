// Dead Wars: The Last Boat - the run: journey map, travel and the clock, stops, deploy, camp jobs,
// caravan, journal and the final screens (design: docs/design/LAST_BOAT.md 3, 6, 7, 10, 12, 13, 18).
// JOURNEY (pick a road) -> TRAVEL (days pass) -> ARRIVE (save) -> STOP (choose one) -> [mission]
// -> CAMP (jobs, save) -> JOURNEY ... until the bridge is crossed, everyone is gone or the boat
// sails. Autosaves: on arrival, right after a mission, after each camp and on Save & quit.
#include "lb.h"
#include "lbart.h"
#include "story.h"
#include "story_data.h"
#include "video.h"
#include "sound.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

RunState run EWRAM_BSS;

// Where a loaded run resumes. runsave.c keeps it in the save header (RunState has no field for it).
u8 run_phase;
#define PH_ROAD      0    // at run.node, its business done: pick the next road
#define PH_STOP      1    // at run.node: the stop menu
#define PH_CAMP      2    // night camp, then the road
#define PH_CAMP_STAY 3    // night camp, then this stop again (rested, or beaten at the bridge)
#define PH_DAWN_DAY  0x10 // with a camp: a day passes at dawn (bridge retry; farm detour, + its fuel)
#define PH_ARRIVE    0x20 // with PH_STOP: the arrival scenes are still to play (arrive_scenes)
#define PH_AFTER     0x40 // with a camp: the mission's scenes are still to play (after_scenes)
#define PH_MASK      0x0F

#define WIN(x, y) SCREENBLOCK(SB_WINDOW)[(y) * 32 + (x)]
#define FILL_TILE (TILE_WINDOW + 4) // window centre: an opaque backing for text on the backdrop
#define GREY BGPAL_TEXT_GREY
#define RED  BGPAL_TEXT_RED
#define YEL  BGPAL_TEXT_YEL
#define BLUE BGPAL_TEXT_BLUE

// ---------------------------------------------------------------- slice rules (designer: tune)
// The full game's boat sails on Day 50/40/36/32 (LAST_BOAT.md 16). The Act 1 slice is 3-5 travel
// days, so it gets its own short clock (SLICE_NUMBERS.md N1): Normal 10, Easy +2, Hard -1, Very Hard -2.
static const u8 ship_days[4] = { 12, 10, 9, 8 };
static const u8 supply_pct[4] = { 150, 100, 75, 60 };          // starting supplies vs Normal
static const u8 supply_base[RES_COUNT] = { 24, 12, 20, 4, 4 }; // Normal: food fuel ammo meds parts
// turn time after a bite by difficulty (16): bite_days in person.c (person_bitten)
static const u8 skill_xp0[6] = { 0, 10, 25, 45, 70, 100 };     // XP at each level (person.c steps)
#define DEPLOY_MAX  4
#define BYPASS_FUEL 2
#define RAID_CHANCE 25  // percent, unguarded nights
#define REST_HP     2

typedef struct {
	const char *name, *l1, *l2; // full name; map label in two short lines
	const char *kind;           // <= 13 chars
	s8 mission;                 // tac_missions[] index, -1 = none
	u8 road_event;              // a road storylet fires on arrival (the gas station)
	u8 x, y;                    // map label: marker at (x, y), words from x + 2 on rows y, y + 1
} Node;

enum { N_CEDAR, N_MAIN, N_GAS, N_FARM, N_HIGHWAY, N_BRIDGE, NODE_COUNT };
static const Node nodes[NODE_COUNT] = {
	[N_CEDAR]   = { "Cedar Falls",     "Cedar",    "Falls",   "Home",         -1, 0,  1,  9 },
	[N_MAIN]    = { "Main Street",     "Main",     "Street",  "Scavenge run",  0, 0,  6,  5 },
	[N_GAS]     = { "Gas Station",     "Gas",      "Station", "Road stop",    -1, 1,  6, 13 },
	[N_FARM]    = { "Hensley Farm",    "Hensley",  "Farm",    "Get through",   2, 0, 16,  5 },
	[N_HIGHWAY] = { "County Highway",  "County",   "Highway", "Long road",    -1, 0, 16, 13 },
	[N_BRIDGE]  = { "Miller's Bridge", "Miller's", "Bridge",  "The crossing",  1, 0, 20,  9 },
};

// fuel: extra on top of the cars' own fuel per leg (vehicle_info[].fuel)
typedef struct { u8 from, to, days, fuel; } Edge;
static const Edge edges[] = {
	{ N_CEDAR, N_MAIN, 1, 0 },  { N_CEDAR, N_GAS, 1, 0 },
	{ N_MAIN, N_FARM, 2, 0 },   { N_MAIN, N_HIGHWAY, 1, 1 },
	{ N_GAS, N_FARM, 1, 0 },    { N_GAS, N_HIGHWAY, 2, 1 },
	{ N_FARM, N_BRIDGE, 2, 0 }, { N_HIGHWAY, N_BRIDGE, 1, 0 },
};
#define EDGE_COUNT ((int)(sizeof edges / sizeof edges[0]))
#define ROADS_MAX 3

enum { J_REST, J_COOK, J_TEND, J_FORAGE, J_GUARD, J_COUNT };
static const char *const job_names[J_COUNT] = { "Rest", "Cook", "Tend", "Forage", "Guard" };
// LOG_DEATH arg: how (tac_rules.c writes 0 bled out, 1 killed by a zombie; story.c 4)
enum { DC_BLED, DC_ZOMBIE, DC_STARVED, DC_RAID, DC_STORY };

static u8 job[ROSTER_MAX];   // camp jobs, kept from night to night (not saved)
static bool ate = true;      // the last day's meal was whole: cooks can stretch it
static bool gfx_ready;       // the journey backdrop is in VRAM
static bool on_road;         // travelling: the map shows the destination, not road costs

#define SUM_MAX 24
static char sum[SUM_MAX][29] EWRAM_BSS; // the night's summary lines
static int nsum;

// ---------------------------------------------------------------- small helpers

static u32 rnd(void)
{
	u32 x = run.rng ? run.rng : 0x2545F491u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return run.rng = x;
}

static int roll(int n) // 0..n-1 without a divide
{
	return (int)(((rnd() >> 16) * (u32)n) >> 16);
}

static int alive_count(void)
{
	int n = 0;
	for (int i = 0; i < run.nroster; i++)
		n += run.roster[i].alive != 0;
	return n;
}

static int list_alive(u8 *idx)
{
	int n = 0;
	for (int i = 0; i < run.nroster; i++)
		if (run.roster[i].alive)
			idx[n++] = i;
	return n;
}

static const char *pname(int i)
{
	return i >= 0 && i < run.nroster ? person_name(&run.roster[i]) : "Someone";
}

static void morale_add(int d)
{
	int m = run.morale + d;
	run.morale = m < 0 ? 0 : m > 10 ? 10 : m;
}

static void res_add(int r, int n)
{
	int v = run.res[r] + n;
	run.res[r] = v < 0 ? 0 : v > 65535 ? 65535 : v;
}

static int heal(Person *p, int n) // returns HP gained
{
	if (p->hp >= p->maxhp)
		return 0;
	int hp = MIN(p->hp + n, p->maxhp);
	n = hp - p->hp;
	p->hp = hp;
	return n;
}

static int ration_need(void)
{
	int n = alive_count();
	return run.rations ? n : (n + 1) >> 1; // 0 half, 1 (or 2) full
}

static int vehicle_count(void)
{
	int n = 0;
	for (int i = 0; i < 4; i++)
		n += run.vehicle[i] > VH_NONE && run.vehicle[i] < VH_COUNT;
	return n;
}

static int cars_fuel(void)
{
	int f = 0;
	for (int i = 0; i < 4; i++)
		if (run.vehicle[i] < VH_COUNT)
			f += vehicle_info[run.vehicle[i]].fuel;
	return f;
}

static int leg_fuel(const Edge *e)
{
	return cars_fuel() + e->fuel;
}

static int roads(int node, const Edge **out) // the roads on from `node` (out may be NULL)
{
	int n = 0;
	for (int i = 0; i < EDGE_COUNT && n < ROADS_MAX; i++)
		if (edges[i].from == node) {
			if (out)
				out[n] = &edges[i];
			n++;
		}
	return n;
}

static int node_mission(int node)
{
	if (node < 0 || node >= NODE_COUNT)
		return -1;
	int k = nodes[node].mission;
	return k >= 0 && k < tac_mission_count ? k : -1;
}

static u8 sub_sat(u8 v, int n)
{
	return v > n ? v - n : 0;
}

// The fate of someone who isn't alive, from the Chronicle (the last word wins).
static const char *fate(int i)
{
	const char *f = "Dead";
	for (int k = 0; k < run.log_count; k++) {
		const LogRec *r = &run.log[(run.log_head + k) % LOG_MAX];
		if (r->actor != i)
			continue;
		if (r->event == LOG_DEATH)
			f = "Dead";
		else if (r->event == LOG_TURNED)
			f = "Turned";
		else if (r->event == LOG_LEFT_BEHIND)
			f = "Left behind";
	}
	return f;
}

static void note(const char *fmt, ...)
{
	if (nsum >= SUM_MAX)
		return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(sum[nsum++], sizeof sum[0], fmt, ap);
	va_end(ap);
}

// Every storylet goes through here: story effects (join, kill) must never leave the roster
// count past the array, whatever they do. Someone a storylet kills (a mercy choice) gets their
// death scene; the storylet has charged the morale itself, and a death scene never chains.
static void story(int phase, int subject)
{
	u8 was[ROSTER_MAX];
	int n = MIN(run.nroster, ROSTER_MAX);
	for (int i = 0; i < n; i++)
		was[i] = run.roster[i].alive;
	story_fire(phase, subject);
	if (run.nroster > ROSTER_MAX)
		run.nroster = ROSTER_MAX;
	for (int i = 0; i < n && phase != SP_DEATH && alive_count(); i++)
		if (was[i] && !run.roster[i].alive) {
			story_fire(SP_DEATH, i);
			if (run.nroster > ROSTER_MAX)
				run.nroster = ROSTER_MAX;
		}
}

// ---------------------------------------------------------------- screen helpers

static void backdrop(void)
{
	video_show_title();          // font, windows, UI palettes; BG2 = charblock 1 + SB_TITLE
	REG_DISPCNT = DCNT_BLANK;    // swap the title picture for the journey map unseen
	int n = MIN(MAX(lbart_journey_tile_count, 0), 512);
	volatile u16 *cb = CHARBLOCK(1);
	const u8 *t = lbart_journey_tiles; // bytes: no alignment assumed
	for (int i = 0; i < n * 16; i++)
		cb[i] = t[2 * i] | t[2 * i + 1] << 8;
	for (int i = 0; i < 16; i++)
		PAL_BG[16 * BGPAL_TITLE + i] = lbart_journey_pal[i];
	volatile u16 *sb = SCREENBLOCK(SB_TITLE);
	for (int y = 0; y < 20; y++)
		for (int x = 0; x < 30; x++)
			sb[y * 32 + x] = (lbart_journey_map[y][x] & 0x0FFF) | SE_PAL(BGPAL_TITLE);
	spr.map_visible = spr.cursor_show = spr.pointer_show = 0;
	spr.portrait = spr.hide_unit = spr.anim_unit = spr.flash_unit = -1;
	spr.popup_timer = spr.boom_timer = 0;
	text_clear();
	REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ_1D | DCNT_BG0 | DCNT_BG2 | DCNT_BG3 | DCNT_OBJ;
	gfx_ready = true;
}

static void screen(void)
{
	if (!gfx_ready)
		backdrop();
	text_clear();
	spr.map_visible = spr.cursor_show = spr.pointer_show = 0;
	spr.portrait = -1;
}

// A frameless filled strip, full width.
static void bar(int y, int h)
{
	text_clear_rect(0, y, 30, h);
	for (int j = y; j < y + h; j++)
		for (int i = 0; i < 30; i++)
			WIN(i, j) = FILL_TILE;
}

static void backing(int x, int y, int w)
{
	for (int i = MAX(x, 0); i < MIN(x + w, 30); i++)
		WIN(i, y) = FILL_TILE;
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

static void put_center(int y, const char *s, int pal)
{
	text_put(MAX(0, (30 - (int)strlen(s)) / 2), y, s, pal);
}

static void say(const char *l1, const char *l2)
{
	spr.pointer_show = 0;
	banner(l1, l2, 180);
}

static void wait_key(void)
{
	do
		frame();
	while (!(keys_down & (KEY_A | KEY_B | KEY_START)));
}

// text_menu() with a hover hook (descriptions, map highlights) and an optional no-cancel mode.
static int menu(int x, int y, int w, const char *const *items, const u8 *en, int count, int sel,
                void (*hover)(int), bool cancel)
{
	if (count <= 0)
		return -1;
	if (sel < 0 || sel >= count)
		sel = 0;
	text_window(x, y, w, count + 2);
	for (int i = 0; i < count; i++)
		text_put(x + 2, y + 1 + i, items[i], en && !en[i] ? GREY : BGPAL_UI);
	sfx(SFX_MENU);
	int shown = -1;
	for (;;) {
		if (hover && sel != shown)
			hover(sel);
		shown = sel;
		spr.pointer_show = 1;
		spr.pointer_x = x * 8 + 4;
		spr.pointer_y = (y + 1 + sel) * 8;
		frame();
		if (keys_rep & (KEY_UP | KEY_DOWN))
			sfx(SFX_CURSOR);
		if (keys_rep & KEY_UP)
			sel = sel ? sel - 1 : count - 1;
		if (keys_rep & KEY_DOWN)
			sel = sel + 1 < count ? sel + 1 : 0;
		if (keys_down & KEY_A) {
			if (!en || en[sel]) {
				sfx(SFX_CONFIRM);
				break;
			}
			sfx(SFX_BUZZ);
		} else if ((keys_down & KEY_B) && cancel) {
			sfx(SFX_CANCEL);
			sel = -1;
			break;
		}
	}
	spr.pointer_show = 0;
	text_clear_rect(x, y, w, count + 2);
	return sel;
}

// ---------------------------------------------------------------- status strip and route map

static void field(int x, int y, const char *name, int v, int pal) // pal < 0: grey at 0
{
	text_printf(x, y, pal >= 0 ? pal : v ? BGPAL_UI : GREY, "%s %d", name, MIN(v, 999));
}

static void draw_status(void)
{
	char b[32];
	bar(0, 4);
	text_printf(0, 0, YEL, "Day %d", run.day);
	snprintf(b, sizeof b, "The boat leaves Day %d", run.ship_day);
	text_put(30 - (int)strlen(b), 0, b, run.day + 2 >= run.ship_day ? RED : BGPAL_UI);
	int need = ration_need(), food = run.res[RES_FOOD];
	field(0, 1, "Food", food, food < need ? RED : food < 2 * need ? YEL : BGPAL_UI);
	field(10, 1, "Fuel", run.res[RES_FUEL], run.res[RES_FUEL] < cars_fuel() ? RED : BGPAL_UI);
	field(20, 1, "Ammo", run.res[RES_AMMO], -1);
	field(0, 2, "Meds", run.res[RES_MEDS], -1);
	field(10, 2, "Parts", run.res[RES_PARTS], -1);
	text_printf(20, 2, run.morale <= 2 ? RED : BGPAL_UI, "Morale %d", run.morale);
	text_printf(0, 3, BGPAL_UI, "Alive %d", alive_count());
	text_printf(10, 3, alive_count() > seats_total() ? RED : BGPAL_UI, "Seats %d", seats_total());
	text_put(20, 3, run.rations ? "Full meals" : "Half meals", run.rations ? BGPAL_UI : YEL);
}

static u8 visited_mask(void)
{
	u8 m = 1 << N_CEDAR;
	for (int k = 0; k < run.log_count; k++) {
		const LogRec *r = &run.log[(run.log_head + k) % LOG_MAX];
		if (r->event == LOG_ARRIVE && r->actor < NODE_COUNT)
			m |= 1 << r->actor;
	}
	return m;
}

static int kind_row(const Node *nd)
{
	return nd->y + 2 < 15 ? nd->y + 2 : nd->y - 1;
}

// Node labels on the backdrop: here (blue *), the hovered road (yellow >), roads on (white),
// visited (grey x), the rest grey.
static void draw_map(int hover)
{
	const Edge *opt[ROADS_MAX];
	int n = run.node < NODE_COUNT ? roads(run.node, opt) : 0;
	u8 seen = visited_mask(), next = 0;
	for (int i = 0; i < n; i++)
		next |= 1 << opt[i]->to;
	for (int k = 0; k < NODE_COUNT; k++) {
		const Node *nd = &nodes[k];
		int pal = GREY;
		char mark[2] = { 'o', 0 };
		if (k == run.node) {
			pal = BLUE;
			mark[0] = '*';
		} else if (k == hover) {
			pal = YEL;
			mark[0] = '>';
		} else if (next & 1 << k) {
			pal = BGPAL_UI;
		} else if (seen & 1 << k) {
			mark[0] = 'x';
		}
		int w = 2 + MAX((int)strlen(nd->l1), (int)strlen(nd->l2));
		backing(nd->x, nd->y, w);
		backing(nd->x, nd->y + 1, w);
		text_put(nd->x, nd->y, mark, pal);
		text_put(nd->x + 2, nd->y, nd->l1, pal);
		text_put(nd->x + 2, nd->y + 1, nd->l2, pal);
	}
	for (int k = 0; k < NODE_COUNT; k++)
		text_clear_rect(0, kind_row(&nodes[k]), 30, 1);
	if (hover >= 0 && hover < NODE_COUNT) {
		char b[32];
		const Node *nd = &nodes[hover];
		const Edge *e = NULL;
		for (int i = 0; i < n; i++)
			if (opt[i]->to == hover)
				e = opt[i];
		bool short_fuel = e && !on_road && run.res[RES_FUEL] < leg_fuel(e);
		if (on_road)
			snprintf(b, sizeof b, "%s", nd->kind);
		else if (short_fuel)
			snprintf(b, sizeof b, "Short of fuel");
		else if (e)
			snprintf(b, sizeof b, "%s, %d day%s", nd->kind, e->days, e->days > 1 ? "s" : "");
		else
			snprintf(b, sizeof b, "%s", nd->kind);
		int len = strlen(b), x = MIN(nd->x, 30 - len), y = kind_row(nd);
		backing(x, y, len);
		text_put(x, y, b, short_fuel ? RED : YEL);
	}
}

static void journey_screen(int hover, const char *l1, const char *l2)
{
	screen();
	draw_status();
	draw_map(hover);
	bar(16, 4);
	if (l1)
		text_put(1, 17, l1, YEL);
	if (l2)
		text_put(1, 18, l2, BGPAL_UI);
}

// ---------------------------------------------------------------- the Chronicle

static void log_line(char *b, int size, const LogRec *r)
{
	const char *who = pname(r->actor);
	switch (r->event) {
	case LOG_JOIN:
		snprintf(b, size, "%s joined", who);
		break;
	case LOG_DEATH:
		snprintf(b, size, r->arg == DC_STARVED ? "%s starved" : r->arg == DC_RAID ? "%s died in a raid" :
		         r->arg == DC_ZOMBIE ? "%s was killed" : r->arg == DC_STORY ? "%s died" : "%s bled out", who);
		break;
	case LOG_BITTEN:
		snprintf(b, size, "%s was bitten", who);
		break;
	case LOG_TURNED:
		snprintf(b, size, "%s turned", who);
		break;
	case LOG_AMPUTATE:
		snprintf(b, size, "%s lost %s", who, r->arg ? "a leg" : "an arm");
		break;
	case LOG_KILLS:
		snprintf(b, size, "%s killed %d", who, r->arg);
		break;
	case LOG_MISSION_WON:
	case LOG_MISSION_LOST:
		snprintf(b, size, "%s at %s", r->event == LOG_MISSION_WON ? "Won" : "Beaten",
		         r->actor < tac_mission_count ? tac_missions[r->actor].title : "a stop");
		break;
	case LOG_ARRIVE:
		snprintf(b, size, "Reached %s", r->actor < NODE_COUNT ? nodes[r->actor].name : "a stop");
		break;
	case LOG_LEFT_BEHIND:
		snprintf(b, size, "%s was left behind", who);
		break;
	case LOG_STORY: {
		const char *s = story_log_text(r);
		snprintf(b, size, "%s", s ? s : "A choice was made");
		break;
	}
	default:
		snprintf(b, size, "Something happened");
		break;
	}
}

// Records first..first+rows-1 (oldest = 0) as "dd text" lines.
static void draw_log(int y, int first, int rows)
{
	char b[26];
	for (int k = 0; k < rows && first + k < run.log_count; k++) {
		const LogRec *r = &run.log[(run.log_head + first + k) % LOG_MAX];
		log_line(b, sizeof b, r);
		text_printf(1, y + k, GREY, "%2d", r->day);
		text_put(4, y + k, b, BGPAL_UI);
	}
}

static void journal(void)
{
	const int rows = 14;
	int top = MAX(0, run.log_count - rows);
	for (;;) {
		screen();
		text_window(0, 0, 30, 20);
		text_put(1, 1, "Journal", YEL);
		text_put(23, 1, "Day", GREY);
		text_printf(27, 1, GREY, "%d", run.day);
		if (!run.log_count)
			text_put(4, 3, "Nothing yet.", GREY);
		draw_log(3, top, rows);
		if (top > 0)
			text_put(28, 3, "^", YEL);
		if (top + rows < run.log_count)
			text_put(28, 16, "v", YEL);
		text_put(1, 18, "Up/Down: scroll  A/B: back", GREY);
		for (;;) {
			frame();
			if ((keys_rep & KEY_UP) && top > 0) {
				top--;
				break;
			}
			if ((keys_rep & KEY_DOWN) && top + rows < run.log_count) {
				top++;
				break;
			}
			if (keys_down & (KEY_A | KEY_B | KEY_START)) {
				sfx(SFX_CANCEL);
				text_clear();
				return;
			}
		}
	}
}

// ---------------------------------------------------------------- people screens

static void status_text(const Person *p, char *b, int size)
{
	if (!p->alive)
		snprintf(b, size, "%s", "");
	else if (p->flags & PF_INFECTED)
		snprintf(b, size, "Bit %dd", p->turn_in);
	else if (p->wounds)
		snprintf(b, size, "Hurt");
	else
		b[0] = 0;
}

static void person_sheet(int i)
{
	if (i < 0 || i >= run.nroster)
		return;
	const Person *p = &run.roster[i];
	char b[64];
	screen();
	text_window(0, 0, 30, 20);
	int pt = p->cast < CAST_COUNT ? cast_info[p->cast].portrait : 0xFF;
	if (pt < PORTRAIT_COUNT) {
		spr.portrait = pt;
		spr.portrait_x = 200;
		spr.portrait_y = 8;
		spr.portrait_flip = 0;
	}
	text_put(1, 1, person_name(p), YEL);
	text_put(1, 2, p->cast < CAST_COUNT ? cast_info[p->cast].bg : "Survivor", GREY);
	b[0] = 0;
	for (int t = 0; t < 2; t++)
		if (p->trait[t] > TR_NONE && p->trait[t] < TR_COUNT)
			snprintf(b + strlen(b), sizeof b - strlen(b), "%s%s", b[0] ? ", " : "", trait_names[p->trait[t]]);
	text_put(1, 3, b, BGPAL_UI);
	if (p->alive)
		text_printf(1, 4, p->hp * 3 <= p->maxhp ? RED : BGPAL_UI, "HP %d/%d  Kills %d", p->hp, p->maxhp, p->kills);
	else
		text_printf(1, 4, RED, "%s  Kills %d", fate(i), p->kills);
	int row = 5;
	if (p->alive && (p->flags & PF_INFECTED))
		text_printf(1, row++, RED, "Bitten: turns in %d day%s", p->turn_in, p->turn_in == 1 ? "" : "s");
	if (p->flags & PF_AMPUTEE)
		text_put(1, row++, "Amputee", GREY);
	if (p->alive && p->wounds && row < 7)
		text_put(1, row++, "Wounded: -1 move", GREY);
	text_printf(1, 8, BGPAL_UI, "Grit %d Aim %d Speed %d Nerve %d", p->grit, p->aim, p->speed, p->nerve);
	for (int s = 0; s < SK_COUNT; s++)
		text_printf(1 + (s & 1) * 14, 10 + (s >> 1), p->skill[s] ? BGPAL_UI : GREY, "%-9s %d",
		            skill_names[s], p->skill[s]);
	text_printf(1, 15, BGPAL_UI, "%s, %s", p->weapon < WP_COUNT ? weapon_info[p->weapon].name : "?",
	            p->armor < AR_COUNT ? armor_info[p->armor].name : "?");
	b[0] = 0;
	for (int j = 0; j < run.nroster; j++) {
		int r = run.rel[i][j];
		if (j != i && r && run.roster[j].alive && strlen(b) + 14 < sizeof b)
			snprintf(b + strlen(b), sizeof b - strlen(b), "%s%s %+d", b[0] ? ", " : "Bonds: ", pname(j), r);
	}
	wrap(1, 16, 28, 2, b[0] ? b : "Bonds: none yet", GREY);
	text_put(20, 18, "A/B back", GREY);
	wait_key();
	spr.portrait = -1;
	text_clear();
}

// Keeps sel inside 0..n-1 and the list window [top, top + vis) around it.
static void list_scroll(int *sel, int *top, int n, int vis)
{
	if (*sel < 0)
		*sel = n - 1;
	if (*sel >= n)
		*sel = 0;
	if (*sel < *top)
		*top = *sel;
	if (*sel >= *top + vis)
		*top = *sel - vis + 1;
}

static int list_keys(int *sel) // Up/Down: returns 1 if sel moved
{
	if (keys_rep & KEY_UP) {
		(*sel)--;
		sfx(SFX_CURSOR);
		return 1;
	}
	if (keys_rep & KEY_DOWN) {
		(*sel)++;
		sfx(SFX_CURSOR);
		return 1;
	}
	return 0;
}

static void caravan(void)
{
	const int vis = 9;
	int sel = 0, top = 0, n = run.nroster;
	char b[16];
	for (;;) {
		list_scroll(&sel, &top, n, vis);
		screen();
		text_window(0, 0, 30, 20);
		text_put(1, 1, "Caravan", YEL);
		text_printf(13, 1, alive_count() > seats_total() ? RED : BGPAL_UI, "Seats %d, alive %d", seats_total(), alive_count());
		int row = 2;
		for (int v = 0; v < 4; v++) {
			int t = run.vehicle[v];
			if (t > VH_NONE && t < VH_COUNT)
				text_printf(2, row++, BGPAL_UI, "%-7s %d seats fuel %d %3d%%", vehicle_info[t].name,
				            vehicle_info[t].seats, vehicle_info[t].fuel, MIN(run.vcond[v], 100));
		}
		if (row == 2)
			text_put(2, row++, "No cars. On foot.", RED);
		text_put(2, 7, "People", GREY);
		for (int k = 0; k < vis && top + k < n; k++) {
			const Person *p = &run.roster[top + k];
			int y = 8 + k;
			text_put(2, y, person_name(p), p->alive ? BGPAL_UI : GREY);
			if (p->alive) {
				text_printf(12, y, p->hp * 3 <= p->maxhp ? RED : BGPAL_UI, "%d/%d", p->hp, p->maxhp);
				status_text(p, b, sizeof b);
				text_put(18, y, b, RED);
			} else {
				text_put(12, y, fate(top + k), GREY);
			}
			text_printf(25, y, GREY, "K%d", p->kills);
		}
		text_put(1, 18, "A/R: about  B: back", GREY);
		spr.pointer_show = n > 0;
		spr.pointer_x = 8;
		spr.pointer_y = (8 + sel - top) * 8;
		for (;;) {
			frame();
			if (n && list_keys(&sel))
				break;
			if (n && (keys_down & (KEY_A | KEY_R))) {
				person_sheet(sel);
				break;
			}
			if (keys_down & (KEY_B | KEY_START)) {
				spr.pointer_show = 0;
				text_clear();
				return;
			}
		}
	}
}

// ---------------------------------------------------------------- deaths and the clock

static void die(int i, int event, int how)
{
	Person *p = &run.roster[i];
	p->alive = 0;
	p->hp = 0;
	chronicle(event, i, how);
	morale_add(-2);
}

// Everyone gone, or the boat has sailed: the run is lost (saved, so a reload can't undo it).
static bool check_over(void)
{
	if (run.over)
		return true;
	if (alive_count() && run.day <= run.ship_day)
		return false;
	run.over = 2;
	run_save();
	return true;
}

static void fire_deaths(const u8 *dead, int nd)
{
	for (int k = 0; k < nd && alive_count(); k++)
		story(SP_DEATH, dead[k]);
}

// One day passes: the meal, hunger, infection, the clock. false = the run is over.
// Short of full meals everyone gets half; short of that, each food feeds two and the rest go
// without, the healthiest first so the weakest eat. More food never ends worse.
static bool day_pass(void)
{
	u8 dead[ROSTER_MAX], hungry[ROSTER_MAX];
	char l1[32];
	int nd = 0, need = ration_need(), n = alive_count(), half = (n + 1) >> 1, food = run.res[RES_FOOD];
	if (run.day < 255)
		run.day++;
	ate = food >= need;
	if (ate) {
		run.res[RES_FOOD] -= need;
	} else if (food >= half) { // only on full meals
		run.res[RES_FOOD] -= half;
		say("Not enough food.", "Half meals today.");
	} else {
		run.res[RES_FOOD] = 0;
		memset(hungry, 0, sizeof hungry);
		for (int k = n - 2 * food; k > 0; k--) { // >= 1 of them
			int b = -1;
			for (int i = 0; i < run.nroster; i++)
				if (run.roster[i].alive && !hungry[i] && (b < 0 || run.roster[i].hp > run.roster[b].hp))
					b = i;
			if (b < 0)
				break;
			hungry[b] = 1;
		}
		snprintf(l1, sizeof l1, "Only %d can eat.", 2 * food);
		say(food ? "The food runs out." : "The food is gone.", food ? l1 : "Everyone goes hungry.");
		for (int i = 0; i < run.nroster; i++) {
			Person *p = &run.roster[i];
			if (!p->alive || !hungry[i])
				continue;
			if (p->hp > 1) {
				p->hp--;
				continue;
			}
			die(i, LOG_DEATH, DC_STARVED);
			dead[nd++] = i;
			sfx(SFX_DESTROY);
			snprintf(l1, sizeof l1, "%s starved.", person_name(p));
			say(l1, NULL);
		}
	}
	if ((!run.rations || !ate) && need)
		morale_add(-1); // half meals, or none (designer: tune)
	for (int i = 0; i < run.nroster; i++) {
		Person *p = &run.roster[i];
		if (!p->alive || !(p->flags & PF_INFECTED))
			continue;
		if (p->turn_in > 1) {
			p->turn_in--;
			continue;
		}
		p->turn_in = 0;
		die(i, LOG_TURNED, 0);
		dead[nd++] = i;
		sfx(SFX_INFECT);
		snprintf(l1, sizeof l1, "%s turned.", person_name(p));
		say(l1, "You did what you had to.");
	}
	fire_deaths(dead, nd);
	return !check_over();
}

// ---------------------------------------------------------------- travel

// Fuel for one leg. Short of it, cars stay behind, the last one first; the last car is kept and
// pushed along with dry tanks instead (+1 day). Returns the extra days.
static int pay_fuel(const Edge *e)
{
	char l2[32];
	if (!vehicle_count()) { // only a storylet can take every car
		say("No cars left.", "On foot it takes a day more.");
		return 1;
	}
	for (;;) {
		int cost = leg_fuel(e);
		if (run.res[RES_FUEL] >= cost) {
			run.res[RES_FUEL] -= cost;
			return 0;
		}
		int v = -1;
		for (int i = 0; i < 4; i++)
			if (run.vehicle[i] > VH_NONE && run.vehicle[i] < VH_COUNT)
				v = i;
		if (v < 0 || vehicle_count() <= 1) { // keep the last car: push it along
			run.res[RES_FUEL] = 0;
			say("The tanks run dry.", "Pushing on costs a day.");
			return 1;
		}
		snprintf(l2, sizeof l2, "The %s stays behind.", vehicle_info[run.vehicle[v]].name);
		say("Not enough fuel.", l2);
		run.vehicle[v] = VH_NONE;
		run.vcond[v] = 0;
	}
}

// Party size = seats. Whoever doesn't fit (the last in the roster first) is left behind. For the
// demo someone left behind is gone for good: alive = 0, like the dead (no later storylet yet).
static void seat_check(void)
{
	char l1[32], l2[32];
	int seats = seats_total();
	if (!vehicle_count())
		return; // never: the last car is kept (pay_fuel); on foot nobody needs a seat
	for (int i = run.nroster - 1; i >= 0 && alive_count() > seats; i--) {
		if (!run.roster[i].alive)
			continue;
		run.roster[i].alive = 0;
		chronicle(LOG_LEFT_BEHIND, i, 0);
		morale_add(-2);
		snprintf(l1, sizeof l1, "No seat for %s.", pname(i));
		snprintf(l2, sizeof l2, "%s is left behind.", pname(i));
		say(l1, l2);
	}
}

static void travel_anim(const Edge *e, int day, int days)
{
	const Node *a = &nodes[e->from], *b = &nodes[e->to];
	int ax = a->x * 8, ay = a->y * 8, dx = b->x * 8 - ax, dy = b->y * 8 - ay, total = days * 48;
	spr.pointer_show = 1;
	for (int t = day * 48; t <= (day + 1) * 48; t++) {
		spr.pointer_x = ax + dx * t / total;
		spr.pointer_y = ay + dy * t / total;
		frame();
		if (keys_held & (KEY_A | KEY_B))
			t += 2; // hurry
	}
	spr.pointer_show = 0;
}

// The arrival banner and scenes (SP_STOP, and SP_ROAD at a road stop), then the stop menu. A run
// cut off during them resumes here (PH_ARRIVE) and plays them again with the same dice.
static void arrive_scenes(void)
{
	char b[32];
	const Node *nd = &nodes[run.node];
	journey_screen(-1, nd->name, nd->kind);
	snprintf(b, sizeof b, "Day %d", run.day);
	sfx(SFX_CONFIRM);
	banner(nd->name, b, 90);
	journey_screen(-1, nd->name, nd->kind); // the banner cut into the labels
	story(SP_STOP, -1);
	if (nd->road_event && alive_count()) {
		journey_screen(-1, nd->name, nd->kind);
		story(SP_ROAD, -1);
	}
	run_phase = PH_STOP;
	if (!check_over())
		run_save();
}

static void arrive(int node)
{
	run.node = node;
	chronicle(LOG_ARRIVE, node, 0);
	run_phase = PH_STOP | PH_ARRIVE;
	run_save();
	arrive_scenes();
}

static void travel(const Edge *e)
{
	char l1[32], l2[32];
	int extra = pay_fuel(e);
	seat_check();
	if (check_over())
		return;
	int days = e->days + extra;
	snprintf(l1, sizeof l1, "Road to %s", nodes[e->to].name);
	on_road = true;
	for (int d = 0; d < days; d++) {
		snprintf(l2, sizeof l2, "%d day%s to go. Hold A: faster", days - d, days - d > 1 ? "s" : "");
		journey_screen(e->to, l1, l2);
		travel_anim(e, d, days);
		if (!day_pass())
			break;
		if (!d) { // one road event per leg
			journey_screen(e->to, l1, NULL);
			story(SP_ROAD, -1);
			if (check_over())
				break;
		}
	}
	on_road = false;
	if (!run.over)
		arrive(e->to);
}

// ---------------------------------------------------------------- shared menu entries

enum { ACT_MISSION, ACT_BYPASS, ACT_REST, ACT_CAMP, ACT_RATIONS, ACT_CARAVAN, ACT_JOURNAL, ACT_QUIT,
       ACT_BACK, ACT_COUNT };
static char rations_label[16];

static const char *act_label(int a)
{
	static const char *const names[ACT_COUNT] = { "", "Bypass", "Rest a day", "Make camp", "",
	                                               "Caravan", "Journal", "Save & quit", "Back" };
	if (a == ACT_RATIONS) {
		snprintf(rations_label, sizeof rations_label, "Meals: %s", run.rations ? "full" : "half");
		return rations_label;
	}
	return names[a];
}

// Rations, caravan, journal. Returns false for Save & quit (saved here).
static bool act_extra(int a)
{
	if (a == ACT_RATIONS)
		run.rations = run.rations ? 0 : 1;
	else if (a == ACT_CARAVAN)
		caravan();
	else if (a == ACT_JOURNAL)
		journal();
	else if (a == ACT_QUIT) {
		run_save();
		return false;
	}
	return true;
}

// ---------------------------------------------------------------- journey

static const Edge *road_opt[ROADS_MAX];

static void road_hover(int i)
{
	draw_map(road_opt[i]->to);
}

static int pick_road(int n)
{
	char lines[ROADS_MAX][32];
	const char *items[ROADS_MAX];
	for (int i = 0; i < n; i++) {
		const Edge *e = road_opt[i];
		snprintf(lines[i], sizeof lines[i], "%-15s %dd %2d fuel", nodes[e->to].name, e->days, leg_fuel(e));
		items[i] = lines[i];
	}
	text_clear_rect(0, 16, 30, 4);
	int c = menu(0, 20 - (n + 2), 30, items, NULL, n, 0, road_hover, true);
	draw_map(-1);
	return c;
}

// The route map: A picks the next road, B/START opens the caravan menu. false = save & quit.
static bool journey(void)
{
	char l1[40];
	int n = roads(run.node, road_opt);
	if (!n) { // the end of the slice road
		run.node = 0xFF;
		run.over = 1;
		run_save();
		return true;
	}
	music_play(MUS_AUTO);
	for (;;) {
		snprintf(l1, sizeof l1, "%s - %s", nodes[run.node].name, nodes[run.node].kind);
		journey_screen(-1, l1, "A: pick the road   B: menu");
		do
			frame();
		while (!(keys_down & (KEY_A | KEY_B | KEY_START)));
		if (keys_down & KEY_A) {
			int c = pick_road(n);
			if (c >= 0) {
				travel(road_opt[c]);
				return true;
			}
			continue;
		}
		static const u8 ids[5] = { ACT_RATIONS, ACT_CARAVAN, ACT_JOURNAL, ACT_QUIT, ACT_BACK };
		const char *items[5];
		for (int i = 0; i < 5; i++)
			items[i] = act_label(ids[i]);
		int c = menu(8, 5, 15, items, NULL, 5, 0, NULL, true);
		if (c >= 0 && !act_extra(ids[c]))
			return false;
	}
}

// ---------------------------------------------------------------- stop

static u8 stop_ids[ACT_COUNT];
static char stop_desc_buf[64];

static void stop_hover(int i)
{
	const char *s = "";
	int k = node_mission(run.node);
	const TacMission *m = k >= 0 ? &tac_missions[k] : NULL;
	switch (stop_ids[i]) {
	case ACT_MISSION:
		if (m->objective == OBJ_GET_THROUGH)
			snprintf(stop_desc_buf, sizeof stop_desc_buf, "Send up to 4. %d must reach the far side.", m->need);
		else if (m->objective == OBJ_BRIDGE)
			snprintf(stop_desc_buf, sizeof stop_desc_buf, "Clear the bridge, hold it %d turns, cross.", m->need);
		else
			snprintf(stop_desc_buf, sizeof stop_desc_buf, "Send up to 4 in. Loot, then get out.");
		s = stop_desc_buf;
		break;
	case ACT_BYPASS:
		s = run.res[RES_FUEL] >= BYPASS_FUEL ? "Go around: +1 day, -2 fuel. No fight." : "Going around takes 2 fuel.";
		break;
	case ACT_REST:
		s = "+1 day, meals as usual. +2 HP each, morale +1.";
		break;
	case ACT_CAMP:
		s = "Camp here tonight. Leave at dawn.";
		break;
	case ACT_RATIONS:
		s = run.rations ? "Full: 1 food each a day. Half: less, but -1 morale a day." :
		                  "Half: half the food, -1 morale a day. Full: 1 each.";
		break;
	case ACT_CARAVAN:
		s = "Your people and your cars.";
		break;
	case ACT_JOURNAL:
		s = "What has happened so far.";
		break;
	case ACT_QUIT:
		s = "Saved. Back to the title.";
		break;
	}
	text_window(16, 7, 14, 8);
	wrap(17, 8, 12, 6, s, BGPAL_UI);
}

static const char *mission_label(int k)
{
	int o = tac_missions[k].objective;
	return o == OBJ_BRIDGE ? "Take bridge" : o == OBJ_GET_THROUGH ? "Get through" : "Scavenge";
}

static bool mission(int k);

static void bypass(void)
{
	char l1[32];
	if (run.res[RES_FUEL] < BYPASS_FUEL)
		return;
	run.res[RES_FUEL] -= BYPASS_FUEL;
	snprintf(l1, sizeof l1, "You go around %s.", nodes[run.node].name);
	journey_screen(-1, nodes[run.node].name, nodes[run.node].kind);
	say(l1, "+1 day, -2 fuel.");
	if (day_pass()) {
		run_phase = PH_CAMP;
		run_save();
	}
}

static void rest(void)
{
	if (!day_pass())
		return;
	for (int i = 0; i < run.nroster; i++)
		if (run.roster[i].alive)
			heal(&run.roster[i], REST_HP);
	morale_add(1);
	sfx(SFX_HEAL);
	journey_screen(-1, nodes[run.node].name, nodes[run.node].kind);
	say("A day of rest.", "+2 HP each, morale +1.");
	run_phase = PH_CAMP_STAY;
	run_save();
}

// The stop: choose one. false = save & quit. Nothing blocks the road at a scavenge stop: skipping
// it is free (Make camp), never dearer than going in and straight out.
static bool stop(void)
{
	int node = run.node, k = node_mission(node), sel = 0;
	bool last = !roads(node, NULL), scav = k >= 0 && tac_missions[k].objective == OBJ_SCAVENGE;
	const char *items[ACT_COUNT];
	u8 en[ACT_COUNT];
	music_play(MUS_AUTO);
	for (;;) {
		int n = 0;
		if (k >= 0)
			stop_ids[n++] = ACT_MISSION;
		if (k >= 0 && !last && !scav)
			stop_ids[n++] = ACT_BYPASS;
		stop_ids[n++] = ACT_REST;
		if (k < 0 || (scav && !last))
			stop_ids[n++] = ACT_CAMP;
		stop_ids[n++] = ACT_RATIONS;
		stop_ids[n++] = ACT_CARAVAN;
		stop_ids[n++] = ACT_JOURNAL;
		stop_ids[n++] = ACT_QUIT;
		for (int i = 0; i < n; i++) {
			items[i] = stop_ids[i] == ACT_MISSION ? mission_label(k) : act_label(stop_ids[i]);
			en[i] = stop_ids[i] != ACT_BYPASS || run.res[RES_FUEL] >= BYPASS_FUEL;
		}
		screen();
		draw_status();
		text_window(0, 4, 30, 3);
		text_put(1, 5, nodes[node].name, YEL);
		text_put(29 - (int)strlen(nodes[node].kind), 5, nodes[node].kind, GREY);
		text_put(1, 18, "Choose one", GREY);
		int c = menu(0, 7, 16, items, en, n, sel, stop_hover, false);
		sel = c;
		switch (stop_ids[c]) {
		case ACT_MISSION:
			if (mission(k))
				return true;
			break;
		case ACT_BYPASS:
			bypass();
			return true;
		case ACT_REST:
			rest();
			return true;
		case ACT_CAMP:
			run_phase = PH_CAMP;
			return true;
		default:
			if (!act_extra(stop_ids[c]))
				return false;
			break;
		}
	}
}

// ---------------------------------------------------------------- deploy and the mission

// Who goes in: up to 4 living people (A toggles, START goes, B backs out). Returns how many.
static int deploy_screen(int k, u8 *dep)
{
	const TacMission *m = &tac_missions[k];
	const int vis = 9;
	u8 idx[ROSTER_MAX], pick[ROSTER_MAX];
	char b[16];
	int n = 0, sel = 0, top = 0, cnt = 0;
	for (int i = 0; i < run.nroster; i++)
		if (run.roster[i].alive && run.roster[i].hp)
			idx[n++] = i;
	if (!n)
		return 0;
	memset(pick, 0, sizeof pick);
	for (int j = 0; j < n && cnt < DEPLOY_MAX; j++, cnt++) // the first four, ready to go
		pick[j] = 1;
	for (;;) {
		list_scroll(&sel, &top, n, vis);
		screen();
		text_window(0, 0, 30, 20);
		text_put(1, 1, m->title, YEL);
		wrap(1, 2, 28, 3, m->brief, BGPAL_UI);
		text_printf(1, 6, GREY, "Who goes in? Up to %d.", DEPLOY_MAX);
		for (int r = 0; r < vis && top + r < n; r++) {
			int j = top + r, i = idx[j], y = 7 + r;
			const Person *p = &run.roster[i];
			if (pick[j])
				text_put(2, y, "In", BLUE);
			text_put(5, y, person_name(p), pick[j] ? BGPAL_UI : GREY);
			text_printf(15, y, p->hp * 3 <= p->maxhp ? RED : BGPAL_UI, "%d/%d", p->hp, p->maxhp);
			status_text(p, b, sizeof b);
			text_put(21, y, b, RED);
		}
		text_printf(1, 16, BGPAL_UI, "Going: %d of %d", cnt, DEPLOY_MAX);
		if (m->objective == OBJ_GET_THROUGH && cnt < m->need)
			text_printf(16, 16, RED, "Need %d to win", m->need);
		text_put(1, 17, "A: pick      R: about", GREY);
		text_put(1, 18, "START: go    B: back", GREY);
		spr.pointer_show = 1;
		spr.pointer_x = 4;
		spr.pointer_y = (7 + sel - top) * 8;
		for (;;) {
			frame();
			if (list_keys(&sel))
				break;
			if (keys_down & KEY_A) {
				if (pick[sel] || cnt < DEPLOY_MAX) {
					pick[sel] ^= 1;
					cnt += pick[sel] ? 1 : -1;
					sfx(SFX_CURSOR);
				} else {
					sfx(SFX_BUZZ);
				}
				break;
			}
			if (keys_down & KEY_R) {
				person_sheet(idx[sel]);
				break;
			}
			if (keys_down & KEY_START) {
				if (!cnt) {
					sfx(SFX_BUZZ);
					continue;
				}
				int out = 0;
				for (int j = 0; j < n; j++)
					if (pick[j] && out < DEPLOY_MAX)
						dep[out++] = idx[j];
				sfx(SFX_CONFIRM);
				spr.pointer_show = 0;
				return out;
			}
			if (keys_down & KEY_B) {
				sfx(SFX_CANCEL);
				spr.pointer_show = 0;
				return 0;
			}
		}
	}
}

static void back_screen(void) // the journey map, back from a mission
{
	journey_screen(-1, nodes[run.node].name, run.vars[SV_MISSION_WON] == 1 ? "Done here" : "Beaten back");
}

static void scene_done(int i, int flag)
{
	run.roster[i].flags &= ~flag;
	if (!run.over)
		run_save();
}

// After a mission (PH_AFTER): the death and bite scenes it still owes, SP_MISSION_END, then what it
// means for the road. Each scene is saved once played, so a run cut off here plays only the rest
// on Continue (same dice), none twice and none skipped.
static void after_scenes(void)
{
	for (int i = 0; i < run.nroster; i++)
		if (run.roster[i].flags & PF_DEATH_SCENE) {
			if (alive_count())
				story(SP_DEATH, i);
			scene_done(i, PF_DEATH_SCENE);
		}
	for (int i = 0; i < run.nroster; i++)
		if (run.roster[i].flags & PF_BITE_SCENE) {
			if (alive_count() && run.roster[i].alive)
				story(SP_BITE, i);
			scene_done(i, PF_BITE_SCENE);
		}
	if (alive_count())
		story(SP_MISSION_END, -1);
	run_phase &= ~PH_AFTER;
	if (!run.over && !check_over()) {
		if (run_phase & PH_DAWN_DAY) {
			screen();
			if ((run_phase & PH_MASK) == PH_CAMP_STAY)
				say("You fall back.", "Tomorrow you try again.");
			else
				say("No way through.", "Going round: a day and fuel.");
		}
		run_save();
	}
}

// PF_DEPLOYED on exactly the people going in (until the next mission): mission_start and
// mission_end storylets read it, so nobody who stayed with the cars is put in the fight.
static void mark_deployed(const u8 *dep, int n)
{
	for (int i = 0; i < run.nroster; i++)
		run.roster[i].flags &= ~PF_DEPLOYED;
	for (int j = 0; j < n; j++)
		run.roster[dep[j]].flags |= PF_DEPLOYED;
}

// Deploy, play the mission, apply what it did. false = backed out before it started.
static bool mission(int k)
{
	const TacMission *m = &tac_missions[k];
	u8 dep[DEPLOY_MAX], was_alive[ROSTER_MAX], was_bit[ROSTER_MAX], kills0[ROSTER_MAX];
	u8 dead[ROSTER_MAX], bit[ROSTER_MAX];
	int n = deploy_screen(k, dep), nd = 0, nb = 0;
	if (!n)
		return false;
	mark_deployed(dep, n);
	story(SP_MISSION_START, -1);
	int keep = 0; // a storylet may have changed who can go
	for (int j = 0; j < n; j++)
		if (dep[j] < run.nroster && run.roster[dep[j]].alive && run.roster[dep[j]].hp)
			dep[keep++] = dep[j];
	n = keep;
	mark_deployed(dep, n);
	if (!n)
		return check_over();
	memset(was_alive, 0, sizeof was_alive);
	memset(was_bit, 0, sizeof was_bit);
	memset(kills0, 0, sizeof kills0);
	for (int i = 0; i < run.nroster; i++) {
		was_alive[i] = run.roster[i].alive;
		was_bit[i] = (run.roster[i].flags & PF_INFECTED) != 0;
		kills0[i] = run.roster[i].kills;
	}
	music_play(MUS_AUTO);
	gs.rng = rnd() | 1; // the mission's dice follow the run's seed
	gfx_ready = false;
	int r = tac_run(m, dep, n);
	run.vars[SV_MISSION_WON] = r == 1 ? 1 : 2; // storylets read the last mission's outcome
	backdrop();
	back_screen();

	for (int i = 0; i < run.nroster; i++) {
		Person *p = &run.roster[i];
		if (p->alive && !p->hp)
			p->hp = 1; // nobody carries 0 HP out of a mission
		if (p->kills > kills0[i])
			chronicle(LOG_KILLS, i, p->kills - kills0[i]);
		if (was_alive[i] && !p->alive) {
			dead[nd++] = i;
			morale_add(-2); // tac_rules.c already wrote LOG_DEATH
		} else if (p->alive && (p->flags & PF_INFECTED) && !was_bit[i]) {
			bit[nb++] = i; // the timer is set (by difficulty) at the bite: person_bitten()
		}
	}
	chronicle(r == 1 ? LOG_MISSION_WON : LOG_MISSION_LOST, k, run.node);
	if (r == 1)
		morale_add(1);
	bool last = !roads(run.node, NULL);
	if (!alive_count()) {
		run.over = 2;
	} else if (r == 1 && last) {
		run.node = 0xFF; // across: the end of the slice
		run.over = 1;
	} else if (r != 1 && last) {
		run_phase = PH_CAMP_STAY | PH_DAWN_DAY;
	} else if (r != 1 && m->objective == OBJ_GET_THROUGH) {
		run_phase = PH_CAMP | PH_DAWN_DAY; // the detour: Bypass's day and fuel (camp's dawn)
	} else {
		run_phase = PH_CAMP;
	}
	for (int j = 0; j < nd; j++)
		run.roster[dead[j]].flags |= PF_DEATH_SCENE;
	for (int j = 0; j < nb; j++)
		run.roster[bit[j]].flags |= PF_BITE_SCENE;
	run_phase |= PH_AFTER;
	run_save(); // the outcome is final from here on; its scenes still play once
	after_scenes();
	return true;
}

// ---------------------------------------------------------------- camp

static const char *job_desc(int i, char *l1, char *l2, int size)
{
	const Person *p = &run.roster[i];
	int med = p->skill[SK_MEDICINE];
	bool surgeon = med >= 2 || p->cast == CAST_DOC;
	l2[0] = 0;
	switch (job[i]) {
	case J_COOK:
		if (p->skill[SK_SURVIVAL] >= 2)
			snprintf(l1, size, "Cook: saves 1 food tonight.");
		else
			snprintf(l1, size, "Cook: needs Survival 2.");
		snprintf(l2, size, "Survival %d. Grows with use.", p->skill[SK_SURVIVAL]);
		break;
	case J_TEND:
		snprintf(l1, size, "Tend: the most hurt +%d HP.", 1 + med);
		snprintf(l2, size, "%s", !surgeon ? "Bites need Medicine 2." :
		         run.res[RES_MEDS] ? "Can treat bites (1 med)." : "No meds to treat bites.");
		break;
	case J_FORAGE:
		snprintf(l1, size, "Forage: %d%% food or parts.",
		         MIN(95, 60 + 10 * (p->skill[SK_SCAVENGE] + p->skill[SK_STEALTH])));
		snprintf(l2, size, "Or a wound: -2 HP.");
		break;
	case J_GUARD:
		snprintf(l1, size, "Guard: no raid tonight.");
		break;
	default:
		snprintf(l1, size, "Rest: +%d HP, heals wounds.", REST_HP);
		break;
	}
	return l1;
}

// The job board: one job each (Rest by default). START (or A on Sleep) ends it.
static void jobs_board(void)
{
	const int vis = 10;
	u8 idx[ROSTER_MAX];
	char l1[32], l2[32];
	int n = list_alive(idx), sel = 0, top = 0;
	for (int j = 0; j < n; j++)
		if (job[idx[j]] >= J_COUNT)
			job[idx[j]] = J_REST;
	for (;;) {
		list_scroll(&sel, &top, n + 1, vis); // row n: Sleep
		int guards = 0;
		for (int j = 0; j < n; j++)
			guards += job[idx[j]] == J_GUARD;
		screen();
		text_window(0, 0, 30, 20);
		text_printf(1, 1, YEL, "Night, Day %d", run.day);
		text_printf(19, 1, BGPAL_UI, "Food %d", MIN(run.res[RES_FOOD], 999));
		text_put(1, 2, "One job each, then sleep.", GREY);
		for (int r = 0; r < vis && top + r <= n; r++) {
			int j = top + r, y = 4 + r;
			if (j == n) {
				text_put(2, y, "Sleep", j == sel ? YEL : BGPAL_UI);
				continue;
			}
			const Person *p = &run.roster[idx[j]];
			text_put(2, y, person_name(p), BGPAL_UI);
			text_printf(12, y, p->hp * 3 <= p->maxhp ? RED : BGPAL_UI, "%d/%d", p->hp, p->maxhp);
			if (j == sel)
				text_printf(18, y, YEL, "<%s>", job_names[job[idx[j]]]);
			else
				text_put(19, y, job_names[job[idx[j]]], BGPAL_UI);
			if (p->flags & PF_INFECTED)
				text_put(26, y, "Bit", RED);
			else if (p->wounds)
				text_put(26, y, "Wnd", GREY);
		}
		text_put(1, 15, guards ? "Someone keeps watch." : "No guard: raid risk 25%", guards ? BLUE : RED);
		if (sel < n) {
			job_desc(idx[sel], l1, l2, sizeof l1);
			text_put(1, 16, l1, BGPAL_UI);
			text_put(1, 17, l2, GREY);
		} else {
			text_put(1, 16, "Sleep: the night begins.", BGPAL_UI);
		}
		text_put(1, 18, "</> job R info START sleep", GREY);
		spr.pointer_show = 1;
		spr.pointer_x = 4;
		spr.pointer_y = (4 + sel - top) * 8;
		for (;;) {
			frame();
			if (list_keys(&sel))
				break;
			if (sel < n && (keys_rep & (KEY_LEFT | KEY_RIGHT | KEY_A))) {
				int jb = job[idx[sel]] + ((keys_rep & KEY_LEFT) ? J_COUNT - 1 : 1);
				job[idx[sel]] = jb >= J_COUNT ? jb - J_COUNT : jb;
				sfx(SFX_CURSOR);
				break;
			}
			if (sel < n && (keys_down & KEY_R)) {
				person_sheet(idx[sel]);
				break;
			}
			if ((keys_down & KEY_START) || (sel == n && (keys_down & KEY_A))) {
				sfx(SFX_CONFIRM);
				spr.pointer_show = 0;
				return;
			}
			if (keys_down & KEY_B) { // to Sleep
				sel = n;
				break;
			}
		}
	}
}

static int most_hurt(void)
{
	int best = -1, bv = 0;
	for (int i = 0; i < run.nroster; i++) {
		const Person *p = &run.roster[i];
		int v = p->alive ? (MAX(p->maxhp - p->hp, 0) << 1) + p->wounds : 0;
		if (v > bv) {
			bv = v;
			best = i;
		}
	}
	return best;
}

static int bitten_patient(int t, const u8 *treated)
{
	int best = -1;
	for (int i = 0; i < run.nroster; i++) {
		const Person *p = &run.roster[i];
		if (i != t && p->alive && (p->flags & PF_INFECTED) && !treated[i] &&
		    (best < 0 || p->turn_in < run.roster[best].turn_in))
			best = i;
	}
	return best;
}

// Tender t can treat bitten patient pt: amputate (cures) or antivirals (+2 days). 1 med either way.
static bool treat(int t, int pt)
{
	static const char *const items[4] = { "Take the arm: Aim -2", "Take the leg: Speed -2",
	                                      "Antivirals: +2 days", "Not tonight" };
	Person *p = &run.roster[pt];
	screen();
	text_window(0, 1, 30, 7);
	text_printf(1, 2, RED, "%s was bitten.", pname(pt));
	text_printf(1, 3, BGPAL_UI, "Turns in %d day%s.", p->turn_in, p->turn_in == 1 ? "" : "s");
	text_printf(1, 4, BGPAL_UI, "%s can treat it.", pname(t));
	text_printf(1, 5, GREY, "Each costs 1 med. Meds: %d", MIN(run.res[RES_MEDS], 999));
	int c = menu(1, 9, 28, items, NULL, 4, 3, NULL, true);
	text_clear();
	if (c < 0 || c == 3 || !run.res[RES_MEDS])
		return false;
	run.res[RES_MEDS]--;
	if (c == 2) {
		p->turn_in = MIN(p->turn_in + 2, 255);
		note("Antivirals for %s: +2 days", pname(pt));
		return true;
	}
	p->flags = (p->flags & ~PF_INFECTED) | PF_AMPUTEE;
	p->turn_in = 0;
	if (c == 0)
		p->aim = sub_sat(p->aim, 2);
	else
		p->speed = sub_sat(p->speed, 2);
	chronicle(LOG_AMPUTATE, pt, c); // arg 0 arm, 1 leg
	sfx(SFX_HEAL);
	note("%s lost %s: cured", pname(pt), c ? "a leg" : "an arm");
	return true;
}

static void night_summary(void)
{
	const int rows = 14;
	for (int first = 0; first == 0 || first < nsum; first += rows) {
		screen();
		text_window(0, 0, 30, 20);
		text_printf(1, 1, YEL, "Night, Day %d", run.day);
		for (int r = 0; r < rows && first + r < nsum; r++)
			text_put(1, 3 + r, sum[first + r], BGPAL_UI);
		if (!nsum)
			text_put(1, 3, "A quiet night.", BGPAL_UI);
		text_put(1, 18, "A: on", GREY);
		wait_key();
	}
}

// The night: jobs, then storylets. Saved at the end; a camp cut short replays from its start.
static void camp(void)
{
	u8 dead[ROSTER_MAX], treated[ROSTER_MAX];
	int nd = 0, guards = 0, saved = 0, need = ration_need();
	music_play(MUS_NIGHT);
	jobs_board();
	nsum = 0;
	memset(treated, 0, sizeof treated);
	for (int i = 0; i < run.nroster; i++) {
		Person *p = &run.roster[i];
		if (!p->alive || job[i] != J_GUARD)
			continue;
		guards++;
		skill_use(p, SK_FIREARMS, 2);
		note("%s kept watch", pname(i));
	}
	for (int i = 0; i < run.nroster; i++) { // cooks stretch tonight's meal
		Person *p = &run.roster[i];
		if (!p->alive || job[i] != J_COOK)
			continue;
		if (p->skill[SK_SURVIVAL] >= 2 && ate && saved < need) {
			res_add(RES_FOOD, 1);
			saved++;
			note("%s cooked: +1 food", pname(i));
		} else {
			note("%s cooked: nothing spare", pname(i));
		}
		skill_use(p, SK_SURVIVAL, 3);
	}
	for (int t = 0; t < run.nroster; t++) {
		Person *tp = &run.roster[t];
		if (!tp->alive || job[t] != J_TEND)
			continue;
		bool surgeon = tp->skill[SK_MEDICINE] >= 2 || tp->cast == CAST_DOC;
		int pt = surgeon && run.res[RES_MEDS] ? bitten_patient(t, treated) : -1;
		if (pt >= 0 && treat(t, pt)) {
			treated[pt] = 1;
			skill_use(tp, SK_MEDICINE, 5);
			continue;
		}
		int h = most_hurt();
		if (h < 0) {
			note("%s had nobody to tend", pname(t));
			continue;
		}
		Person *pp = &run.roster[h];
		int got = heal(pp, 1 + tp->skill[SK_MEDICINE]);
		if (pp->wounds)
			pp->wounds--;
		skill_use(tp, SK_MEDICINE, 3);
		note("%s tended %s: +%d HP", pname(t), pname(h), got);
	}
	for (int i = 0; i < run.nroster; i++) {
		Person *p = &run.roster[i];
		if (!p->alive || job[i] != J_FORAGE)
			continue;
		int ch = MIN(95, 60 + 10 * (p->skill[SK_SCAVENGE] + p->skill[SK_STEALTH]));
		bool ok = roll(100) < ch;
		skill_use(p, SK_SCAVENGE, 3);
		skill_use(p, SK_STEALTH, 2);
		if (ok && !roll(3)) {
			res_add(RES_PARTS, 1);
			note("%s found parts: +1", pname(i));
		} else if (ok) {
			int f = 1 + roll(3);
			res_add(RES_FOOD, f);
			note("%s found food: +%d", pname(i), f);
		} else {
			p->hp = p->hp > 2 ? p->hp - 2 : 1; // a wound, not a death
			if (p->wounds < 9)
				p->wounds++;
			note("%s got hurt foraging", pname(i));
		}
	}
	for (int i = 0; i < run.nroster; i++) {
		Person *p = &run.roster[i];
		if (!p->alive || (job[i] != J_REST && job[i] < J_COUNT))
			continue;
		int got = heal(p, REST_HP);
		p->wounds = 0;
		if (got)
			note("%s rested: +%d HP", pname(i), got);
	}
	if (!guards && alive_count() && roll(100) < RAID_CHANCE) {
		u8 idx[ROSTER_MAX];
		int v = idx[roll(list_alive(idx))];
		Person *p = &run.roster[v];
		sfx(SFX_AMBUSH);
		note("Walkers in the night!");
		if (p->hp > 3) {
			p->hp -= 3;
			note("%s was hurt: -3 HP", pname(v));
		} else {
			die(v, LOG_DEATH, DC_RAID);
			dead[nd++] = v;
			note("%s was killed", pname(v));
		}
	}
	night_summary();
	fire_deaths(dead, nd);
	if (alive_count()) {
		screen();
		story(SP_CAMP, -1);
	}
	if (!check_over()) {
		bool stay = (run_phase & PH_MASK) == PH_CAMP_STAY;
		if (run_phase & PH_DAWN_DAY) {
			char l2[32];
			int f = stay ? 0 : MIN(run.res[RES_FUEL], BYPASS_FUEL); // the detour costs what Bypass does
			run.res[RES_FUEL] -= f;
			snprintf(l2, sizeof l2, "The long way round: -%d fuel.", f);
			screen();
			say("Dawn.", stay ? "Another day at the bridge." : f ? l2 : "You take the long way round.");
			if (!day_pass()) {
				music_play(MUS_AUTO);
				return;
			}
		}
		run_phase = stay ? PH_STOP : PH_ROAD;
		run_save();
	}
	music_play(MUS_AUTO);
}

// ---------------------------------------------------------------- the end

static void final_screen(void)
{
	bool won = run.over == 1;
	int alive = alive_count(), per = 11;
	if (!gfx_ready)
		backdrop();
	music_play(won ? MUS_VICTORY : MUS_DEFEAT);
	for (int first = 0; first == 0 || first < run.nroster; first += per) {
		screen();
		text_window(0, 0, 30, 20);
		if (won) {
			put_center(1, "The river is behind you.", YEL);
			put_center(2, "Haven Bay is still far.", BGPAL_UI);
		} else if (!alive) {
			put_center(1, "Everyone is gone.", RED);
			put_center(2, "The road ends here.", BGPAL_UI);
		} else {
			put_center(1, "The boat sailed without you.", RED);
			put_center(2, "Haven Bay's dock is empty.", BGPAL_UI);
		}
		text_printf(1, 4, GREY, "Day %d. %d of %d alive.", run.day, alive, run.nroster);
		text_put(1, 6, "Name", GREY);
		text_put(12, 6, "Fate", GREY);
		text_put(24, 6, "Kills", GREY);
		for (int r = 0; r < per && first + r < run.nroster; r++) {
			const Person *p = &run.roster[first + r];
			text_put(1, 7 + r, person_name(p), p->alive ? BGPAL_UI : GREY);
			if (p->alive)
				text_put(12, 7 + r, "Alive", BLUE);
			else
				text_printf(12, 7 + r, RED, "\x7f %s", fate(first + r));
			text_printf(26, 7 + r, BGPAL_UI, "%d", p->kills);
		}
		text_put(1, 18, "A: on", GREY);
		wait_key();
	}
	screen();
	text_window(0, 0, 30, 20);
	text_put(1, 1, "Chronicle", YEL);
	draw_log(3, MAX(0, run.log_count - 12), 12);
	text_put(1, 18, "A: the end", GREY);
	wait_key();
	text_clear();
	music_play(MUS_AUTO);
}

// ---------------------------------------------------------------- new run, title, the loop

void run_new(int difficulty)
{
	static const u8 crew[4] = { CAST_MARA, CAST_DOC, CAST_JUNE, CAST_HOLT };
	int d = difficulty < 0 ? 0 : difficulty > 3 ? 3 : difficulty;
	memset(&run, 0, sizeof run);
	run.magic = RUN_MAGIC;
	run.version = RUN_VERSION;
	run.size = sizeof(RunState);
	run.difficulty = d;
	run.day = 1;
	run.ship_day = ship_days[d];
	run.node = N_CEDAR;
	run.pace = 1;
	run.rations = 1;
	run.morale = 6;
	run.vehicle[0] = VH_SEDAN;
	run.vehicle[1] = VH_PICKUP;
	run.vcond[0] = run.vcond[1] = 100;
	for (int r = 0; r < RES_COUNT; r++)
		run.res[r] = supply_base[r] * supply_pct[d] / 100;
	for (int i = 0; i < 4; i++) {
		const CastInfo *c = &cast_info[crew[i]];
		Person *p = &run.roster[i];
		p->cast = crew[i];
		p->alive = 1;
		p->grit = c->grit;
		p->aim = c->aim;
		p->speed = c->speed;
		p->nerve = c->nerve;
		p->trait[0] = c->trait[0];
		p->trait[1] = c->trait[1];
		for (int s = 0; s < SK_COUNT; s++) {
			p->skill[s] = MIN(c->skill[s], 5);
			p->skill_xp[s] = skill_xp0[p->skill[s]]; // XP matches the level, so growth stays even
		}
		p->weapon = c->weapon;
		p->armor = c->armor;
		p->sprite = c->sprite;
		p->he = c->he;
		p->maxhp = p->hp = 6 + c->grit;
		strncpy(p->name, c->name, sizeof p->name - 1); // for the .sav reader; person_name() uses cast_info
	}
	run.nroster = 4;
	run.rel[0][2] = run.rel[2][0] = 1;   // Mara and June
	run.rel[3][1] = run.rel[1][3] = -1;  // Holt and Doc
	run.rng = (frame_count * 0x9E3779B1u) ^ 0x5EED1234u;
	if (!run.rng)
		run.rng = 1;
	story_init_run();
	for (int i = 0; i < run.nroster; i++)
		chronicle(LOG_JOIN, i, 0);
	run_phase = PH_ROAD;
	memset(job, 0, sizeof job);
	ate = true;
}

static void intro(void)
{
	static char boat[80];
	snprintf(boat, sizeof boat, "The last boat leaves Haven Bay on Day %d. After that, nobody is coming.", run.ship_day);
	const Line lines[] = {
		{ PT_MARA, "Cedar Falls is gone. We take what we can carry, and we drive." },
		{ PT_JUNE, boat },
		{ PT_HOLT, "First the river. Miller's Bridge is the only way over." },
		{ PT_DOC, "Every day costs food. Every fight costs blood. Choose well." },
		{ 0, NULL },
	};
	journey_screen(-1, nodes[N_CEDAR].name, NULL);
	dialogue(lines);
}

// A save that passed its CRC is trusted, but the run layer still never indexes out of bounds.
static void sanitize(void)
{
	if (run.nroster > ROSTER_MAX)
		run.nroster = ROSTER_MAX;
	run.difficulty &= 3;
	if (run.rations > 2)
		run.rations = 1;
	if (run.node == 0xFF && !run.over)
		run.over = 1;
	if (run.node >= NODE_COUNT && !run.over) {
		run.node = N_CEDAR;
		run_phase = PH_ROAD;
	}
	if ((run_phase & PH_MASK) > PH_CAMP_STAY)
		run_phase = PH_STOP;
	memset(job, 0, sizeof job);
	ate = true;
}

static void run_loop(void)
{
	for (;;) {
		if (!gfx_ready)
			backdrop();
		if (run.over) {
			final_screen();
			run_erase(); // one run: permadeath
			return;
		}
		if (check_over())
			continue;
		int ph = run_phase & PH_MASK;
		if (ph == PH_ROAD) {
			if (!journey())
				return;
		} else if (ph == PH_STOP && (run_phase & PH_ARRIVE)) { // cut off during the arrival
			arrive_scenes();
		} else if (ph == PH_STOP) {
			if (!stop())
				return;
		} else if (run_phase & PH_AFTER) { // cut off after a mission
			back_screen();
			after_scenes();
		} else {
			camp();
		}
	}
}

static void diff_hover(int i)
{
	static const char *const desc[4] = {
		"+50% supplies. Boat: Day 12", "Base supplies. Boat: Day 10",
		"-25% supplies. Boat: Day 9", "-40% supplies. Boat: Day 8",
	};
	text_window(0, 15, 30, 3);
	text_put(1, 16, desc[i & 3], BGPAL_UI);
}

static bool confirm_erase(void)
{
	static const char *const items[2] = { "Erase it, start over", "Keep my run" };
	screen();
	text_window(2, 4, 26, 4);
	put_center(5, "You have a run going.", BGPAL_UI);
	put_center(6, "This erases your run.", RED);
	return menu(3, 9, 24, items, NULL, 2, 1, NULL, true) == 0;
}

// The Last Boat's own title menu (main.c): Continue / New Run / Back.
void lb_main(void)
{
	static const char *const items[3] = { "Continue", "New Run", "Back" };
	static const char *const diffs[4] = { "Easy", "Normal", "Hard", "Very Hard" };
	gfx_ready = false;
	music_play(MUS_AUTO);
	for (;;) {
		bool has = run_exists();
		u8 en[3] = { has, 1, 1 };
		screen();
		text_window(0, 2, 30, 5);
		put_center(3, "THE LAST BOAT", YEL);
		put_center(5, "One run. No second chances.", GREY);
		int c = menu(9, 9, 12, items, en, 3, has ? 0 : 1, NULL, true);
		if (c == 0) {
			if (!run_load()) {
				say("The save can't be read.", NULL);
				continue;
			}
			sanitize();
			run_loop();
		} else if (c == 1) {
			screen();
			text_window(0, 2, 30, 5);
			put_center(3, "THE LAST BOAT", YEL);
			put_center(5, "How hard is the road?", GREY);
			int d = menu(8, 8, 14, diffs, NULL, 4, 1, diff_hover, true);
			if (d < 0 || (has && !confirm_erase()))
				continue;
			run_erase(); // an older slot must never bring the last run back
			run_new(d);
			intro();
			run_save();
			run_loop();
		} else {
			break;
		}
		if (!gfx_ready)
			backdrop(); // back from a mission screen
	}
	spr.pointer_show = 0;
	spr.portrait = -1;
	text_clear();
	music_play(MUS_AUTO);
}
