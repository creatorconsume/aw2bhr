# Battle scene art: asset contract
From: engineer (claude/dw-engine)   Priority: P1 (owner request; the engine runs on placeholders today)

The engine is `src/scene.c`. Survivors stand on the left and the horde on the right. Each side shows N figures, where N is its displayed HP (1-10): a front row of 5 and a back row of 5. Today's placeholder is the 16x16 map sprite drawn at 2x.

## Symbols (all additive, in build/gen/assets.h)
1. `extern const uint8_t battle_gfx[UT_COUNT][2][512];`
   - One 32x32 4bpp figure per unit type, in `UT_*` order (SURVIVOR..BLOATER). Patient Zero gets its own `battle_zero_gfx[2][512]`.
   - Frame 0 is idle and frame 1 is attacking (muzzle flash, lunge or spit pose).
   - Figures face RIGHT. The engine mirrors the horde with an affine matrix.
   - Tile order is 1D: 16 tiles, row-major 4x4. Colour 0 is transparent.
   - Palettes are `pal_survivor` and `pal_horde`, the same 16 colours as the map sprites, so there are no new OBJ banks.
   - Vehicles: keep the drawing inside about 28x24 so 10 can stand side by side. The figures are 20 px apart, and 10 px apart between rows.
2. Optional: `extern const uint8_t battle_fx[4][128];` holds 16x16 hit effects (shot spark, bite, acid splash, blood). These use `pal_cursor`.
3. `enum { SBG_PLAIN, SBG_WOODS, SBG_URBAN, SBG_RUBBLE, SBG_COUNT };` with `scene_bg_tiles[SBG_COUNT][64][32]` and `scene_bg_map[SBG_COUNT][10][15]`.
   - Each is a 120x80 backdrop strip: up to 64 unique 8x8 tiles, and a map of tile index | hflip<<10 | vflip<<11.
   - Each side's backdrop follows the terrain under that unit:
     - SBG_WOODS: woods
     - SBG_URBAN: road, bridge, ruins, wreck, buildings, helipad
     - SBG_RUBBLE: rubble
     - SBG_PLAIN: everything else
   - Use one BG palette, `pal_scene[16]`. The engine darkens it at night.

## VRAM and palette budget (engine side)
- OBJ: battle figures load at scene open into tiles 320..383 (2 units x 2 frames x 16 tiles). FX go at 384..399.
- BG: the two backdrops load into charblock 1 at tiles 0..127 (the title art is not on screen during a battle). The palette goes in BG bank 13.
- Screen layout: backdrops fill text rows 0..13 per half (15 tiles wide), and the HP panels are rows 14..19.

Until these symbols exist, the engine keeps the placeholders. When you push them, drop a note here and I'll switch the code over (a few lines in scene.c).
