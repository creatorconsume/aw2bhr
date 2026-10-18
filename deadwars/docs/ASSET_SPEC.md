# Dead Wars — asset pipeline contract

All art is authored as **text pixel grids** in `art/*.txt` and converted by
`tools/gfx.py` into `build/gen/assets.c` + `build/gen/assets.h` (plus preview
PNGs in `build/preview/`). The engine only depends on the symbols listed here.

GBA format notes: tiles are 8x8, 4bpp (32 bytes, low nibble = left pixel),
colors are BGR555 `u16`. Palette index 0 is transparent everywhere.

## Palettes
| symbol | used for |
|---|---|
| `pal_ui[16]` | BG bank 0: font + window frames. 0 transp, 1 text white, 2 text shadow/dark, 3 window fill, 4 window border light, 5 window border dark, 6 highlight/selection (yellow), 7 red text, 8 blue text, rest free |
| `pal_terrain[16]` | BG bank 1 terrain. Index 0 unused (never drawn). **Indices 13,14,15 = team color light/mid/dark** (neutral greys in the file; engine recolors per owner) |
| `pal_overlay[16]` | BG bank 7: move/attack highlight tiles |
| `pal_survivor[16]` | OBJ bank 0: survivor units (team blue) |
| `pal_horde[16]` | OBJ bank 1: zombie units (sickly green/grey skin, red accents) |
| `pal_cursor[16]` | OBJ bank 3: cursor, HP digits, menu pointer, explosion/fire effects. 6 smoke grey, 7 smoke light (fx) |
| `pal_portrait[PORTRAIT_COUNT][16]` | OBJ banks 4..: one per portrait |
| `pal_title[16]` | title screen BG (bank 0 while title shown) |

## Terrain (BG, 16x16 metatiles)
```c
enum { MT_PLAIN, MT_FOREST, MT_HILL, MT_WATER, MT_BRIDGE_H, MT_BRIDGE_V,
       MT_RUINS, MT_WALL, MT_WRECK, MT_HOUSE, MT_HOUSE_LOOTED, MT_WORKSHOP,
       MT_SAFEHOUSE, MT_HOSPITAL, MT_NEST, MT_RADIO, MT_HELIPAD,
       MT_ROAD0 /* + 16 autotile variants: MT_ROAD0 + mask, mask bits N=1 E=2 S=4 W=8 */,
       MT_COUNT = MT_ROAD0 + 16 };
extern const u8  terrain_tiles[];            // TERRAIN_TILE_COUNT * 32 bytes
#define TERRAIN_TILE_COUNT ...
extern const u16 terrain_meta[MT_COUNT][4];  // screen entries TL,TR,BL,BR: tile index (relative to terrain_tiles) | hflip<<10 | vflip<<11 ; palette bits = 0
```
Buildings (HOUSE..HELIPAD) use team-color indices 13-15 for roofs/flags so the
engine can show ownership via palette banks.

## Overlay (BG, 16x16)
```c
enum { OV_NONE, OV_MOVE, OV_ATTACK, OV_FOG_UNUSED, OV_COUNT };
extern const u8  overlay_tiles[];  #define OVERLAY_TILE_COUNT
extern const u16 overlay_meta[OV_COUNT][4];
```
OV_NONE must be all-transparent. OV_MOVE / OV_ATTACK are designed to be
alpha-blended over terrain (solid-ish fill with a 1px lighter inner border).

## Units (OBJ, 16x16, 2 animation frames, facing RIGHT)
```c
enum { UG_SURVIVOR, UG_HEAVY, UG_MEDIC, UG_SNIPER, UG_PICKUP, UG_BUS, UG_ARMOR,
       UG_WALKER, UG_RUNNER, UG_BRUTE, UG_SPITTER, UG_BLOATER, UG_COUNT };
extern const u8 unit_gfx[UG_COUNT][2][128];  // 4 tiles each, 1D order TL,TR,BL,BR
```
UG_SURVIVOR..UG_ARMOR use `pal_survivor`, UG_WALKER.. use `pal_horde`.

## Boss units (OBJ, 16x16, 2 frames, facing RIGHT, pal_horde)
```c
enum { BOSS_ZERO, BOSS_COUNT };                 // append only
extern const u8 boss_gfx[BOSS_COUNT][2][128];   // same layout as unit_gfx
```
Kept out of `unit_gfx` so `OBJT_CURSOR` (right after the 96 unit tiles) does not
move. BOSS_ZERO is Patient Zero: pale, upright, cream hospital gown, glowing eyes;
frame 1 raises his arm with acid wisps. Free OBJ tiles for it: 116..123
(between the pointer and the portraits) or anywhere after the explosion frames.

## Cursor / small UI sprites (OBJ, pal_cursor)
```c
extern const u8 cursor_gfx[2][128];   // 16x16, 2 pulse frames, bracket corners
extern const u8 digit_gfx[10][32];    // 8x8 small HP digits 0-9, drawn bottom-right of units (outlined)
extern const u8 pointer_gfx[32];      // 8x8 menu hand/arrow pointing right
```

## Font + windows (BG, pal_ui)
```c
extern const u8 font_tiles[96][32];   // ASCII 32..127, 8x8, 1=text 2=shadow
extern const u8 window_tiles[9][32];  // 9-slice: TL,T,TR,L,C,R,BL,B,BR (fill uses idx 3)
```

## Portraits (OBJ 32x32, 16 tiles each in 1D row-major order)
```c
enum { PT_MARA, PT_DOC, PT_JUNE, PT_HOLT, PT_ZERO, PT_ZERO_BOSS, PORTRAIT_COUNT };  // append only
extern const u8 portrait_gfx[PORTRAIT_COUNT][512];
```
- MARA: survivor leader, ex-firefighter captain, 40s, short dark hair, soot, determined.
- DOC: Dr. Ellis Park, field medic, glasses, tired eyes, scrubs.
- JUNE: teen mechanic/scavenger, goggles on forehead, ponytail, grin.
- HOLT: grizzled ex-soldier, beard, beanie, scar.
- ZERO: "Patient Zero", the horde's eerie first infected, hospital gown, pale, glowing eyes.
- ZERO_BOSS: Patient Zero unmasked for boss fights: brows down, white-hot eyes, open
  jaw, spreading veins, acid halo. Speaker name is the same as ZERO.

Portrait palettes go in OBJ banks `4 + PT_*`. With 6 portraits banks 4..9 are full
(bank 10 is `OBJPAL_HORDE_DONE`), so a 7th portrait needs the engine's bank layout
to change first.

## Title screen (BG 240x160 4bpp, pal_title)
```c
extern const u8  title_tiles[];   #define TITLE_TILE_COUNT (<= 512 after dedupe)
extern const u16 title_map[20][30];   // screen entries (tile | flip bits)
```
Logo "DEAD WARS", ruined city skyline, moon, zombie silhouettes.

## Effects: drawn, export pending (proposal, not yet in assets.h)
Sprites are in `art/fx.txt`. They get C symbols once the `gfx.py` freeze lifts ("hardening merged")
and the engineer agrees the names. Every effect is 16x16, 4 tiles per frame, paced like the
explosion (BOOM_FRAMES 32 = 4 frames x 8):
| proposed symbol | frames | palette | use |
|---|---|---|---|
| `fx_rise_gfx[4][128]` | 4 | pal_horde | a survivor killed by a zombie rises as a Walker (frame 3 = UG_WALKER pose) |
| `fx_burst_gfx[4][128]` | 4 | pal_horde | Bloater bursts on death |
| `fx_burn_gfx[4][128]` | 4 | pal_cursor | nest burning: loop 0-2 during Burn, 3 = embers when destroyed |
| `fx_flag_gfx[2][128]` | 2 | pal_survivor | "secured" flag pop over a captured building |
| `vip_gfx[32]` | 1 (8x8) | pal_cursor | VIP star drawn at (px+9, py-3), poking above the unit's top-right, clear of HP digits, cargo and capture markers |

## Extras provided by gfx.py (additive, not required by the engine)
- `#define UG_FIRST_HORDE UG_WALKER`, `TERRAIN_TEAM_LIGHT/MID/DARK` (13/14/15).
- `font_tiles` char 127 (DEL) is a skull icon.
- `pal_title[1]`/`[2]` are white/black, so font tiles also read on the title palette.
- Art source format (text pixel grids, palettes, compose/derive, glyph sets) is
  documented at the top of `tools/gfx.py`. Review sheets: `build/preview/*.png`
  (`scene.png` / `scene_1x.png` is a mock battle map for readability checks).
