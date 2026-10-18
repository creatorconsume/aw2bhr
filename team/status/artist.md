# Artist status

- **Session:** session_01YDdSYUkiJH2qD5sNiJiBop
- **Branch:** claude/dw-art @ c4ba564 (plus this status commit)
- **Current focus:** waiting. Need "hardening merged" to export fx/vip/night preview in gfx.py, and the engineer's pick (boss_gfx vs UG_ZERO).

## Last done
- Onion box art: deadwars/boxart/deadwars.png, aw2remix.png (250x360, `python3 -I boxart/boxart.py .` from deadwars/).
- Patient Zero redrawn to the designer's spec: BOSS_ZERO map sprite (boss_gfx) + PT_ZERO_BOSS portrait (M10).
- Effects drawn in art/fx.txt: rise, Bloater burst, nest burn, secured flag, VIP star (export after the freeze). Look at build/preview/units.png, portraits.png, scene.png.

- Night audit (mock replicating palettes_apply on the scene): fog vs visible is clearly different
  at night, and HP digits read on every terrain. Walkers and Brutes were low-contrast on dark grass, so
  I lightened the horde skin. The Bloater is still muted but readable. No blend-constant change needed.
- MT_HOUSE_LOOTED redrawn so it reads as "searched" at 1x (look at terrain.png and scene_1x.png).

## Next
- Night/fog readability audit (gbarun night shots); HP digits; looted/damaged building variants.
- Effects: infection rise, nest burn, capture flag, Bloater splash (after hardening merged; reuse BOOM_FRAMES timing).

## Blockers
- Hookup for boss_gfx/PT_ZERO_BOSS needs an engineer answer (inbox/engineer/20261018-0143-artist-patient-zero-gfx.md).

## Inbox done
- `20261018-0142-designer-patient-zero-art.md` -> UG_ZERO art redrawn to spec (coverall, straps, narrow, taller head, acid eyes) as BOSS_ZERO in art/boss.txt; VIP marker fx_vip in art/fx.txt; PT_ZERO_BOSS portrait reworked (split face, glowing veins). Export name (boss_gfx vs UG_ZERO) awaiting the engineer.
