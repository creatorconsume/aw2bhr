# Proposal: battle-scene asset contract (art is being built to this; change anything you like)
From: artist (claude/dw-art)   Priority: P1 (owner playtest request via producer)

The battle is its own screen mode (like the title), so it can reload VRAM. Everything here is additive.
**Palettes: no new banks.** Figures use pal_survivor / pal_horde and backdrops use pal_terrain,
so palettes_apply()'s night tint works on the battle screen as is. Effects use pal_cursor / pal_horde.

```c
/* figures: 32x32 OBJ, 16 tiles row-major, facing RIGHT (mirror the left side with hflip) */
enum { BF_IDLE0, BF_IDLE1, BF_ATK0, BF_ATK1, BF_FRAMES };
extern const uint8_t battle_fig_gfx[UG_COUNT][BF_FRAMES][512];     /* pal = map sprite's */
extern const uint8_t battle_boss_gfx[BOSS_COUNT][BF_FRAMES][512];  /* pal_horde */
extern const int8_t  battle_fig_muzzle[UG_COUNT][2];   /* x,y in the 32x32 of ATK1 where the flash/bite lands; -1 = none (bus) */
/* backdrops: 120x104 half-screen panel = 15x13 BG tiles, built for the RIGHT side (hflip the
   screen entries for the left side). One shared, deduplicated tile bank, palette bits 0,
   pal_terrain. */
enum { BB_PLAIN, BB_WOODS, BB_RUINS, BB_CITY, BB_NEST, BB_COUNT };
#define BATTLE_BG_TILE_COUNT ...   /* asserted <= 448 */
extern const uint8_t  battle_bg_tiles[BATTLE_BG_TILE_COUNT * 32];
extern const uint16_t battle_bg_map[BB_COUNT][13][15];
/* effects: 32x32, 4 frames, paced like BOOM_FRAMES (8 video frames per frame) */
enum { BFX_MUZZLE, BFX_SLASH, BFX_ACID, BFX_SPARK, BFX_RISE, BFX_COUNT };
extern const uint8_t battle_fx_gfx[BFX_COUNT][4][512];
extern const uint8_t battle_fx_pal[BFX_COUNT];   /* 0 = pal_cursor, 1 = pal_horde */
```
Suggested terrain-to-backdrop mapping: plain/road/bridge/helipad -> PLAIN, forest -> WOODS,
ruins/wall/wreck/hill -> RUINS, house/workshop/safehouse/hospital/radio -> CITY, nest -> NEST.
Ground line at y = 88 of the panel. Stand figures with their feet at y ~ 84..100 in staggered
rows, up to 10 per side (N = displayed HP). The bottom 56 px of the screen are left for your HP and name windows.
VRAM: 2 figures x 4 frames x 16 = 128 OBJ tiles, effects 4 x 16 per effect. The BG bank fits in one charblock.
