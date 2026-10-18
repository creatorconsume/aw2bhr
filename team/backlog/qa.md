# Backlog: QA & platform (`claude/onion-lab`)

Seeded by the producer on 2026-10-18. The QA branch has `tools/emu-lab` (the `lrun` libretro runner, Onion
mGBA/gpSP option profiles, gpSP Onion rev 6250979) and 5405c85 (map animation, pending merge). From now
on only QA edits this file. Reorder freely and tick items off with the commit hash.

P0 = do first, or it blocks someone. P1 = next. P2 = later. QA never fixes another role's code. File bugs into the
owner's inbox with a repro script, screenshots and the commit.

## P0

- [ ] **Take over `deadwars/test/smoke.sh`.** The producer session wrote it on 2026-10-18 and it will land on
  integration. It does: build, build gbarun, then boot to title, Campaign, M1, START past the intro, A past the
  banner, and the map. It checks the title and map shots with Pillow (not blank, clearly different) and reads RAM
  via `nm` (`spr.cursor_show`/`map_visible`/`pointer_show`, and `gs.mission`/`day`/`side`/`over`). Exit codes are
  0 pass, 1 check failed, 2 missing prerequisite, 3 build failed. It passed on integration, integration +
  onion-lab, and integration + deadwars-dev. Reconcile it with `tools/emu-lab/scripts/deadwars-smoke.txt` so there is
  one boot path.
- [ ] **Verify the hardening fixes** once the producer posts "hardening merged". Run every script in
  `deadwars/test/playtest/`: `m1_hud_layout`, `m1_undo_keeps_capture`, `m1_victory_results`,
  `m1_zombie_range_and_objective`, `m3_bus_drop_cursor`, `m4_text_overflow`. Check each `EXPECT`, then turn each
  `EXPECT` into an automatic pass/fail (a RAM dump or pixel probe) in a runner such as
  `deadwars/test/playtest/run.sh`. m3 and m4 need the engineer's debug unlock; until then use a scratch `save.c`
  edit and never commit it.
- [ ] **Emulator matrix for every integration head.** Run smoke and the playtests on:
  - libmgba gbarun
  - `lrun` with mGBA-libretro and `onion-mgba.opt`
  - `lrun` with gpSP-onion 6250979 and `onion-gpsp.opt`
  - gpSP master
  Record the results per commit in `docs/qa/MATRIX.md`, and file a sign-off in `team/inbox/producer/`.
- [ ] **Official-BIOS boot of Dead Wars.** Without `LOGO_ROM`, `gbafix.py` leaves the Nintendo logo blank; today's
  ROM has 0 non-zero logo bytes. Onion's mGBA uses `gba_bios.bin` whenever it's present (`mgba_use_bios = "ON"`,
  `skip_bios = "OFF"`), and a real BIOS intro refuses a bad logo.
  - Confirm on the lab. The BIOS comes only from the owner or producer and is never committed.
  - Add a release check that the logo is present (compare a hash, not the bytes).
  - Report to the producer and the engineer.
- [ ] **AW2 black-screen repro matrix**, for the producer to run with the real ROM. QA never needs the ROM. Write
  `docs/qa/AW2_BLACKSCREEN_MATRIX.md` and an `lrun` boot script that reaches the title.
  - Cores: mGBA (Onion opts), gpSP-onion, gpSP master.
  - BIOS: none, official, or a wrong 16 KiB file (generate it, for example all zeros).
  - Auto state: none, from the other core, from another ROM version under the same name, or saved mid-boot.
  - ROM: loose `.gba`, or a `.zip` with an extra `.txt`.
  - mGBA idle loop: "Remove Known" vs "Don't Remove".
  - Original vs remix.
  - Expected result: a title screenshot by frame N, plus a "black/white/ok" column for the producer to fill in.

## P1

- [ ] **Onion profile tests for Dead Wars:**
  - Take an auto state on the map, reset, load the state, and play 300 frames on both cores.
  - Suspend, power off (reset with the `.srm` kept), then Continue on both cores (`gpsp_save_method = "gpSP"`).
  - Load a state saved by an older build under the same filename. Document what happens.
- [ ] **Performance report on gpSP-onion** with `count frame_count`: the title, an M1 horde turn, an M5 night horde turn,
  the Sniper range view, and dialogue. Send the numbers to `team/inbox/engineer/` and repeat after each perf change.
- [ ] **Extend smoke.sh.** Run `python3 -I deadwars/test/check_maps.py deadwars/src/missions.c` and fail on
  `BAD`/`OUT OF MAP`/`OVERLAP`. Add an option to run the same boot on `lrun` + gpSP-onion.
- [ ] **Role-branch testing.** Run smoke and the matrix on each push to `claude/dw-engine`, `dw-design`, `dw-art` and
  `deadwars-dev`. File bugs into the owners' inboxes.
- [ ] **Audio checks.** Use gbarun or `lrun` WAV capture: the title music isn't silent and nothing clips. Get the
  expected values from audio.

## P2

- [ ] Soak tests: random input for 100k+ frames per mission with `lrun --hang-secs`, to catch hangs and stuck turns.
- [ ] `docs/miyoo/DEVICE_TESTS.md`: a checklist for the owner on real hardware (sleep and resume, both cores, sound,
  scaling).
- [ ] One `tools/emu-lab/run_all.sh` entry point that every role can run before a push.
