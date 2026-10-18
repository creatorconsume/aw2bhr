// Dead Wars - core game types and shared state
#ifndef GAME_H
#define GAME_H
#include "gba.h"
#include "assets.h"

#define MAP_MAX_W 30
#define MAP_MAX_H 20
#define MAX_UNITS 96
#define SCREEN_TW 15   // visible metatiles
#define SCREEN_TH 10

// ---- terrain ----
enum {
	TT_PLAIN, TT_FOREST, TT_HILL, TT_WATER, TT_BRIDGE, TT_RUINS, TT_WALL, TT_WRECK,
	TT_ROAD, TT_HOUSE, TT_WORKSHOP, TT_SAFEHOUSE, TT_HOSPITAL, TT_NEST, TT_RADIO,
	TT_HELIPAD, TT_COUNT
};
enum { MOVE_FOOT, MOVE_BOOT, MOVE_TIRES, MOVE_TREAD, MOVE_ZOMBIE, MOVE_COUNT };
#define IMPASSABLE 99

typedef struct {
	const char *name;
	u8 defense;          // terrain stars
	u8 cost[MOVE_COUNT]; // movement cost per move type
	u8 building;         // capturable / ownable
	u16 income;
} TerrainInfo;
extern const TerrainInfo terrain_info[TT_COUNT];

// ---- units ----
enum {
	UT_SURVIVOR, UT_HEAVY, UT_MEDIC, UT_SNIPER, UT_PICKUP, UT_BUS, UT_ARMOR,
	UT_WALKER, UT_RUNNER, UT_BRUTE, UT_SPITTER, UT_BLOATER, UT_COUNT
};
#define FIRST_ZOMBIE UT_WALKER
enum { SIDE_SURVIVOR, SIDE_HORDE, SIDE_NEUTRAL = 0xFF };

#define UF_INFANTRY  0x01  // can capture/search, can ride the bus, rises when killed by zombies
#define UF_VEHICLE   0x02
#define UF_INDIRECT  0x04  // cannot move and fire in the same turn
#define UF_TRANSPORT 0x08
#define UF_HEALER    0x10
#define UF_EXPLODES  0x20

typedef struct {
	const char *name;
	u16 cost;
	u8 move, movetype, vision, minrange, maxrange, flags;
	const char *desc;
} UnitInfo;
extern const UnitInfo unit_info[UT_COUNT];
extern const u8 base_damage[UT_COUNT][UT_COUNT]; // percent, 0 = cannot attack

typedef struct {
	u8 type, side, x, y;
	u8 hp;          // 1..100
	u8 moved;       // acted this turn
	u8 capture;     // remaining capture points on current tile (20 = none started)
	u8 cargo;       // index+1 of carried unit (bus), 0 = empty
	u8 carried;     // 1 if inside a transport (not on map)
	u8 dormant;     // zombie asleep until survivors come near
	u8 alive;
	u8 pad;
} Unit;

// ---- mission definitions ----
enum { GOAL_ROUT, GOAL_SURVIVE, GOAL_EVAC, GOAL_NESTS };

typedef struct { u8 portrait; const char *text; } Line;

typedef struct {
	u8 type, x, y, side, hp, dormant;
} UnitPlacement;

typedef struct {
	const char *title;
	const char *objective;      // short text for the objective screen
	u8 w, h;
	const char *const *rows;    // map rows, see map.c for the legend
	const UnitPlacement *units; // terminated by type 0xFF
	u16 funds, income_scale;    // income_scale: percent applied to building income
	u8 goal, goal_days;         // days to survive / evac deadline (0 = none)
	u8 evac_needed;             // units that must reach a helipad (GOAL_EVAC)
	u8 night_every;             // every Nth day is night (0 = never, 1 = always)
	u8 spawn_every;             // nests spawn every N horde turns (0 = never)
	u8 spawn_night_extra;       // extra spawns per nest at night
	u8 day_wave, wave_size;     // on this day a horde wave arrives at the nests
	const Line *intro, *outro;  // terminated by text == NULL
} Mission;

extern const Mission missions[];
extern const int mission_count;

// ---- live game state (saved wholesale for Suspend) ----
typedef struct {
	u32 magic;
	u8 mission;
	u8 w, h;
	u8 day;
	u8 side;           // whose turn
	u8 night;
	u8 evacuated;
	u8 over;           // 0 running, 1 won, 2 lost
	u32 funds;
	u8 terrain[MAP_MAX_H][MAP_MAX_W];
	u8 owner[MAP_MAX_H][MAP_MAX_W];
	u8 looted[MAP_MAX_H][MAP_MAX_W];
	u8 fog[MAP_MAX_H][MAP_MAX_W];      // 1 = hidden from the player
	Unit units[MAX_UNITS];
	u8 cur_x, cur_y, cam_x, cam_y;
	u32 rng;
	u16 kills, losses;
} GameState;
extern GameState gs;

typedef struct {
	u32 magic;
	u8 unlocked;        // missions unlocked (index of highest playable + 1)
	u8 anim_speed;      // 0 = fast, 1 = normal
	u8 has_suspend;
	u8 pad;
	u16 best_days[16];
} Profile;
extern Profile profile;

// ---- helpers shared across modules ----
// main.c / video.c
void video_init(void);
void wait_frames(int n);
void frame(void);              // wait for vblank, commit OAM, animate
extern u16 keys_down, keys_held, keys_rep;
void input_poll(void);
extern u32 frame_count;

// text.c
void text_clear(void);
void text_put(int x, int y, const char *s, int pal);
void text_putn(int x, int y, s32 n, int pal);
void text_window(int x, int y, int w, int h);
void text_clear_rect(int x, int y, int w, int h);
int  text_menu(int x, int y, int w, const char *const *items, const u8 *enabled, int count, int start);
void dialogue(const Line *lines);
void banner(const char *l1, const char *l2, int frames);
void text_printf(int x, int y, int pal, const char *fmt, ...);

// map.c
void map_load(int mission);
int  terrain_at(int x, int y);
int  move_cost(int movetype, int tt);
void map_draw(void);
void map_redraw_tile(int x, int y);
void fog_update(void);
void overlay_clear(void);
void overlay_set(int x, int y, int kind);
void camera_follow(int x, int y, bool smooth);
void camera_apply(void);
int  unit_at(int x, int y);    // index or -1 (ignores carried units)
void occ_begin(void);          // fast unit_at until occ_end(); nothing may move in between
void occ_end(void);
int  damage_estimate(const Unit *att, const Unit *def, bool luck);
int  damage_calc(const Unit *att, const Unit *def, int luck); // fixed luck 0..9
u32  rng_next(void);

// units.c
void sprites_draw(int hide_index);
void palettes_apply(void);
extern s16 cam_px, cam_py; // camera pixel offset

// battle.c
void battle_run(int mission);
int  find_paths(int u, u8 *dist, u8 *prev);
void attack(int a, int d);
void unit_move_anim(int u, const u16 *path, int len);
int  build_path(const u8 *prev, int sx, int sy, int tx, int ty, u16 *path);
int  unit_move_points(const Unit *u);
void start_side_turn(int side);
void info_panel(void);
bool can_end_on(int u, int x, int y);
void kill_unit(int i, int killer);
bool in_range(const Unit *u, int x, int y, int fx, int fy);
int  spawn_unit(int type, int side, int x, int y, int hp);
void check_victory(void);

// ai.c
void horde_turn(void);

// ui.c
void info_card(void);
void forecast_draw(int attacker, int defender);
void path_arrow_clear(void);
void path_arrow_update(const u8 *dist, const u8 *prev, int sx, int sy);
extern u8 turn_risen;
void summary_mark_horde(void);
void summary_mark_upkeep(void);
void summary_show(const char *l1, const char *l2, int frames);

// save.c
void profile_load(void);
void profile_save(void);
bool suspend_load(void);
void suspend_save(void);
void suspend_clear(void);

#define ABS(x) ((x) < 0 ? -(x) : (x))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define DISP_HP(hp) (((hp) + 9) / 10)

#endif
