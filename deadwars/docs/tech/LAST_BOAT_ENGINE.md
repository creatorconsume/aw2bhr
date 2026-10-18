# The Last Boat: engine guide (demo build)

How the Act 1 slice of *Dead Wars: The Last Boat* is put together: which file does what, how data
moves between them, how to run the demo and the tests, and what the demo does and doesn't have yet.
The design is `docs/design/LAST_BOAT.md`; the number changes the designer proposed (and which ones
are in) are in `docs/design/SLICE_NUMBERS.md`; storylet authoring is `docs/tech/STORY.md`.

The classic AW2-style campaign is still in the same ROM, unchanged: The Last Boat is the last item of
the title menu ("Campaign / Continue / How to Play / Options / The Last Boat"). One fix there: a
classic suspend left by an older build (not `DWS2`) is dropped at boot (`save.c` `profile_load()`),
so "Continue" is never a dead default.

## 1. Module map

| File | Role | Owns |
|---|---|---|
| `src/lb.h` | the contract between the modules: people, the run state, the tactical state and every function below | |
| `src/lb_data.c` | rules data (weapons, armour, cars, the cast, zombie stats, trait and skill names) and the 3 slice maps with their legend | `tac_missions[]` |
| `src/person.c` | person helpers: move points, names, skills from use, seats, the Chronicle log | |
| `src/run.c` | the run: title menu, new run, journey map, travel and the clock, stops, deploy, camp jobs, caravan, journal, final screens | `RunState run`, `run_phase` |
| `src/runsave.c` | the ironman save: two SRAM slots (A 0x2000, B 0x4000), written in turn, CRC-checked | |
| `src/story.c`, `src/story.h` | the storylet runtime: pick, bind, play, effects | |
| `tools/storyc.py` | compiles and integrity-checks `data/story/*.tsv` into `build/gen/story_data.{c,h}` | |
| `src/tac_rules.c` | the tactical rules, no graphics (host-testable): map loading, buildings and doors, moves, cover, line of sight, hit chance, attacks, infection, noise and the Horde meter, waves, searching, barricades, extraction, objectives | `TacState tac` |
| `src/tac.c` | the mission flow: survivor phase, cursor and menus, moving with undo, actions, info cards, objective and results screens, the `tac_fx_*` feedback hooks | `bool tac_mode` |
| `src/tac_ai.c` | the horde turn: waking, hunting by sight, noise goals, flow fields, bashing barricaded doors | |
| `src/tac_gfx.c` | map graphics: roofs, door/cache/exit/status icons, the fog of war (line of sight), the HUD | |
| `src/lbart.h`, `tools/lbart.py`, `art/lb.txt` | generated art: people sprites, roofs, icons, the journey backdrop (`build/gen/lbart.c`) | |
| `src/sound.c` | music layers follow the Horde level (`sound_horde_level`) | |

Engine hooks in the shared code (all keyed on `tac_mode`): `map_redraw_tile()` asks `tac_redraw_tile()`
(roofs), `build_oam()` asks `tac_unit_tile()` (people sprites) and calls `tac_oam()` (icons), units with
`inside` set aren't drawn, people show raw HP, the battle scene is off, and the music director picks
DAY / NIGHT / HORDE / LASTSTAND from the Horde level.

Memory (this build): ROM 255 KB; IWRAM .data + .bss about 10 KB of 32 KB; EWRAM .bss 55 KB of 256 KB
(`gs`, `run`, `tac`, path fields, story buffers). Big arrays are `EWRAM_BSS`. No division by a
non-power-of-two in per-tile or per-unit loops.

## 2. Data flow

```
title menu -> lb_main()                                   run.c
  New Run: run_new(difficulty) -> story_init_run() -> intro -> run_save()
  run_loop: by run_phase
    PH_ROAD   journey(): pick a road -> travel(): fuel, seats, day_pass() x days, one SP_ROAD
              -> arrive(): LOG_ARRIVE, save (PH_STOP|PH_ARRIVE), SP_STOP (+ SP_ROAD at the gas
                 station), save (PH_STOP)
    PH_STOP   stop(): the mission / Bypass (get through) or Make camp (scavenge, no mission) / Rest,
              or the caravan, journal, save & quit
              mission(): deploy_screen() -> PF_DEPLOYED on who goes -> SP_MISSION_START (who can
                still go: PF_DEPLOYED again) -> gs.rng from the run's seed
                -> tac_run(mission, roster indices, n)                       tac.c
                     tac_load() (rules), the camera snapped to the first person, graphics,
                     the survivor phase / tac_end_survivor_phase() / tac_horde_turn() loop
                     until tac.result, the results screen;
                     adds tac.loot to run.res
                -> vars[SV_MISSION_WON], LOG_KILLS, LOG_MISSION_WON/LOST, morale
                -> save (camp phase|PH_AFTER, PF_DEATH_SCENE / PF_BITE_SCENE on the people)
                -> SP_DEATH per death, SP_BITE per new bite (a save after each), SP_MISSION_END
                -> save (the camp phase)
    PH_CAMP / PH_CAMP_STAY  camp(): the job board, guards, cooks, tending (amputate / antivirals),
              foraging, resting, the raid roll, the night summary, SP_DEATH, SP_CAMP, dawn, save
  A run cut off by a power cut resumes at its last save: PH_ARRIVE plays the arrival scenes again,
  PH_AFTER the mission's scenes not yet played, a camp the whole night (same dice: run.rng is saved).
  until the bridge is crossed (run.over 1), everyone is gone or the boat sails (run.over 2):
  final_screen() (memorial + Chronicle), run_erase()
```

Who writes what into `run.roster[]`:
- **tac_rules.c** (during a mission): `hp` on extraction (1 if carried out downed), `alive = 0` and
  `hp = 0` on death (with `LOG_DEATH` arg 0 bled out / 1 killed by a zombie), `PF_INFECTED` and
  `turn_in` (person.c `person_bitten()`: 3 + grit/2 days, then by difficulty) and `LOG_BITTEN` on a
  bite, `kills`, `xp`, skill XP (`skill_use`).
- **tac.c**: Help (a downed person back up at 1 HP, Medicine XP to the helper).
- **run.c**: `PF_DEPLOYED` on exactly who goes in (storylets: `deployed`); after the mission:
  `LOG_KILLS`, morale; between missions: meals, hunger and starvation, turning, camp jobs,
  amputation, people left without a seat.
- **story.c**: storylet effects on bound living people (HP, traits, infection with a bite's timer
  via `person_bitten()`, skills, bonds); a storylet death is `LOG_DEATH` arg 4 and gets its own
  `SP_DEATH` scene from run.c's `story()`.

Inside a mission `gs.units[i]` and `tac.actor[i]` are parallel. People are spawned first, in deploy
order, and their slots are never reused (zombies only take slots with `actor.person == 0xFF`), so
`tac.actor[]` keeps every deployed person's record after they leave the map. `actor.hp` is the truth,
mirrored in `gs.units[i].hp`.

Turn bookkeeping: tac.c clears `noisy`, `moved`, `acted` at the start of the survivor phase and calls
`tac_end_survivor_phase()` once at its end (bleeding out, the bridge hold, the turn count, +1 Horde,
the objective). tac_ai.c sets `unseen` and clears `moved` at the start of the horde turn. Waves and
risen dead arrive with `moved = 1` and act from the next horde turn. `tac.result` is checked after
every action and every zombie action.

### Rules decisions made at integration (SLICE_NUMBERS.md N1-N8, all applied)
- Boat sails Day 12 / 10 / 9 / 8 (Easy..Very Hard).
- Holt has Survival 2 (Cook works in the slice); the crossbow does 3 damage.
- **Flanked** (+15 to hit, +1 damage) only when the target has cover on some edge but none toward
  the attacker, point blank too (cover itself is ignored point blank). A target in the open is just
  "In the open". The forecast says which.
- The **Horde meter** grows by +1 every round, and every fill is scaled by difficulty (x0.75, x1,
  x1.25, x1.5). The quarter points carry over to the next fill (`tac.horde_q`), so the round tick and
  quiet melee scale too: a quiet team meets Main Street's first wave in round 16 / 12 / 10 / 8.
- A **barricaded door** blocks melee both ways until it breaks; guns still fire through it.
- **Waves** spawn on map edges, 5+ tiles from everyone, never on deploy or exit tiles. On a Get
  through map they come from behind (no nearer the exits than the person nearest them) while
  there is room there, else from any edge.
- Doors face out by their place in the footprint (bottom row S, top row N, then the sides). Main
  Street's pharmacy and shop doors now open onto the street; the Hensley farmhouse is one 2x2 house;
  the bridge map has no east exit (the bridge is held, not left).
- A zombie that loses its quarry clears `alerted` and counts as unaware again (first strike +25/+1
  against it): it isn't hunting anyone. Reaching a noise means standing on it or next to it across an
  open edge (not a wall); a zombie's own bite or door bash doesn't overwrite its goal.
- A zombie picks the seen person it can bite now first; one behind a wall next to it counts as 3
  tiles away (it has to go round).
- Cover inside a building: every side is the wall (full) except the door edge (half), as the HUD and
  the R card show.
- **Meals**: short of full meals everyone eats half (morale -1, nobody loses HP); short of that each
  food feeds two and the rest go without (-1 HP, death at 1 HP), the healthiest first.
- **Stop prices**: Bypass (get through) is +1 day, -2 fuel; failing the farm takes the same detour
  (+1 day and up to 2 fuel at dawn), so trying never costs less than going around. A scavenge stop
  offers Make camp (free) instead of Bypass, and a scavenge run is won only if loot comes out (an
  instant retreat is a loss, not +1 morale).

## 3. Running the demo

```sh
make -C deadwars                       # deadwars.gba (zero warnings)
deadwars/test/demo/run.sh              # plays test/demo/lb_demo.txt on mGBA, checks it
deadwars/test/demo/run.sh -n -g        # no rebuild; also on gpSP (tools/emu-lab) and compare
```

`run.sh` writes `deadwars/build/demo/mgba/NN_name.png` (2x) for every shot. The route: title ->
The Last Boat -> New Run (Normal) -> intro -> journey -> road to Main Street (a road storylet) ->
arrival and a stop storylet with a choice -> Scavenge -> deploy 4 -> six turns on Main Street (moving,
the path arrow, the action menu, searching the house, the person and tile cards, the START menu and
the Objective screen, horde turns, Holt's rifle kill that brings wave 1, June's crossbow, Holt's
second kill and wave 2, falling back to the caravan, leaving by the exit) -> results -> the
mission-end storylet -> camp jobs (Guard, Tend) -> the night summary -> a camp storylet with a choice
-> the road to Hensley Farm (2 days) -> its stop storylet -> the caravan, a person sheet, the journal
-> the farm's night mission: deploy, briefing, the first turn and a night horde turn.

`check.py` fails the run if any text glyph sits on a window frame tile (text running out of its box)
and checks the end state (at the farm, at night, all four alive). With `-g` the same script plays on
gpSP's Onion core; its first wait is 57 instead of 60 because gpSP's frame counter runs 3 frames ahead
at the same input and the run's seed comes from it. Both cores must then end in byte-identical
`RunState` and `GameState`.

The run is deterministic: the seed is `frame_count` when New Run is picked, and everything after it
(storylets, the mission's dice, the AI) follows that seed. If the storylet data, the rng order or the
frames before New Run change, the storylets that play change too: re-read the shots and adjust the
presses in `lb_demo.txt` (its header says how).

## 4. Tests and tools

| Command | What |
|---|---|
| `deadwars/test/all.sh` | everything below in one go (about a minute) |
| `make -C deadwars clean && make -C deadwars` | must print no warning |
| `make -C deadwars PERF=1` | the perf build (the classic stress map) |
| `make -C deadwars/test/unit run` | host tests of `tac_rules.c` against hand-made maps and the 3 slice maps (`test_tac.c`), of the horde turn (`tac_ai.c`) and the mission flow (`tac.c`: undo, people notices, the start camera; `test_flow.c`), and of the run layer (`run.c`, `runsave.c`: meals, power cuts at every byte of a save or an erase, scenes replayed after a cut, stop prices, who was deployed; `test_run.c`), and of the classic battery save (`save.c`: an old build's suspend; `test_save.c`), ASan + UBSan (`SAN=` off) |
| `python3 -I deadwars/tools/storyc.py --selftest` | the storylet compiler's fixtures |
| `make -C deadwars/test/story run` | selftest + the runtime fuzzers over `data/story` and `test/story/edge` |
| `make -C deadwars/test/sim` | the classic balance simulator still builds |
| `deadwars/test/smoke.sh` | boot and classic mission 1 |
| `deadwars/test/playtest/run.sh` | classic regressions on mGBA and gpSP (needs `tools/emu-lab`) |
| `deadwars/test/demo/run.sh -g` | the demo on both cores |
| `python3 -I deadwars/tools/lbart.py deadwars/art/lb.txt OUT` | regenerate the art (the Makefile does it) |

Frame budget: on gpSP the demo's horde turns lose at most a few frames over several hundred (a
one-frame hitch when the fog is recomputed or a field is built); screen loads (deploy, results)
drop up to 6 frames behind a blank screen. Measure with lrun's `count frame_count` after each shot.

## 5. In the demo, and not yet

**In:** the six-stop Act 1 route with days, fuel, food and the boat's deadline; Scavenge, Bypass,
Rest and Make camp at stops; three missions (Main Street scavenge by day, Hensley Farm get-through at
night, Miller's Bridge hold); move + 1 action, undo until fog is revealed or a roof lifts; hit chance with cover,
flanking, range, night, height and first strike; noise, hidden status, the Horde meter with three
waves; 2x2+ buildings with roofs, doors, barricades and searching; car trunks and dropped packs;
downed people, Help, bleeding out, bites and infection, the dead rising; retreat to the caravan and
exits; camp jobs (Rest, Cook, Tend with amputate or antivirals, Forage, Guard) and night raids; 76
storylets with choices and the Chronicle; the caravan, person sheets and the journal; difficulty
(supplies, the clock, the Horde meter, bite timers); the A/B ironman save; music layers on the Horde
meter.

**Not yet** (design v1 items still missing): panic overrides (Flee/Freeze/Berserk) and Nerve checks;
special abilities, wounds from big hits, riot gear's melee cover, bonded-ally bonuses, trait quirks in
missions; wading (water is impassable); Spitter and Bloater behaviour (they act as Walkers); roof
portrait badges and the who's-inside list; cover edge bars on the map and the noise radius ring;
Rosa and Tess can't join yet (their storylets can't fire); vehicle condition, cargo limits, Pace,
Rain; a mission suspend (a power cut mid-mission rewinds to the stop's last save, after its arrival scenes) and a travel save (a cut on
the road rewinds to the last stop or camp, and the road can be picked again); the 1v1 battle scene
for The Last Boat; per-character death stings and the camp/road themes.
