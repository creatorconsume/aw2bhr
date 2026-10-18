# Status: Audio

Only the Audio session edits this file. Update it with every push.

- **Session:** `session_0168cF2mwktk5orSxuVfkrjt`
- **Branch:** `claude/deadwars-dev`
- **Branch head:** see `git log -1 origin/claude/deadwars-dev` (this file is updated in the same commit)
- **Updated:** 2026-10-18 01:49 UTC

## Current focus
Part Two audio is in: the Patient Zero theme and boss stings, plus the briefing, last stand and credits songs. Waiting for the engine's
boss code to hook CALL/LINK/ROAR, and for "hardening merged" to re-check the hooks.

## Last done
- Songs: `zero` (boss, plays by itself when a `UF_BOSS` unit is alive), `briefing` (under intros, outros and How to Play),
  `laststand` (the last 2 days of survive missions), `credits` (for a future credits screen). The night drone moved up an octave
  for the small speaker. 10 songs in total.
- SFX: heal, Bloater burst, Spitter acid, huge-wave alarm, disabled-menu buzz, dialogue blip, and boss Call / Link / Roar. 29 in total.
- The director knows about dialogue (`sound_dialogue()`), the boss and last stand. The victory jingle leads into the briefing bed under the outro.
- music.py: a bare `[ ... ]` now repeats once (it was 2, a trap).
- Earlier: 30bbfa5 engine + 6 songs + 20 SFX + Options; de354bd HOST_SIM stubs + audio README.

## Numbers (current)
CPU in `sound_update()` (`-DSOUND_PROFILE` build, 4,030 frames of title, battle and horde turns, lrun with the Onion opts):

| Core | avg cycles/frame | max | avg / max share of a frame |
|---|---|---|---|
| gpSP Onion rev 6250979 | 1,292 | 4,981 | 0.46% / 1.77% |
| mGBA 0.10.2 | 1,113 | 4,760 | 0.40% / 1.69% |

Missed frames are the same on both cores and not caused by sound: 1,210 (gpSP) / 1,215 (mGBA) game frames in 1,232 during End Day + the horde turn on M1.

Loudness (mGBA WAV, % of full scale, rms in s16 units, no clipping anywhere):

| Song | peak | rms | | Song | peak | rms |
|---|---|---|---|---|---|---|
| title | 54% | 3974 | | zero | 72% | 5123 |
| day | 71% | 4315 | | briefing | 25% | 2333 |
| night | 37% | 2214 | | laststand | 70% | 4732 |
| horde | 65% | 4765 | | credits | 67% | 3956 |
| victory | 56% | 4074 | | defeat | 63% | 3787 |

SFX peaks: Link 74%, Burst 61%, Shot 58%, Ambush 53%, Roar 49%, Wave 46%, Call 45%, Destroy 43%, Evac 42%, Bite/Acid 37%, Nightfall 35%,
Infect 31%, Burn 28%, Overrun 24%, Found/Secure 21%, Dawn 19%, Build 18%, Confirm/Empty 16%, Cancel/Buzz/Heal 14%, Menu 12%,
Step/Shuffle/Blip 7%, Cursor 4%.
Loops: every looping song restarts on the exact frame (per-frame loudness correlation peaks at offset 0, with no level dip at the seam).
Mission intro check: the briefing plays under the dialogue (same level), and the day theme starts after the banner (correlation 0.97).

## Next
- Hook CALL/LINK/ROAR once the engineer lands the boss (requested). A dormant-zombie wake SFX if the engineer agrees to the hook.
- Re-merge after "hardening merged" and check every hook.

## Blockers
- None. Boss SFX hooks depend on the engineer's `UT_ZERO`/`UF_BOSS` work.

## Inbox done
<!-- `<inbox filename>` -> outcome (commit / branch) -->
- (no requests to audio yet; BOSS_PATIENT_ZERO.md section 8 handled proactively -> this push)
