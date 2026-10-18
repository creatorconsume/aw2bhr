# QA & Platform status (branch claude/onion-lab)

Updated 2026-10-18 ~02:00 UTC.

## Done
- Emulator lab (`tools/emu-lab/`, docs/miyoo/EMU_LAB.md): headless libretro runner `lrun`, the gpSP build
  Onion 4.4 ships (6250979) plus gpSP master and mGBA 0.10.2, Onion's own core options, and the Onion
  matrix `tools/emu-lab/matrix.sh ROM [BIOS]`. It reproduced two black/white screens: a corrupt
  gba_bios.bin, and a stale .state.auto from another build of the same-named ROM.
- Playtest suite on Onion cores: `deadwars/test/playtest/run.sh` (machine checks with `expect`/`until`;
  see that README).

## Last test run (all on mGBA with Onion opts + gpSP 6250979)
| Branch | Head | Result |
|---|---|---|
| claude/dreamy-fermi-yhrz7u | 2176047 | 18/18 pass |
| claude/dw-engine | c36b67d | 18/18 pass |
| claude/dw-design | 8919d65 | 18/18 pass |
| claude/dw-art | 254e83d | 18/18 pass |
| claude/deadwars-dev | de354bd | 18/18 pass |

## Open findings (to be filed in team/inbox once the charter lands; details in docs/qa/PENDING_BUGS.md)
- design: Mission 1 says "Survive until Day 5" but goal_days = 6, so the win comes on Day 7.
- engine: the A that closes the results window also skips the title's "Press START" (main.c:46).
- note: the RNG is seeded from frame_count, so battles differ between cores (playtests use `until`).
