# AW2 Remix: balance pass

This is a modest, data-only balance pass for **Advance Wars 2 - Remix**. It is a community-style trim of the outliers plus small buffs for the weakest COs.

- **Scope.** The pass changes only the CO table, one unit price and 32 bytes of new modifier entries. It changes no code. The changes apply in every mode: campaign, versus and war room.
- **Implementation.** `tools/remix/balance.py`.
  - Each change is a frozen record that holds `before` and `after`.
  - `apply(rom, alloc)` checks every `before` value first, then writes. A second run, or a non-stock ROM, raises `ValueError` and nothing is written.
  - `verify(rom)` returns `[]` when every change is present.

```
import balance
log = balance.apply(rom, alloc)            # alloc(nbytes, align=4) -> ROM offset; called once for 32 bytes
assert balance.verify(rom) == []
python3 -I tools/remix/balance.py BASE.gba /scratch/out.gba   # stand-alone test (refuses paths in the repo)
python3 -I tools/remix/balance.py --table                     # print the record list as Markdown
```

To **revert** a change, delete its record from `CHANGES`. To **retune** one, edit its `after` value. Nothing else depends on these records.

## Changes

"Atk/def" are the additive % modifiers: damage = base × (100 + atk) / 100. "Luck" values are roll ranges: the game adds `rand % good` and subtracts `rand % bad`, so 10 means 0..9 %.

| # | target | before | after | in play | rationale |
|---|---|---|---|---|---|
| 1 | **Sturm** D2D, all units atk/def (also his unused COP block) | +20 / +20 | **+15 / +15** | 120/120 → 115/115 | Day-to-day 120/120 plus all-terrain movement is the most lopsided AW2 kit. His movement is part of his identity and is unchanged. |
| 2 | **Sturm** Meteor Strike, all units atk/def | +40 / +40 | **+30 / +30** | 140/140 → 130/130 | The 8 HP meteor is coded and stays. Only the stat spike is trimmed. |
| 3 | **Grit** Snipe Attack, indirect atk (range +2 kept) | +50 | **+40** | 150 % → 140 % | A 3-star COP with +2 range and 150 % indirects is the standard Grit complaint. |
| 4 | **Grit** Super Snipe, indirect atk (range +3 kept) | +50 | **+40** | 150 % → 140 % | Matches the COP. The +3 range is kept. |
| 5 | **Kanbei** unit cost, all power states | +20 % | **+30 %** | Infantry 1300, Tank 9100, Md Tank 20800, Neotank 28600 | 130/130 at only +20 % cost is top tier. His units stay elite, but he fields fewer of them. |
| 6 | **Hawke** COP stars (SCOP 9 kept) | 5 | **6** | Black Wave needs 1 more star | The coded 1 HP drain and heal across the map is the most repeatable swing power. Its HP values are code, so the price is the data-only lever. |
| 7 | **Colin** COP stars (SCOP 6 kept) | 2 | **3** | Gold Rush every 3 stars | Gold Rush (funds ×1.5, coded) at 2 stars snowballs the economy. His cheap units are unchanged. |
| 8 | **Sami** SCOP stars (COP 3 kept) | 8 | **7** | Victory March 1 star sooner | Victory March was rarely reachable. This is a small buff. |
| 9 | **Sonja** bad luck, all power states | 10 | **5** | bad roll 0..9 % → 0..4 % | Halves her signature drawback. Hidden HP, vision and counters (coded or flags) are unchanged. |
| 10 | **Flak** D2D good luck (bad 10 kept) | 15 | **20** | −9..+14 % → −9..+19 % | His variance pays off a little more often. His powers are unchanged. |
| 11 | **Battleship** cost | 28000 G | **25000 G** | with Kanbei: 32500 G | Navy is the weakest part of AW2. 25000 G is the Days of Ruin price. |

### Effect on the pilot missions (Orange Star 138–142)

- **Enemy:** Flak (138–141) gets change 10, which makes him slightly *harder*, in keeping with the remix. Lash (142) is untouched.
- **Player:** Andy and Max are untouched. Sami (139, 142) reaches Victory March one star sooner. That rarely matters within Lash Out's 7-day limit.
- **Later chapters:**
  - Nerfs to player-side COs: Grit, Colin and Kanbei.
  - Nerfs to enemy-side COs: Hawke and Sturm.
  - Small buffs: Sonja and Sami.
- **Net difficulty.** The pass is roughly neutral overall. If the late Black Hole missions feel softer because of changes 1, 2 and 6, compensate in the maps rather than here.

### Considered and left out

- **Grimm.** He is an Advance Wars DS CO and doesn't exist in AW2. AW2 has 19 COs, IDs 0–18.
- **Coded effects.** These are out of scope because they live in code:
  - HP effects: Hawke's 1/2 HP, Sturm's 8 HP meteor, Andy's repairs.
  - Colin's Gold Rush ×1.5 multiplier and Power of Money scaling.
  - Lash's terrain bonus.
  - Sonja's Counter Break.
- **Sturm's all-terrain day-to-day movement** (his move-table pointers are ALL1 for clear and rain). It is his identity, and the stat trim is enough for a "modest" pass.
- **Neotank and Bomber price increases.** In a very hard remix they would mostly weaken Black Hole's late-game armies.
- **Star counts above 10.** Sturm's 10-star SCOP is the largest stock meter, so stay at 10 or below. `apply()` asserts this.

## Verification (2026-10-18, stock sha1 `14dd0b22…`)

1. **Layout cross-check.**
   - The dumped values match known AW2 data. Examples: Nell luck 20/60/100; Flak 15/10, 40/20, 80/30; Kanbei 130/130 at +20 %; Colin −20 % cost / −10 atk; Grit +1/+2/+3 range; Hawke 5/9 and Sturm 0/10 stars; Hachi −10/−50 %; Sami capture +50/+1900.
   - The field semantics come from the decomp getters, listed under "Mechanics" below.
2. **Byte diff of the scratch build.** The only changes are:
   - in the CO table (`0x5D41F0…0x5D4E1C`)
   - Battleship cost (`0x5D624E`)
   - the 32 bytes returned by `alloc`

   The cartridge header and `0x36E00–0x36E40` are untouched. `dump_aw2.py cos/units` diffs show exactly the 11 intended changes. A second `apply()` raises and leaves the ROM unchanged.
3. **Emulator (headless mGBA via `gbadbg`).**
   - **Boot.** The `boot_cleanup.txt` run (power-on → new campaign → Cleanup → Options) is pixel-identical to the stock ROM.
   - **Versus setup.** New Versus game on Ball Islands. The P1 CO byte `0x020232DD` was poked on the stock ROM and on the balanced ROM:
     - **Build menus.** Kanbei base: Infantry 1300, Mech 3900, Recon 5200, Tank 9100, Md Tank 20800, Neotank 28600, APC 6500. Kanbei port: B Ship 32500 (stock 33600), Cruiser 23400. Andy port: B Ship 25000 (stock 28000).
     - **HUD power meter (small + big stars).** Hawke 6+3 (stock 5+4), Colin 3+3 (stock 2+4), Sami 3+4 (stock 3+5). Andy 3+3 on both.
     - **Grit COP.** With power state `0x020232DE` = 1, a freshly built Artillery shows fire range 2–5. That value is read from the new free-space entry and matches stock. Back in D2D it shows 2–4.
   - **Not exercised at runtime:**
     - the Sonja and Flak luck rolls
     - the atk/def percentages of the Sturm and Grit entries

     Both use the same getters and the same repointed entries whose reads were checked above.

## Mechanics learned (supplements ROM_NOTES §2.4 and §6)

- **Player structs.**
  - The array is at `*0x08499598` = `0x02023284`, `0x3C` bytes each, indexed by player 1..4. P1 is at `0x020232C0`.
  - Fields: `+0x08` funds, `+0x1D` CO ID, `+0x1E` power state (0 D2D, 1 COP, 2 SCOP; this selects the power block), `+0x26` player number.
  - P1 funds are therefore at `0x020232C8`. ROM_NOTES §6's `0x02023304` is P2's funds field.
- **Modifier slots** (`sub_080430B0` and the getters after it):
  - Foot units (domain 0) use **only** slot 0.
  - Every other unit uses domain slot + role slot.
  - The role comes from the unit's **min range** (`+0x0E`), not `+0x1B` (`sub_080432E0`): >1 → 6 indirect, 1 → 5 direct, 0 → 7 transport.
- **Luck.** `damage += rand % good − (bad ? rand % bad : 0)` (`0x08024D54`).
  - Good luck comes from `sub_08042E2C` (+0x0E). It defaults to 10 when `gUnknown_03003FC0[8] == 0`.
  - Bad luck comes from `sub_08042E84` (+0x10).
- **Cost.** `price = base × (100 + cost%) / 100` (`sub_08042C9C` → `sub_08043270`, block +0x14) for the current power state.
- **Capture.** `100 + capture%` (`sub_08042EDC`, block +0x16).
- **Counter %.** Block +0x12 (`sub_08042FC4`).
- **Stars.**
  - Charge needed = star value × stars: header +0x0C for the COP (`sub_080441D4`), +0x10 for the SCOP (`sub_08044208`).
  - Star value = `9000 × f(times used) / 100` (`sub_0804419C`).
  - Power activation (`sub_08044B28`) stores the state in player +0x1F and calls the block's +0x04 function.
- **Block +0x08 flags** (`sub_0804301C`).
  - 0x20 and 0x40 select doubled or plain terrain-star bonuses (`sub_08043304` / `sub_0804334C`). This is Lash: 0x40 for D2D and COP, 0x60 for SCOP. GUESS on the exact use.
  - Sonja's 01/09/0D and Eagle's 80 are other flags.
- **Header +0x08** is an s16 read by `sub_08042F34` (Drake = 7, everyone else 0; GUESS: rain-related). Header +0x06 is read by `sub_08042F7C` (0 for all).
- **Modifier entries `0x5D3BF0..0x5D3D30`.** Only `0x5D3BF0` (all zero) is shared across COs. Every other entry belongs to one CO, though several are shared between that CO's blocks. `balance.py` still writes fresh entries and repoints, as ROM_NOTES recommends.
