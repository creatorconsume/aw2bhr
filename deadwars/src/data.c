// Dead Wars - static rules data: terrain, units, damage chart
#include "game.h"

#define X IMPASSABLE
//                                       name         def  foot boot tire tread zomb  bld income
const TerrainInfo terrain_info[TT_COUNT] = {
	[TT_PLAIN]     = { "Plain",     1, { 1, 1, 2, 1, 1 }, 0,    0 },
	[TT_FOREST]    = { "Woods",     2, { 1, 1, 3, 2, 1 }, 0,    0 },
	[TT_HILL]      = { "Rubble",    4, { 2, 1, X, X, 2 }, 0,    0 },
	[TT_WATER]     = { "River",     0, { X, X, X, X, X }, 0,    0 },
	[TT_BRIDGE]    = { "Bridge",    0, { 1, 1, 1, 1, 1 }, 0,    0 },
	[TT_RUINS]     = { "Ruins",     3, { 1, 1, X, 2, 1 }, 0,    0 },
	[TT_WALL]      = { "Barricade", 0, { X, X, X, X, X }, 0,    0 },
	[TT_WRECK]     = { "Wreck",     1, { 1, 1, X, X, 1 }, 0,    0 },
	[TT_ROAD]      = { "Road",      0, { 1, 1, 1, 1, 1 }, 0,    0 },
	[TT_HOUSE]     = { "House",     2, { 1, 1, 1, 1, 1 }, 1,  500 },
	[TT_WORKSHOP]  = { "Workshop",  3, { 1, 1, 1, 1, 1 }, 1, 1000 },
	[TT_SAFEHOUSE] = { "Safehouse", 4, { 1, 1, 1, 1, 1 }, 1, 1000 },
	[TT_HOSPITAL]  = { "Hospital",  3, { 1, 1, 1, 1, 1 }, 1,  500 },
	[TT_NEST]      = { "Nest",      0, { 1, 1, 2, 1, 1 }, 0,    0 },
	[TT_RADIO]     = { "Radio",     3, { 1, 1, 1, 1, 1 }, 1,  500 },
	[TT_HELIPAD]   = { "Helipad",   0, { 1, 1, 1, 1, 1 }, 0,    0 },
};
#undef X

#define INF UF_INFANTRY
#define VEH UF_VEHICLE
//                           name       cost  mv  movetype     vis min max flags
const UnitInfo unit_info[UT_COUNT] = {
	[UT_SURVIVOR] = { "Survivor", 1000, 3, MOVE_FOOT,   2, 1, 1, INF, "Cheap. Captures and searches." },
	[UT_HEAVY]    = { "Heavy",    3000, 2, MOVE_BOOT,   2, 1, 1, INF, "Shotgun + armor. Good on rubble." },
	[UT_MEDIC]    = { "Medic",    2000, 3, MOVE_FOOT,   2, 1, 1, INF | UF_HEALER, "Heals adjacent allies 1HP/turn." },
	[UT_SNIPER]   = { "Sniper",   3500, 2, MOVE_FOOT,   3, 2, 3, INF | UF_INDIRECT, "Range 2-3. Can't move and fire." },
	[UT_PICKUP]   = { "Pickup",   4000, 7, MOVE_TIRES,  5, 1, 1, VEH, "Fast truck with a mounted gun." },
	[UT_BUS]      = { "Bus",      4000, 6, MOVE_TIRES,  2, 0, 0, VEH | UF_TRANSPORT, "Carries 1 foot unit. No attack." },
	[UT_ARMOR]    = { "Armor",    7000, 5, MOVE_TREAD,  2, 1, 1, VEH, "Armored plow truck. Tough." },
	[UT_WALKER]   = { "Walker",   1000, 2, MOVE_ZOMBIE, 2, 1, 1, 0, "Slow. Bites. Never stops." },
	[UT_RUNNER]   = { "Runner",   1500, 4, MOVE_ZOMBIE, 3, 1, 1, 0, "Fast and fragile." },
	[UT_BRUTE]    = { "Brute",    5000, 3, MOVE_ZOMBIE, 2, 1, 1, 0, "Huge. Crushes vehicles." },
	[UT_SPITTER]  = { "Spitter",  2500, 2, MOVE_ZOMBIE, 3, 2, 2, UF_INDIRECT, "Spits acid at range 2." },
	[UT_BLOATER]  = { "Bloater",  2000, 2, MOVE_ZOMBIE, 2, 1, 1, UF_EXPLODES, "Bursts on death: 2HP to all near." },
};
#undef INF
#undef VEH

// attacker (row) vs defender (column), percent at full HP. Zombies never hurt zombies,
// survivors never hurt survivors.
const u8 base_damage[UT_COUNT][UT_COUNT] = {
	//               SUR HEA MED SNI PIC BUS ARM  WAL RUN BRU SPI BLO
	[UT_SURVIVOR] = {  0,  0,  0,  0,  0,  0,  0,  55, 60, 15, 50, 50 },
	[UT_HEAVY]    = {  0,  0,  0,  0,  0,  0,  0,  75, 80, 55, 70, 70 },
	[UT_MEDIC]    = {  0,  0,  0,  0,  0,  0,  0,  30, 35,  5, 25, 25 },
	[UT_SNIPER]   = {  0,  0,  0,  0,  0,  0,  0,  70, 80, 40, 75, 70 },
	[UT_PICKUP]   = {  0,  0,  0,  0,  0,  0,  0,  70, 75, 25, 65, 65 },
	[UT_BUS]      = {  0 },
	[UT_ARMOR]    = {  0,  0,  0,  0,  0,  0,  0,  90, 95, 70, 85, 85 },
	[UT_WALKER]   = { 45, 30, 50, 55, 15, 20,  5,   0 },
	[UT_RUNNER]   = { 50, 35, 55, 60, 20, 25,  5,   0 },
	[UT_BRUTE]    = { 90, 70, 95, 95, 70, 80, 45,   0 },
	[UT_SPITTER]  = { 60, 45, 65, 70, 45, 50, 25,   0 },
	[UT_BLOATER]  = { 35, 25, 40, 45, 15, 15,  5,   0 },
};
