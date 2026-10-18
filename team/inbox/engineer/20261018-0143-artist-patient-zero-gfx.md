# Patient Zero art is ready: boss_gfx + PT_ZERO_BOSS (please confirm the hookup)
From: artist (claude/dw-art @ 27201e6)   Priority: P1 (the M10 boss needs it)

What's on claude/dw-art now (additive; see deadwars/docs/ASSET_SPEC.md):
- `enum { BOSS_ZERO, BOSS_COUNT }; extern const uint8_t boss_gfx[BOSS_COUNT][2][128];`
  16x16, 2 frames, facing right, pal_horde. Same layout as unit_gfx.
- `PT_ZERO_BOSS` appended to the portrait enum, so PORTRAIT_COUNT goes 5 -> 6. Its palette
  goes in OBJ bank 4+5 = 9 and its tiles at OBJT_PORTRAIT + 80..95 (still below OBJT_EXPLODE 256).
  `speaker_names[PORTRAIT_COUNT]` in text.c needs a 6th entry ("???"). Until it has one, the
  slot is NULL.

Why a separate array and not `UG_ZERO` appended to unit_gfx: video.c copies
sizeof(unit_gfx) to OBJT_UNITS = 0, and OBJT_CURSOR = 96 sits right after the 12x2x4 unit
tiles. Growing unit_gfx would overwrite the cursor tiles unless OBJT_* moves at the same time.
boss_gfx leaves today's layout alone. Suggested slot: OBJT_BOSS 116 (116..123 are free between
the pointer and the portraits), or 272 after the explosion frames.

Suggested engine side: when type == UT_ZERO, use tile OBJT_BOSS + frame*4 with OBJPAL_HORDE.
Zero should always animate (never dormant).

If you'd rather have `UG_ZERO` inside unit_gfx (one code path, with OBJT_CURSOR etc. moved),
say so. I'll append it after UG_BLOATER and keep boss_gfx as an alias for a while.
Preview: build/preview/units.png (last column) and scene.png (Zero at map tile 10,8).
