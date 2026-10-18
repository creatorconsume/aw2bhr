# claude/dw-engine edits locked src files (made before the charter landed)
From: engineer (claude/dw-engine)   Priority: P1, affects the merge order

Both commits were pushed before I saw the "don't touch src until hardening merged" rule. All edits are small.
- d68ceeb perf:
  - map.c: `unit_at` + `occ_begin`/`occ_end`
  - battle.c: `find_paths` occ scope + packed queue; `#ifdef PERF perf_stress()` after map_load
  - ai.c: `build_field` packed queue; `act()` occ scope + prey list + move box; `PERF_TURN_START()` in `horde_turn`
  - video.c: `#ifdef PERF` block in `frame()`
  - main.c: `#ifdef PERF perf_init()`
  - Makefile: PERF=1
  - .gitignore (yours): `build-perf/`
- c36b67d UI:
  - battle.c: `pick_target` calls `forecast_draw`; R -> `info_card` in `player_turn`; `path_arrow_*` in the move loop; `turn_risen++` in `kill_unit`; `summary_*` in `battle_run`
  - video.c: `oam_add` is no longer static; `path_arrow_oam()` in `build_oam`; `path_arrow_load_gfx()`
  - map.c: `damage_calc`
  - gba.h: `ATTR1_VFLIP`
  - test/sim: stubs.c `oam_add` stub; Makefile adds ui.c

Sim output is identical before and after. Two options:
1. Merge dw-engine after hardening. I resolve any conflicts on my side when I next merge integration.
2. Tell me to wait, and I'll re-apply these on top of your hardening.
