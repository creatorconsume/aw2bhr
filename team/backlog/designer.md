# Backlog: Game designer (`claude/dw-design`)

Seeded by the producer on 2026-10-18 from the repo state at integration `b348364`. From now on only the
designer edits this file. Reorder freely and tick items off with the commit hash.

P0 = do first, or it blocks someone. P1 = next. P2 = later.
**Until `team/status/producer.md` says "hardening merged", do not edit `missions.c`, `data.c` or `test/sim/`.** The
producer is still tuning M1-M5 with the simulator. Design docs are fine.

## P0

- [ ] **`deadwars/docs/design/PART_TWO.md`: missions 6-10.** Pick up from the M5 outro ("Tomorrow we take the fight
  to him"). Write one page per mission: title, goal type, map size (max 30×20) with a sketch, starting units and funds,
  `night_every`/`spawn_every`/`day_wave`, the new twist, story beats, the intended winning plan, the expected ways to
  lose, and the mechanics, art and music it needs. Introduce one new twist per mission. M10 is the Patient Zero fight.
  Today's goal types are `GOAL_ROUT`, `GOAL_SURVIVE`, `GOAL_EVAC` and `GOAL_NESTS`. Anything else is an engineer request.
- [ ] **`deadwars/docs/design/BOSS_PATIENT_ZERO.md`.** Cover:
  - stats and HP model (phases, damage cap or regeneration), move and range, and what can hurt him (`base_damage` row/column)
  - abilities: calls the horde at night (wakes every dormant unit), spawns Walkers next to him every N horde turns, and
    maybe a fear or infection aura
  - counterplay: burning nests or holding radio towers weakens him
  - AI behaviour, win condition (`GOAL_BOSS`), and dialogue at HP thresholds. The `PT_ZERO` portrait exists.
  - art: a map sprite, maybe a second portrait pose. Audio: a boss theme and a roar or call SFX.
- [ ] **`deadwars/docs/design/DIFFICULTY.md`.** The owner wants VERY HARD but fair. Write it as testable rules:
  - no unwinnable starts, threats visible before they hit (ambush only in night fog), no single coin flip that decides a mission
  - target win rates for the sim bot per mission
  - whether there are difficulty modes, and which knobs they turn: `spawn_every`, `wave_size`, `funds`/`income_scale`,
    the night +15% and +1 move
- [ ] **Requests out.**
  - `team/inbox/engineer/`: events, boss unit, `GOAL_BOSS`, more than 9 missions, any escort/defend goal, difficulty switch.
  - `team/inbox/artist/`: boss sprite, new terrain, new portrait.
  - `team/inbox/audio/`: boss theme, briefing theme, per-mission music moments.
  - Link each request to the section of your doc it comes from.

## P1 (after "hardening merged")

- [ ] **Retune M1-M5 with `test/sim`.** Record the before and after win rates, timeouts and causes of loss in
  `deadwars/docs/design/BALANCE_LOG.md`. `check_maps.py` must stay clean. The producer will hand it to you; it needs
  `m\d+` for M10.
- [ ] **Build M6-M10 in `missions.c`** as the engine features land: maps, placements, intro and outro. Sim-run each one
  and ask QA for a playtest script per mission.
- [ ] **Numbers review in `data.c`.** Document the rationale in `deadwars/docs/design/UNITS.md`.
  - prices against power
  - Brute vs vehicles (90-95% vs foot units)
  - Bloater burst (fixed 2 HP, in `battle.c`)
  - the Sniper's 2-3 range with no move-and-fire
  - Medic +1 HP per turn and building heals (+2, Hospital +3)
  - zombie and terrain defence (Rubble 4, River 0)
- [ ] **Data-driven tables.** The Search outcomes (55% supplies, 25% survivor, 20% ambush) and the nest spawn mix (Runner,
  Bloater and Spitter rolls, gated by day) are hard-coded in `battle.c` and `ai.c`. If you want to tune them per mission,
  ask the engineer to move them into `data.c`/`Mission`.
- [ ] **Dialogue lint.** Lines must fit 23 columns × 5 rows when a portrait shows, or 28 × 5 without one. The
  `m4_text_overflow` playtest found an overflow. Add a host check under `test/sim/` and run it on every text change.

## P2

- [ ] Par days and ranks per mission, shown in mission select (engine request).
- [ ] New How to Play pages for the Part Two mechanics. The text lives in `main.c` (engine); send the text as a request.
- [ ] "Hard remix" variants of M1-M5 for a post-game mode.
