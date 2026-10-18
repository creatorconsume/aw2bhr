# QA: findings waiting for the team inbox (file into team/inbox/<role>/ once the charter lands)

1. **design**: Mission 1's objective text does not match its data. `src/missions.c:215` says "Survive until
   Day 5." and the outro opens with "Day five...", but `goal_days = 6` and `check_victory` wins on
   `gs.day > goal_days`, i.e. on Day 7. README.md:49 also says Day 5. Repro: `deadwars/test/playtest/run.sh qa_3_m1_victory.txt`
   (`expect gs+7 07` passes; the results window shows "Days 7"). Fix: set goal_days = 4, or change the texts to Day 7.
2. **engine**: The A that closes the results window also skips the title's "Press START" and opens the
   menu. `src/main.c:46` tests `keys_down` before the first `frame()`, so it still sees the A from
   `results()`. Repro: qa_3_m1_victory, screenshot `after-results.png` shows the menu on both cores.
   Proposed patch: `do frame(); while (!(keys_down & (KEY_START | KEY_A)));` (same pattern anywhere a wait
   loop starts right after another screen consumed a key).
3. **design/QA note**: battles are not reproducible across cores. RNG seed is `0x1234567 + frame_count`
   (`src/map.c:79`) and gpSP starts the game 2 frames after mGBA. Not a bug for players; it is why
   playtests must use `until` instead of fixed waits. A fixed seed per mission (or one stored in the
   save) would make playtests and bug repros deterministic.
4. **engine (cosmetic, from the emulator lab)**: blank header logo in non-release builds, versioned
   release ROM name, src/text.c:29 array-bounds warning. Already passed to the producer.
