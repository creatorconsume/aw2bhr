# Dead Wars

A zombie-apocalypse turn-based tactics game for the Game Boy Advance, built on
Advance Wars 2–style rules. It is original homebrew (no Nintendo code or art), so it runs
in any GBA emulator, including Onion OS on the Miyoo Mini Plus.

You lead the last survivors of Cedar Falls against a horde that grows every
time it wins.

## What's different from Advance Wars

| Mechanic | How it works |
|---|---|
| **Infection** | Any foot unit killed by a zombie rises next to you as a Walker. |
| **Nests** | Nests spawn new zombies every few days. Foot units can **Burn** them (works like capturing). |
| **Night** | Every few days night falls: fog of war, zombies move +1 tile and hit 15% harder. Radio towers you hold reveal a radius around them. |
| **Scavenging** | **Search** an unlooted house for supplies, a survivor who joins you, or an ambush. |
| **Supplies** | Buildings you **Secure** give income each day. Spend it at a Workshop to recruit. |
| **Dormant zombies** | Some zombies sleep until you get within 4 tiles, get hurt, or night falls. |
| **Ambush** | At night you can walk into a zombie hidden in the fog. Your move stops short. |
| **Overrun** | Zombies take over buildings. If the horde takes your Safehouse, you lose. |
| **Bloaters** | They burst when killed, dealing 2 HP to everything next to them. |

### Units

Survivors: **Survivor** (cheap, captures), **Heavy** (armored, cheap on rubble), **Medic**
(heals neighbours), **Sniper** (range 2–3, can't move and fire), **Pickup** (fast gun truck),
**Bus** (carries one foot unit), **Armor** (tough plow truck).

Horde: **Walker**, **Runner** (fast), **Brute** (vehicle crusher), **Spitter** (range 2),
**Bloater** (explodes).

## Controls

| Button | Action |
|---|---|
| D-Pad | Move the cursor |
| A | Select a unit or confirm. On a zombie: show how far it can reach. On a Workshop: recruit |
| B | Cancel / undo a move |
| START | Menu (End Day, Objective, Options, Suspend) |
| SELECT / L | Jump to your next unit that hasn't moved |
| Hold B during the horde's turn | Speed it up |

There are no battle animations; combat resolves on the map. **Options** (title screen or
the START menu) switches Fast/Normal speed, Music and SFX on/off, and has a Sound Test.

## Campaign (pilot: Part One)

1. **First Night**: hold the firehouse until Day 5.
2. **Supply Run**: burn both nests across the river.
3. **Bridge Out**: get three units onto the helipad by Day 8.
4. **St. Agnes**: clear the hospital where it began.
5. **The Long Night**: survive seven days in the town square.

The game saves to battery (SRAM). Progress and one suspended battle are kept.

## Building

```sh
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi python3-pil
make            # -> deadwars.gba
```

`LOGO_ROM=path/to/any_rom_you_own.gba` optionally copies the boot logo into the header.
Emulators don't need this; real hardware does.

Art is plain-text pixel grids in `art/`, converted by `tools/gfx.py`
(see `docs/ASSET_SPEC.md`). Missions, maps and dialogue are in `src/missions.c`. The map legend
is at the top of `src/map.c`. `python3 test/check_maps.py src/missions.c` validates maps.

## Sound

Music and effects use only the four GBA PSG channels (2 square, wave, noise), so they cost
almost nothing: `sound_update()` runs once per frame from `frame()` and averages under 1,000
CPU cycles (0.3% of a frame). The music picks itself from the game state (title, day, night,
horde turn, victory, defeat), and the battle theme resumes where it left off after the horde's turn.

- `audio/music.txt`: all songs, written in a small MML-like text format (documented at the top of
  `tools/music.py`, which compiles it to `build/gen/music.c`). Loops are checked to be seamless.
- `src/sound.c`: the sequencer, the director, and the sound effects (C tables of short register
  steps). An effect borrows a channel and gives it back to the music when it ends.

## Testing

`test/gbarun.c` is a headless mGBA runner that plays scripted button presses and
saves screenshots (`sudo apt install libmgba-dev`):

```sh
gcc -O2 -o gbarun test/gbarun.c -lmgba
./gbarun deadwars.gba test/smoke.txt out   # writes out-*.ppm
```

Script commands are listed at the top of `test/gbarun.c`. `record NAME` / `stop` capture the
emulator's audio as a WAV, `channels MASK` isolates audio channels, and `peek ADDR LABEL`
prints a variable (for example `frame_count`, to confirm no frames are dropped).
