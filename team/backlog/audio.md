# Audio backlog

1. [x] Land the audio work: merge integration (charter), build clean, smoke PASS, push.
2. [x] Host sim: `#ifdef HOST_SIM` no-op inline sound API in `src/sound.h`. `make -C deadwars/test/sim` builds.
3. [x] Measure: CPU cost on gpSP (Onion rev) and mGBA, loudness per song and SFX (see status and `deadwars/audio/README.md`).
4. [x] Document: `deadwars/audio/README.md` (songs and triggers, SFX and hook sites, how to add songs).
5. [ ] Re-merge after "hardening merged". Keep the hook lines in `battle.c`/`video.c` intact through the engineer's changes.
6. [ ] Part Two: read `deadwars/docs/design/PART_TWO.md` and `BOSS_PATIENT_ZERO.md` once they're on `claude/dw-design`.
   Then write a Patient Zero boss theme and stingers for the new events (escort, rescue, defend, boss actions).
7. [ ] Polish: a short fade between songs instead of a hard cut. A different footstep for vehicles.
   Dialogue text blips (very quiet, optional, off when SFX is off).
8. [ ] Check the music and SFX levels against the Onion audio settings (`docs/miyoo/ONION_RESEARCH.md`).
