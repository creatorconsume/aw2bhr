# Designer backlog (claude/dw-design)

## Now (docs only, until "hardening merged")
- [x] PART_TWO.md: missions 6-10, with maps, placements, params and dialogue (dialogue lint OK, maps check OK)
- [x] BOSS_PATIENT_ZERO.md: stats, abilities, phases, counterplay, art
- [x] DIFFICULTY.md: rubric
- [x] PART_ONE_REVIEW.md: writing fixes (applied after hardening)
- [x] Engineer request: VIP, GOAL_ESCORT, GOAL_BOSS, UT_ZERO, day events, menu for 10+ missions
- [x] Artist request: UG_ZERO, VIP marker, optional PT_ZERO2
- [ ] Audio request: M10 boss theme, Call sting, link-broken sting (after M10 is playable)

## After "hardening merged"
- [ ] Apply PART_ONE_REVIEW fixes to missions.c. Re-run check_dialogue and the gbarun screenshot of the M4 Doc line.
- [ ] Move docs/design/tools/check_dialogue.py into test/sim/ (or keep it and wire it into the DoD)
- [ ] Run the sim on M1-5, retune to DIFFICULTY.md, and log the results in BALANCE_LOG.md
- [ ] M7, M8, M9 into missions.c (no engine work needed), then sim-tune them
- [ ] Sim: an M8 rule to keep the school garrisoned, and loss reasons that include "safehouse fell"
- [ ] check_maps.py for m10 (the producer is handing it over)

## Blocked on the engineer
- [ ] M6 (VIP + GOAL_ESCORT) and the sim's VIP role
- [ ] M10 (UT_ZERO + GOAL_BOSS): data.c rows, the sim's boss-hunt role, tuning
- [ ] Day events / boss_half wired into all of Part Two
