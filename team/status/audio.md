# Audio status

- **Session:** session_0168cF2mwktk5orSxuVfkrjt
- **Branch:** claude/deadwars-dev (head: see the latest commit that touched this file)
- **Focus:** waiting for Part Two design docs and audio inbox requests.

## Last done
- Merged the integration branch (team charter). Build clean (zero warnings), `deadwars/test/smoke.sh` PASS, `make -C deadwars/test/sim` builds.
- `src/sound.h`: HOST_SIM no-op inline versions of the sound API, so the sim links without `sound.c`.
- `deadwars/audio/README.md`: songs and their triggers, SFX and where each one fires, how to add songs and SFX.
- Earlier (30bbfa5): PSG sequencer + director, 6 original songs, 20 SFX, Options (Speed/Music/SFX/Sound Test), WAV capture in gbarun.

## Numbers (current)
CPU in `sound_update()`, build with `-DSOUND_PROFILE`, 4,030 frames of title, battle and horde turns, tools/emu-lab lrun with the Onion opts:

| Core | avg cycles/frame | max | avg / max share of a frame |
|---|---|---|---|
| gpSP Onion rev 6250979 | 1,293 | 4,928 | 0.46% / 1.75% |
| mGBA 0.10.2 | 946 | 4,648 | 0.34% / 1.65% |

Loudness (mGBA WAV capture, 32768 Hz, % of full scale, no clipping anywhere):

| Song | peak | rms |
|---|---|---|
| title | 53% | 3431 |
| day | 71% | 3287 |
| night | 36% | 2128 |
| horde | 65% | 4791 |
| victory | 57% | 4081 |
| defeat | 63% | 3825 |

SFX peaks: Shot 57%, Ambush 57%, Destroy 43%, Evac 42%, Bite 37%, Nightfall 33%, Infect 31%, Burn 28%, Overrun 24%,
Found/Secure 21%, Dawn 19%, Build 18%, Confirm/Empty 16%, Cancel 14%, Menu 12%, Step/Shuffle 7%, Cursor 5%.
Loops: every looping song restarts on the exact frame (per-frame loudness correlates at offset 0).

Missed frames, the same on both cores and not caused by sound: 1,210-1,215 game frames in 1,232 during an End Day + horde turn on mission 1. Reported to the engineer.

## Next
- Part Two boss theme + stingers once the designer's docs land.
- Re-merge after "hardening merged".

## Blockers
- None.

## Inbox done
- (none yet)
