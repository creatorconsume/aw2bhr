# Backlog: Audio (`claude/deadwars-dev`)

Seeded by the producer on 2026-10-18. The audio branch is at 30bbfa5: a PSG sequencer, six songs (title,
day, night, horde, victory, defeat), 20 SFX, and Music/SFX options in `Profile`. From now on only audio
edits this file. Reorder freely and tick items off with the commit hash.

P0 = do first, or it blocks someone. P1 = next. P2 = later.

## P0

- [ ] **Get 30bbfa5 merged.** (hooks listed in team/inbox/engineer/20261018-0143-audio-sound-hooks.md; waiting for the producer's merge) It conflicts with QA's 5405c85 (map animation) in `battle.c`: `kill_unit()` has the
  explosion vs `sfx(SFX_DESTROY)`, and `unit_move_anim()` has the step speed and `anim_face` vs the step SFX. The
  producer resolves the conflict by keeping both. After "hardening merged":
  - merge integration and check that every `sfx()` hook is still in place
  - run `deadwars/test/smoke.sh` and get zero warnings in your files
  - put the list of hook lines you own in other files into `team/inbox/engineer/`
- [x] **CPU budget on gpSP.** gpSP 0.46% avg / 1.77% max, numbers in the status file. Measure the sequencer and SFX cost on QA's emu-lab gpSP Onion core with
  `count frame_count`. Measure the title song, the horde song with dense SFX (a long horde turn), and the night song
  with fog redraws. The target is about 2% of a frame in the worst case, with no missed frames. Put the numbers in
  your status.
- [ ] **Save compatibility.** (layout proposed in team/inbox/engineer/, waiting for the engineer) `snd_flags`/`snd_magic` in `Profile` must survive the engineer's save versioning and
  CRC work. Agree the layout in `team/inbox/engineer/`.

## P1

- [x] **Boss music for Patient Zero (M10)**: song `zero` plus SFX Call/Link/Roar (hooks requested from the engineer)., plus a short "he calls them" sting when night falls or the horde wakes.
  Follow the designer's `BOSS_PATIENT_ZERO.md`.
- [x] **Missing songs:** briefing, laststand and credits done. The title doubles as the menu theme (the director can't tell the menus apart from the title).
  - a briefing/dialogue theme (calm, under intros and outros)
  - a mission select/menu theme
  - a credits theme
  - a "last stand" variant for the final days of survive missions
  Per-mission music needs an engine hook: request a `Mission` field or `music_play` cue points.
- [~] **Missing SFX** (done: heal, burst, acid, wave alarm, buzz, blip, boss roar. Open: nest spawn per turn (too noisy, skipped), dormant wake (needs a hook), radio reveal (Secure covers it)), from moments in the code that have no sound yet:
  - heal (Medic and buildings)
  - Bloater burst
  - Spitter acid
  - nest spawn and the "A huge wave!" alarm (`ai.c`)
  - a dormant zombie waking
  - the radio tower reveal
  - a disabled-menu buzz
  - a subtle, rate-limited dialogue typewriter blip
  - boss roar and boss hit
- [~] **Mixing for the Miyoo speaker.** (night drone moved up an octave; bass lines in octaves 2-3 use a saw wave so their harmonics carry) It's small, so check that the wave-channel bass sits in an audible octave,
  set the SFX priority so the lead line isn't stolen, and balance the volume between songs.

## P2

- [ ] A Direct Sound (DMA) sample channel for one or two signature sounds (a scream, the boss roar), only if the
  gpSP budget allows it.
- [ ] Audio regression with QA: use gbarun's WAV capture to check that the title is not silent, nothing clips, and
  loops are seamless. Give QA the expected values.
- [ ] Sound test polish, and a jingle for unlocking a mission.
