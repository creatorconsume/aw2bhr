# Dead Wars — Part Two: "The Source"

Status: **design draft** (designer). Part Two won't go into `missions.c` until the producer
posts "hardening merged". The maps, placements and dialogue below are written to be pasted
straight in. Maps use the legend at the top of `src/map.c`. Placements have been checked
against the map: nothing on `~` or `#`, no vehicles on `^ r w`, and no overlapping units.
Dialogue passes `docs/design/tools/check_dialogue.py`: 23 columns × 5 rows next to a portrait,
28 × 5 for the narrator.

## Arc

Part One ended with the convoy arriving and Patient Zero pulling back at sunrise. In Part
Two, Station 9 stops running and starts hunting him. Doc's files say Zero was brought into
St. Agnes from the **Cedar Falls reservoir pumping station**, and he is heading back there.

| # | Title | Objective (in game) | Goal type | Core question for the player |
|---|---|---|---|---|
| 6 | Static | Doc must Secure the radio tower. | **GOAL_ESCORT** (new) + VIP | Can you push a fragile unit through a defended route? |
| 7 | Two Fires | Hold both safehouses until Day 9. | GOAL_SURVIVE (two `S`) | How do you split a small force between two fronts? |
| 8 | Last Bus Out | Evac 7 units by Day 8. The school must not fall. | GOAL_EVAC (+ a safehouse you can lose) | Speed against safety, under a hard deadline. |
| 9 | Burn It Down | Burn all five nests. It is always night. | GOAL_NESTS, `night_every = 1` | Fight blind in fog. Radio towers are your eyes. |
| 10 | Patient Zero | Destroy Patient Zero by Day 12. | **GOAL_BOSS** (new) + **UT_ZERO** (new) | Burn his nests to weaken him, then commit everything. |

Target bot win rates, continuing the Part One curve:

| Mission | Target | Notes |
|---|---|---|
| 6 | 35–55% | Escort missions punish greed. The VIP rule is the main difficulty. |
| 7 | 35–55% | |
| 8 | 30–50% | |
| 9 | 30–45% | |
| 10 | 20–40% | The finale: the hardest in the game, but winnable with nest-burning discipline. |

### Engine needs (requested in `team/inbox/engineer/`)

1. **VIP unit.** A new `Mission.vip` field holds an index into the placement list (`0xFF` = none).
   If the VIP dies, you lose ("Doc is down."). A VIP who is bitten does *not* rise; the
   mission is simply over. The VIP gets a star marker (an art request).
2. **GOAL_ESCORT.** You win when the VIP finishes Securing a target building. The target
   type is in a new `Mission.goal_tt` (M6 uses `TT_RADIO`).
3. **GOAL_BOSS.** You win when the boss unit dies. `goal_days`, if set, is a deadline: you
   lose when `day > goal_days`.
4. **UT_ZERO**, the Patient Zero boss unit. Full spec in the engineer request
   (`team/inbox/engineer/20261018-0141-designer-part-two-mechanics.md`) and summarised below.
5. **Day events (nice to have).** `Mission.events` is a list of `{ day, lines }`. It plays
   a dialogue at the start of the player's turn on that day. It's used for the twists below.
   Without it, each twist still works mechanically and only its line is lost.
6. **No engine work** is needed for M7 (two safehouses: losing either one already loses the
   mission), M8 (GOAL_EVAC plus a safehouse) or M9 (GOAL_NESTS with permanent night).
   I'll confirm this when I implement them.

### Simulator needs (designer-owned, `test/sim/`)

- GOAL_ESCORT: a VIP role that follows `goal_f` toward `goal_tt` and travels behind its
  escorts. Escorts score highly for tiles that cover the VIP's path. The VIP never attacks
  unless the target would die.
- GOAL_BOSS: once at least half the nests are burned, or the boss is under 50%, the hunt
  role targets UT_ZERO. Before that, it burns nests.
- M8: never strip the school's garrison below 2 units before Day 6.
- Report VIP deaths and "school fell" as separate loss reasons in the outcomes column.

---

## Mission 6 — Static

**Setting:** the WCFR radio mast on the ridge east of the river. Doc thinks the horde moves
like one body steered from a single point. The mast's direction-finder could triangulate
that point, but only Doc can rig it.

**Objective:** "Doc must Secure the radio tower." The VIP is Doc, a **Medic**. Lose if Doc
dies.

**Params:** funds 2000, income 100%, `GOAL_ESCORT`, `goal_tt = TT_RADIO`, night every 3,
spawn every 3 (+1 at night), wave on Day 4 ×2.

**Map (22×13):** start SW (Safehouse `S` 1,7; Workshop `K` 0,9). The river splits the map,
with two crossings: the north bridge (row 4) and the south bridge (row 9). The radio `x`
stands at (19,1) in rubble with a Spitter on overwatch. One nest lies far east (21,6).
**A second nest (5,11) sits just behind the start**, which is the twist.

```c
static const char *const m6_rows[] = {
	"ff..h...f~~...h..^^^..",
	"f.r......~~..f...^.x^.",
	"...f.....~~......^-.^.",
	"..h......~~..r...--..h",
	"..-------==------..f..",
	"..-..w...~~...w.r..h..",
	"h.-..f...~~.......f..n",
	".S-.r....~~..h..^.....",
	"..-.h..f.~~.f..^^..f..",
	"K.-------==------r..h.",
	"..f...h..~~....f-.....",
	"ff...n...~~..r..-..fff",
	"fff..h...~~..h..-.ffff",
};
static const UnitPlacement m6_units[] = {
	{ UT_SURVIVOR, 1, 6, S, 0, 0 }, { UT_HEAVY, 2, 6, S, 0, 0 }, { UT_SURVIVOR, 2, 8, S, 0, 0 },
	{ UT_MEDIC, 1, 8, S, 0, 0 } /* VIP: placement index 3 */, { UT_SNIPER, 3, 7, S, 0, 0 },
	{ UT_PICKUP, 2, 5, S, 0, 0 },
	{ UT_WALKER, 12, 4, Z, 0, 1 }, { UT_BRUTE, 11, 5, Z, 0, 1 }, { UT_WALKER, 13, 3, Z, 0, 1 },
	{ UT_SPITTER, 17, 1, Z, 0, 0 }, { UT_WALKER, 20, 2, Z, 0, 0 }, { UT_WALKER, 18, 0, Z, 0, 1 },
	{ UT_RUNNER, 15, 7, Z, 0, 1 }, { UT_WALKER, 13, 7, Z, 0, 1 }, { UT_BLOATER, 16, 9, Z, 0, 1 },
	{ UT_WALKER, 12, 10, Z, 0, 1 }, { UT_RUNNER, 20, 5, Z, 0, 1 }, { UT_WALKER, 20, 8, Z, 0, 0 },
	{ UT_WALKER, 6, 1, Z, 0, 0 }, { UT_WALKER, 4, 11, Z, 0, 0 }, { UT_WALKER, 7, 12, Z, 0, 0 },
	{ UT_RUNNER, 14, 11, Z, 0, 1 }, { UT_SPITTER, 21, 3, Z, 0, 1 },
	END
};
```

**Design intent:** the north bridge is short but has a dormant Brute on the far bank. The
south route is longer and safer at first, but the road ends in rubble. Doc moves 3 and needs
two full-HP turns on the tower (20 capture points at 10 per turn), so he must arrive healthy
with escorts around him. On Day 4 the rear nest spills a wave into the base, and the player
must decide whether to leave a guard or bring everyone forward. The Spitter on the ridge can
hit Doc while he captures, so it has to die first. That gives the Sniper a clear job.

**Twist:** Day 4. The rear nest by the safehouse boils over. "It's following the signal."

```c
static const Line m6_intro[] = {
	{ NONE, "Two days after the convoy.\nCedar Falls is quiet. Too quiet." },
	{ PT_DOC, "They don't wander. They move together, like something is steering them." },
	{ PT_DOC, "The WCFR mast has a direction-finder. If I rig it, I can trace where he's calling from." },
	{ PT_JUNE, "That mast is on the ridge past the river. Lot of teeth between here and there." },
	{ PT_MARA, "Then we walk Doc up there. Nobody else can do it. If he goes down, we have nothing." },
	{ PT_HOLT, "Copy. Doc stays in the middle. Everybody else is a wall." },
	{ 0, NULL },
};
static const Line m6_day4[] = {
	{ PT_JUNE, "Mara! The nest behind the safehouse just woke up!" },
	{ PT_ZERO, "...I... hear you..." },
	{ PT_DOC, "He's listening to the radio too. Hurry!" },
	{ 0, NULL },
};
static const Line m6_outro[] = {
	{ PT_DOC, "Signal's locked. There. The bearing runs east, to the reservoir." },
	{ PT_HOLT, "The pumping station. That's where the Guard found him." },
	{ PT_MARA, "Then that's where we finish this. But not today." },
	{ PT_JUNE, "Uh, guys? The direction-finder works both ways. The convoy camps are lighting up." },
	{ 0, NULL },
};
```

---

## Mission 7 — Two Fires

**Setting:** the convoy left its wounded in two places: the old clinic (west) and the
fire-road depot (east). Zero answered the broadcast by sending the horde at both.

**Objective:** "Hold both safehouses until Day 9." Either safehouse falling is a loss (the
existing `safehouse_lost` rule).

**Params:** funds 3000, income 100%, `GOAL_SURVIVE`, `goal_days = 8`, night every 2, spawn
every 2 (+1 at night), wave on Day 5 ×2.

**Map (22×13):** almost mirror-symmetric, with two walled compounds and a nest in each
corner. The asymmetry is the puzzle:
- **West compound (`S` 6,6):** one north gap, one east gap, and the Workshop `K` (6,8) set
  into the south wall like a gate. You recruit here.
- **East compound (`S` 15,6):** three gaps (N, W, S) and no workshop. It is weaker but has
  more units at the start.
- Two ring roads (rows 3 and 9) let the Bus and Armor shuttle between them, but at night
  runners cut the roads.

```c
static const char *const m7_rows[] = {
	"^n...f..h..r..h..f..n^",
	"^......f........f....^",
	"f.h..r....ff....r..h.f",
	"..------------------..",
	".h-.##.#......#.##.-h.",
	"..-.#..#..rr..#..#.-..",
	"..-.#.S...^^...S.#.-..",
	"..-.#..#..rr..#..#.-..",
	"h.-.##K#......#.##.-.h",
	"..------------------..",
	"f.h..r....ff....r..h.f",
	"^......f........f....^",
	"^n...f..h..r..h..f..n^",
};
static const UnitPlacement m7_units[] = {
	{ UT_HEAVY, 6, 5, S, 0, 0 }, { UT_SNIPER, 5, 6, S, 0, 0 }, { UT_SURVIVOR, 6, 7, S, 0, 0 },
	{ UT_HEAVY, 15, 5, S, 0, 0 }, { UT_SURVIVOR, 16, 6, S, 0, 0 }, { UT_MEDIC, 16, 7, S, 0, 0 },
	{ UT_ARMOR, 12, 9, S, 0, 0 }, { UT_BUS, 9, 3, S, 0, 0 },
	{ UT_WALKER, 2, 0, Z, 0, 0 }, { UT_WALKER, 19, 0, Z, 0, 0 }, { UT_WALKER, 2, 12, Z, 0, 0 },
	{ UT_WALKER, 19, 12, Z, 0, 0 }, { UT_RUNNER, 5, 1, Z, 0, 0 }, { UT_RUNNER, 16, 11, Z, 0, 0 },
	{ UT_BRUTE, 0, 6, Z, 0, 1 }, { UT_BRUTE, 21, 6, Z, 0, 1 }, { UT_SPITTER, 11, 0, Z, 0, 1 },
	{ UT_SPITTER, 11, 12, Z, 0, 1 }, { UT_BLOATER, 8, 2, Z, 0, 0 }, { UT_WALKER, 13, 10, Z, 0, 0 },
	END
};
```

**Design intent:** zombies path to the nearest owned building, and safehouses count as
distance 0 in `build_field()`, so each nest feeds its nearer compound. The horde does not
pick a side; geometry does. The Brute at each flank wakes on the first night (Day 2) and
heads for the nearer gap. The Armor in the middle can answer one Brute per night, not both.
That is the real decision. The Medic sits in the weak east compound on purpose.

**Twist:** Day 5. The wave arrives, and Zero speaks over the depot's radio.

```c
static const Line m7_intro[] = {
	{ PT_HOLT, "Convoy rolled out at dawn. Left their wounded in two spots: the clinic and the depot." },
	{ PT_JUNE, "And the horde heard us on that mast. They're coming at both." },
	{ PT_MARA, "We can't pick one. Split up. Holt, Doc, the depot. June and me, the clinic." },
	{ PT_DOC, "Both places have to stand. If either falls, those people die." },
	{ PT_MARA, "Eight days. Keep the road open between us. Nobody gets cut off." },
	{ 0, NULL },
};
static const Line m7_day5[] = {
	{ PT_ZERO, "...two... fires... one... goes... out..." },
	{ PT_HOLT, "He's on the depot radio. He's TALKING to us." },
	{ PT_MARA, "Then let him listen. Both fires stay lit." },
	{ 0, NULL },
};
static const Line m7_outro[] = {
	{ PT_HOLT, "Last of the wounded are out. Both sites held." },
	{ PT_DOC, "He sounded... human. Tired. Like he didn't want this either." },
	{ PT_MARA, "Doesn't matter what he wants. June, what's that on the scanner?" },
	{ PT_JUNE, "Kids. A teacher on a CB radio, at Lincoln Elementary. They've been hiding for nine days." },
	{ 0, NULL },
};
```

---

## Mission 8 — Last Bus Out

**Setting:** Lincoln Elementary, across town. A teacher and four others are holed up. A
second evac bird can land at the lot west of town on Day 8, and only then.

**Objective:** "Evac 7 units by Day 8. Don't lose the school." The school is a Safehouse
`S` (21,5). If the horde overruns it, the mission is lost, even after everyone else is out.
This is the twist: you can't strip the school bare to run for the pad.

**Params:** funds 0, income 100% (there is no Workshop, so funds only come from searching
houses), `GOAL_EVAC`, `goal_days = 8`, `evac_needed = 7`, night every 2, spawn every 2 (+1
at night), wave on Day 4 ×2.

**Map (25×12):** helipad `@` (0,5) at the far west. A main road runs across to the school
compound, walled with an entrance on each side (W and E, both on row 6). There are 6 starting
units and 5 stranded at the school (hurt), so 11 bodies against 7 seats. Houses on the way
can give you survivors, which are also evac bodies. A nest in mid-route (9,6) sits right next
to the road. The Brute (15,6) sleeps on the only road into the school.

```c
static const char *const m8_rows[] = {
	"ff..h.....f...^^...h..fff",
	"f.....r.......n....f....f",
	"..h..f....h.......r..h...",
	".--------------..........",
	".-.f..w...r...-....#####.",
	"@-....h......w-....#hSh#.",
	".-..f....n....------.....",
	".-.h.....f....-....#h.h#.",
	".---------.......f.#####.",
	"..f..h....-..r......f....",
	"ff....r...------..h...n..",
	"fff..h...f.....f.......ff",
};
static const UnitPlacement m8_units[] = {
	{ UT_BUS, 1, 3, S, 0, 0 }, { UT_PICKUP, 2, 3, S, 0, 0 }, { UT_SURVIVOR, 1, 4, S, 0, 0 },
	{ UT_SURVIVOR, 2, 5, S, 0, 0 }, { UT_HEAVY, 1, 6, S, 0, 0 }, { UT_MEDIC, 2, 7, S, 0, 0 },
	{ UT_SURVIVOR, 20, 5, S, 50, 0 }, { UT_SURVIVOR, 22, 5, S, 60, 0 }, { UT_SURVIVOR, 21, 6, S, 70, 0 },
	{ UT_HEAVY, 20, 7, S, 40, 0 }, { UT_MEDIC, 22, 7, S, 60, 0 },
	{ UT_WALKER, 6, 4, Z, 0, 1 }, { UT_WALKER, 10, 4, Z, 0, 1 }, { UT_RUNNER, 9, 5, Z, 0, 1 },
	{ UT_WALKER, 13, 5, Z, 0, 1 }, { UT_SPITTER, 14, 2, Z, 0, 1 }, { UT_BRUTE, 15, 6, Z, 0, 1 },
	{ UT_WALKER, 18, 4, Z, 0, 0 }, { UT_WALKER, 18, 7, Z, 0, 0 }, { UT_WALKER, 24, 6, Z, 0, 0 },
	{ UT_WALKER, 24, 3, Z, 0, 0 }, { UT_WALKER, 21, 9, Z, 0, 0 }, { UT_RUNNER, 17, 9, Z, 0, 1 },
	{ UT_BLOATER, 19, 9, Z, 0, 1 }, { UT_WALKER, 11, 9, Z, 0, 1 }, { UT_WALKER, 8, 10, Z, 0, 1 },
	{ UT_WALKER, 21, 2, Z, 0, 1 }, { UT_RUNNER, 23, 1, Z, 0, 1 },
	END
};
```

**Design intent:** the best line is to hold the school with the stranded group (houses give
2 stars and heal on owned buildings) while the starting group fights east, then reverse into
a fighting retreat. A Bus round trip is about 3 days. The Bus counts as two seats (itself
plus cargo), so it is the most valuable unit on the map. Losing it to the Brute is the
classic failure. The Pickup wins the race but can't carry anyone. Infected survivors become
walkers inside your formation, so the 5 hurt stranded units are a liability until healed.

**Tuning knobs:** `evac_needed` (6 is easy, 8 is brutal), the stranded units' HP, and the
Brute's dormant flag.

```c
static const Line m8_intro[] = {
	{ NONE, "Lincoln Elementary.\nA teacher, four survivors, one CB radio." },
	{ PT_JUNE, "Her name's Mrs. Okafor. She says the school's walls are holding. For now." },
	{ PT_HOLT, "Evac bird can land at the west lot on Day 8. One trip. Seven seats." },
	{ PT_MARA, "Seven seats, and we don't leave the school until the last of them is out." },
	{ PT_DOC, "If that school falls, every one of them turns. Keep it standing." },
	{ 0, NULL },
};
static const Line m8_day4[] = {
	{ PT_JUNE, "Okafor says something big is pounding on the east doors." },
	{ PT_MARA, "Tell her we're coming. Tell her to keep the kids away from the windows." },
	{ 0, NULL },
};
static const Line m8_outro[] = {
	{ PT_HOLT, "Bird's up. Seven out. ...That's a good day, Captain." },
	{ PT_JUNE, "Okafor gave me something. A map the Guard dropped. The nests ring the reservoir." },
	{ PT_DOC, "They're feeding him. Every nest is a link back to him. Burn them, and he gets weaker." },
	{ PT_MARA, "They only show themselves at night. So we go at night." },
	{ 0, NULL },
};
```

---

## Mission 9 — Burn It Down

**Setting:** the woods and farm roads around the reservoir. It is always night: `night_every
= 1` means fog for the whole mission, zombies move +1 and hit 15% harder, and no zombie is
dormant.

**Objective:** "Burn all five nests."

**Params:** funds 5000, income 100%, `GOAL_NESTS`, night every 1, spawn every 2 (+0 at night,
so each nest makes 1 every other day), wave on Day 6 ×2.

**Map (24×13):** base in the west (`K` 2,8, `S` 2,9, `H` 2,10). Two radio towers: one next to
the base (1,7), which is easy and lights up the home side, and one deep in the NE (21,2) on the
way to three nests. Nests: (0,0) NW, (9,6) centre, (23,0) NE, (23,5) E, (23,12) SE. Three of
them sit on the east edge, so the far tower is what makes a push in that direction survivable.

```c
static const char *const m9_rows[] = {
	"n...f..^^..h...f..^^...n",
	"..h..r.....f..r.....h...",
	".f..w...h.-----...f..x..",
	"....----------.-..w.....",
	"..r.-..f..^^.r.-....h..f",
	"h...-.h...^^...---.....n",
	"..f.-....n.......-..r...",
	".x..-..r.....h...-..f...",
	"..K.-----......h.-..h...",
	"..S.....-------------...",
	"..H.f.....h....f....^^..",
	"f...h..r.....^^..h......",
	"ff..f..h..f...f..r.....n",
};
static const UnitPlacement m9_units[] = {
	{ UT_ARMOR, 3, 9, S, 0, 0 }, { UT_HEAVY, 3, 8, S, 0, 0 }, { UT_HEAVY, 1, 9, S, 0, 0 },
	{ UT_SNIPER, 2, 7, S, 0, 0 }, { UT_MEDIC, 3, 10, S, 0, 0 }, { UT_SURVIVOR, 1, 8, S, 0, 0 },
	{ UT_SURVIVOR, 4, 9, S, 0, 0 }, { UT_PICKUP, 5, 9, S, 0, 0 },
	{ UT_SPITTER, 9, 5, Z, 0, 0 }, { UT_WALKER, 10, 6, Z, 0, 0 }, { UT_BRUTE, 13, 4, Z, 0, 0 },
	{ UT_WALKER, 22, 0, Z, 0, 0 }, { UT_WALKER, 23, 1, Z, 0, 0 }, { UT_SPITTER, 20, 0, Z, 0, 0 },
	{ UT_RUNNER, 22, 5, Z, 0, 0 }, { UT_WALKER, 22, 6, Z, 0, 0 }, { UT_BLOATER, 21, 11, Z, 0, 0 },
	{ UT_WALKER, 22, 12, Z, 0, 0 }, { UT_WALKER, 1, 1, Z, 0, 0 }, { UT_RUNNER, 2, 0, Z, 0, 0 },
	{ UT_WALKER, 16, 8, Z, 0, 0 }, { UT_SPITTER, 14, 11, Z, 0, 0 },
	END
};
```

**Design intent:** this is the fog mission. Ambushes stop your moves short, so the
player has to move in a tight column, scout with the Pickup (vision 5), and take the towers.
Spitters near each nest make burning costly: the burner sits in place for 2 turns and the
Spitter gets free shots. The night spawn mix is 45% Runners, which is why permanent night is
already hard without any new mechanic. If the bot's win rate is under 25%, raise `spawn_every`
to 3 before touching anything else.

**Twist:** Day 3. Zero is seen at the edge of the fog (dialogue only), and his voice taunts
on every nest you burn. The "nest burned" taunt is a nice-to-have event hook. Otherwise it
uses a day event.

```c
static const Line m9_intro[] = {
	{ NONE, "The reservoir woods.\nNo moon tonight. No moon any night, out here." },
	{ PT_DOC, "Five nests ring the water. Each one is a link to him. Cut them all." },
	{ PT_JUNE, "Radio masts out here still have power. Grab them and I can light up the woods for you." },
	{ PT_HOLT, "Tight column. Nobody walks into the dark alone." },
	{ PT_MARA, "Burn them all. Then he's got nothing left to hide behind." },
	{ 0, NULL },
};
static const Line m9_day3[] = {
	{ PT_HOLT, "Contact, tree line! Tall one. He's just... watching." },
	{ PT_ZERO, "...you... burn... my... children..." },
	{ PT_MARA, "Hold fire. He wants us to chase him into the dark. Stick to the nests." },
	{ 0, NULL },
};
static const Line m9_outro[] = {
	{ PT_JUNE, "That's the last one. Look at the smoke." },
	{ PT_DOC, "I felt that. All of them did. The whole horde just... flinched." },
	{ PT_ZERO, "...come... then..." },
	{ PT_MARA, "Pumping station. Dawn. Everybody eats, everybody sleeps. Then we end it." },
	{ 0, NULL },
};
```

---

## Mission 10 — Patient Zero

**Setting:** the Cedar Falls reservoir pumping station. The Guard's old perimeter wall rings
it. Inside, a crater of rubble surrounds the nest where Zero sits.

**Objective:** "Destroy Patient Zero by Day 12."

**Params:** funds 6000, income 100%, `GOAL_BOSS`, `goal_days = 12`, night every 3, spawn
every 3 (+1 at night), wave on Day 6 ×2.

**Map (26×16):** the player's base is a row of buildings along the south edge (`H S K P H`,
row 14) with neutral radio towers in the SW and SE corners. The perimeter wall (cols 7–19,
rows 2–10) has four gates: N (12–13,2), W (7,6), E (19,6) and S (12–13,10). Inside it is a
rubble crater (cols 10–16, rows 4–8) with **one plain gap at (13,8)**. Rubble stops tires and
treads, so the Armor can only reach Zero through that gap. Heavies (boot, rubble cost 1) are
the crater's natural assault unit. 8 nests: six outside the wall, plus (13,3) inside the
north wall and (13,6) under Zero.

```c
static const char *const m10_rows[] = {
	"^^^f..n.....^^.....n..f^^^",
	"^^..r....h..^^..h.....r.^^",
	"f...f..#####..#####.....f.",
	"..h....#.....n.....#..h...",
	"....r..#..r^^^^^r..#....r.",
	"n......#..^.....^..#.....n",
	"...f......^..n..^.....f...",
	"..r....#..^.....^..#..r...",
	"....h..#..r^^.^^r..#..h...",
	"f......#...........#.....f",
	"...n...#####..#####..n....",
	"..h.....r.........r....h..",
	".f....------------------f.",
	"..x...-....h..h.....-..x..",
	"f.....-..H.S.K.P.H..-....f",
	"ff.......f........f.....ff",
};
static const UnitPlacement m10_units[] = {
	{ UT_ARMOR, 10, 13, S, 0, 0 }, { UT_HEAVY, 11, 13, S, 0, 0 }, { UT_HEAVY, 14, 13, S, 0, 0 },
	{ UT_SNIPER, 12, 13, S, 0, 0 }, { UT_MEDIC, 13, 13, S, 0, 0 }, { UT_SURVIVOR, 9, 13, S, 0, 0 },
	{ UT_SURVIVOR, 16, 13, S, 0, 0 }, { UT_PICKUP, 8, 12, S, 0, 0 },
	{ UT_ZERO, 13, 6, Z, 0, 0 }, { UT_BRUTE, 13, 4, Z, 0, 0 }, { UT_BRUTE, 13, 9, Z, 0, 1 },
	{ UT_SPITTER, 11, 5, Z, 0, 0 }, { UT_SPITTER, 15, 7, Z, 0, 0 }, { UT_WALKER, 12, 3, Z, 0, 1 },
	{ UT_WALKER, 14, 3, Z, 0, 1 }, { UT_RUNNER, 8, 6, Z, 0, 1 }, { UT_RUNNER, 18, 6, Z, 0, 1 },
	{ UT_BLOATER, 12, 11, Z, 0, 1 }, { UT_WALKER, 6, 1, Z, 0, 0 }, { UT_WALKER, 19, 1, Z, 0, 0 },
	{ UT_WALKER, 1, 6, Z, 0, 0 }, { UT_WALKER, 24, 6, Z, 0, 0 }, { UT_WALKER, 4, 9, Z, 0, 1 },
	{ UT_WALKER, 21, 9, Z, 0, 1 }, { UT_SPITTER, 2, 11, Z, 0, 1 }, { UT_RUNNER, 23, 11, Z, 0, 1 },
	END
};
```

**Design intent:** a two-phase fight that the player chooses how to stage.
1. **Weaken.** Zero's "Hive Link" regenerates him for every nest still standing (see the
   spec below). With 8 nests he out-heals anything short of a full alpha strike. Burning the
   outer nests is the first half of the mission. It is also where the deadline bites:
   12 days for 6 outer nests plus the assault.
2. **Assault.** Zero leaves the crater only at night (Days 3, 6, 9, 12) or when a survivor is
   within his reach. The ideal kill: bait him out at night onto plain ground (1 star) where
   the Armor can reach him, or go in through the gap with Heavies on the rubble. Anything
   killed inside his aura rises as a **Runner**, so a failed assault snowballs.

**Twist:** Day 6. The wave, plus Zero's "Call" (he summons extra zombies at night). Zero
also has a final line when he drops below half HP (a nice-to-have `boss_half` hook).

```c
static const Line m10_intro[] = {
	{ NONE, "Cedar Falls Reservoir.\nPumping Station 2. Where it began." },
	{ PT_DOC, "Patient Zero. Walter Kessling, 54. Pump operator. Admitted with a fever and a bite he couldn't explain." },
	{ PT_HOLT, "Guard walled the station in. Didn't keep him in. Kept everyone else out." },
	{ PT_DOC, "Every nest still standing feeds him. Burn them, or he'll heal faster than we can hurt him." },
	{ PT_ZERO, "...Ellis... you... came... back..." },
	{ PT_MARA, "Twelve days of food. One shot. Station 9, let's go to work." },
	{ 0, NULL },
};
static const Line m10_day6[] = {
	{ PT_ZERO, "...all... of... you... come... HOME..." },
	{ PT_JUNE, "Every nest is boiling over! Mara, they're coming from everywhere!" },
	{ PT_MARA, "Hold the line! Keep burning!" },
	{ 0, NULL },
};
static const Line m10_boss_half[] = {
	{ PT_ZERO, "...it... hurts... it... always... hurt..." },
	{ PT_DOC, "He's weakening! Don't let up!" },
	{ 0, NULL },
};
static const Line m10_outro[] = {
	{ PT_ZERO, "...quiet... finally..." },
	{ NONE, "The horde stops.\nAll of it, all at once." },
	{ PT_DOC, "They're just... standing there. The link's gone. They're only bodies now." },
	{ PT_HOLT, "Bodies we can handle. Bodies don't plan." },
	{ PT_JUNE, "So that's it? We won?" },
	{ PT_MARA, "We survived. Station 9, sound off." },
	{ PT_MARA, "...Four of us. That's who's left. That's enough." },
	{ NONE, "END OF PART TWO.\nThanks for playing Dead Wars!" },
	{ 0, NULL },
};
```

(The last Mara line echoes her first line in Mission 1. Keep the two in sync if either
changes.)

---

## Patient Zero (UT_ZERO): summary

The full spec is in `BOSS_PATIENT_ZERO.md`, which also covers the Frenzy phase at half HP. The engineer request is `team/inbox/engineer/20261018-0141-designer-part-two-mechanics.md`.

| Stat | Value | Why |
|---|---|---|
| Move | 3, plus the night +1 | Same as a Brute. He shouldn't outrun Runners. |
| Move type | MOVE_ZOMBIE | |
| Range | 1 (direct) | |
| Cost (AI value) | 20000 | |
| Vision | 3 | |
| Hive Link | regen +5 HP per nest on the map at the start of each horde turn, max +30 | Ties nest-burning to the boss. |
| Aura | horde units within 2 tiles: +15% attack, +1 terrain star | Escorts become dangerous. Kill or bait them away. |
| Raise | survivors killed within 2 tiles of him rise as **Runners** at 80 HP | "Faster infection." |
| Call | every night horde turn: spawn 2 zombies (Walker or Runner) next to him | Night assault is a gamble. |
| Leash | by day he stays within 3 tiles of his start unless a survivor is within his attack reach | Forces the player to go in or bait him. |
| Death | every other horde unit takes 30 HP, and the mission is won | Payoff. |
| Never dormant, never captures, Bloater bursts don't hurt him | | |

Damage (percent at full HP, before terrain):

| Zero attacks → | SUR | HEA | MED | SNI | PIC | BUS | ARM |
|---|---|---|---|---|---|---|---|
| | 95 | 75 | 100 | 100 | 70 | 85 | 50 |

| Attacker → Zero | SUR | HEA | MED | SNI | PIC | ARM |
|---|---|---|---|---|---|---|
| | 10 | 35 | 3 | 40 | 25 | 50 |

Sanity math: Zero on plain (1 star), with no aura bonus to himself, at 100 HP takes Armor 45,
Sniper 36 and Heavy 31, about 112 in total. So a fully healthy three-unit strike *can*
one-turn him in the open. In the crater (rubble, 4 stars) the same strike does about 75.
Hive Link with 4 nests left heals 20, so in there it takes about two strikes with all nests
burned and about three with half of them. That gap is the design: bait him out, or burn first.
