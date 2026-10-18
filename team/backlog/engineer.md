# Backlog: Gameplay engineer (`claude/dw-engine`)

Seeded by the producer on 2026-10-18 from the repo state at integration `b348364`, plus the unmerged
`claude/onion-lab` (5405c85, map animation) and `claude/deadwars-dev` (30bbfa5, audio). From now on only
the engineer edits this file. Reorder freely and tick items off with the commit hash.

P0 = do first, or it blocks someone. P1 = next. P2 = later.
**Until `team/status/producer.md` says "hardening merged", do not edit existing `deadwars/src/*.c`/`*.h`.**
New files, measurements and design notes are fine. The general engine bugs from the playtests (HUD overflow,
stale `keys_down`, undo losing capture progress, bus drop cursor, text overflow) belong to the hardening
pass. Don't redo them.

## P0

- [ ] **Technical design for Part Two** (brief, first task 1). Propose in `team/inbox/designer/` (copy to producer):
  - `src/events.c`: data-driven mission events. Triggers: day N (start of the survivors' or horde turn), a unit
    entering a rect, a unit's HP below X, kill count. Actions: dialogue, spawn a group at tiles, wake all, reveal
    an area, change the goal or the night cadence. Store fired flags in `GameState` so Suspend keeps them.
  - Boss: `UT_ZERO` appended after `UT_BLOATER`, so existing indices stay stable. Also `GOAL_BOSS` (kill a tagged
    unit) and an HP-threshold hook for dialogue and phases.
  - More than 9 missions: `mission_select()` in `main.c` prints `'1' + i`, so mission 10 shows `:`. Also
    `Profile.best_days[16]` and the `unlocked` logic.
  - Escort or VIP goal ("unit X must survive") and defend-a-tile goal, if the designer asks for them.
- [ ] **gpSP performance review** (measure only until hardening is merged). Use QA's `tools/emu-lab` `lrun` with the
  gpSP Onion core (rev 6250979) and `count frame_count`. Measure: an M5 night horde turn, the Sniper/Spitter
  range view, map scrolling and dialogue typing. Suspects found by reading the code:
  - `unit_at()` scans all 96 units. `find_paths()` calls it for every neighbour (≈600 tiles × 4 × 96), and
    `act()` calls it per tile. Fix: an occupancy grid kept up to date in `spawn_unit`, moves and `kill_unit`.
  - `show_ranges()` builds the attack overlay with an O((W·H)²) loop over `in_range`. Fix: dilate the reachable set by
    min..max range.
  - `act()` scores every reachable tile × 96 targets with `damage_estimate`. Fix: pre-filter targets within move+range.
  - `copy16`/`fill16` are CPU loops from ROM. Use DMA3 or `CpuFastSet` for VRAM/OAM, and IWRAM ARM code for hot loops.
  - Done when the before/after frame counts per scenario are in `team/status/engineer.md`, and no animation drops
    frames on gpSP-onion.
- [ ] **Damage forecast with counter** (new file `src/forecast.c` now, hook it into `pick_target()` after hardening).
  The window currently shows only `Dmg %`. Add: expected damage, the defender's counter after taking that damage
  (only if adjacent, direct and able to hurt the attacker; mirror `attack()`), "kills" when lethal, and the
  defender's terrain stars. Night +15% and the 0..9 luck should show as a range, for example `45-54%`.
- [ ] **QA hooks** (ask QA what they need): `make DEBUG=1` unlocks every mission. The m3/m4 playtests need a scratch
  edit of `save.c` today. Also add an optional fixed RNG seed: `map_load()` seeds from `frame_count`, so
  playtests can drift.

## P1

- [ ] **Unit and terrain info screen.** Press R on a unit to show its name, HP, move and move type, vision, range,
  cost, flags and its `unit_info[].desc` text. `desc` exists in `data.c` but nothing displays it. Press R on an empty
  tile to show the terrain name, stars, move costs per type and income.
- [ ] **Path arrows.** While the player picks a destination, draw an AW-style arrow from the unit to the cursor along
  the cursor's trail, falling back to `build_path()` when the trail is too long. This needs arrow overlay tiles from the
  artist. Request them as additive symbols (new `OV_ARROW_*` after the current set, or a separate `arrow_meta`) so
  `ASSET_SPEC.md` stays backward compatible.
- [ ] **Move animation polish, on top of 5405c85** (QA branch, being merged: lockstep idle, facing, hit shake,
  explosion). Still to do: holding B fast-forwards every horde animation, including the explosion and banners. Show a
  popup for counter damage. Don't animate fogged horde moves. Make the camera stop jittering during horde moves.
- [ ] **Difficulty modes.** Add `Profile.difficulty` and `gs.difficulty`, with a hook such as `diff_pct(knob)` for spawn
  cadence, wave size, funds and night boost. The designer sets the numbers in `DIFFICULTY.md`. The owner wants VERY
  HARD as the default.
- [ ] **Leader (CO-style) powers.** Only if the designer specs them: a power meter that charges on damage dealt and
  taken, one power per leader, kept in `GameState` (and so in Suspend).
- [ ] **Suspend and save robustness.** `Profile` and `GameState` carry only a magic number. Add a version, size and
  CRC16 to both, and reject a suspend written by an older build with a different layout. Keep the profile in two slots
  (A/B) so a power-off during a write can't wipe progress. Keep the audio fields (`snd_flags`/`snd_magic`) compatible.
  Decide with the producer whether `suspend_clear()` on Continue should stay, because a crash after Continue loses the
  suspend. Test against Onion auto-states with QA.
- [ ] **Title and credits flow.** Add a credits screen after the M5 and later the M10 outro, with music cues from
  audio, and make "Press START" blink. If boot to title, the title menu order or "Campaign" changes, tell QA, because
  `deadwars/test/smoke.sh` scripts that path.
- [ ] **End Day confirmation** when ready units remain, and a counter of units left to move.

## P2

- [ ] Fix the `-Warray-bounds` warning in `text.c` `dialogue()` (the `text_put` of `"v"`/`" "` gets inlined). It
  showed up in a clean build of integration + `claude/onion-lab`.
- [ ] Remember the cursor position per unit and per turn. Have SELECT/L skip units that can't act.
- [ ] A minimap or unit-count overlay for 30×20 maps.
- [ ] Redraw terrain tiles only during VBlank, for `map_redraw_tile` bursts in `fog_update` and night changes.
- [ ] Mission select: show the objective, best days and a rank once the designer defines par days.
