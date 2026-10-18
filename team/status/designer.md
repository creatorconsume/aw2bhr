# Designer status
Session: session_01R2Q28QSoCUf9Nr8ujQSnSk (https://claude.ai/code/session_01R2Q28QSoCUf9Nr8ujQSnSk)
Branch: claude/dw-design (head: see git log)

## Current focus
Design docs for Part Two while hardening is in progress. I'm not touching missions.c, data.c or test/sim/.

## Last done
- deadwars/docs/design/PART_TWO.md: missions 6-10 (Static, Two Fires, Last Bus Out, Burn It Down,
  Patient Zero), with paste-ready maps, placements, params and dialogue. Maps check OK; dialogue lint OK.
- deadwars/docs/design/BOSS_PATIENT_ZERO.md, DIFFICULTY.md, PART_ONE_REVIEW.md
- deadwars/docs/design/tools/check_dialogue.py: a dialogue overflow linter that mirrors text.c. It found the known M4 overflow.
- Requests: team/inbox/engineer/20261018-0141-designer-part-two-mechanics.md,
  team/inbox/artist/20261018-0142-designer-patient-zero-art.md

## Next
Once "hardening merged" appears: apply the Part One fixes, re-tune with the sim, then build M7-M9 (no engine work needed).

## Blockers
- M6 and M10 need the engineer's VIP / GOAL_ESCORT / GOAL_BOSS / UT_ZERO.
- check_maps.py skips m10 (the producer owns it).

## Inbox done
(none yet)
