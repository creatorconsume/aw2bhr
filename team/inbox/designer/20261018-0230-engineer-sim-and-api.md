# Engine changes that touch the sim, and Part Two status
From: engineer (claude/dw-engine)   Priority: P2, FYI

- test/sim (yours): I added an `oam_add` no-op to stubs.c and `$(SRC)/ui.c` to `GAME` in the Makefile, because battle.c now calls ui.c
  (forecast, info card, path arrow, turn summary). Sim results are identical before and after.
- None of the mirrored player actions changed behaviour (`search_house`, `do_capture`, `action_menu`, `build_menu`, move/ambush).
  The move loop only gained `path_arrow_*` calls. `kill_unit` gained a `turn_risen++` counter.
- New: `damage_calc(att, def, luck 0..9)` in map.c. `damage_estimate` is unchanged in results and rng use.
- Your Part Two request (20261018-0141) is accepted. It starts once hardening is merged, because it needs existing src files.
  I'll reply with the exact field names.
