# AW2 Remix map file format (v1)

Remixed campaign maps are plain text files in `tools/remix/maps/<id>_<slug>.txt`. `build_remix.py` turns each one into a new LZ77 terrain blob and a new predeployed-unit list, writes both to free space, and repoints that map's table entry. Only `*.txt` files directly in `tools/remix/maps/` are built. Subdirectories such as `maps/orig/` are ignored.

## Starting a map

Start from the export of the original map. Never start from scratch.

```
python3 -I tools/remix/export_map.py --orig              # (re)writes tools/remix/maps/orig/
cp tools/remix/maps/orig/139_border_skirmish.txt tools/remix/maps/
# edit tools/remix/maps/139_border_skirmish.txt, then:
python3 -I tools/remix/build_remix.py --only-maps 139 --start-map 139 --resolved-dir <scratch>/resolved
python3 -I tools/remix/boot_test_map.py "<scratch>/remix/Advance Wars 2 - Remix (test start 139).gba" <scratch>/t139 --map 139
```

`export_map.py --orig` writes these exports. `maps/orig/` is git-ignored, because it is data from the original game; regenerate it from your own ROM:

| file | map | campaign slot |
|---|---|---|
| `138_cleanup.txt` | 138 Cleanup, normal | normal mission 1 |
| `139_border_skirmish.txt` | 139 Border Skirmish, normal | normal mission 2 |
| `140_orange_dawn.txt` | 140 Orange Dawn, normal | normal mission 3 |
| `141_flak_attack.txt` | 141 Flak Attack, normal | normal mission 4 |
| `142_lash_out.txt` | 142 Lash Out, normal | normal mission 5 |
| `172_border_skirmish_hard.txt` | 172 Border Skirmish, hard | Advance Campaign mission 1 (slot of 138) |
| `173_orange_dawn_hard.txt` | 173 Orange Dawn (29×10), hard, no predeployed units | Advance mission 2 (slot of 139) |
| `174_andy_s_time_hard.txt` | 174 Andy's Time, hard | Advance mission 3 (slot of 140) |
| `175_mountain_ops_hard.txt` | 175 Mountain Ops, hard | Advance mission 4 (slot of 141) |
| `176_sea_for_all_hard.txt` | 176 Sea for All, hard | Advance mission 5 (slot of 142) |

### How the hard maps line up with the normal ones

The campaign flow table (`0x615194`, see BUILD.md) gives the Advance ("hard") Campaign its own nodes, 34–41, which play maps 172–179. Each of those nodes copies the world-map position of normal node 0–7.

- **By campaign slot:** 138 ↔ 172, 139 ↔ 173, 140 ↔ 174, 141 ↔ 175, 142 ↔ 176.
- **By name:** only Border Skirmish (139 ↔ 172) and Orange Dawn (140 ↔ 173) have a harder twin.
- **No hard version:** Cleanup, Flak Attack and Lash Out don't appear in the Advance Campaign.

Normal entries 138–145 have NULL hard pointers (+0x30/+0x38).

## File layout

A file has a header, then `[terrain]`, an optional `[tiles]` section, and `[units]`. A `#` starts a comment anywhere on a line; no map symbol uses `#`. Blank lines are ignored.

```
format     1
map        139            # map table index; must equal the file name prefix
variant    normal         # normal: entry +0x2C/+0x34 (+0x00); hard: +0x30/+0x38
name       Border Skirmish      # informational (map-name text is not patched)
size       15x10          # must equal the original map size
players    2              # informational (entry +0x18)
armies     OS BH          # informational (entry +0x40)
cos        Sami Flak      # optional: CO per player slot (entry +0x3C..); 'choose' = 0xFF
fog        off            # optional: on|off (entry +0x17)
day_limit  0              # optional: 0 = none (entry +0x24)
par_days   8              # optional: speed-rank par days (+0x20 normal / +0x22 hard)
events_checked yes        # optional, see "Event safety"
```

- **Applying keys:** each optional key is applied only when it is present. Exports always list them with the original values, so they rebuild as no-ops.
- **Keys shared by both variants:** `cos`, `fog` and `day_limit` live in the entry once. A normal file and a hard file for the same map must not disagree on them.
- **Not per map:** starting funds and funds per property are global; see ROM_NOTES §1.4.

### `[terrain]`: the symbolic grid (the part you edit)

The grid has one whitespace-separated token per cell, with `size` rows of `size` tokens each. Column alignment is only cosmetic. The exporter adds an `x:` ruler and `# y=N` row comments.

| token | terrain | token | terrain |
|---|---|---|---|
| `.` | plain | `~` | sea |
| `T` | wood | `,` | shoal (beach) |
| `^` | mountain | `%` | reef |
| `=` | road | `r` | river |
| `+` | bridge | `P` / `X` | pipeline / pipe seam |
| `S` / `s` | missile silo / used silo | `?` | special: keep/require the exact tile from `[tiles]` (BH structures, volcano, …) |
| `Hn` | HQ | `cn` | city |
| `bn` | base | `an` | airport |
| `pn` | port | `Ln` | lab |

For properties, `n` is the owner player slot. `0` means neutral and `1`–4 are slots in the entry's army order (slot 1 = OS in the campaign). Owners beyond `players` are rejected.

### `[tiles]`: exact graphic tiles (optional)

The game draws the tile IDs that are stored in the map. It never recomputes autotiles for roads, rivers, shores, shadows or mountain peaks (ROM_NOTES §1.2). The grid uses 3-hex-digit tile IDs in the same layout as `[terrain]`, and `---` means "pick automatically". For each cell, the builder decides as follows:

1. The symbol is `?`: the tile in `[tiles]` is used as-is and is required.
2. The tile's terrain and owner (from the tile table `0x0C1BC4`) match the symbol: the tile is kept.
3. The tile doesn't match, or is `---`: the tile is auto-picked. This covers editing `[terrain]` without touching `[tiles]`.
4. A neighbour's terrain changed: the cell is re-picked, but only if the cell still has its original tile, the builder judges that tile implausible next to the new neighbour, and it isn't a property.

Leaving out `[tiles]` entirely is allowed and is the easiest way to edit. Cells whose terrain type is unchanged keep their original tile, and everything else is auto-tiled.

**The autotiler** is learned at build time from all 187 stock terrain blobs. For each cell it picks the tile most often seen for the same terrain type and owner, with the same 8 neighbouring terrain types. If that context was never seen, it falls back to coarse neighbour classes, and then to a per-direction (naive Bayes) score.

- **Accuracy:** a leave-one-sample-out test on the stock maps reproduced 94.9% of cells exactly (measured before woods were restricted to single trees). Most misses are stylistic choices such as big-forest clump tiles or river variants.
- **Woods:** only single-tree tiles are picked automatically: 087, or 086 when shadowed from the west. If you want the clumped big-forest tiles 024–027/044–047/064–067, write them in `[tiles]`.
- **What it gets right:** road junctions and corners, river bends and mouths, shores, shoals, bridges, mountain peaks drawn into the tile above, and building and tree shadows (verified on screen in two test maps).
- **Checking the result:** every auto-picked cell is listed in the build log. `--resolved-dir DIR` writes the file back with the final `[tiles]` so you can pin what you like. Always check the screenshot from `boot_test_map.py`.

### `[units]`: predeployed unit list

```
# owner type         x   y   hp ammo fuel  ai
  P1    Tank          2   7  100    9   70   0
  P2    Infantry     12   3  100    0   99   3
  P2    MdTank        8   2  100  max  max   4
```

- **Required fields:** `owner type x y`. The rest default to hp 100, ammo 99, fuel 99 and ai 0, and `-` keeps a default.
- **Owner:** `P1`–`P4` (or `1`–`4`).
- **hp:** 1–100, where 100 is 10 displayed HP.
- **ammo / fuel:** raw bytes. `99` or `max` means full, because the game clamps to the unit type's maximum. Stock lists use 0 ammo for units without primary ammo.
- **ai:** byte 9, copied to unit+0xB. Its meaning is unknown (GUESS: hold/advance groups). Stock usage:

  | value | count | typical use |
  |---|---|---|
  | 0 | 1030 | all player units, some enemies |
  | 4 | 1002 | most enemy units |
  | 3 | 122 | mostly enemy Infantry/Mech |
  | 5 | 119 | |
  | 1 | 102 | |
  | 6 | 58 | |
  | 2 | 32 | APC/Recon, artillery |

  When adding enemies, copy the value used by a similar unit in a stock map.
- **Unit types:** `Infantry Mech MdTank Tank Recon APC Neotank Artillery Rockets AntiAir Missiles Fighter Bomber BCopter TCopter Battleship Cruiser Lander Sub`. Case-insensitive aliases also work, for example `inf md art rkt aa a-air msl bcop tcop bs`, the numeric type IDs, or the game's own names.
- **Raw bytes:** the normally zero bytes 3, 7, 8, 10 and 11 can be set with `b3=N`. No stock list uses them.
- **Order and grouping:** units are grouped by owner in ascending order (an `FE pp` header per owner, then `FF`). Within an owner, file order is kept. That order becomes the in-game unit slot order.
- **No list:** a line `none` gives a NULL unit-list pointer (no predeployed units, as in 173–176 hard). An empty `[units]` gives an empty list (just the terminator), like 145 Liberation.

The commented "overlay" grid in exports shows units as `<owner><letter>` on the terrain. It is regenerated on export and never parsed.

## Validation (the build fails on any error)

- `size` equals the original map. The map must also fit the 0xA14-byte map buffer.
- Every token is a known symbol. Every tile ID is a valid map tile (tile→terrain type ≠ 0). A tile never seen in a stock map gives a warning.
- Property and unit owners are ≤ `players`.
- Each unit is inside the map and on terrain its movement type can enter under clear weather (move-cost table `0x5D511C`, read from the ROM being built, so rebalanced data is respected).
- No two units share a tile. A player has at most 50 units.
- **Event safety:** the player's (non-Black-Hole) predeployed units must keep their position and type. Event scripts (+0x04/+0x08, not decoded) may refer to them; Cleanup's tutorial does.
  - Changing HP, ammo or AI is fine, and so is adding player units.
  - If you move, remove or retype one, playtest the mission with `--start-map` and `boot_test_map.py`, then add `events_checked yes` to turn the error into a warning.
- A player slot without an HQ gives a warning.
- Two files for the same map and variant, or conflicting shared header keys, are an error.
- After all steps the builder checks that every terrain pointer in the table is unique and decompresses, that +0x00 == +0x2C, and that the header and idle loop are untouched.

## How a map is written to the ROM

- **Terrain:** the decompressed layout `u8 w, u8 h, u16 tile[h][w]` is compressed with `aw2lib.lz77_compress`. That is BIOS LZ77 type 0x10; it is checked by decompressing it again. The result goes to free space at `0x620000`+, 4-byte aligned. The builder then repoints +0x2C and +0x00 (normal) or +0x30 (hard).
  - A hard file for an entry whose +0x30 equals +0x2C (172–179 and others) repoints only +0x30, which gives that entry separate normal and hard blobs, as many stock entries already have.
- **Units:** a fresh unit list is written to free space and the builder repoints +0x34 (normal) or +0x38 (hard).
- **What stays:** the old blobs stay in place, unused. Other entries and the event pointers are not touched.

## Round trip guarantee

The following command checks every stock map variant (213 of them):

```
python3 -I tools/remix/export_map.py --roundtrip
```

For each one it runs export → text → parse → rebuild, and the decompressed terrain and unit list must be byte-identical to the original, both with and without `[tiles]`. `test_remix.py` also builds a ROM from the 10 exported originals, reads the relocated data back through the new pointers, and checks it is identical.
