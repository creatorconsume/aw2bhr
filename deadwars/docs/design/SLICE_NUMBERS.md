# The Last Boat: slice numbers and story content (designer review)

Status: **N1-N8 applied** by the integrator (2026-10-18), as proposed, in `src/lb_data.c`, `src/run.c` and `src/tac_rules.c`; N9 needed no change. The Horde meter also grows by difficulty (LAST_BOAT.md 16: x0.75 / x1 / x1.25 / x1.5). The run.c requests R1-R3 and R4 (doors by position) are in too; R5 (recruiting Rosa and Tess) is not. See `docs/tech/LAST_BOAT_ENGINE.md`.
Scope: the Act 1 slice (Cedar Falls to Miller's Bridge), `LAST_BOAT.md` sections 5-11, 16 and 18.
Sources reviewed: `src/lb_data.c` (weapons, armour, cast, zombies, the three maps), `src/run.c` ("slice rules"), `src/tac_rules.c` (hit chance, noise, waves, buildings, objectives), and `data/story/`.

Part 1 is the list of number and rule changes. Parts 2 to 5 explain each one. Part 6 describes the story content added to `data/story/`, and part 7 lists what the other roles need to do.

---

## 1. Proposed changes at a glance

| # | Where | Current | Proposed | Why |
|---|---|---|---|---|
| N1 | `run.c` `ship_days[]` (and the `diff_hover` text) | Easy 14 / Normal 12 / Hard 10 / Very Hard 9 | **12 / 10 / 9 / 8** | Normal 12 leaves 6-8 spare days on every route (2.1), so the clock almost never ends a run. Acceptance criterion 1 asks that a run can be lost to time. |
| N2 | `lb_data.c` Holt's skills | Survival 1 | **Survival 2** | Nobody in the crew has Survival 2, so the **Cook** job never saves food and `camp_hunt_dusk` never fires. Holt is ex-army, so it fits. In the slice sim, hunt_dusk goes from 0% of runs to 24%. |
| N3 | `tac_rules.c` `atk_calc()` flank bonus | +15 to hit and +1 damage whenever the target has no cover from the attacker, including a target standing in the open | Give the bonus only when the target has cover on **another** edge (a real flank). In the open there is no bonus. | Zombies almost never stand in cover, so today every ranged shot gets +15 and +1. June hits 95%, Doc 81%, Holt 95%. With the change: June 84%, Doc 66%, Holt 95%. Attacks then miss the way the owner asked for (`LAST_BOAT.md` 0, "Info"). |
| N4 | `lb_data.c` crossbow | dmg 4 | **dmg 3** | The crossbow is silent (noise 1), free (0 ammo) and kills an aware walker (4 HP) in one shot. It beats every gun. At 3 damage it still kills runners outright. Together with N3, it kills a walker in one shot only with first strike (+1 when the target is unaware). Without N3, the open-ground +1 would still make that one shot. |
| N5 | `tac_rules.c` `tac_end_survivor_phase()` | Only noise fills the Horde meter | **+1 Horde per round** | "The longer and louder you stay, the worse it gets" (8.5). Today silent searching costs no time, so a careful team can loot all 32 tiles on Main Street. With +1 per round, the first wave comes by round 12 on Main Street and round 10 at the bridge, even with no noise. |
| N6 | `lb_data.c` `main_rows` rows 7-8 | Pharmacy and shop doors are on the bottom row and face **south**, away from Main Street | Move the door letter to the top-middle tile: `PpP` / `SsS` on row 7 | Both shops then open north onto the street (checked against the loader's door rule, 4.1). The dormant walker at (13,8) is still inside the shop. |
| N7 | `lb_data.c` `farm_rows` row 3 | `o..#####.........hH..w..X` | `o..#####........hH...w..X` | **Bug.** The farmhouse's two rows are offset by one column (`HH` at x 16-17, `hH` at x 17-18). The loader reads them as two 2x1 buildings, and one of them has no door letter. After the fix it is one 2x2 house at (16,2) with its door facing south. The wreck stays at x 21 and the row stays 25 wide. |
| N8 | `lb_data.c` `bridge_rows` (23,5) (23,6) | `x` (exit zone) on the far bank | `-` (plain road) | For a bridge mission only the hold counts (`tac_check_objective`). Leaving by the east exit never helps, and if everyone leaves that way the mission is lost. The exit misleads the player. |
| N9 | `data/story/phase.tsv` quiet weights | camp 4, road 6, stop 8, mission_end 6, death 0, bite 0 | **no change** | With the new content, a storylet plays at about 89% of camps, 92% of road events, 76% of stops and 64% of mission ends (slice sim). That is the rhythm the design asks for. |

Not changed, and why:
- **Starting supplies** (Normal: food 24, fuel 12, ammo 20, meds 4, parts 4).
  - The fuel is exact for routes a and c and one short for the fast routes b and d (2.1). Running short drops the pickup, or costs a day when only one car is left. That is a real choice, and N1 makes the day matter.
  - With 4 meds, about 1 bite per run, and the storylets that spend meds, meds stay tight. That is good.
- **Wave thresholds** (Main {12,24,36} x3, Bridge {10,20,30} x3, Farm {10,20,30} x2). Look at them again after N5 is in. One rifle shot (noise 10) brings wave 1 at the bridge. That is harsh but fair for the finale.
- **Zombie table**: keep it. The `hearing` column is never read: `tac_noise` uses the noise radius only. Either use it (radius + hearing - 6) or drop the column. That is a rules decision; no number changes.
- **Vehicles, REST_HP 2, RAID_CHANCE 25%, bite_days {+2, 0, -1, -1}**: fine for the slice.

---

## 2. The run: clock and supplies

### 2.1 Routes (run.c `edges[]`; sedan 2 + pickup 2 = 4 fuel per leg)

| Route | Days on the road | Fuel | Missions | Arrives at the bridge on day |
|---|---|---|---|---|
| a. Main Street, Hensley Farm, bridge | 1 + 2 + 2 = 5 | 12 | scavenge, get through, bridge | 6 |
| b. Main Street, County Highway, bridge | 1 + 1 + 1 = 3 | 13 | scavenge, bridge | 4 |
| c. Gas Station, Hensley Farm, bridge | 1 + 1 + 2 = 4 | 12 | get through, bridge | 5 |
| d. Gas Station, County Highway, bridge | 1 + 2 + 1 = 4 | 13 | bridge | 5 |

Missions take no day. These cost a day: rest, bypass, failing the farm (the detour), each retry at the bridge, and pushing a car with dry tanks. The run is lost when `day > ship_day`.

**Spare days with the current clock (Normal 12):** a 6, b 8, c 7, d 7. With N1 (Normal 10): a 4, b 6, c 5, d 5.

A Normal player who takes the longest route, rests once and fails the bridge once still has 2 days left with N1. On Very Hard (8) the longest route leaves 2 spare days. The slice sim (6.4) puts runs lost to time at 0.1-0.7% with the current clock and 1.7-2.4% with N1. Its missions are dice and its players rest little, so the real numbers will be higher. That is the direction we want.

The other option is to charge a day for every mission, as the design's resource table says ("Days: every leg, rest and mission"). That changes the run flow, not just a table, so N1 is the smaller change for the slice.

### 2.2 Supplies

- **Food** (Normal 24, 1 per person per day at full rations):
  - Route a eats 20 on the road alone; Hard (18) and Very Hard (14) must scavenge.
  - Main Street holds about 39 food if every tile is searched. That is why N5 matters: without a clock in the mission, food stops mattering after Main Street.
- **Fuel** (12): see 2.1. Fuel storylets add some variance:
  - gains: tanker +3, abandoned car +2, Rosa's tune-up +2;
  - losses: breakdown -1/-2, going around a herd -2.
- **Morale** (start 6):
  - It has no mechanical effect yet, except through storylets. Content now reads it: someone wants to leave at 4 or less, the music night at 7 or less, the pill scene at 6 or less, the doctor's good day at 7 or more, the nerve recovery at 4 or more.
  - Suggestion for M4: morale 2 or less gives -1 Nerve on panic checks.

---

## 3. People (cast_info)

Hit chances under the current rules (`tac_rules.c`), against a walker:

| Person | HP | Move | Weapon | To hit (open) | Damage | Walker hits them | Bite chance per hit | Turns in |
|---|---|---|---|---|---|---|---|---|
| Mara | 10 | 4 | machete (melee) | 88%, 95% unaware | 4 (+1 unaware) | 35% | 8% | 5 days |
| Doc | 8 | 3 | pistol | 81% (N3: 66%) | 4 (N3: 3) | 40% | 24% | 4 days |
| June | 8 | 4 | crossbow | 95% (N3: 84%) | 5 (N3: 4; N3+N4: 3) | 30% | 24% | 4 days |
| Holt | 10 | 3 | rifle (min range 2) | 95% (N3: 95%) | 6 (N3: 5) | 40% | 8% | 5 days |
| Rosa | 9 | 4 | bat (melee) | 83%, 95% unaware | 3 (+1) | 35% | 21% | 4 days |
| Tess | 8 | 5 | knife (melee) | 95% | 2 (+1) | 25% | 24% | 4 days |

Reading it:
- **Mara** is the quiet killer: she kills a walker in one hit and is hard to bite. **Holt** is the loud one: certain hits, and noise 10.
- **Doc and June** are fragile and bite-prone (no jacket, Grit 2). That is good: the people with the useful skills are the ones at risk.
- **Holt** can't shoot an adjacent zombie (range 2-7) and falls back to fists (1 damage). Keep this: it is a good tactical wrinkle.
- **Survival**:
  - Everyone has 0-1, so Cook ("needs Survival 2") is dead in the slice. See N2.
  - Cooking gives 3 XP a night, so Mara (Survival 1, 10 XP) needs five cook nights to reach 2.
- **Medicine 2** (needed for amputation) is Doc only. If Doc dies, bites become fatal.
  - The story content softens this on purpose. When a Caring person dies, someone "takes the med bag" (`death_epitaph_caring`: Medicine XP +15).
  - Mara starts at Medicine 1 with 10 XP, so that bag takes her to Medicine 2. The story passes Doc's job on to the next person.
- **Traits in play** (start): Mara Brave/Loyal, Doc Weary/Caring, June Calm/Curious, Holt Hothead/Brave.
  - Cowardly, Selfish, Cynic, Addict and Optimist exist in the slice only through storylet arcs (6.2).
  - Reckless and Greedy need Tess or a future character.
- **Relationships at start**: Mara-June +1 and Holt-Doc -1. Keep them. The promise arc needs Mara-June at +1 or more. Holt's shooting lesson skips Doc while they are at odds.

---

## 4. Maps (checked with a script that mirrors `add_building()` and `tac_edge_open()`)

Results for the three maps:
- Every exit tile, every bridge tile and every building tile can be reached from the deploy points.
- No zombie starts on a blocked tile.
- The dormant zombies inside buildings are on purpose: (15,1) and (6,10) are in houses, and (13,8) is in the shop.

### 4.1 Door direction rule (tac_rules.c)
The door tile opens to the first outside neighbour in the order S, E, W, N:
- on the bottom row, a door faces S;
- on a 2-wide building, the top-left tile faces W and the top-right tile faces E;
- only the top-middle tile of a 3-wide building can face N.

So **a 2-wide building can never face north**. Main Street's houses south of the road all turn their backs to the street. N6 fixes the two 3-wide shops with data only. For the houses, a rule change is suggested (7, request R4).

### 4.2 Main Street (scavenge run, 24x16, day)
- **Layout**:
  - Deploy points and the west exit sit together on the road's west end.
  - Two awake walkers stand at mid-road, so the first steps already pose the stealth-or-guns question.
  - The house next to the deploy zone has no zombie: a safe first search, which works as a tutorial. Keep it.
  - The Brute (20,11) and the runner (22,2) are dormant at the far end. The far end is where a loud fight goes wrong.
- **Doors**:
  - The pharmacy (the meds) and the shop face away from the street. Fix: N6.
  - The houses at (19,7), (5,10) and (15,11) face south. That is acceptable, but it would be better with R4.
- **Size**: 32 searchable tiles is a lot for one mission. Keep the count and add the clock (N5).

### 4.3 Miller's Bridge (take the bridge, 24x12, day)
- **Layout**:
  - The river (columns 10-12) is impassable. The bridge is row 5, three tiles long, so a single survivor holds it and the others guard both ends.
  - Two awake walkers stand on the bridge, so the first fight happens at the choke point.
  - The fences at (7,4) and (7,6) give cover to shoot from on the near bank.
- **Waves**: they spawn on map edges, at least 4 tiles from the survivors and never on deploy tiles, so they come from both banks. That matches the design.
- **Loot**: the garage and house on the near bank have no zombies. They are free loot, useful only after a failed attempt. Fine.
- **Exits**: the east exit is a false promise (N8).

### 4.4 Hensley Farm (get through, 25x10, night, 3 of up to 4 must reach the east edge)
- **Bug**: the misaligned farmhouse (N7).
- **Lanes**: the fence L at x 3 (rows 3-5) and row 3 (x 3-7) splits the start into a north lane past the garage and a south lane past the farmhouse. Good.
  - The woods at x 13-14 are a mid-map hiding spot.
  - The awake walker at (21,6) stands in front of the exits, and a dormant runner waits in the SE woods. Getting three people out quietly is the puzzle; one shot wakes the farm.
- **Rule**: with "need 3" and 4 deployed, losing two people loses the mission. Keep it: it is the slice's hardest test.

---

## 5. Rules notes (no number changes)

- **Zombie attack noise**: each zombie attack makes noise 2, hit or miss, so long melee brawls fill the Horde meter too. Keep it: it is the right pressure against "Mara solves everything".
- **Infection from storylets**: `infect` and `cure` from storylets bypass armour and Grit. The new content cures only through costly choices:
  - `bite_doc_gamble`: 3 meds, -2 HP, a 30% chance;
  - the existing `bite_confession`: Grit 4, -3 HP, a 50% chance.

  Amputation stays the reliable cure, through the camp **Tend** job.
- **Storylet deaths** (`hp_add -99` in the mercy choices):
  - The engine plays no death scene for them and charges no morale, so the content charges -2 morale itself, matching `die()` in run.c.
  - They are written to the Chronicle as LOG_DEATH arg 4 (request R2).

---

## 6. Story content: designer set 1 (data/story)

### 6.1 What was added
**62 storylets**, appended after the engineer's 14 (76 in total; row order unchanged), plus **5 vars**. One text line of the example set changed (`death_grief_bonded`, see 6.3).

| Phase | New | Storylets |
|---|---|---|
| camp | 20 | food_theft, fever, wants_to_leave, doc_tired, watch_together, holt_lesson, mara_promise, counting_days, nightmare, june_music, cynic_needle, reckless_dare, brave_watch, doc_pills, meds_missing, dog_hunger, nerve_back, dog_warns, rosa_believe, tess_stash |
| road | 12 | breakdown, body_backpack, horde_crossing, bandit_toll, bandit_revenge, stray_dog, hitchhiker, abandoned_car, radio_static, radio_back, stranger_bait, rosa_engine |
| stop | 7 | church_board, garden, army_checkpoint, school, tanker, greedy_jeweler, tess_scout |
| mission_end | 6 | won, lost_blame, lost_shoulder, ammo_low, doc_laughs, froze |
| death | 9 | epitaph_brave, epitaph_caring, epitaph_hothead, grave_generic, holt_words, june_names, selfish_mouth, loyal_keepsake, doc_failed |
| bite | 8 | hide, put_me_down, doc_gamble, amputation_talk, hothead_denial, holt_orders, vow_mara, vow_june |

| Var | Values | Set by | Read by |
|---|---|---|---|
| `mission_won` | 0 none yet, 1 won, 2 lost | **run.c, after each mission (request R1)** | mission_end_won / _lost_blame / _lost_shoulder |
| `dog` | 0 not met, 1 with us, 2 gone | road_stray_dog, camp_dog_hunger | camp_dog_hunger, camp_dog_warns |
| `bandits` | 0 not met, 1 paid or talked past, 2 shot through | road_bandit_toll | road_bandit_toll, road_bandit_revenge |
| `promise` | 0 none, 1 mercy, 2 a cure | camp_mara_promise | bite_vow_mara, bite_vow_june |
| `radio_lost` | 0 fine, 1 silent, 2 back | road_radio_static, road_radio_back | the same two |

The compiler warns that `mission_won` is "read but no effect ever changes it". That is expected, because run.c writes it. Until R1 is in, the three storylets that read it never fire.

Effects stay modest:
- resources ±1..3;
- morale ±1..2 (-2 for a mercy kill or for eating the dog);
- rel ±1;
- HP losses are rare and always guarded by an HP condition, so they can't kill by accident;
- the only deliberate deaths are the mercy choices.

### 6.2 Arcs (how the slice crew grows new traits)
The starting four have only Brave, Loyal, Weary, Caring, Calm, Curious and Hothead. These arcs bring in the rest:

| Arc | Trigger | Result | Pays off in |
|---|---|---|---|
| Hunger to Selfish | `camp_food_theft`, "Let it go" | 25%: the thief +Selfish | road_stranger_rob, death_selfish_mouth |
| Static to Cynic | `road_radio_static` (June) | 30%: June +Cynic (in place of Curious) | camp_cynic_needle |
| Pills to Addict | `camp_doc_pills`, "Look the other way" | Doc: Weary out, Addict in | camp_meds_missing |
| Good days to Optimist | `mission_end_doc_laughs` (morale 7+, day 4+) | Doc: Weary out, Optimist in | camp_thin_stew choice 3 |
| Near death to Cowardly | `mission_end_froze` (Brave, Nerve 3 or less, HP under 25%) | Brave out, Cowardly in | camp_nerve_back (Cowardly out, Brave in) |
| Grief and mercy to Haunted | grief, mercy kills, the hitchhiker, the checkpoint logbook | +Haunted | camp_nightmare (30% chance to lose Haunted) |
| Hothead to Calm | existing `camp_holt_cools_off` | | |
| The promise | `camp_mara_promise` (mercy or a cure) | `promise` = 1 or 2 | bite_vow_mara / bite_vow_june (priority 1: the vow always comes due) |
| Bandits | `road_bandit_toll`, "Shoot our way through" | `bandits` = 2 | road_bandit_revenge (priority 1, once) |
| The dog | `road_stray_dog` | `dog` = 1 | camp_dog_warns, camp_dog_hunger (share, release, or eat it) |

Every precise trait swap uses `trait_remove` and then `trait_add` at 100%, so a chance roll can never leave half a swap. The single `trait_add` effects with a chance (Selfish, Cynic, Haunted) replace the person's second trait on purpose.

### 6.3 Consistency rules the content follows
- **Nobody is buried.** People killed by zombies rise (`tac_kill`), and the rest are left on the road. Death scenes raise markers and cairns, or pile stones; they never mention a body. The example line in `death_grief_bonded` ("They bury...") was changed to a cairn to match.
- **Amputation is done at camp**, by the **Tend** job in `treat()`. `bite_amputation_talk` only sets it up ("Tonight, then") and never cures by itself.
- **Chance effects get hedged text.** The reply never says something happened when a roll decides it; the banner reports the outcome.
- **Rosa and Tess** speak in the no-portrait layout. Their 4 storylets fire once they can join (request R5).
- **Text**: every line passes the compiler's fit check at worst-case name lengths. After a pronoun, verbs take the agreement helpers (`{A.He} walk{A.s}`), and the only contractions used are ones that suit he, she and they alike (`{A.He}'d`). Every line was also read back with "they" for unfiltered roles.

### 6.4 Measured
- `python3 -I tools/storyc.py data/story <out>`: 0 errors; 1 expected warning (`mission_won`, see 6.1).
- `--selftest`: OK.
- `story_data.c` built with the GBA flags: 0 warnings, 36 KB of ROM data.
- **Runtime fuzz** (`test/story/story_fuzz.c` with this data, ASan + UBSan, 2000 runs, seeds 1-12):
  - 0 failures in about 62 M checks per seed;
  - every storylet fires and every choice is picked;
  - the rarest storylet still fires at least 8 times per seed.
- **Slice sim** (a scratch tool, not in the repo; request R7):
  - Setup: 1000 runs with the real crew and their traits, relationships and supplies, on the real route graph and clock. Missions are dice, so this measures the story content, not balance.
  - **88-89% of the 76 storylets fire in at least one run** with N2 (86-87% without), over five seeds. Criterion 4 asks for 80%.
  - The most frequent camp storylet is `camp_holt_temper`, at 14-17% of camps. The limit is 50%.
  - What never fires in the slice:
    - Rosa and Tess (4 storylets) and `camp_new_face`: nobody can join yet;
    - Reckless and Greedy content: nobody in the crew has those traits;
    - without N2, `camp_hunt_dusk`;
    - sometimes the deep Selfish and Addict payoffs, which are under 1% of runs.

---

## 7. Requests to other roles

| ID | Owner | Request |
|---|---|---|
| R1 | run.c | In `mission()`, after `tac_run()` returns: `run.vars[SV_MISSION_WON] = r == 1 ? 1 : 2;` (add `#include "story_data.h"`). Do it before the death, bite and mission_end scenes. |
| R2 | run.c | Add `DC_STORY` (= 4) to the DC_ enum and show "%s died" for it in `log_line()`. This repeats the story engineer's request; mercy kills use it. |
| R3 | run.c | In `story()`, after `story_fire()`: for each person alive before and dead after, call `story_fire(SP_DEATH, i)` **without** the -2 morale; the storylet already charged it. Skip this when the call was itself a death scene. Mercy kills then get an epitaph. |
| R4 | tac_rules.c | Door direction: prefer the side that faces out of the footprint (top row N, bottom row S, left column W, right column E), then the current order. Or prefer the side that faces a road tile. Then 2-wide houses south of Main Street can face the street. |
| R5 | run.c / story engine | A way for Rosa and Tess (and unnamed survivors) to join: a `join` effect or a recruit at a stop. Five storylets wait for it. |
| R6 | integrator | N1-N8 above. N6-N8 are data-only edits in `lb_data.c`. N2 and N4 are one number each. N3 and N5 are small rule changes in `tac_rules.c`. |
| R7 | QA / story engineer | Optional: keep a slice-run reachability sim next to `story_fuzz.c` (it `#include`s `story_fuzz.c` with `main` renamed, builds the real crew and walks the route). The designer has the source. |
| R8 | story engineer | Optional: a way to mark a var as "set by the game" (for example `set_by=game` in var.tsv) to silence the `mission_won` warning. Also optional: two builtins, `deployed` (the person went on the last mission) and `node` (where we are), so mission_end and stop scenes can name places and stop assuming who was there. `docs/tech/STORY.md` section 11 still says "14 storylets"; point it here. |
