# Boss: Patient Zero (`UT_ZERO`)

Status: design spec. The engineer request is in
`team/inbox/engineer/20261018-0141-designer-part-two-mechanics.md`, and the art request is in
`team/inbox/artist/20261018-0142-designer-patient-zero-art.md`. He appears in Mission 10 (see
`PART_TWO.md`). Missions 4, 5, 6, 7 and 9 use him only as a voice (`PT_ZERO`).

## 1. Who he is

Walter Kessling, 54, pump operator at Cedar Falls Reservoir Pumping Station 2. He was admitted
to St. Agnes with a fever and a bite he couldn't explain, broke his restraints on day one and
walked home. The horde follows him like a hive: Doc calls it "the link". He isn't evil, just in
pain, and he wants everything quiet. In the last fight, his lines move from menace to
exhaustion ("it always hurt").

## 2. Stats

| Field | Value | Notes |
|---|---|---|
| name | "Zero" | The info panel shows the name: 4 letters, so it fits. |
| cost | 20000 | Only used for the AI's value maths and the kill score. Not recruitable. |
| move / movetype | 3 / MOVE_ZOMBIE | Night adds +1, like every zombie. |
| vision | 3 | |
| range | 1–1 | Direct, so he takes and deals counterattacks. |
| flags | `UF_BOSS` (new) | Must not be `UF_INFANTRY`, `UF_INDIRECT` or `UF_EXPLODES`. |
| hp | 100 | Shown as 10 like everything else. Toughness comes from the damage table and regeneration. |

### Damage (`base_damage`, percent at full HP)

Zero's row (as attacker):

| SUR | HEA | MED | SNI | PIC | BUS | ARM |
|---|---|---|---|---|---|---|
| 95 | 75 | 100 | 100 | 70 | 85 | 50 |

Zero's column (as defender):

| SUR | HEA | MED | SNI | PIC | BUS | ARM |
|---|---|---|---|---|---|---|
| 10 | 35 | 3 | 40 | 25 | 0 | 50 |

Zombies never hurt zombies, so all horde rows are 0.

## 3. Abilities

Each ability is listed in the order the horde turn resolves it.

1. **Hive Link (regeneration).** At the start of each horde turn, Zero heals
   `MIN(30, 5 × nests_on_map)` HP, capped at 100. With 8 nests he heals 30 and with 2 nests
   he heals 10. With 0 nests he doesn't regenerate at all. This is the core counterplay hook:
   *burn nests first.*
2. **Call (night).** On each night horde turn, after the nests spawn, he summons 2 zombies on free
   adjacent tiles. Each is a Walker, or a Runner with 50% chance, and fresh spawns act next turn
   (`moved = 1`). They use the same spill rule as `spawn_at_nests`.
3. **Aura.** Every *other* horde unit within Manhattan distance 2 of Zero gets +15% attack
   (it stacks with night, so +30% at night) and +1 defensive terrain star. Zero himself gets neither.
4. **Raise.** A survivor infantry unit killed by *any* zombie within 2 tiles of Zero (the
   victim's tile is what counts) rises as a **Runner** at 80 HP instead of a Walker.
5. **Leash (AI).** By day, Zero considers only destinations within distance 3 of his start
   tile, unless a survivor is already within his move+attack reach. By night there is no leash:
   he hunts using the normal field. When he has no target, he returns toward his start tile
   rather than shambling toward buildings. He never captures buildings.
6. **Immunities.** He is never dormant (ignore the placement flag). Bloater bursts don't hurt him.
7. **Death: the link breaks.** When Zero dies, every other horde unit takes 30 HP (never below
   1 HP, like a Bloater burst), the "The link is broken." banner plays, and with `GOAL_BOSS`
   the mission is won.

### Phases and HP thresholds

- **Above 50 HP:** normal.
- **First time he drops to 50 HP or below:** play `boss_half` dialogue (nice to have:
  `Mission.boss_half`, a `Line *`), and **Frenzy** for the rest of the mission: his move becomes 4,
  plus 1 at night. That makes it a race: a wounded Zero comes out to fight, which is exactly
  when the player can reach him on open ground.

## 4. Goal: `GOAL_BOSS`

- **Win:** no living `UT_ZERO`. The rest of the horde doesn't need to be cleared.
- **Lose:** the usual safehouse or wipe rules, or `goal_days > 0 && day > goal_days`.
- **Objective screen:** show "Zero HP x/10" and "Nests: n", because the player must be able to
  read the regen state (fairness rubric: threats are telegraphed).

## 5. Counterplay (for the player and the sim bot)

- **Burn the nests.** Each nest burned takes away 5 HP/turn of regen and one spawn source.
- **Bait him at night.** He leaves the crater to hunt, and on plain (1 star) a full three-unit
  strike (Armor 45, Sniper 36, Heavy 31, about 112) can kill him in a single turn.
- **Kill his escorts first.** The aura makes Brutes and Spitters near him much stronger.
- **Don't feed him.** Infantry that die near him rise as Runners. Keep Survivors and Medics
  back, and send Heavies and Armor forward.
- **Sniper.** Range 2–3 lets you chip at him without a counterattack. With 4 nests left the
  regen is 20/turn, so a Sniper alone can't out-damage it. That is intentional.

## 6. Dialogue

The lines live in `PART_TWO.md`, and they also use `PT_ZERO` in red text (already supported
by `dialogue()`).

## 7. Art

- **Map sprite** `UG_ZERO`, 16x16, horde palette. A tall, gaunt figure in a torn pump-station
  coverall, with broken restraint straps hanging from both wrists, and a bob animation like the
  others. He must read as different from a Brute at a glance, so use a narrower silhouette and
  a 1px taller head. If the horde palette has room, add one bright accent (eyes).
- **Optional portrait** `PT_ZERO2`, 32x32, used in M10 only: the same face as `PT_ZERO`, but
  further gone, with the face split and veins glowing. Palette banks allow exactly one more
  portrait (see `team/roles/designer.md`). If this slot is better spent elsewhere, M10 uses
  `PT_ZERO`.
- **Aura hint (nice to have):** horde units inside the aura flash in the attack-range preview.
  This is engine UI, not art.

## 8. Audio (request to follow once M10 is playable)

A boss theme for M10, or a variant of the night theme. Also an SFX sting for Call and a
heavier death sting for "the link is broken".
