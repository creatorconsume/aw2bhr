# Dead Wars playtests (QA regression suite)

```sh
deadwars/test/playtest/run.sh                 # every test, on mGBA (Onion options) and gpSP 6250979 (Onion 4.4)
deadwars/test/playtest/run.sh qa_3_m1_victory.txt
CORES="mgba gpsp-onion gpsp" deadwars/test/playtest/run.sh   # add gpSP master
```
It builds the ROM and `tools/emu-lab` (see docs/miyoo/EMU_LAB.md) and writes screenshots and logs to
`deadwars/build/playtest/<test>/<core>/`.

Tests are `tools/emu-lab/lrun` scripts: the `deadwars/test/gbarun.c` commands plus `expect`, `until`,
`poke`, `peek`, `count`, `savestate`/`loadstate`. Use symbols from `deadwars/build/deadwars.map` with
byte offsets, e.g. `expect gs+7 02` (gs.day == 2) or `until spr 01 1200` (wait for the map cursor).
A test fails on an `expect`/`until` failure, a crash, or a hang/black-screen report. `# needs-sram: OTHER`
starts a test from OTHER's `.srm` (same core).

Prefer `until` on game state over long fixed waits. gpSP starts the game 2 frames later than mGBA,
and Dead Wars seeds its RNG from `frame_count`, so battles diverge between cores. Fixed-wait
scripts that depend on the horde's moves are not portable. For the same reason, the cross-core
screenshot comparison only prints notes (`STRICT_CMP=1` makes them failures).

Field offsets used (src/game.h; update if the structs change): `gs` +4 mission, +7 day, +8 side,
+11 over (1 won, 2 lost). `profile` +4 unlocked, +6 has_suspend. `spr` (src/video.h) +0 cursor_show,
+1 map_visible.

| Test | Checks |
|---|---|
| m1_* / m3_* / m4_* | visual regressions from the design playtests (EXPECT comments, compare screenshots) |
| qa_1_smoke_suspend | boot, mission 1, move a unit, End Day, Day 2, Suspend sets has_suspend |
| qa_2_continue | reboot from the .srm: has_suspend at boot, Continue resumes mission 1 Day 2 |
| qa_3_m1_victory | survive goal reached (poked) → over = 1 on Day 7, results window waits for A, mission 2 unlocked |
