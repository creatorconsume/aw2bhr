# AW2 "very hard remix": ROM data notes

These notes cover Advance Wars 2: Black Hole Rising (USA), sha1 `14dd0b22c894865867aff89e8116b2dffae25605`.

- **Offsets.** All offsets are ROM file offsets unless written as a bus address (`0x08xxxxxx` = file offset + `0x08000000`).
- **Labels.** `gUnknown_XXXXXXXX` is the label the decomp (`aw2bhr/`) already uses for that address.
- **Confidence levels:**
  - **VERIFIED** means a copy of the ROM was patched, booted headless in mGBA, and the change showed up on screen. Watchpoints and breakpoints were also used to trace the code.
  - **DATA-MATCH** means the bytes match the published AW2 values exactly (from memory, or cross-checked against Commander Wars), but no runtime patch was tried.
  - **GUESS** is my inference only.

Tools are in `tools/remix/`. Run the Python scripts with `python3 -I`.

| file | purpose |
|---|---|
| `aw2lib.py` | Constants, the LZ77 decompressor and compressor, and readers for the map table, terrain, unit lists, unit stats and the text table. |
| `dump_aw2.py ROM [maps\|units\|cos\|terrain\|all] [ids]` | Prints maps (a terrain-type letter grid next to the raw tile IDs), predeployed units, unit stats, COs and terrain. |
| `make_test_roms.py ROM OUTDIR` | Rebuilds the verification ROMs t1–t6 and two `gbadbg` boot scripts: power-on → Cleanup map → Options menu (`boot_cleanup.txt`), and power-on → Cleanup → select Infantry (`boot_select.txt`). |
| `gbadbg.c` | The `gbarun` runner plus savestates, `dump`, `poke`, write/read watchpoints and breakpoints (libmgba 0.10). Build: `gcc -O2 -o gbadbg gbadbg.c -lmgba`. |

Boot path used for every runtime check: `boot_cleanup.txt` from `make_test_roms.py`. It runs: power-on → Start → Campaign → New → 218 A presses through the prologue and the Cleanup intro → B → the map is up at about frame 11,400 (a few seconds headless). Read watchpoints work on IO, but not on ROM or on the map buffers. Write watchpoints work.

---

## 1. Campaign maps

### 1.1 Map table: `0x5C77A0` (`gUnknown_085C77A0`), 192 entries × 0x5C bytes — VERIFIED

Map ID = index. The current map ID is held at `gUnknown_03003FC0+2` (the byte at `0x03003FC2`).

| ID range | content |
|---|---|
| 0 | dummy |
| 1–107 | Versus / Battle maps |
| 108–137 | War Room |
| **138–171** | **Normal campaign** (34 missions) |
| **172–179** | **Hard ("Advance") campaign replacements for the Orange Star chapter** |
| 180–191 | Design Room "Edit 1–3" slots. The loader special-cases IDs 0xB4–0xBF: `(id-0xB4) <= 0xB`. |

The table ends at `0x5CBEA0`; the war-room and versus unit lists follow it.

Entry layout. `v` is the campaign variant: 0 = normal, 1 = hard. `sub_0803866C` returns 1 when `gUnknown_03003FC0[1]==1` and save flag 0x60 is set (GUESS: hard campaign).

| off | type | meaning | conf |
|---|---|---|---|
| 0x00 | ptr | Terrain LZ77. Same value as +0x2C in every entry; update both when repointing. | VERIFIED |
| 0x04 | ptr | Event/trigger table: NULL-terminated list of event pointers, then a thumb function pointer. The events are 8-byte opcodes; `07 ff 00 00 ptr` runs a dialogue script and `05 00 00 00 fn` calls a function. Not decoded. Tutorial and dialogue triggers are here. | GUESS |
| 0x08 | ptr | Second script block (map start / conditions?). Not decoded. | GUESS |
| 0x0C | u32 | Always 0 in the campaign. | |
| 0x10 | ptr | Optional LZ77 OBJ graphics, decompressed to OBJ VRAM by `sub_0803FD80` (special structures, e.g. T Minus 15 and Show Stopper). | DATA-MATCH |
| 0x14 | u16 | Map name text ID (see §5). | VERIFIED |
| 0x16 | u8 | Unknown (1..0x12 in the campaign; music or intro?). | |
| **0x17** | u8 | **Fog of war flag** (nonzero → `gUnknown_03003FC0+0xD = 1`). | **VERIFIED** (t2 shows fog in Cleanup) |
| 0x18 | u16 | Number of players (2–4). | DATA-MATCH |
| 0x1A | u16 | Map category (1 = campaign, 2–6 = versus groups, 7 = war room, 8 = design). | GUESS |
| 0x1C,0x1E | u16 | Unknown (usually 1,1; Andy's Time and Duty & Honor have 2,2). | |
| **0x20 / 0x22** | u16 | **Speed-rank par days** (normal / hard). Day ≤ par → speed score 100 (`sub_080263A4`). | VERIFIED by code |
| **0x24** | u16 | **Day limit** (Lash Out 7, Test of Time 7, Two-Week Test 14, T Minus 15 15, Danger x 9 9; 0 = none). | DATA-MATCH |
| 0x26 | u8 | 0xFF | |
| 0x27 | u8 | Sequence/record index (campaign 0x05..0x2E, war room 0x2F..0x4C). | GUESS |
| 0x28 | u8 | 1 for the campaign and some war room maps. | |
| **0x2C + 4v** | ptr | **Terrain LZ77** for variant v. A hard (+0x30) value of 0 falls back to normal (`sub_080247A4`). | VERIFIED |
| **0x34 + 4v** | ptr | **Predeployed unit list** for variant v. 0 = no units; there is no fallback (`sub_080196C0`). | VERIFIED |
| **0x3C..0x3F** | u8[4] | **CO per player slot** (CO IDs in §2.4; 0xFF = player chooses). | DATA-MATCH; COs are copied into game state by `sub_080364F4` |
| **0x40..0x43** | u8[4] | **Army per slot**: 1 OS, 2 BM, 3 GE, 4 YC, 5 BH. A BH slot becomes CPU-controlled (`+0x39..` = 2) and every other army is human (`sub_080346FC`). | VERIFIED by code |
| 0x44..0x47 | u8[4] | Team per slot (allies share a number). | DATA-MATCH |
| 0x48..0x57 | 4 × (u8,u8,u16) | Per-slot flags OR-ed into the player struct (+0x2D/+0x2E), e.g. `03 04` in Andy's Time. | GUESS |
| 0x58 | u8 | Campaign chapter: 1 OS, 2 BM, 3 GE, 4 YC, 5 final. Selects the campaign UI/colours (`sub_08024984`). | DATA-MATCH |

Caveats:
- `sub_080206B0` reverse-looks-up a map ID by scanning every entry's +0x2C pointer. Keep each terrain pointer unique per map; don't point two maps at the same blob.
- The map buffer is filled by `LZ77UnCompWram` into `*gUnknown_03003F68` (= `0x02003010`). The loader allocates 0xA14 bytes (`sub_08014E44(0xA14)`), so keep `2 + 2*w*h <= 0xA14` (w·h ≤ 1289; the largest stock map is 46×28). For the campaign, stay at or below the existing sizes (≤ 30×20 is safe).

### 1.2 Terrain blob — VERIFIED

GBA BIOS LZ77 (type 0x10), compressed. Decompressed layout: `u8 width, u8 height, u16 tile[height][width]`, row-major. The tile IDs are *graphic* tiles: autotile variants for roads, rivers and shores, shadowed plains, and so on. Terrain type is looked up per tile:

- **Tile → terrain table: `0x0C1BC4`** (`gUnknown_080C1BC4`), u8[0x200]. Value = `terrain_type | owner<<5`. VERIFIED: it matches the in-RAM terrain-type grid at `0x0201F882` for every tile of Cleanup.
- Property tiles: `0x1C0 + 5*owner + kind`.
  - Kind: 0 HQ, 1 Base, 2 City, 3 Airport, 4 Port.
  - Owner: 0 neutral, 1..4 = player slot. Colour comes from the army at +0x40.
  - Labs (type 0x14) are `0x1D9 + owner`.
  - `0x1E0..0x1FD` duplicate `0x1C0..`.
  - Example: OS HQ in Cleanup = `0x1C5`; BH (slot 2) HQ = `0x1CA`.
- Tile catalog by terrain type (from the table; `*` = never used in a stock map):

```
Plain    (01): 001 003 004* 021 043 106* 107* 122* 123* 126* 127*   (021/043 = plain shadowed by wood/mountain/building)
River    (02): 018-01C 038-03D 058-05D 078-07D 098-09D 0B8-0BF 0D8-0DF 0F8-0FD 118-11D 138 139 (autotiles; 019 = horizontal, 078/05B = bends)
Mountain (03): 002 020 022 023
Wood     (04): 024-027 044-047 064-067 086 087 0A6 0A7 0C6 0C7 0E6*  (087 = typical single wood)
Road     (05): 040-042 060-062 080-085 0A0-0A5 0C0-0C5 0E0-0E5 100 101 104 105 124 125 144 145 164 165 (040 = vertical, 061 = horizontal, 080 = vertical shadowed)
Sea      (07): 008-00D 017 028-02D 033-035 037 048-04C 068-06C 088-08B 0A8-0AB 0C8 0C9 0E8-0EC 108-10C 128-12C (02A = open sea)
Bridge   (0C): 013 014 015 016 036
Shoal    (0D): 010 02E 02F 031 032 04F 050 051 053-055 06D 06E 075 08F 092 093 0AF 0B0 0B2 0B3 0B6 0B7 0CD-0D0 0D2 0D3 0D6 0D7 0EF 0F2 0F3 112 113
Reef     (13): 168
Pipeline (0F): 102 103 120 121 140-143 160 161 ; Pipe seam (10): 162 163
Silo (11): 180 ; used silo (12): 1A0 ; Minicannons (15-18): 182-185 ; Laser (19): 181 ; Cannon (1A/1B): 187 18A
Volcano (1C): 1A7 ; BH Factory (1D): 18D ; Deathray (1E): 190 ; Blocked (1F): 1AB 1AF ; BH filler (09): 186 188 189 ...
```

- Autotiling. The game does not recompute autotiles at map load; the stored IDs are drawn as-is. A remixed map must pick consistent variants, for example by copying neighbourhoods from stock maps. The decompressed tiles are copied to the live buffer at `0x0201EE72` and the type grid at `0x0201F882` (RAM addresses, for debugging).
- Recompression. `aw2lib.lz77_compress` (greedy, VRAM-safe) output is accepted by the BIOS (VERIFIED in t1).
- Relocation. To grow a map, write the new blob to free space and repoint +0x00 and +0x2C (+0x30 for hard).

### 1.3 Predeployed unit list — VERIFIED (parser `sub_080196F4`)

The list is a sequence of 12-byte records:

| bytes | meaning |
|---|---|
| `FE pp 00…` | Following units belong to player slot `pp` (1..4). Each player has 64 unit slots. |
| `FF 00…` | End of list (12 bytes). |
| unit record | see below |

Unit record fields:

| byte | field | notes |
|---|---|---|
| 0 | x | |
| 1 | y | |
| 2 | unit type | §2.1 |
| 3 | copied to unit+1 | always 0 |
| 4 | HP | 1–100; 100 = 10 displayed HP |
| 5 | ammo | 99 = max; otherwise clamped to the type's max |
| 6 | fuel | clamped to the type's max |
| 7, 8 | ignored | 0 |
| 9 | AI behaviour byte | 0..6, copied to unit+0xB; enemy units mostly 3, 4 or 5. Meaning: GUESS (hold/advance groups). |
| 10, 11 | ignored | 0 |

The lists are packed back to back (Cleanup `0x5CE1F0` is immediately followed by Border Skirmish `0x5CE25C`), so adding units requires relocating the list to free space and repointing +0x34. VERIFIED in t1:
- The BH Infantry at (12,3) became a Tank.
- The OS Infantry at (2,9) became a Md Tank.

### 1.4 Orange Star missions (normal campaign IDs 138–145; hard IDs 172–179)

| ID | name | entry @ | W×H | terrain LZ @ (len) | units @ (len) | COs P1/P2 | par days N/H | day limit | fog |
|---|---|---|---|---|---|---|---|---|---|
| 138 (0x8A) | Cleanup | 0x5CA938 | 15×10 | 0x1C7528 (154) | 0x5CE1F0 (108) | Andy/Flak | 8 | – | 0 |
| 139 (0x8B) | Border Skirmish | 0x5CA994 | 15×10 | 0x1C75C4 (168) | 0x5CE25C (168) | Sami/Flak | 8 | – | 0 |
| 140 (0x8C) | Orange Dawn | 0x5CA9F0 | 15×10 | 0x1C766C (166) | 0x5CE304 (216) | Max/Flak | 6 | – | 0 |
| 141 (0x8D) | Flak Attack | 0x5CAA4C | 15×10 | 0x1C7714 (131) | 0x5CE3DC (276) | Max/Flak | 8 | – | 0 |
| 142 (0x8E) | Lash Out | 0x5CAAA8 | 15×15 | 0x1C7798 (250) | 0x5CE4F0 (288) | Sami/Lash | 7 | 7 | 0 |
| 143 (0x8F) | Andy's Time | 0x5CAB04 | 18×15 | 0x1C7894 (299) | 0x5CE610 (48) | Andy/Flak | 12 | – | 0 |
| 144 (0x90) | Test of Time | 0x5CAB60 | 15×10 | 0x1C79C0 (186) | 0x5CE640 (252) | Andy/Flak | 7 | 7 | 1 |
| 145 (0x91) | Liberation | 0x5CABBC | 20×15 | 0x1C7A7C (268) | 0x5CE73C (12, empty) | choose/Flak | 17 | – | 0 |

The hard Orange Star maps (Border Skirmish, Orange Dawn 29×10, Andy's Time, Mountain Ops, Sea for All, POW Rescue, Test of Time, Liberation) use only the hard slots (+0x38 units). Missions 146+ keep the same IDs and use +0x30/+0x38 as their hard variants.

`dump_aw2.py ROM maps 138 139 140 141 142` prints the full grids and unit rosters. Commander Wars has human-readable copies of the same maps in `Commander_Wars/maps/advance_wars_2_campaign.camp/*.map`; the sizes match. Event scripts are separate from the unit lists, so check tutorial and dialogue events (+0x04) before moving the units they talk about.

Per-mission settings that live outside the map entry:
- **Starting funds are not per-map.** `sub_08026D68` zeroes funds and adds first-turn income.
- Funds per property = `gUnknown_03003FC0+0x28` (u32 at `0x03003FE8`). It is set to 1000 by the game-state defaults at `0x0803479E…0x080347E4`, where `movs r0,#250; lsls r0,#2` sits at ROM `0x0347E0` (VERIFIED by watchpoint). It is global to all modes.
- Weather has no map field that I found; it is presumably event or script driven.
- Win conditions use the default HQ/rout rule, plus the day limit at +0x24 and the event scripts.

---

## 2. Units, damage, terrain, COs

### 2.1 Unit table: `0x5D5ABC` (`gUnknown_085D5ABC`), 25 records × 0x5C — DATA-MATCH; move VERIFIED (t6)

Type IDs:

| ID | unit | ID | unit | ID | unit | ID | unit |
|---|---|---|---|---|---|---|---|
| 1 | Infantry | 7 | APC | 15 | Missiles | 21 | Battleship |
| 2 | Mech | 8 | Neotank | 16 | Fighter | 22 | Cruiser |
| 3 | Md Tank | 10 | Artillery | 17 | Bomber | 23 | Lander |
| 5 | Tank | 11 | Rockets | 19 | B Copter | 24 | Sub |
| 6 | Recon | 14 | A-Air | 20 | T Copter | | |

Slots 0, 4, 9, 12, 13 and 18 are unused copies (perhaps reserved for the DS units).

| off | field |
|---|---|
| 0x00 / 0x02 / 0x04 | u16 text IDs: name, primary weapon, secondary weapon |
| **0x06** | **u16 cost / 10** (Inf 100 = 1000 G … Battleship 2800) |
| 0x08 | u16 0 or 10 (0 for units with no primary weapon) |
| **0x0A** | **move** (VERIFIED: setting Infantry to 6 widens the move range) |
| 0x0B | max ammo |
| 0x0C | vision |
| 0x0D | unknown (1–5) |
| 0x0E / 0x0F | min / max attack range |
| 0x10 | max fuel |
| 0x11 | flag (unknown) |
| 0x14 | ptr: transport cargo list (APC 0x085D63B8, T Copter 0x085D63F4, Lander 0x085D6430, Cruiser 0x085D646C), else 0 |
| 0x18 | domain: 0 foot, 1 vehicle, 2 plane, 3 copter, 4 ship → CO modifier slots 0–4 |
| 0x19 | movement type: 0 inf, 1 mech, 2 tread, 3 tire, 4 air, 5 sea, 6 lander → move-cost row |
| 0x1A | class bitmask (1 foot, 2 tire, 4 tread, 0x10 air, 0x20 sea) |
| 0x1B | CO role: 1 foot, 5 direct, 4 indirect, 2 transport, 6 lander. Matches CO slots 5 = direct, 6 = indirect, 7 = transport. |
| 0x1C, 0x1D | unknown (weapon/sound class?) |
| **0x1E + d** | **primary weapon base damage vs defender type d** (u8, d = 0..25) |
| **0x38 + d** | **secondary weapon (MG etc.) base damage vs type d** |
| 0x54, 0x58 | ptrs (0x085D573C/575C ground, …77C/79C air, …7BC/7DC copter, …7FC/81C sea): GUESS anim/sound |

The damage chart is fully DATA-MATCH with AW2: Mech bazooka vs Tank 55, AA vs B Copter 120, Bomber vs Infantry 110, Rockets vs Infantry 95, and so on. The Md Tank and Tank cannons also carry Infantry/Mech values (50/45, 35/30); their MG values are higher. Print the chart with `dump_aw2.py ROM units`, or see `unit_stats()`.

### 2.2 Terrain types: `0x5D583C` (`gUnknown_085D583C`), 20 bytes × 32 types — DATA-MATCH

Record layout: `+0x00..0x0B` three gfx pointers, `+0x0C` u16 name text, `+0x0E` u16 description text, **`+0x10` u8 defence stars**.

| type | terrain | type | terrain | type | terrain | type | terrain |
|---|---|---|---|---|---|---|---|
| 1 | Plain | 6 | City | 11 | Port | 16 | Pipe seam |
| 2 | River | 7 | Sea | 12 | Bridge | 17/18 | Silo |
| 3 | Mountain | 8 | HQ | 13 | Shoal | 19 | Reef |
| 4 | Wood | 9 | BH filler | 14 | Base | 20 | Lab |
| 5 | Road | 10 | Airport | 15 | Pipeline | 21–31 | BH structures |

### 2.3 Movement costs — DATA-MATCH

`u8[7 movetypes][32 terrain types]`, 0xFF = impassable:

| address | table |
|---|---|
| `0x5D511C` | clear |
| `0x5D51FC` | snow |
| `0x5D52DC` | rain |
| `0x5D53BC` | all-1 (Sturm, Lash powers) |
| `0x5D549C`… | Sami powers |

Each CO power block chooses three of these tables (see below).

### 2.4 CO table: `0x5D3DD0` (`gUnknown_085D3DD0`), 19 × 0x104 bytes — DATA-MATCH; slot/move VERIFIED (t5)

CO IDs = index:

| ID | CO | ID | CO | ID | CO | ID | CO |
|---|---|---|---|---|---|---|---|
| 0 | Nell | 5 | Grit | 10 | Sturm | 15 | Hachi |
| 1 | Andy | 6 | Kanbei | 11 | Flak | 16 | Colin |
| 2 | Max | 7 | Sonja | 12 | Lash | 17 | Jess |
| 3 | Olaf | 8 | Eagle | 13 | Adder | 18 | Sensei |
| 4 | Sami | 9 | Drake | 14 | Hawke | | |

The names are text IDs 0xA17+i. These are the IDs used in map +0x3C.

Header:

| off | field |
|---|---|
| 0x00 | u32 name text |
| 0x04 | u32 portrait ID? |
| **0x0C** | **u32 COP stars** |
| **0x10** | **u32 SCOP stars** (Andy 3/6, Sami 3/8, Eagle 3/9, Hawke 5/9, Sturm 0/10 — all match AW2) |
| 0x15 | army group |
| 0x16 | army (1 OS … 5 BH) |
| 0x17, 0x18 | unknown |
| 0x1C | thumb fn (default 0x0805C1C5; Andy 0x0805C209, Olaf 0x0805C269, Eagle 0x0805C1D9, Drake/Hawke 0x0805C1C1) |
| 0x20.. | quote and bio text IDs |

Three power blocks follow, each 0x44 bytes: **D2D at 0x38, COP at 0x7C, SCOP at 0xC0**.

| block off | field |
|---|---|
| 0x00 | u32 power name text (0 for D2D) |
| 0x04 | **effect function pointer** (default `0x080443D9`) |
| 0x08 | flags byte: Sonja 01/09/0D, Hachi SCOP 02, Lash 40/60, Eagle 80 |
| 0x0C | vision bonus? (Sonja 1/2/2) |
| **0x0E** | **s16 good luck %** (Nell 20/60/100, Flak 15/40/80; others 10) |
| **0x10** | **s16 bad luck %** (Flak 10/20/30) |
| 0x12 | s16 counter-attack bonus % (Kanbei SCOP 50, Sonja 50) |
| **0x14** | **s16 unit cost %** (Kanbei +20, Colin −20, Hachi −10/−50) |
| 0x16 | s16 capture bonus (Sami 50; Victory March 0x76C = instant) |
| 0x18 / 0x1C / 0x20 | ptrs: movement-cost tables for clear / snow / rain |
| **0x24..0x40** | **8 pointers to modifier entries**, one per slot: 0 foot, 1 vehicle, 2 air, 3 copter, 4 sea, 5 direct, 6 indirect, 7 transport |

Each modifier entry is 8 bytes: `s16 atk%, s16 def%, s16 move, s16 range`. The entries are shared and live at `0x5D3BF0..0x5D3D30`; `0x5D3BF0` = all zero. Examples:
- Max D2D: slot 5 → +20 atk, slot 6 → −10 atk and −1 range.
- Kanbei D2D: slots 0–4 → +30 atk / +30 def.
- Andy Hyper Upgrade: slots 0–4 → +20 atk, +1 move.

VERIFIED: pointing Andy's D2D slot 0 at `0x5D3C10` (+1 move) gave infantry a 4-tile range.

To rebalance, write new 8-byte entries to free space and repoint the slots. Don't edit the shared entries in place, because several COs share them.

**Hard-coded effects (code, not data).** These are the special power functions, as power-block +0x04 pointers:

| CO | power | function |
|---|---|---|
| Olaf | snow immunity (D2D/COP) | `0x080443ED` |
| Olaf | Winter Fury | `0x08044C45` |
| Sonja | all blocks | `0x080447ED` |
| Sturm | Meteor Strike | `0x08044A89` |
| Drake | Tsunami | `0x08044C81` |
| Drake | Typhoon | `0x08044CBD` |
| Hawke | Black Wave | `0x08044CF9` |
| Hawke | Black Storm | `0x08044D35` |
| Hachi | Barter / Merchant Union | `0x0804483D` |
| Colin | Gold Rush | `0x08044805` |
| Sensei | COP | `0x08045449` |
| Sensei | SCOP | `0x08045461` |

Andy's Hyper Repair, Grit's ranges and similar use the default function. HP healing, and the global power +10% defence (GUESS) or star charge rate, are in code that I haven't located. HP/damage numbers inside the special functions (e.g. Hawke drain, Sturm meteor 8 HP) would need code patches.

---

## 3. Options and speed

### 3.1 Battle animation ("Visual") — VERIFIED (t3)

Options menu → `Visual A / Visual B / Visual C / No Visual`. The stored value is 1 = A (all animations), 2 = B (battle animation only), 3 = C (player battles only), **0 = No Visual**.

- The save options struct is at `*gUnknown_0808E53C` = `0x0200C420`; byte +0x0E = Visual. It is copied to `gUnknown_03003FC0+9` (`0x03003FC9`) at `0x08034810`.
- The new-save default is set in `sub_08016A54`: **ROM `0x016A6A`: `01 22` (`movs r2,#1`) → change to `00 22` for "No Visual" by default.** After that patch, a fresh save shows "No Visual" in the in-battle Options menu.
- An existing save keeps its own setting.

### 3.2 Unit move speed on the map — VERIFIED (t4)

- `sub_08035B00(id)` returns the per-frame move speed from **`0x49CD88` (`gUnknown_0849CD88`), 25 × 36-byte map-sprite records, s16 at +0x18** (= 2 px/frame for every unit).
- **If Visual ≠ A (value ≠ 1), the speed is doubled** (`lsls r0,#17; asrs r0,#16` at `0x08035B1A`). The "No Visual" default therefore already moves units at 4 px/frame. VERIFIED with a watchpoint on the moving unit's X at `0x0200D796`.
- For more speed, set +0x18 to 4 or 8; t4 used 8 and got 8 px/frame in Visual A, 16 otherwise. Alternatively change `0x035B1A` from `40 04` to `80 04` (×4) — GUESS, untested.
- The step code is at `0x080362E0`. It handles values that don't divide 16 by carrying the remainder, but powers of two are safest.

---

## 4. Free space — VERIFIED

`0x617EE4 – 0x7FFFFF` (≈1.9 MiB) is solid `0xFF`. It is the tail of the decomp's last incbin, `gUnknown_086173F0`, `0x6173F0..0x800000`; the non-FF bytes before `0x617EE4` are real data. There is no other large 0x00/0xFF run.

Recommendation: put remix data at `0x620000+`. t1 used `0x620000` for the recompressed Cleanup terrain and it booted fine. The ROM can also be extended to 16/32 MiB if ever needed.

---

## 5. Text

- **Text table: `0x610A38` (`gUnknown_08610A38`)**, u32 pointers to NUL-terminated ASCII strings, IDs 0..~0xCF3.
- Control bytes seen: `0x0D` newline, `0x0E` pause/wait, `0x0F` page.
- Map names start at 0xA34 (campaign 0xABD..; hard Orange Star 0xAC5..). CO names start at 0xA17; unit names at 0x8BA..; terrain names at 0x92C..
- To change text, write a new string to free space and repoint its slot.

## 6. Useful RAM (for debugging, not patching)

| address | content |
|---|---|
| `0x03003FC0` | game state: +1 mode, +2 map ID, +9 Visual, +0xD fog, +0x28 funds/property |
| `0x02003010` | decompressed map |
| `0x0201EE72` | live tile grid |
| `0x0201F882` | terrain-type grid |
| `0x02023300` | player structs (`*gUnknown_08499598`, 0x3C bytes each); funds at +8 → P1 funds `0x02023304` |
| `0x0200C420` | save options |
| `0x0200C2D0` | campaign records (8 bytes/map: normal, hard) |

## 7. Recommended patching approach

1. Write a Python patcher (reuse `aw2lib.py`) that starts from the sha1-checked original ROM and applies a declarative spec. Don't edit the decomp sources: `make compare` must keep matching the baserom. Treat the remix as a post-build patch, or emit a BPS/IPS from the patched ROM.
2. Maps (138–142 for the pilot):
   - Decompress the terrain, edit tiles using IDs copied from stock maps so the autotiles stay consistent, recompress, and append to free space at ≥ `0x620000`.
   - Repoint entry +0x00 and +0x2C, keeping each pointer unique.
   - Build new unit lists (`FE pp` / 12-byte records / `FF`), append them, and repoint +0x34.
   - Optionally change +0x3C COs, +0x17 fog, +0x20 par days and +0x24 day limit.
   - Leave +0x04/+0x08 (events) alone unless needed. Check Cleanup's tutorial triggers in game: they refer to specific starting units.
3. Balance:
   - Edit the unit table bytes directly (cost/10, move, ammo, fuel, damage arrays).
   - For COs, edit luck/cost/capture fields in place. For stat modifiers, allocate new 8-byte modifier entries in free space and repoint the slot pointers rather than editing the shared entries.
4. Quality of life: `0x016A6A` → `00` (No Visual by default, which also gives 2× map move speed). Optionally raise the `0x49CD88+36*t+0x18` speeds.
5. Verify each build headless with `tools/remix/gbadbg` and `make_test_roms.py`'s boot script: power-on to the Cleanup map takes about 11,400 frames, then screenshots.

## 8. Open questions / not found

- Event script format (+0x04/+0x08), weather setup, and the per-mission "special" win conditions.
- AI behaviour byte semantics (unit record byte 9).
- Where power-star charge, the global power defence bonus and HP-restoring effects live.
- Full semantics of the CO setup in `sub_080364F4`, which handles 0xFF = "player chooses".
- How the campaign chooses the next map ID (flow table not located). Campaign records are indexed `(id-0x8A)` for IDs 0x8A..0xB3.
