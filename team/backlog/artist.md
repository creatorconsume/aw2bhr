# Backlog: Artist (`claude/dw-art`)

Seeded by the producer on 2026-10-18 from the repo state at integration `b348364`, plus the unmerged
`claude/onion-lab` commit 5405c85, which adds `explode0-3` sprites to `art/ui.txt` and `gfx.py`. From now
on only the artist edits this file. Reorder freely and tick items off with the commit hash.

P0 = do first, or it blocks someone. P1 = next. P2 = later.
**Until "hardening merged", do not touch `art/ui.txt` or `tools/gfx.py`.** 5405c85 changes both, so build on its
version after the merge. All art is original. Every contract change in `ASSET_SPEC.md` is additive and agreed with
the engineer through the inbox.

## P0

- [ ] **Art audit** (brief, task 1). Use `build/preview/scene_1x.png` plus smoke and gbarun screenshots, with your own
  scripts for night and fog. Check:
  - Walker vs Runner vs Bloater at 1×
  - survivor blue vs horde red team colours on buildings
  - looted vs unlooted houses
  - the HP digits over busy terrain
  - the dormant look: frame 0 only, which reads as "not animated"
- [ ] **Night palette check.** `palettes_apply()` in `video.c` (engine-owned) blends terrain 7/16 toward RGB15(2,3,10),
  fog another 7/16 toward (0,0,4), and units 3/16 toward (4,6,14).
  - Add a night mock (`scene_night.png`) to the previews once you may touch `gfx.py`.
  - Check that fogged and visible tiles are clearly different at night, horde units are readable on dark grass and
    roads, and the radio-tower reveal edge shows.
  - If the blend constants need to change, send numbers to `team/inbox/engineer/`.
- [ ] **Patient Zero map sprite.** Wait for the designer's `BOSS_PATIENT_ZERO.md`, but start concepts now. It's 16×16 with
  2 frames on `pal_horde`, or ask for its own OBJ palette bank (12-15 are free). Use a hospital gown, glowing eyes and a
  silhouette distinct from the Walker. Append it as a new `UG_` entry after `UG_BLOATER` so indices stay stable. Agree
  the enum and the frame count with the engineer first.
- [ ] **Unit death and infection effects**, all additive symbols:
  - infection "rise" (a fallen survivor gets up as a Walker), 4 frames
  - Bloater burst splash, 4 frames
  - nest burning, 2-4 frames
  - optional "secured" flag pop
  Reuse 5405c85's explosion timing (`BOOM_FRAMES` 32) so the engineer can reuse its code path.

## P1

- [ ] **Path arrow overlay tiles** for the engineer's path arrows: horizontal and vertical straights, 4 corners, 4
  heads and 4 tails, on `pal_overlay`. Ship them as new symbols, not a change to `OV_*`.
- [ ] **Unit info screen icons**: move types (foot, boot, tires, tread, zombie), a terrain star, and a range icon. Use
  OBJ 8×8 icons or spare font codes; `font_tiles` 127 is already a skull. Agree the codes with the engineer.
- [ ] **New terrain for Part Two** as the designer requests it, for example a lab, quarantine fence, burning ground or
  barricade gate. New `MT_`/`TT_` entries need both the engineer and the designer.
- [ ] **Portraits.** Add a sixth portrait if the designer adds a character, and an optional Patient Zero "close-up"
  pose for M10 (512 bytes each; OBJ VRAM has room).
- [ ] **Credits and Part Two title art** for the credits screen (engine) and the Part Two title card.

## P2

- [ ] Give window frames, cursor, pointer and fonts one consistent look (brief, task 4).
- [ ] A colour-blind pass: blue vs red teams and the move vs attack overlays. Add a pattern to the attack overlay.
- [ ] Check the art at Miyoo scale: 240×160 on a 640×480 screen is non-integer (about 2.67×). Use QA's emu-lab shots
  scaled the way Onion does, and fix 1-px details that shimmer or vanish.
