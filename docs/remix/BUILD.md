# Building "Advance Wars 2 - Remix"

`tools/remix/build_remix.py` builds the remix from the user's original ROM. Run it with `python3 -I`; it needs only the standard library plus the local modules.

The original ROM is AW2: Black Hole Rising (USA), sha1 `14dd0b22c894865867aff89e8116b2dffae25605`. The build writes three files:

| file | content |
|---|---|
| `<scratch>/remix/Advance Wars 2 - Remix.gba` | the playable ROM, under a different file name so it gets its own `.sav` |
| `<scratch>/remix/aw2-remix.bps` | a BPS patch: original → remix |
| `<scratch>/remix/Advance Wars 2 - Remix.build-log.txt` | every change, every free-space block, warnings |

`<scratch>` is `$AW2_SCRATCH`, which defaults to this session's scratchpad. The base ROM is taken from `$AW2_BASE_ROM` or `<scratch>/rom/`. The builder **refuses to write ROMs, patches or resolved maps inside the repo**.

## Usage

```
python3 -I tools/remix/build_remix.py                       # full release build + BPS
python3 -I tools/remix/build_remix.py --only-maps 139,141   # only these map files (also 138-142, 'none')
python3 -I tools/remix/build_remix.py --start-map 141       # TEST ROM: Campaign > New starts at 141
python3 -I tools/remix/build_remix.py --resolved-dir DIR    # also dump auto-tiled map files
python3 -I tools/remix/test_remix.py                        # self-tests (~3 s)
python3 -I tools/remix/export_map.py --orig                 # re-export the original maps
python3 -I tools/remix/bps.py apply BASE.gba aw2-remix.bps OUT.gba
```

When several agents build at the same time, each should pass its own `--out` (plus `--no-bps`) inside its scratch directory, so they don't overwrite the shared release files.

Other options:

| option | effect |
|---|---|
| `--rom` | base ROM to build from |
| `--out` | output ROM path |
| `--bps` / `--no-bps` | write the patch to this path / skip the patch |
| `--maps-dir` | read map files from another directory |
| `--no-balance` | skip `balance.py` |
| `--qol` | also apply the speed and animation patches (off by default; the stock Options menu already offers Visual A/B/C/Off, and anything but A doubles map speed) |
| `--visual N` | new-save default animation: 0 = No Visual (default), 1 = A, 2 = B, 3 = C |
| `--move-speed N` | map move speed in px/frame (default 4) |
| `--alloc-start` | first free-space offset handed out (default `0x620000`) |
| `--strict` | treat warnings as errors |

## Pipeline

1. **Load the base ROM.** Check its sha1 and copy it into a `bytearray`.
2. **Set up the free-space allocator.** `FreeSpace` is a bump allocator over `[0x620000, 0x800000)`, 4-byte aligned by default, and all patch steps share it.
   - **Range:** that is ≈1.9 MiB of `0xFF` fill. It starts at `0x620000` rather than `0x617EE4` to leave a margin after the last real data of the incbin that ends there (ROM_NOTES §4).
   - **Contract:** `alloc(nbytes, align=4)` returns a **ROM file offset** (add `0x08000000` for a pointer), and `alloc.put(data, align=4, label=...)` allocates and writes in one call.
   - **Checks:** every block is checked to be `0xFF` when it is handed out, and every allocation is labelled in the build-log audit.
3. **QoL patches.** Each patch first checks the expected original bytes.
   - **Battle animations off for new saves.** `0x016A6A` changes from `01 22` to `00 22` (`movs r2,#0` = "No Visual"). An existing save keeps its own setting. The remix has a different file name, so it starts with a fresh save.
     - VERIFIED: the release ROM's in-map Options menu shows "No Visual".
   - **Faster map movement.** The s16 at `0x49CD88 + 36*t + 0x18` goes from 2 to **4** for all 25 unit types. The game doubles it when Visual ≠ A, so with No Visual a unit moves **8 px/frame**: 2 frames per tile, against 4 px/frame for stock No Visual and 2 px/frame for stock Visual A.
     - **How it was chosen:** frame-by-frame captures of a Tank moving 4 tiles on Orange Dawn.

       | `--move-speed` | px/frame (No Visual) | frames for 4 tiles | result |
       |---|---|---|---|
       | 2 (stock) | 4 | 16 | |
       | 4 | 8 | ~7 | half-tile steps; still reads as motion |
       | 8 | 16 | ~3 | jumps a whole tile per frame, looks like teleporting; rejected |

     - Players who switch back to Visual A get 4 px/frame.
     - Not separately checked: whether the enemy-turn movement uses the same table (expected, since it calls the same `sub_08035B00`).
4. **Balance.** If `tools/remix/balance.py` exists, the builder calls `balance.apply(rom, alloc)` and appends the returned log lines. If the module also has `verify(rom)`, any problems it reports fail the build.
5. **Maps.** Each `tools/remix/maps/<id>_<slug>.txt` is validated, auto-tiled, LZ77-compressed and written to free space, and its entry is repointed. The format and the validation rules are in [MAP_FORMAT.md](MAP_FORMAT.md).
6. **`--start-map` (test ROMs only).** See below.
7. **Final checks.** The build fails if any of these does not hold:
   - The ROM size is unchanged.
   - The cartridge header `0x000000-0x0000C0` is unchanged: title, game code `AW2E`, checksum. mGBA's Flash512 save override and its idle-loop setting are keyed on the game code.
   - `0x036E00-0x036E40` is unchanged (the idle loop at `0x08036E08`).
   - For every map entry, +0x00 == +0x2C.
   - Every terrain pointer in the 192-entry table is unique and decompresses to a valid map.

   An audit then lists every changed byte range with its allocation label. Changes outside free space are labelled "in place".
8. **Write the ROM and BPS.** The patch is decoded again with our own decoder and must reproduce the output sha1; otherwise the build fails.

## BPS patch

`tools/remix/bps.py` is a BPS encoder and decoder (the beat format used by Flips, beat, Rom Patcher JS and others).

- **Encoder:** a linear diff, which suits ROM hacks: `SourceRead` for unchanged runs of 4 bytes or more, `TargetRead` literals for the rest. It writes the source, target and patch CRC32s.
- **Decoder:** implements all four actions (SourceRead, TargetRead, SourceCopy, TargetCopy, including overlapping TargetCopy) and checks the CRCs.

Verification:

- **Every build:** `decode(base, patch)` must give the output sha1.
- **Independent cross-check, done once.** The independent python-bps 5 (PyPI) was run from a scratch venv, with a `time.clock` shim for Python 3.13:
  - `bps-validate` accepts `aw2-remix.bps`.
  - Its applier reproduces sha1 `93ab575c…` of the remix built at that time.
  - Our decoder applies a python-bps patch that uses SourceCopy, and python-bps applies our patch for a target that grows.
- **Tests:** `test_remix.py` includes a hand-made patch exercising all four actions, plus random shrink/grow round trips.

For the Miyoo Mini Plus, there are two ways to get the ROM onto the device:

- Apply the patch on a PC (Rom Patcher JS, Flips, or `bps.py apply`) and copy `Advance Wars 2 - Remix.gba` to `Roms/GBA/`.
- Or copy the built ROM directly.

The original ROM stays playable under its own name.

## Test method: start any campaign mission directly (`--start-map`)

**Problem:** missions 139–142 sit behind earlier missions. **Solution:** a data-only patch of the campaign flow table, applied to test ROMs only.

### The campaign flow table

The table is `gUnknown_08615194` at file offset **`0x615194`**: 42 nodes of 0x30 bytes, where node *i* plays map 138 + *i*. Nodes 0–33 are the normal campaign (138–171) and nodes 34–41 are the Advance Campaign (172–179).

`sub_08077F30` and `sub_08077F9C` read the node index from `gUnknown_0202FDFC+0xC` and store `node+0x00` into the current map ID byte `0x03003FC2`. This was VERIFIED by a write watchpoint, which fires at `pc 0x08077FB8` when the world-map A press selects the mission.

| node offset | field |
|---|---|
| +0x00 | u16 map ID |
| +0x02..+0x05 | mission flags/difficulty (normal 1/1, hard 2–4) (GUESS) |
| +0x06 / +0x08 | world-map x / y (hard nodes 34–41 copy nodes 0–7) |
| +0x0C | pointer to a world-map struct (shared by node *i* and 34+*i*) (GUESS) |
| +0x10 | per-mission index (GUESS) |
| +0x14 | per-mission data/script pointer (GUESS) |
| +0x24 / +0x28 | optional normal/hard function and script pointers (final missions) |

### The patch

- `--start-map ID` copies node (ID−138) over node 0 but keeps node 0's +0x06..+0x0F (world-map position and the +0x0C struct).
- After the prologue, the world-map cursor sits on the mission flag, and the first A press selects mission ID with its **real map ID**, its own map entry, events and intro dialogue.
- Because the game uses the real map ID, records and hard-coded map-ID checks (`sub_08034CD4`: tutorial hooks for 0x8A/0x8B/0x8D/0x8F) should behave as in a normal playthrough of that mission. This is inferred from the code.
- Observed on screen: the correct intro dialogue, CO, funds, day limit and fog.
- After the mission, the campaign should continue from node 1 (not tested).

An earlier idea, copying the 0x5C map entry over slot 138, was rejected. The game would run with map ID 138, so Cleanup's map-ID-specific code and records would apply to the wrong map.

For Advance-Campaign maps 172–179, the test ROM also copies the hard unit list (+0x38) into +0x34 and the hard par into +0x20, because New Campaign plays in normal mode. This shows the hard map, units and fog (172 boots fine). The hard-mode flag is not set, so it is not a faithful hard-campaign playtest.

### Booting a test ROM: `tools/remix/boot_test_map.py`

```
python3 -I tools/remix/build_remix.py --start-map 141
python3 -I tools/remix/boot_test_map.py "<scratch>/remix/Advance Wars 2 - Remix (test start 141).gba" <scratch>/t141 --map 141
```

The script drives `gbadbg` (built from `tools/remix/gbadbg.c`; set `$GBADBG` to point at the binary) through stdin:

1. Power-on → Start → Campaign → New, using the key sequence from `make_test_roms.py`.
2. Press A until the map ID byte `0x03003FC2` equals ID.
3. Advance the intro dialogue with A. Before each A press, the script checks for player control: Right, then Left, must move the map cursor, whose X is the u16 at `0x030033E4` (Y at `0x030033E6`). The check is repeated after a 60-frame pause, so A is never pressed on the live map.
4. Write a screenshot `<prefix>-map.png` and an mGBA savestate `<prefix>.ss` at the first player turn.
5. Check that the decompressed map buffer in RAM (`*0x03003F68`) equals the ROM terrain of that map. The exit status is 0 only if it does.

To continue a playtest from the savestate, start a gbadbg script with `load <prefix>.ss` and then send the usual `press` / `wait` / `shot` lines.

VERIFIED (each row: build → boot → screenshot, map buffer == ROM terrain):

| start map | ROM | A presses to select / through intro | player control at frame | screenshot shows |
|---|---|---|---|---|
| 138 Cleanup | release ROM | 131 / 40 | 13,604 | Andy, 1000 G; Options menu shows "No Visual" |
| 139 Border Skirmish | `--start-map 139` | 123 / 49 | 14,622 | Sami, 3000 G, stock layout |
| 140 Orange Dawn | `--start-map 140` | 145 / 39 | 14,094 | Max, 2000 G |
| 141 Flak Attack | `--start-map 141` | 129 / 34 | 12,588 | Max, 3000 G, air armada |
| 142 Lash Out | `--start-map 142` | 124 / 33 | 12,204 | Sami, 4000 G, "7 Day(s) Left" |
| 172 Border Skirmish (hard) | `--start-map 172` | 137 / 8 | 8,952 | fog, hard unit list |
| 139 remixed (test file) | `--start-map 139` + edited map | 123 / 49 | 14,622 | new roads, mountains, forest, city and enemies render correctly |
| 141 remixed (test file) | `--start-map 141` + edited map | 129 / 34 | 12,588 | new island with shoals/reef, river mouth, bridge |

Each boot takes 10–15 s headless. Note that **Cleanup (138) has tutorial pop-ups**: selecting a unit opens Nell's explanation, so scripted inputs there must press A through them.

## Tests

`python3 -I tools/remix/test_remix.py` runs:

- **BPS:** varints, all four actions, random round trips, the 8 MiB ROM, rejection of a wrong source.
- **Text round trip:** all 213 stock map variants are byte-identical.
- **ROM round trip:** the 10 exported originals are rebuilt, relocated and read back identical.
- **Validation:** tank on a mountain, overlapping units, owner beyond `players`, a moved player unit, bad size, bad symbol, property owner beyond `players`, and that `events_checked` accepts the moved unit.
- **Release build:** protected regions intact, QoL bytes set, the BPS reproduces the output, and output into the repo is refused.

## Files

| file | role |
|---|---|
| `tools/remix/build_remix.py` | the builder (allocator, QoL, balance hook, maps, start-map, checks, BPS) |
| `tools/remix/mapfile.py` | map text format: parse/format, export, learned autotiler, validation, encoding |
| `tools/remix/export_map.py` | export original maps; `--orig`, `--roundtrip` |
| `tools/remix/bps.py` | BPS encode/decode/apply/info |
| `tools/remix/boot_test_map.py` | headless boot to a mission's first player turn (savestate + screenshot + RAM check) |
| `tools/remix/test_remix.py` | self-tests |
| `tools/remix/maps/orig/*.txt` | exports of 138–142 and 172–176 (starting points; not built; git-ignored, regenerate with `export_map.py --orig`) |
| `tools/remix/maps/*.txt` | remixed maps that get built (none yet) |
