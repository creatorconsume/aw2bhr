// Dead Wars - campaign missions (map legend in map.c)
#include "game.h"

#define NONE 0xFF
#define S SIDE_SURVIVOR
#define Z SIDE_HORDE
#define END { 0xFF, 0, 0, 0, 0, 0 }

// ---------------------------------------------------------------- 1. First Night
static const char *const m1_rows[] = {
	"ff..h....f...^^n",
	"f.......w.f.....",
	".S---------..r..",
	"..-.h.....-..f..",
	"h.-..f.r..-...^^",
	"..-....w..----.n",
	"K.-.h.....-.....",
	"..-..ff...-.h.f.",
	"f.-------.-.....",
	"ff.h.....f..r..^",
	"fff....h...ff.^^",
};
static const UnitPlacement m1_units[] = {
	{ UT_SURVIVOR, 2, 1, S, 0, 0 }, { UT_SURVIVOR, 1, 3, S, 0, 0 }, { UT_HEAVY, 3, 3, S, 0, 0 },
	{ UT_SURVIVOR, 0, 5, S, 70, 0 },
	{ UT_WALKER, 8, 3, Z, 0, 0 }, { UT_WALKER, 11, 1, Z, 0, 0 }, { UT_WALKER, 13, 6, Z, 0, 0 },
	{ UT_WALKER, 9, 8, Z, 0, 0 }, { UT_WALKER, 14, 5, Z, 0, 0 }, { UT_WALKER, 14, 1, Z, 0, 0 },
	{ UT_RUNNER, 13, 9, Z, 0, 0 }, { UT_WALKER, 6, 9, Z, 60, 0 },
	{ UT_WALKER, 6, 0, Z, 0, 0 }, { UT_WALKER, 6, 5, Z, 0, 0 }, { UT_WALKER, 5, 8, Z, 0, 0 },
	{ UT_RUNNER, 9, 6, Z, 0, 0 }, { UT_SPITTER, 7, 3, Z, 0, 0 }, { UT_BRUTE, 9, 4, Z, 0, 0 },
	END
};
static const Line m1_intro[] = {
	{ NONE, "Cedar Falls. Day 1 of the outbreak.\nThe sirens stopped an hour ago." },
	{ PT_MARA, "Station 9, sound off! ...Okay. Four of us. That's who's left." },
	{ PT_MARA, "Radio says the National Guard rolls through in four days. We hold this firehouse until then." },
	{ PT_MARA, "Search the houses for supplies. Anyone they bite gets back up, so nobody fights alone." },
	{ PT_MARA, "And when the sun goes down... stay close to the light." },
	{ 0, NULL },
};
static const Line m1_outro[] = {
	{ PT_MARA, "Day five. No Guard. No convoy. Nobody's coming." },
	{ PT_MARA, "Then we stop waiting. Pack everything. We move at dawn." },
	{ 0, NULL },
};

// ---------------------------------------------------------------- 2. Supply Run
static const char *const m2_rows[] = {
	"ff..h...~~..h...^^nf",
	"f.S---..~~...f..r..f",
	"..-..h..~~.h..ff....",
	"K.-.....~~.....w..h.",
	"..------==------.---",
	"h.-.f...~~..r...-.f.",
	"..-..w..~~....h.-...",
	"f.-.....~~.f....-.^^",
	"..----..~~..----..^n",
	".h.f....~~..-.h...^^",
	"...h..f.~~..-.....f.",
	"ff......==---..r....",
	"fff..h..~~..h....fff",
};
static const UnitPlacement m2_units[] = {
	{ UT_SURVIVOR, 2, 2, S, 0, 0 }, { UT_SURVIVOR, 3, 1, S, 0, 0 }, { UT_HEAVY, 1, 2, S, 0, 0 },
	{ UT_PICKUP, 1, 4, S, 0, 0 }, { UT_SURVIVOR, 0, 3, S, 80, 0 },
	{ UT_WALKER, 10, 4, Z, 0, 0 }, { UT_WALKER, 11, 2, Z, 0, 1 }, { UT_WALKER, 13, 6, Z, 0, 1 },
	{ UT_WALKER, 12, 9, Z, 0, 1 }, { UT_RUNNER, 15, 4, Z, 0, 1 }, { UT_WALKER, 17, 1, Z, 0, 0 },
	{ UT_WALKER, 18, 2, Z, 0, 0 }, { UT_BLOATER, 14, 8, Z, 0, 1 }, { UT_WALKER, 18, 9, Z, 0, 0 },
	{ UT_WALKER, 17, 7, Z, 0, 1 }, { UT_RUNNER, 11, 11, Z, 0, 1 }, { UT_WALKER, 6, 7, Z, 0, 1 },
	END
};
static const Line m2_intro[] = {
	{ PT_JUNE, "Hey! Don't shoot! I'm June. I fix things. I fixed that truck you just drove in on, actually." },
	{ PT_MARA, "You're alone out here?" },
	{ PT_JUNE, "Not alone. There's the nests. Two of 'em, across the river. They keep making more of those things." },
	{ PT_JUNE, "Burn the nests and this side of town goes quiet. I can get the old garage running for you." },
	{ PT_MARA, "Welcome to Station 9, June. Let's light them up." },
	{ 0, NULL },
};
static const Line m2_outro[] = {
	{ PT_JUNE, "Whoa. Quiet. Like, actually quiet." },
	{ PT_MARA, "Don't get used to it. Pick a radio channel and keep listening." },
	{ PT_JUNE, "...Mara. Someone's broadcasting. A soldier. He says there's a helicopter." },
	{ 0, NULL },
};

// ---------------------------------------------------------------- 3. Bridge Out
static const char *const m3_rows[] = {
	"fff..h..^^..r..h..f...",
	"f.S------...n..---....",
	"..-..f..-..^^..-.h.f..",
	"K.-..h..-.....r-....@.",
	"..-.....----w---------",
	"h.-..r......f...h.--..",
	"..---------.f..n...-..",
	"f....h....-.....f..-.h",
	"...f.....w-..h.....-..",
	".h...r....-.^^...w.-..",
	"..h..f....-..r.....-..",
	"ff....h...---------..f",
};
static const UnitPlacement m3_units[] = {
	{ UT_SURVIVOR, 2, 2, S, 0, 0 }, { UT_HEAVY, 3, 1, S, 0, 0 }, { UT_PICKUP, 2, 4, S, 0, 0 },
	{ UT_BUS, 1, 3, S, 0, 0 }, { UT_SURVIVOR, 0, 5, S, 0, 0 }, { UT_MEDIC, 1, 1, S, 0, 0 },
	{ UT_WALKER, 9, 1, Z, 0, 1 }, { UT_WALKER, 12, 2, Z, 0, 0 }, { UT_WALKER, 13, 1, Z, 0, 0 },
	{ UT_RUNNER, 10, 4, Z, 0, 1 }, { UT_WALKER, 14, 4, Z, 0, 1 }, { UT_BRUTE, 17, 4, Z, 0, 1 },
	{ UT_WALKER, 15, 7, Z, 0, 0 }, { UT_WALKER, 16, 6, Z, 0, 0 }, { UT_SPITTER, 13, 9, Z, 0, 1 },
	{ UT_WALKER, 19, 2, Z, 0, 1 }, { UT_WALKER, 21, 5, Z, 0, 1 }, { UT_RUNNER, 18, 9, Z, 0, 1 },
	{ UT_WALKER, 8, 8, Z, 0, 1 }, { UT_BLOATER, 11, 6, Z, 0, 1 }, { UT_WALKER, 20, 4, Z, 0, 1 },
	END
};
static const Line m3_intro[] = {
	{ PT_HOLT, "...anyone receiving, this is Sergeant Holt. Evac bird lands at the Route 9 helipad. Seven days." },
	{ PT_HOLT, "Pilot won't wait. Three seats. Bring anyone who can still walk." },
	{ PT_MARA, "Three seats. There's more of us than that." },
	{ PT_JUNE, "Three get out and tell the world. That's three more than zero. The bus fits two if you load it." },
	{ PT_MARA, "Fine. Get three of ours to the pad and Evac them. Everyone else covers the road." },
	{ 0, NULL },
};
static const Line m3_outro[] = {
	{ PT_HOLT, "Bird's up. Good work. ...You're not getting on it, are you, Captain?" },
	{ PT_MARA, "Somebody has to find out where this started." },
	{ PT_HOLT, "Then I'm staying too. The Guard pulled out of St. Agnes Hospital on day one. Something's in there." },
	{ 0, NULL },
};

// ---------------------------------------------------------------- 4. St. Agnes
static const char *const m4_rows[] = {
	"ff.h....^^..h..f..^n",
	"f.S--.....r.....w..^",
	"..-.f..##-##..h.....",
	"K.-....#...#.f....r.",
	"..---..-.b.-----....",
	"h.-....#...#...-.h..",
	"..-.r..##-##...-....",
	"f.-.......-..r.-..f.",
	"..------..-....--..n",
	".h...f....-..h.....^",
	"ff..h...n.-...f..h.^",
	"fff..r....---.....ff",
};
static const UnitPlacement m4_units[] = {
	{ UT_SURVIVOR, 2, 2, S, 0, 0 }, { UT_HEAVY, 3, 1, S, 0, 0 }, { UT_HEAVY, 1, 2, S, 0, 0 },
	{ UT_SNIPER, 2, 3, S, 0, 0 }, { UT_ARMOR, 2, 5, S, 0, 0 }, { UT_SURVIVOR, 1, 4, S, 0, 0 },
	{ UT_BRUTE, 9, 4, Z, 0, 0 }, { UT_WALKER, 8, 3, Z, 0, 0 }, { UT_WALKER, 10, 3, Z, 0, 0 },
	{ UT_WALKER, 8, 5, Z, 0, 0 }, { UT_SPITTER, 10, 5, Z, 0, 0 }, { UT_WALKER, 9, 7, Z, 0, 0 },
	{ UT_RUNNER, 12, 4, Z, 0, 1 }, { UT_WALKER, 15, 2, Z, 0, 1 }, { UT_WALKER, 18, 1, Z, 0, 0 },
	{ UT_BLOATER, 17, 6, Z, 0, 1 }, { UT_WALKER, 18, 8, Z, 0, 0 }, { UT_RUNNER, 16, 9, Z, 0, 1 },
	{ UT_WALKER, 8, 9, Z, 0, 0 }, { UT_WALKER, 7, 10, Z, 0, 0 }, { UT_SPITTER, 13, 10, Z, 0, 1 },
	{ UT_WALKER, 5, 7, Z, 0, 1 }, { UT_WALKER, 12, 7, Z, 0, 1 },
	END
};
static const Line m4_intro[] = {
	{ PT_HOLT, "St. Agnes. The Guard walled the courtyard in. Didn't help." },
	{ PT_DOC, "Hello? HELLO? I'm on the roof! Dr. Ellis Park! I've been up here for a week!" },
	{ PT_DOC, "Listen to me: the first patient was brought in here. The infection spread from this building. I have his files." },
	{ PT_ZERO, "............" },
	{ PT_MARA, "Clear every last one of them. Burn the nests. We're getting the doctor out." },
	{ 0, NULL },
};
static const Line m4_outro[] = {
	{ PT_DOC, "Thank you. I can patch your people up. That's what Medics are for." },
	{ PT_DOC, "Patient Zero... his bed was empty. Restraints snapped. He's still out there, and he's... different." },
	{ PT_HOLT, "Different how?" },
	{ PT_DOC, "The others follow him. Like a hive. When it gets dark, he calls them." },
	{ 0, NULL },
};

// ---------------------------------------------------------------- 5. The Long Night
static const char *const m5_rows[] = {
	"n..f..h....h..f..n",
	"..^...r..w....^...",
	"f..h.##-#-##.h..f.",
	"..r..#H.-.H#..w...",
	"h....#.-S-.#....h.",
	"--------KX--------",
	"..f..#.-P-.#..f...",
	"..w..#H.-.H#..r..h",
	"f..h.##-#-##.h...f",
	"...^...r..h...^...",
	"..h..f.....w..f...",
	"n...h..f..f...h..n",
};
static const UnitPlacement m5_units[] = {
	{ UT_SURVIVOR, 7, 4, S, 0, 0 }, { UT_HEAVY, 8, 3, S, 0, 0 },
	{ UT_SNIPER, 9, 3, S, 0, 0 }, { UT_MEDIC, 8, 7, S, 0, 0 },
	{ UT_SURVIVOR, 10, 4, S, 0, 0 },
	{ UT_WALKER, 1, 1, Z, 0, 0 }, { UT_WALKER, 16, 1, Z, 0, 0 }, { UT_WALKER, 1, 10, Z, 0, 0 },
	{ UT_WALKER, 16, 10, Z, 0, 0 }, { UT_RUNNER, 3, 5, Z, 0, 0 }, { UT_RUNNER, 14, 5, Z, 0, 0 },
	{ UT_BRUTE, 0, 5, Z, 0, 0 }, { UT_SPITTER, 17, 4, Z, 0, 0 }, { UT_BLOATER, 12, 9, Z, 0, 0 },
	{ UT_WALKER, 4, 9, Z, 0, 0 }, { UT_WALKER, 13, 2, Z, 0, 0 },
	END
};
static const Line m5_intro[] = {
	{ PT_MARA, "We've barricaded the old town square. Workshop, radio, clinic. It's the best spot in Cedar Falls." },
	{ PT_JUNE, "Radio picked up a military channel. A real convoy, this time. Seven days out." },
	{ PT_DOC, "Every night it'll get worse. He's coming for us. I can feel the horde moving like one thing." },
	{ PT_HOLT, "Then we make the walls count. Seven days, people. Nobody gets bitten." },
	{ PT_ZERO, "...the... living..." },
	{ 0, NULL },
};
static const Line m5_outro[] = {
	{ PT_HOLT, "Engines. Real engines! That's the convoy!" },
	{ PT_MARA, "We made it. All of us that are left." },
	{ PT_DOC, "Patient Zero pulled back with the sunrise. He's not done with us." },
	{ PT_MARA, "Neither are we. Rest up. Tomorrow we take the fight to him." },
	{ NONE, "END OF PART ONE. Thanks for playing the Dead Wars pilot!" },
	{ 0, NULL },
};

// width is taken from the first row at load time
#define MAP(r) 0, (sizeof(r) / sizeof(r[0])), r

const Mission missions[] = {
	{ "First Night", "Survive until Day 5.", MAP(m1_rows), m1_units,
	  0, 30, GOAL_SURVIVE, 6, 0, 2, 1, 1, 4, 2, m1_intro, m1_outro },
	{ "Supply Run", "Burn both zombie nests.", MAP(m2_rows), m2_units,
	  3000, 100, GOAL_NESTS, 0, 0, 3, 1, 1, 5, 2, m2_intro, m2_outro },
	{ "Bridge Out", "Evac 3 units by Day 8.", MAP(m3_rows), m3_units,
	  2000, 100, GOAL_EVAC, 8, 3, 3, 2, 1, 5, 3, m3_intro, m3_outro },
	{ "St. Agnes", "Destroy all zombies and nests.", MAP(m4_rows), m4_units,
	  5000, 100, GOAL_ROUT, 0, 0, 3, 2, 1, 6, 2, m4_intro, m4_outro },
	{ "The Long Night", "Survive until Day 8.", MAP(m5_rows), m5_units,
	  2000, 20, GOAL_SURVIVE, 7, 0, 2, 1, 2, 5, 3, m5_intro, m5_outro },
};
const int mission_count = sizeof(missions) / sizeof(missions[0]);
