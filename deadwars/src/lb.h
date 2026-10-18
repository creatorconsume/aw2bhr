// Dead Wars: The Last Boat - people, run state and tactical rules (design: docs/design/LAST_BOAT.md)
// This header is the contract between the modules:
//   person.c   person helpers           lb_data.c  rules data + maps (designer)
//   tac_rules.c tactical rules (pure)   tac.c / tac_ai.c / tac_gfx.c  mission flow, horde AI, map graphics
//   run.c      journey, stops, camp     runsave.c  A/B save          story.c  storylets (story.h)
#ifndef LB_H
#define LB_H
#include "game.h"

// ---------------------------------------------------------------- static data (lb_data.c)
enum { SK_FIREARMS, SK_MELEE, SK_MEDICINE, SK_MECHANICS, SK_STEALTH, SK_SCAVENGE, SK_SURVIVAL, SK_LEADERSHIP, SK_COUNT };
enum {
	TR_NONE, TR_BRAVE, TR_COWARDLY, TR_HOTHEAD, TR_CALM, TR_LONER, TR_LOYAL, TR_CARING, TR_SELFISH,
	TR_GREEDY, TR_RECKLESS, TR_PARANOID, TR_OPTIMIST, TR_CYNIC, TR_DEVOUT, TR_ADDICT, TR_HAUNTED,
	TR_STUBBORN, TR_CURIOUS, TR_FRAGILE, TR_GLUTTON, TR_PACIFIST, TR_CLEVER, TR_WEARY, TR_COUNT
};
enum { WP_FISTS, WP_KNIFE, WP_BAT, WP_MACHETE, WP_CROSSBOW, WP_PISTOL, WP_SHOTGUN, WP_RIFLE, WP_COUNT };
enum { AR_NONE, AR_JACKET, AR_RIOT, AR_COUNT };
enum { RES_FOOD, RES_FUEL, RES_AMMO, RES_MEDS, RES_PARTS, RES_COUNT };
enum { VH_NONE, VH_SEDAN, VH_PICKUP, VH_VAN, VH_COUNT };

typedef struct {
	const char *name;
	u8 minr, maxr;   // range in tiles (melee: 1, 1)
	u8 dmg;
	s8 acc;          // hit% modifier
	u8 noise;        // noise radius when used
	u8 ammo;         // run.res[RES_AMMO] used per attack (0 = none)
	u8 skill;        // SK_FIREARMS or SK_MELEE
} WeaponInfo;
extern const WeaponInfo weapon_info[WP_COUNT];

typedef struct {
	const char *name;
	u8 infect_cut;   // infection chance reduction, percent points
	u8 move_pen;
} ArmorInfo;
extern const ArmorInfo armor_info[AR_COUNT];

typedef struct {
	const char *name;
	u8 seats, cargo, fuel; // fuel per leg
} VehicleInfo;
extern const VehicleInfo vehicle_info[VH_COUNT];

typedef struct {
	const char *name, *bg;
	u8 grit, aim, speed, nerve;
	u8 trait[2];
	u8 skill[SK_COUNT];
	u8 weapon, armor;
	u8 sprite;       // LBG_* person sprite (lbart.h)
	u8 portrait;     // PT_* or 0xFF
	u8 he;           // pronoun: 0 he, 1 she, 2 they
} CastInfo;
enum { CAST_MARA, CAST_DOC, CAST_JUNE, CAST_HOLT, CAST_ROSA, CAST_TESS, CAST_COUNT };
extern const CastInfo cast_info[CAST_COUNT];

typedef struct {
	u8 hp, move, sight, hearing;
	u8 hit;          // base hit%
	u8 dmg;
	u8 infect;       // infection chance on a hit, percent
	u8 door_dmg;     // damage to a barricaded door per attack
} ZombieInfo;
extern const ZombieInfo zombie_info[UT_COUNT]; // rows UT_WALKER..UT_BLOATER

extern const char *const trait_names[TR_COUNT];
extern const char *const skill_names[SK_COUNT];

// ---------------------------------------------------------------- people (run state)
#define ROSTER_MAX 16
#define PF_INFECTED 0x01   // bitten: turns into a zombie when turn_in reaches 0
#define PF_AMPUTEE  0x02
#define PF_PANIC    0x04   // panicked this mission (overrides)

typedef struct {
	u8 cast;         // CAST_* (named) or 0xFF (unnamed: name[] holds the name)
	u8 alive;        // 0 = dead (a grave)
	u8 hp, maxhp;
	u8 grit, aim, speed, nerve;
	u8 trait[2];
	u8 skill[SK_COUNT], skill_xp[SK_COUNT];
	u8 weapon, armor;
	u8 flags;
	u8 turn_in;      // infected: days until they turn
	u8 wounds;       // -1 to a stat each, healed at camp
	u8 kills;
	u8 sprite;       // LBG_* map sprite
	u8 he;           // pronoun
	u16 xp;
	char name[10];
} Person;

#define LOG_MAX 256  // Chronicle ring buffer (4-byte records)
typedef struct { u8 day, event, actor, arg; } LogRec;
enum { LOG_JOIN, LOG_DEATH, LOG_BITTEN, LOG_TURNED, LOG_AMPUTATE, LOG_KILLS, LOG_MISSION_WON, LOG_MISSION_LOST,
       LOG_STORY, LOG_ARRIVE, LOG_LEFT_BEHIND, LOG_COUNT };

#define STORY_VARS 64
#define STORY_FLAG_BYTES 64  // 512 storylets

typedef struct {
	u32 magic;
	u16 version, size;      // layout check (runsave.c)
	u8 difficulty;          // 0 Easy 1 Normal 2 Hard 3 Very Hard
	u8 day, ship_day;       // the boat sails after ship_day
	u8 node;                // journey node (run.c); 0xFF = arrived
	u8 pace, rations;       // 0..2 each
	u8 morale;              // 0..10
	u8 over;                // 0 running, 1 won, 2 lost
	u8 nroster;
	u8 vehicle[4], vcond[4]; // VH_* and condition 0..100
	u16 res[RES_COUNT];
	Person roster[ROSTER_MAX];
	s8 rel[ROSTER_MAX][ROSTER_MAX]; // relationship -3..3, symmetric
	s16 vars[STORY_VARS];           // story world variables (story.h)
	u8 story_flags[STORY_FLAG_BYTES]; // storylet fired-once bits
	u8 story_cool[64];              // storylet cooldown day stamps (story.c decides use)
	u16 log_head, log_count;
	LogRec log[LOG_MAX];
	u32 rng;
} RunState;
extern RunState run;

// person.c
int  person_move(const Person *p);           // move points (2 + speed/2 rounded up, minus armour, wounds)
const char *person_name(const Person *p);
void skill_use(Person *p, int skill, int xp); // skills grow from use (XP 10/25/45/70/100 -> 1..5)
int  seats_total(void);
void chronicle(int event, int actor, int arg); // append a LogRec (day = run.day)

// run.c
void lb_main(void);          // title menu entry: new run / continue
void run_new(int difficulty);

// runsave.c: two slots so a power cut never loses the ironman run
#define RUN_SLOT_A 0x2000
#define RUN_SLOT_B 0x4000
#define RUN_MAGIC  0x44574C42 // "DWLB"
#define RUN_VERSION 1
void run_save(void);
bool run_load(void);
bool run_exists(void);
void run_erase(void);

// ---------------------------------------------------------------- tactical (tac_*.c)
#define TAC_MAX_BLDG 24
#define TAC_MAX_PACKS 8
enum { BK_HOUSE, BK_SHOP, BK_GARAGE, BK_PHARMACY, BK_COUNT };
enum { OBJ_SCAVENGE, OBJ_GET_THROUGH, OBJ_BRIDGE, OBJ_COUNT };

typedef struct {
	u8 x, y, w, h;   // footprint
	u8 door_x, door_y, door_dir; // door tile (inside) and the direction it opens to (0 N 1 E 2 S 3 W)
	u8 kind;
	u8 door_hp;      // > 0 = barricaded
	u8 searched;     // bit per tile (row-major in the footprint, w*h <= 8)
} Building;

typedef struct {
	u8 person;       // roster index (survivors) or 0xFF (zombies)
	u8 hp, maxhp;
	u8 moved, acted;
	u8 alerted;      // zombie: heading to goal
	u8 gx, gy;       // zombie goal
	u8 noisy;        // survivor made noise this turn (not hidden)
	u8 unseen;       // zombie started its turn unseen (first strike)
	u8 down;         // downed survivor: own turns left before bleeding out (0 = not downed)
	u8 extracted;    // 1 = left by an exit, 2 = left by the caravan (retreat)
	u8 carry[RES_COUNT]; // loot carried
} Actor;

typedef struct { u8 x, y, used; u8 res[RES_COUNT]; } TacPack;

typedef struct {
	const char *title, *brief;
	const char *const *rows;     // legend in lb_data.c, NULL-terminated
	const UnitPlacement *zombies; // UT_* type, x, y, -, -, dormant; ends with type 0xFF
	u8 objective, need;          // need: people through / turns to hold
	u8 night;
	u8 waves[3];                 // horde meter thresholds
	u8 wave_size;
} TacMission;
extern const TacMission tac_missions[];
extern const int tac_mission_count;

typedef struct {
	const TacMission *m;
	Actor actor[MAX_UNITS];      // parallel to gs.units
	Building bldg[TAC_MAX_BLDG];
	u8 nbldg;
	u8 bmap[MAP_MAX_H][MAP_MAX_W];  // building index + 1
	u8 zone[MAP_MAX_H][MAP_MAX_W];  // ZN_* bits
	u8 cache[MAP_MAX_H][MAP_MAX_W]; // CACHE_* (car trunks) or CACHE_PACK + pack index
	TacPack pack[TAC_MAX_PACKS];
	u16 horde;                   // horde meter
	u8 wave;                     // waves spawned
	u8 turn, held;               // turn number; bridge: turns held
	u8 result;                   // 0 running, 1 success, 2 failed (retreat / all lost)
	u8 through;                  // people extracted at ZN_EXIT
	u16 loot[RES_COUNT];         // extracted loot
} TacState;
#define ZN_EXIT   0x01
#define ZN_DEPLOY 0x02           // deploy points also let people retreat to the caravan
#define ZN_BRIDGE 0x04
#define CACHE_TRUNK 1
#define CACHE_PACK  16           // + pack index
extern TacState tac;

// rules (tac_rules.c): no graphics; host-testable (test/unit). Feedback goes through tac_fx_*.
void tac_load(const TacMission *m, const u8 *deploy, int n);   // deploy: roster indices
bool tac_in_bldg(int x, int y);
bool tac_edge_open(int x, int y, int dir, bool zombie);         // may a step cross this edge?
int  tac_move_cost(int x, int y, bool zombie);                  // IMPASSABLE if blocked
int  tac_paths(int ui, u8 *dist, u8 *prev);                     // dist/prev like find_paths()
int  tac_cover(int dx, int dy, int ax, int ay);                 // 0/20/40 of (dx,dy) vs attacker at (ax,ay)
bool tac_los(int x0, int y0, int x1, int y1);
bool tac_hidden(int ui);
bool tac_sees(int z, int t);                                    // zombie z can see survivor t
int  tac_weapon(int a, int d);                                  // WP_* used by survivor a on d, -1 = can't
int  tac_hit_chance(int a, int d, int *cover_out);              // 5..95, cover_out may be NULL
int  tac_damage(int a, int d);
void tac_noise(int x, int y, int radius);                       // alerts zombies, fills the horde meter, waves
void tac_attack(int a, int d);                                  // rolls, applies, feedback, XP, ammo, infection
void tac_kill(int i, int killer);
int  tac_search(int ui);                                        // loot found (sum), 0 = nothing / not searchable
bool tac_can_search(int ui);
int  tac_barricade_target(int ui);                              // building index, or -1
void tac_barricade(int ui);
void tac_extract(int ui);
void tac_end_survivor_phase(void);                              // bleeding out, bridge hold, objective
void tac_check_objective(void);
int  tac_spawn_zombie(int type, int x, int y, bool dormant);
int  tac_survivors_left(void);                                  // on the map, alive
#define TAC_DIRX(k) ((k) == 1 ? 1 : (k) == 3 ? -1 : 0)
#define TAC_DIRY(k) ((k) == 2 ? 1 : (k) == 0 ? -1 : 0)

// feedback hooks (tac.c on the GBA, stubs in test/unit)
void tac_fx_hit(int d, int dmg);
void tac_fx_miss(int d);
void tac_fx_text(const char *l1, const char *l2);  // banner
void tac_fx_noise(int x, int y, int radius);

// flow (tac.c), AI (tac_ai.c), graphics (tac_gfx.c)
int  tac_run(const TacMission *m, const u8 *deploy, int n); // plays a mission, applies results to run, returns tac.result
void tac_horde_turn(void);
void tac_gfx_load(void);        // after video_load_game_gfx(): roof tiles, icons, person sprites
bool tac_redraw_tile(int x, int y); // map_redraw_tile() hook; true if it drew the tile
void tac_oam(void);             // build_oam() hook: doors, barricades, caches, status icons
int  tac_unit_tile(int i, int frame); // build_oam() hook: OBJ tile of gs.units[i] (people use lbart sprites)
void tac_hud(void);             // horde meter, objective, selected person
void tac_fog(void);             // visibility from survivors (LOS), updates gs.fog and zombie draw flags

#endif
