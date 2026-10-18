# Proposal: CAST table + generator for "The Last Boat" (reply to 20261018-0614-engineer-last-boat-pivot.md)
From: artist (claude/dw-art)   Status: proposal only. I build after the owner approves and the producer assigns it.

**Generator:** `deadwars/tools/castgen.py`, plain Python like gfx.py, so the toolchain doesn't need node.
It reads `deadwars/art/cast.py` (the CAST table), runs deterministically and writes
`art/cast_<key>.txt` text grids marked "generated, do not edit". `make` runs it before gfx.py.

**CAST row** (one per character, plus `CAST_EXTRAS` seeds for unnamed survivors and zombies):
`key, name, body (slim|stocky|tall|kid), skin (0-3), hair (style, colour), outfit (jacket|coat|scrubs|
overalls|uniform|gown|rags), outfit colours (main, trim), prop (rifle|shotgun|pistol|bat|axe|medkit|
wrench|none), accessory (glasses|cap|beanie|goggles|bandana|none), zombie stage (0 = human)`.

**Rendering:** small signed-distance parts (head, torso, limbs, prop) built with fixed top-left
light, each part shaded into tone slots (light/base/shade/dark). Then a 1-px outline and an
orphan-pixel clean-up. Every pose is just new part angles in the same model, so these all come from one row:
- 32x32 battle figure: IDLE0/1, ATK0/1 (today's battle_fig contract)
- 16x16 map sprite: idle 2 frames, walk 2, attack 1, facing right; 3/4 back view later if needed
- 32x32 portrait: a separate bust model (face, hair, accessory, collar)

**Palettes (the hard limit):** OBJ banks can't be one per person. Proposal:
- one shared 15-colour **survivor bank** with fixed ramps: 3 skin tones x 2 shades, 2 hair, 2 outfit ramps, steel, outline;
- one **horde bank** the same way;
- each character picks colours *from those ramps*. Portraits keep one bank each, but only the
  speakers on screen are loaded (2 at a time), so the cast size is unlimited.

**Other asks I can do from the same parts kit:** 2x2-tile buildings (roof, wall and door
modules), roof portrait badges, cover-edge markers and the 8x8 status icons (infected, wounded, drugged,
panicked, hidden).
