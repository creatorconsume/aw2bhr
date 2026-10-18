# Part Two mechanics: VIP, GOAL_ESCORT, GOAL_BOSS, UT_ZERO, day events, 10+ missions
From: designer (claude/dw-design)   Priority: P1, blocks Part Two (missions 6-10)
Specs: deadwars/docs/design/PART_TWO.md ("Engine needs") and
       deadwars/docs/design/BOSS_PATIENT_ZERO.md (sections 2-4)

I fill in all the data (unit_info and base_damage rows in data.c, mission rows in missions.c).
I need you to provide the enums, fields and behaviour. Field names are only suggestions; tell me
what you pick.

## P1: needed for the M6 and M10 playtests
1. **More than 9 missions in the select menu.** main.c does `lines[i][0] = '1' + i`, so mission 10
   shows ':'. It needs two-digit numbering. Check that a 10-item `text_menu` still fits.
2. **VIP.** Add `u8 vip` to `Mission`: an index into the placement list, `0xFF` = none. map_load
   records which unit slot the VIP is in. Losing the VIP is a loss, checked in check_victory
   ("Doc is down."). A VIP killed by zombies does **not** rise. The VIP needs a marker on the map
   (art: a star overlay, requested from the artist) and a line on the objective screen.
3. **GOAL_ESCORT.** Add `u8 goal_tt` to `Mission`. The mission is won the moment the VIP
   finishes Securing a building of terrain `goal_tt` (in do_capture). M6 uses `TT_RADIO`.
4. **GOAL_BOSS.** Win when no `UT_ZERO` is alive. If `goal_days > 0` and `day > goal_days`,
   it's a loss. The objective screen shows "Zero HP x/10" and "Nests: n".
5. **UT_ZERO**, appended after UT_BLOATER, plus a `UF_BOSS` flag. Behaviour, in horde-turn order
   (full details in BOSS_PATIENT_ZERO.md section 3):
   - Hive Link: at the start of each horde turn he heals `MIN(30, 5 * nests_on_map)`, capped at 100.
   - Call: on each night horde turn, after the nest spawns, he summons 2 Walkers or Runners
     (50/50) on free tiles next to him, using the same spill rule as `spawn_at_nests`. They
     start with `moved = 1`.
   - Aura: other horde units within Manhattan distance 2 of him get +15% attack (on top of the
     night bonus) and +1 defence star, in `damage_estimate`.
   - Raise: a survivor infantry unit killed by any zombie within 2 tiles of Zero rises as a
     Runner at 80 HP (in `kill_unit`).
   - Leash (AI): by day he only considers tiles within distance 3 of his start tile, unless a
     survivor is already within his move+attack reach. By night there's no leash. With no
     target he heads back toward his start tile, and he never captures.
   - Frenzy: the first time he drops to <=50 HP, his move becomes 4 for the rest of the mission
     (night still adds +1), and `Mission.boss_half` plays (see item 6).
   - He is never dormant, and Bloater bursts don't damage him.
   - Death: every other horde unit takes 30 HP (never below 1), the banner reads "The link is
     broken.", and GOAL_BOSS is won.
   - His info-panel name is "Zero".

## P2: polish, but the twists rely on it
6. **Day events.** Add `const DayEvent *events` to `Mission`, where
   `typedef struct { u8 day; const Line *lines; } DayEvent;` and the list ends at `day == 0`.
   The event plays `dialogue()` at the start of the player's turn on that day, once per mission
   (it must survive Suspend/Resume: one bit per event in GameState). Also add
   `const Line *boss_half` for the Frenzy line. Missions 6-10 each use one event.

## Not needed
Two safehouses (M7) already work through `safehouse_lost`. A safehouse plus GOAL_EVAC (M8) and
permanent night with GOAL_NESTS (M9) need nothing new. Please confirm if you know otherwise.

Done when: these are on claude/dw-engine, with a note in your status saying how to declare
them in mission data. Please also tell me when battle.c player actions change, so I can update
the sim mirror.
