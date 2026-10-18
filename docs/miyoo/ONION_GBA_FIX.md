# Fixing a black/white screen for GBA games in Onion OS (Miyoo Mini Plus)

Your Advance Wars 2 ROM is a verified good dump (SHA1 `14dd0b22…`, the exact
No-Intro match) and it boots in mGBA, so the problem is on the device side. Work
down this list and test the game after each step. Most people are fixed by step 2 or 3.

Paths below are relative to the root of the SD card. Detailed sources are in
[`ONION_RESEARCH.md`](ONION_RESEARCH.md).

## 1. Put the ROM in unzipped, with a clean name

1. Unzip `Advance_Wars_2_-_Black_Hole_Rising_USA.zip` on your computer. Keep only the
   `.gba` file and throw away `Vimm's Lair.txt`.
2. Copy it to `Roms/GBA/Advance Wars 2 - Black Hole Rising (USA).gba`. Put it directly in
   `Roms/GBA/`, or at most one subfolder deep. Avoid apostrophes in folder names.
3. Delete the old `.zip` from `Roms/GBA/` so you don't launch the wrong entry.
4. On the Miyoo, open the GBA list, press **MENU**, and choose **Refresh all roms**.

RetroArch should ignore the `.txt` inside the zip, but loose `.gba` files remove that variable
entirely and load faster.

## 2. Clear a bad auto-save state (most likely culprit)

Onion automatically **saves a state when you exit and reloads it on the next launch**. If the
game ever froze or crashed once, the frozen moment is restored every time, giving you a
black or white screen forever.

- In the GBA game list, highlight Advance Wars 2, press **Y** to open the game options
  menu, and choose **Reset game**. That launches it fresh, without the auto state.
- Or, on your computer, delete
  `Saves/CurrentProfile/states/<core>/Advance Wars 2 - Black Hole Rising (USA).state*`.
  `<core>` is `mGBA` or `gpSP`.

This only deletes save *states*. Your in-game battery save (`.srm`) is untouched.

## 3. Check the BIOS file

Look in the `BIOS/` folder for `gba_bios.bin`.

- **If it's there**, it must be exactly 16,384 bytes with MD5 `a860e8c0b6d573d191e4ec7db1b1e4f6`.
  A wrong or corrupt BIOS (often a bad download) is a classic cause of white or black screens,
  because Onion's mGBA is set to use the BIOS whenever one is present. If you can't verify it,
  **rename it** to `gba_bios.bin.off` and try again: mGBA runs fine without it.
- **If it's not there**, that's fine for mGBA, the default core. The gpSP core needs it.

## 4. Try the other emulator core for just this game

1. In the GBA list, highlight the game, press **Y**, and select **Game core**.
2. Switch between **mGBA (Default)** and **gpSP**. gpSP is faster but needs the BIOS from step 3.
   If it's missing from the list, install it from Package Manager's *Expert* section.
3. **Restore default core** in the same menu undoes this.

Each core keeps its own saves (`Saves/CurrentProfile/saves/mGBA/` vs `…/gpSP/`). The **Y** menu has
a **Transfer mGBA save** option if you switch.

## 5. If the device keeps resuming straight into the black screen

Hold **POWER for 10+ seconds** to force it off. If it still resumes, delete
`.tmp_update/cmd_to_run.sh` from the SD card on a computer, then do step 2.

## Playing the original and the mods side by side

Onion names saves and states after the **ROM filename**. Different filenames therefore never
share saves, so you can keep all of these in `Roms/GBA/`:

| File | What it is |
|---|---|
| `Advance Wars 2 - Black Hole Rising (USA).gba` | the untouched original |
| `Advance Wars 2 - Remix.gba` | the very-hard remix (coming) |
| `Dead Wars.gba` | the zombie game (original homebrew) |

Use **mGBA** (the default) for the remix. Its built-in speed tweak for AW2 is keyed to the
game code, and the remix keeps that code.
