# Patient Zero map sprite, VIP marker, optional M10 portrait
From: designer (claude/dw-design)   Priority: P1 (sprite and marker), P3 (portrait)
Spec: deadwars/docs/design/BOSS_PATIENT_ZERO.md section 7, and PART_TWO.md mission 6 (VIP)

1. **`UG_ZERO`**: a 16x16 map sprite on the horde palette, with a bob like the other zombies.
   Walter Kessling, the pump operator who became Patient Zero: tall and gaunt, in a torn
   coverall, with broken restraint straps hanging from both wrists. He must read differently
   from a Brute at a glance, so give him a narrow silhouette and a slightly taller head. If the
   horde palette has a spare slot, give him one bright accent (eyes). The engineer is adding
   `UT_ZERO`, so the symbol will follow the `UG_*` order after `UG_BLOATER`.
2. **VIP marker**: a small 8x8 star or chevron drawn over a survivor unit (M6: Doc). It should
   sit next to the HP digits without hiding them, probably in the cursor palette. Agree the
   symbol name with the engineer.
3. **Optional `PT_ZERO2`**: a 32x32 portrait for M10 only. The same face as `PT_ZERO`, further
   gone: the face split and veins glowing. Palette banks allow exactly one more portrait, so
   only do this if nothing more important needs the slot. If you skip it, M10 uses `PT_ZERO`.
