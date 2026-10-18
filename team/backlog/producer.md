# Backlog: Producer (`claude/dreamy-fermi-yhrz7u`, integration)

Seeded on 2026-10-18. Only the producer edits this file.

P0 = do first, or it blocks someone. P1 = next. P2 = later.

## P0

- [ ] **Finish the hardening pass.** That covers the bug review fixes, the `deadwars/test/playtest/` scripts, the
  M1-M5 tuning in `missions.c`, and `deadwars/test/sim/`. Commit it, post **hardening merged** in `team/status/producer.md`,
  and message the engineer, designer, artist, audio and QA.
- [ ] **Land `deadwars/test/smoke.sh`.** It was written and tested on 2026-10-18 and passes on integration,
  integration + onion-lab, and integration + deadwars-dev. It is in QA's paths, so hand it over in the status file.
- [ ] **Merge the pre-split branches.**
  - `claude/onion-lab`: emu-lab, plus 5405c85 map animation in `battle.c`/`video.c`/`video.h`/`art/ui.txt`/`gfx.py`.
  - `claude/deadwars-dev` 30bbfa5: audio, the `Profile` sound fields, the music rule in the `Makefile`, and the
    `gbarun.c` additions.
  - They conflict in `battle.c` (`kill_unit()` explosion vs `sfx(SFX_DESTROY)`; `unit_move_anim()` step speed and
    `anim_face` vs the step SFX). Keep both sides.
  - After the merge, run a clean build. Integration + onion-lab showed a `-Warray-bounds` warning in `text.c`
    `dialogue()`. Then run smoke and the sim.
- [ ] **Start the engineer, designer and artist sessions.** Record their ids in `team/README.md`.
- [ ] **Hand `deadwars/test/check_maps.py` to the designer.** Its regex is `m\d`, and Part Two needs `m\d+`.

## P1

- [ ] **Finish and verify the AW2 Remix `tools/remix/build_remix.py`** (exists, WIP). It runs: the sha1-checked base
  ROM, then QoL, then `balance.apply`, then the map files, then the safety checks, then a BPS round trip. Write the
  output outside the repo.
  - QoL: `0x016A6A` → `00` (No Visual by default, which also doubles map move speed), and optionally the move speed
    at `0x49CD88 + 36*t + 0x18`.
  - Keep header code `AW2E`. Leave the code near the idle-loop addresses alone, because both cores key hacks to
    `AW2E`: mGBA uses `0x08036E08` and gpSP uses `0x08036E2A`, and gpSP has no switch to turn its hack off.
- [ ] **Remix maps.** Make the 5 Orange Star hard maps (IDs 138-142) per ROM_NOTES §7: recompress the terrain to
  `0x620000+`, build new unit lists, and check the Cleanup tutorial events. Verify headless with `gbadbg` and
  `boot_cleanup.txt`, then on QA's gpSP-onion core.
- [ ] **Run QA's AW2 black-screen matrix** with the owner's ROM, fill in the results, and update
  `docs/miyoo/ONION_GBA_FIX.md` if anything new turns up.
- [ ] **Release checklist.**
  - Build Dead Wars releases with `LOGO_ROM=<owner's ROM>`. Onion's mGBA boots through `gba_bios.bin` when it's
    present, and a blank logo fails the real BIOS intro.
  - Only from a QA-signed integration commit.
  - Deliver the Dead Wars ROM and the remix BPS or ROM directly to the owner. Never commit them.
  - Name the files as in ONION_GBA_FIX.md, and include a per-game `.opt` for the remix if the matrix says
    "Don't Remove" is needed.

## P2

- [ ] Update `deadwars/README.md` (producer-owned): the Options/sound menu, Part Two, difficulty, and the test commands
  (smoke, sim, emu-lab).
- [ ] Every 30-45 minutes: read every inbox across branches, merge the green role branches, and post a status summary.
- [ ] Groom the backlogs with each role after every merge wave.
