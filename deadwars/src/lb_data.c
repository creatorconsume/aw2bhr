// Dead Wars: The Last Boat - placeholder rules data and slice maps.
// The designer owns these numbers (LAST_BOAT.md); they move to data/*.tsv with the story compiler.
#include "lb.h"
#include "lbart.h"

//                              name        min max dmg  acc noise ammo skill
const WeaponInfo weapon_info[WP_COUNT] = {
	[WP_FISTS]    = { "Fists",     1, 1, 1,   0, 1, 0, SK_MELEE },
	[WP_KNIFE]    = { "Knife",     1, 1, 2,  10, 1, 0, SK_MELEE },
	[WP_BAT]      = { "Bat",       1, 1, 3,   0, 2, 0, SK_MELEE },
	[WP_MACHETE]  = { "Machete",   1, 1, 4,   0, 2, 0, SK_MELEE },
	[WP_CROSSBOW] = { "Crossbow",  2, 5, 3,   5, 1, 0, SK_FIREARMS },
	[WP_PISTOL]   = { "Pistol",    1, 4, 3,   0, 6, 1, SK_FIREARMS },
	[WP_SHOTGUN]  = { "Shotgun",   1, 2, 6,  10, 9, 1, SK_FIREARMS },
	[WP_RIFLE]    = { "Rifle",     2, 7, 5,  10, 10, 1, SK_FIREARMS },
};

const ArmorInfo armor_info[AR_COUNT] = {
	[AR_NONE]   = { "None",    0, 0 },
	[AR_JACKET] = { "Jacket", 10, 0 },
	[AR_RIOT]   = { "Riot gear", 25, 1 },
};

const VehicleInfo vehicle_info[VH_COUNT] = {
	[VH_NONE]   = { "-",      0, 0, 0 },
	[VH_SEDAN]  = { "Sedan",  4, 2, 2 },
	[VH_PICKUP] = { "Pickup", 2, 6, 2 },
	[VH_VAN]    = { "Van",    6, 3, 3 },
};

const char *const trait_names[TR_COUNT] = {
	"", "Brave", "Cowardly", "Hothead", "Calm", "Loner", "Loyal", "Caring", "Selfish", "Greedy",
	"Reckless", "Paranoid", "Optimist", "Cynic", "Devout", "Addict", "Haunted", "Stubborn",
	"Curious", "Fragile", "Glutton", "Pacifist", "Clever", "Weary",
};
const char *const skill_names[SK_COUNT] = {
	"Firearms", "Melee", "Medicine", "Mechanics", "Stealth", "Scavenge", "Survival", "Leader",
};

//                                                        grit aim spd nrv  traits                    Fi Me Md Mc St Sc Su Le   weapon       armor       sprite       portrait  he
const CastInfo cast_info[CAST_COUNT] = {
	[CAST_MARA] = { "Mara",  "Firefighter",    4, 2, 3, 4, { TR_BRAVE, TR_LOYAL },      { 1, 2, 1, 0, 0, 1, 1, 2 }, WP_MACHETE,  AR_JACKET, LBG_MARA,    PT_MARA, 1 },
	[CAST_DOC]  = { "Doc",   "ER doctor",      2, 2, 2, 3, { TR_WEARY, TR_CARING },     { 0, 0, 3, 0, 0, 1, 1, 1 }, WP_PISTOL,   AR_NONE,   LBG_DOC,     PT_DOC,  0 },
	[CAST_JUNE] = { "June",  "Radio operator", 2, 3, 4, 3, { TR_CALM, TR_CURIOUS },     { 1, 0, 0, 1, 2, 2, 0, 0 }, WP_CROSSBOW, AR_NONE,   LBG_JUNE,    PT_JUNE, 1 },
	[CAST_HOLT] = { "Holt",  "Ex-army",        4, 4, 2, 2, { TR_HOTHEAD, TR_BRAVE },    { 3, 1, 0, 0, 0, 0, 2, 1 }, WP_RIFLE,    AR_JACKET, LBG_HOLT,    PT_HOLT, 0 },
	[CAST_ROSA] = { "Rosa",  "Mechanic",       3, 2, 3, 3, { TR_CYNIC, TR_LOYAL },      { 0, 1, 0, 3, 0, 1, 1, 0 }, WP_BAT,      AR_NONE,   LBG_ROSA,    0xFF,    1 },
	[CAST_TESS] = { "Tess",  "Runaway",        2, 2, 5, 2, { TR_RECKLESS, TR_SELFISH }, { 0, 1, 0, 0, 3, 2, 1, 0 }, WP_KNIFE,    AR_NONE,   LBG_TESS,    0xFF,    1 },
};

//                        hp mv sight hear hit dmg infect door
const ZombieInfo zombie_info[UT_COUNT] = {
	[UT_WALKER]  = { 4, 2, 4, 6, 50, 2, 30, 1 },
	[UT_RUNNER]  = { 3, 4, 5, 8, 60, 2, 30, 1 },
	[UT_BRUTE]   = { 12, 2, 3, 6, 55, 4, 20, 6 },
	[UT_SPITTER] = { 5, 2, 5, 6, 45, 2, 0, 1 },
	[UT_BLOATER] = { 6, 2, 3, 5, 40, 2, 40, 1 },
};

/* Tactical map legend (tac_rules.c):
 *  .  plain    f  woods    ^  rubble    ~  river    =  bridge (bridge zone)   w  wreck
 *  #  fence    -  road     r  ruins     n  nest     c  wreck with a car trunk to search
 *  x  road, exit zone      X  plain, exit zone      o  plain, deploy point
 *  buildings: a rectangle of H (house) S (shop) G (garage) P (pharmacy); the lower-case
 *  letter of the same kind marks the door tile (exactly one per building)
 */
#define W UT_WALKER
#define R UT_RUNNER
#define B UT_BRUTE
#define Z(t, x, y, d) { t, x, y, SIDE_HORDE, 0, d }
#define END { 0xFF, 0, 0, 0, 0, 0 }

// 1. Scavenge run: loot what you can on Main Street, then get back to the caravan (west).
static const char *const main_rows[] = {
	"ff...f......f.....f..fff",
	"f..HH....GG...HH.....ff.",
	"...hH....Gg...Hh........",
	"..w..........w......#...",
	"xx--------------c-------",
	"xx--------------------..",
	"..oo.w..........w.....f.",
	".oo....PpP..SsS....HH...",
	"f......PPP..SSS....hH..f",
	"ff..........w.......w...",
	".....HH..........f......",
	"...f.hH...ff...HH....w..",
	"..........ff...Hh.......",
	".w.....f.............ff.",
	"ff....fff....f.....ffff.",
	"fff..ffff...fff...ffffff",
	NULL,
};
static const UnitPlacement main_z[] = {
	Z(W, 12, 3, 0), Z(W, 17, 6, 0), Z(W, 21, 5, 1), Z(R, 22, 2, 1), Z(W, 8, 12, 1),
	Z(W, 15, 1, 1), Z(W, 13, 8, 1), Z(B, 20, 11, 1), Z(W, 6, 10, 1),
	END
};

// 2. Take the bridge: clear Miller's Bridge and hold it for 3 turns while the caravan crosses.
static const char *const bridge_rows[] = {
	"ff..f.....~~~.....f..fff",
	"f.........~~~......HH.ff",
	"..HH..w...~~~......hH...",
	"..hH......~~~...........",
	"oo.....#..~~~..w.....f..",
	"oo--------===-----------",
	"o......#..~~~...#......-",
	"..f.......~~~.......w...",
	".GG..f....~~~...f.......",
	".gG.......~~~.....SS..f.",
	"..........~~~.....sS....",
	"f...ff....~~~..f......ff",
	NULL,
};
static const UnitPlacement bridge_z[] = {
	Z(W, 11, 5, 0), Z(W, 12, 5, 0), Z(W, 14, 4, 0), Z(R, 17, 6, 0), Z(W, 20, 3, 1),
	Z(W, 15, 8, 1), Z(B, 21, 9, 1), Z(W, 19, 0, 1),
	END
};

// 3. Get through: cross Hensley farm to the east road; at least 3 must make it.
static const char *const farm_rows[] = {
	"fff..ff......ff....ff..ff",
	"f.......GGG.......f......",
	"o.......GgG.....HH.......",
	"o..#####........hH...w..X",
	"o..#.........f..........X",
	"o..#..HH.....ff.....#...X",
	"o.....Hh.....f......#...X",
	"...w.........#####..#....",
	"f.........w.........f....",
	"ff....ff.......ff.....fff",
	NULL,
};
static const UnitPlacement farm_z[] = {
	Z(W, 10, 4, 0), Z(W, 12, 7, 1), Z(R, 16, 5, 0), Z(W, 18, 2, 1), Z(W, 21, 6, 0),
	Z(W, 22, 1, 1), Z(B, 17, 8, 1), Z(W, 7, 8, 1), Z(R, 23, 9, 1),
	END
};
#undef W
#undef R
#undef B
#undef Z
#undef END

#define ROWS(r) r
const TacMission tac_missions[] = {
	{ "Main Street", "Scavenge what you can. Get back to the caravan (west road).",
	  ROWS(main_rows), main_z, OBJ_SCAVENGE, 0, 0, { 12, 24, 36 }, 3 },
	{ "Miller's Bridge", "Clear the bridge and hold it 3 turns.",
	  ROWS(bridge_rows), bridge_z, OBJ_BRIDGE, 3, 0, { 10, 20, 30 }, 3 },
	{ "Hensley Farm", "Get at least 3 people to the east road.",
	  ROWS(farm_rows), farm_z, OBJ_GET_THROUGH, 3, 1, { 10, 20, 30 }, 2 },
};
const int tac_mission_count = sizeof(tac_missions) / sizeof(tac_missions[0]);
