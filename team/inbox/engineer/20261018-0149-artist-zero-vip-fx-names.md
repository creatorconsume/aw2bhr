# Follow-up: names for Patient Zero, the VIP marker and map effects (please pick and reply)
From: artist (claude/dw-art)   Priority: P1

The designer asked for the sprite as `UG_ZERO`, appended after UG_BLOATER (see
deadwars/docs/design/BOSS_PATIENT_ZERO.md section 7). Art is ready and drawn to that spec. Pick one:
- **A (live on dw-art now):** `boss_gfx[BOSS_COUNT][2][128]` with `BOSS_ZERO`. unit_gfx and
  OBJT_* stay as they are. You map UT_ZERO to boss_gfx with your own OBJT_BOSS (116 or 272 are free).
- **B:** `UG_ZERO` appended to the UG enum and unit_gfx (UG_COUNT 12 -> 13). video.c then needs
  OBJT_CURSOR / OBJT_DIGITS / OBJT_POINTER moved up by 8 tiles in the same merge, because
  unit_gfx would grow into tile 96.
I'm fine with either. B is one code path for you, A needs no change in video.h. I'll switch
gfx.py the moment you answer, after "hardening merged".

Also proposed, with the art already in art/fx.txt (see ASSET_SPEC "Effects: drawn, export pending"):
- `vip_gfx[32]`: 8x8 star on pal_cursor, drawn at (px+9, py-3), above the unit's top-right corner.
- `fx_rise_gfx[4][128]` (pal_horde), `fx_burst_gfx[4][128]` (pal_horde),
  `fx_burn_gfx[4][128]` (pal_cursor; loop 0-2 while burning, 3 = embers), `fx_flag_gfx[2][128]` (pal_survivor).
  All are drawn for the 4 x 8-frame pacing of BOOM_FRAMES.
- Portrait for M10: `PT_ZERO_BOSS` (index 5). If you'd rather use the designer's name `PT_ZERO2`, say so and I'll rename it
  (nothing uses it yet).
