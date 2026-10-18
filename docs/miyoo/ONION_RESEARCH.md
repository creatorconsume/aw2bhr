# Onion OS (Miyoo Mini Plus): Advance Wars 2 black/white screen, research notes

Researched 2026-10-18 from raw.githubusercontent.com only. Nothing here is from memory unless it is explicitly labelled "memory".

Abbreviations for sources:
- ONION = https://raw.githubusercontent.com/OnionUI/Onion/main
- SITE = https://raw.githubusercontent.com/OnionUI/OnionUI.github.io/main (pre-built docs HTML of the Onion docs, **version 4.3**)
- LRDOCS = https://raw.githubusercontent.com/libretro/docs/master
- Confidence: HIGH = read in source or docs; MED = inferred from source plus standard RetroArch behaviour; LOW = memory or guess.

Version caveat: ONION `Makefile` says `VERSION=4.4.0-beta-20260120`, RetroArch `1.22.2-1`. The built docs site (SITE) is only version 4.3. The user may be on 4.3.x or 4.4 beta. Where the two differ I say so.

---

## TL;DR: what most likely goes wrong and the fix order

The Onion source does not show any reason for a clean `.zip` containing an extra `.txt` to fail (section 3). The more likely causes, most likely first:

1. **Stale or bad auto save state.** Onion auto-loads state slot -1 on every launch (`savestate_auto_load = "true"`, `savestate_auto_save = "true"`). If an earlier launch (other core, other ROM version, failed boot) left a bad `.state.auto`, the game comes back to the same black/white screen every time. Fix: Game list, press **Y**, **Reset game** (starts without loading the auto state), or delete `Saves/CurrentProfile/states/<Core>/<romname>.state.auto`.
2. **A wrong or corrupt `BIOS/gba_bios.bin`.** Onion's default mGBA settings have `mgba_use_bios = "ON"`. mGBA loads any 16384-byte file named `gba_bios.bin` even if the checksum is wrong (it only logs a warning), then boots through it. A bad BIOS gives black or white screens. Fix: delete it, or replace it with the official one (md5 `a860e8c0b6d573d191e4ec7db1b1e4f6`).
3. **Core mismatch.** Try the other core per game: Game list, **Y**, **Game core**, pick gpSP (or mGBA if gpSP is currently selected).
4. Re-copy the ROM un-zipped as `Roms/GBA/<name>.gba` and **Refresh list**.
5. If the ROM is the *modified* aw2bhr build (header game code still `AW2E`): both mGBA and gpSP ship per-game-code hacks for `AW2E` that hard-code a code address (idle-loop skipping). See section 4. For a hack, turn mGBA's "Idle Loop Removal" to "Don't Remove" with a per-game `.opt` (section 5).

---

## 1. Default GBA core, alternatives, and how to switch

### Default core
- **mGBA (`mgba_libretro`) is the default GBA core** on Onion. Docs: "The default GBA emulator on Onion is currently mGBA." Same text in the main-branch source of the docs.
  - ONION/website/docs/04-emulators/03-handhelds/05-nintendo-gba.md
  - SITE/docs/emulators/gba.html
  - Confidence: HIGH for the docs statement. Note this docs file is only known to be correct for 4.3; I could not read the shipped GBA `launch.sh` (see "What I could not fetch" below).
- Alternatives listed: lr-gpsp, lr-mgba, lr-meteor, lr-mednafen-gba, lr-vba_next. Rom folder `GBA`. Extensions `.bin .gba .zip .7z`. Same file as above. HIGH.
- **gpSP is not necessarily installed by default.** Docs say to use it you either change the core with the GLO menu or "install gpSP emulator in expert section from Package Manager". HIGH (docs). The GLO source only lists cores whose `.so` and `.info` exist in `RetroArch/.retroarch/cores/`.
- Evidence that gpSP is at least present as a switchable GBA core in current source: GLO script has a "Transfer mGBA save" option that appears when the chosen core is `gpsp_libretro` and a `Saves/CurrentProfile/saves/mGBA/<romname>.srm` exists.
  - ONION/static/build/.tmp_update/script/game_list_options.sh
  - ONION/static/build/.tmp_update/script/transfer_mgba_save.sh
  - HIGH.

### Switch core for ONE game (this is the supported way)
From the game list (Games tab, GBA):
1. Highlight the game and press **Y** to open the **GLO** (Game List Options) menu. (Y is the default; reassignable in Tweaks.)
2. Choose **"Game core: mGBA (Default)"** (label shows the current core).
3. A list titled "GAME CORE (.gba)" appears with every installed core that supports the extension. Pick **gpSP**. Press A.
4. To undo: GLO, **Restore default core** (this entry only shows when a non-default core is set).
5. "Reset game" is in the same menu: it launches without loading the auto save state.

Sources: ONION/website/docs/07-apps/01-included-in-onion/game-list-options.md ("Press Y in a game list", "Use the 'Game core' option to change core per game"), and the code in ONION/static/build/.tmp_update/script/game_list_options.sh (`change_core`, `reset_core`, `reset_game`). HIGH.

Mechanism (HIGH, read in code):
- `change_core` writes `core = "<corename>_libretro"` into `<rom folder>/.game_config/<rom basename without last extension>.cfg`. Example: `Roms/GBA/.game_config/Advance Wars 2 - Black Hole Rising (USA).cfg` containing `core = "gpsp_libretro"`.
- If you pick the default core again, `reset_core` deletes that cfg file.
- At launch, `runtime.sh` `override_game_core` reads that file and rewrites the command to `./retroarch -v -L ".retroarch/cores/<core>.so" "<rompath>"`.
  - ONION/static/build/.tmp_update/runtime.sh
- For `.zip`/`.7z` ROMs, `change_core` looks inside the archive to learn the inner extension (largest file first, or a name containing `[!]`). So a small `.txt` inside the zip does not confuse the core picker.

### Switch core GLOBALLY
- The GLO menu is per game only. I found no GLO option for a global default.
- Options for a global change (MED, inferred):
  a. Edit `Emu/GBA/launch.sh` on the SD card and change `mgba_libretro.so` to `gpsp_libretro.so`. The GLO code determines the default core by parsing launch.sh: either a line `default_core=<name>` (then `<name>_libretro`) or the text between `.retroarch/cores/` and `.so`. So editing that line also changes what GLO shows as "(Default)". Requires gpSP's `gpsp_libretro.so` to exist in `RetroArch/.retroarch/cores/`.
  b. Set the core on each game (above). A per-game `.game_config/*.cfg` file can also be created by hand (format above), which is what the project can ship.
- ONION/website/docs "Combining systems or cores" (SITE/docs/advanced/combining-systems.html) shows the launch.sh pattern: `cd /mnt/SDCARD/RetroArch/` then `HOME=/mnt/SDCARD/RetroArch/ ./retroarch -v -L ".retroarch/cores/$core" "$1"`. HIGH that this pattern is what Onion launch scripts use; LOW (memory) that the stock GBA launch.sh is a 3-line variant of it.

### RetroArch quick menu
- Open in-game: **MENU + SELECT** (SITE/docs/apps/shortcuts.html, ONION/website/docs/07-apps/01-included-in-onion/shortcuts.md and retroarch.md). HIGH.
- Core options (BIOS use, idle loop, etc.): Quick Menu, Options (RetroArch "Core Options"). To make them stick for one game: Quick Menu, Options, **Manage Core Options**, **Create Game Options file** (names vary by RA version). Standard RetroArch behaviour, MED.
- The quick menu is not the way Onion switches cores. The GLO menu is.
- FAQ: when you change RetroArch settings you must save them via Quick Menu, **Overrides**, **Save Game Overrides / Save Core Overrides / Save Content Overrides**, or they are lost (SITE/docs/faq.html). HIGH.

### Onion's shipped core option defaults (HIGH, read from the repo)
- mGBA: https://raw.githubusercontent.com/OnionUI/Onion/main/static/configs/Saves/CurrentProfile/config/mGBA/mGBA.opt
  - `mgba_use_bios = "ON"`, `mgba_skip_bios = "OFF"`, `mgba_idle_optimization = "Remove Known"`, `mgba_force_gbp = "ON"`, `mgba_frameskip = "disabled"`, `mgba_color_correction = "OFF"`, `mgba_interframe_blending = "OFF"`.
  - Note `mgba_force_gbp = "ON"` (upstream default is OFF). Upstream's description: it "may cause glitches such as flickering or lag" in games with the GB Player logo (mGBA `libretro_core_options.h`). Advance Wars 2 is not a GB Player-enhanced game, so probably irrelevant, but it is a cheap thing to turn off when debugging. LOW-MED.
- gpSP: https://raw.githubusercontent.com/OnionUI/Onion/main/static/configs/Saves/CurrentProfile/config/gpSP/gpSP.opt
  - `gpsp_bios = "auto"`, `gpsp_boot_mode = "game"`, `gpsp_drc = "enabled"`, `gpsp_save_method = "gpSP"` (this key is not in upstream gpSP master, so Onion's gpSP build is a fork or older), frameskip off.
- Per-core overrides: `.../config/gpSP/gpSP.cfg` has `aspect_ratio_index = "23"` and `video_dingux_ipu_keep_aspect = "true"`; `.../config/mGBA/mGBA.cfg` has `video_dingux_ipu_keep_aspect`, `audio_driver = "sdl"`, `fastforward_frameskip`.

### What I could not fetch
- The actual `Emu/GBA/launch.sh` and `config.json`. I tried about 100 plausible paths under `static/build/Emu/`, `static/packages/Emu/<name>/Emu/GBA/` and several tags; all 404. The package directory name is not guessable from raw alone. So "default is mGBA" rests on the docs, not on the launch script itself. The `Makefile` confirms packages are copied from `static/packages/Emu/` to the Package Manager data dir.

---

## 2. BIOS

- File name: **`gba_bios.bin`** (lowercase, exact; names are case sensitive).
- SD card folder: **`BIOS/`** at the SD card root, i.e. `/mnt/SDCARD/BIOS/gba_bios.bin`.
  - RetroArch `system_directory = "/mnt/SDCARD/BIOS"`: ONION/static/configs/RetroArch/.retroarch/retroarch.cfg
  - Docs: "placed in the root /BIOS folder": SITE/docs/emulators.html; SITE/docs/installation/fresh.html
  - HIGH.
- Official md5: `a860e8c0b6d573d191e4ec7db1b1e4f6` (16384 bytes). Source: libretro core info files
  - https://raw.githubusercontent.com/libretro/libretro-core-info/master/gpsp_libretro.info (firmware0_opt = "false", i.e. flagged as required in the info file)
  - https://raw.githubusercontent.com/libretro/libretro-core-info/master/mgba_libretro.info (firmware0_opt = "true", optional)
  - HIGH.
- Onion docs: "gba_bios.bin (required for lr-gpsp, optional for other cores)": ONION/website/docs/04-emulators/03-handhelds/05-nintendo-gba.md. HIGH (as a statement of the docs).
- **mGBA** (libretro/mgba `src/platform/libretro/libretro.c`): only reads the BIOS when core option `mgba_use_bios` is ON, from `<system dir>/gba_bios.bin`. Onion's shipped default is ON (see section 1), so a file in `BIOS/` *is used*. In `src/gba/gba.c` `GBALoadBIOS`: wrong size (not 16384 bytes) -> ignored; a "known broken debug BIOS" -> ignored; **any other checksum only logs "BIOS checksum incorrect" and is still used**. So a bad dump, a renamed DS/other file of the right size, or a modified BIOS will be run. HIGH (source). The practical consequence (black/white screen) is an inference: MED.
  - https://raw.githubusercontent.com/libretro/mgba/master/src/platform/libretro/libretro.c
  - https://raw.githubusercontent.com/libretro/mgba/master/src/gba/gba.c
  - https://raw.githubusercontent.com/libretro/mgba/master/src/platform/libretro/libretro_core_options.h
- **gpSP** (libretro/gpsp `libretro/libretro.c`): current upstream master does not strictly need the BIOS. `gpsp_bios` = auto/builtin/official; auto tries `<system dir>/gba_bios.bin` and falls back to a built-in open-source BIOS if the file is missing or `bios_rom[0] != 0x18`. It also forces the built-in BIOS for non-standard headers and for Pokemon hacks. The libretro docs page also calls the BIOS "Optional". HIGH for upstream master. **Onion's gpSP build may be older** (it has an extra `gpsp_save_method` option) and Onion's docs say required, so treat "required" as the safe assumption for Onion. MED.
  - https://raw.githubusercontent.com/libretro/gpsp/master/libretro/libretro.c
  - https://raw.githubusercontent.com/libretro/gpsp/master/libretro/libretro_core_options.h
  - https://raw.githubusercontent.com/libretro/docs/master/docs/library/gpsp.md
- Known issues without BIOS:
  - mGBA: none; it falls back to its built-in HLE BIOS. Missing BIOS is safe. A *wrong* BIOS is the risk.
  - gpSP: older builds, per Onion docs, need it; upstream master falls back to the built-in. Upstream's own description warns the built-in BIOS "might result in incompatibility problems with some games".
  - I found no Advance Wars-specific BIOS note anywhere.
- Test: temporarily rename `BIOS/gba_bios.bin` to `gba_bios.bin.off`; if mGBA now boots the game, the BIOS file is the culprit.

---

## 3. Zip handling, and could an extra `.txt` break launching?

**Short answer: no, per source. Archives are handled by RetroArch, which only considers files whose extension is in the core's `valid_extensions`. A `.txt` is skipped.** Still, un-zipped is the safest layout (recommendation at the end).

Evidence:
- Onion lists `.zip` and `.7z` for GBA (docs file above). HIGH. (The `config.json` `extlist` itself was not readable.)
- Onion passes the zip path straight to RetroArch: `./retroarch -v -L .retroarch/cores/<core>.so "<path to .zip>"`. `runtime.sh` `launch_game` only does `realpath` on the rom path and a core override; there is no unzip step. HIGH. ONION/static/build/.tmp_update/runtime.sh
- Valid extensions per core (read from the core source):
  - mGBA: `need_fullpath = false`, `valid_extensions = "gba|gb|gbc|sgb"`, `block_extract = false` (libretro/mgba libretro.c `retro_get_system_info`). RetroArch loads the matching member into memory and hands the core a data blob. HIGH.
  - gpSP: `need_fullpath = true`, `block_extract = false`, `valid_extensions = "gba|bin|agb|gbz|u1"` (libretro/gpsp libretro.c). RetroArch therefore extracts the matching member to a temp dir and passes that path. HIGH.
  - Note libretro core-info files list `gba|bin` (gpSP) and `gb|gbc|gba` (mGBA); neither lists `zip`, so RetroArch does the extraction. HIGH.
- RetroArch's choice of member (libretro/RetroArch `tasks/task_content.c`, `content_file_get_path`): it calls `file_archive_get_file_list(content_path, valid_exts)` (list filtered by the core's valid extensions), sorts alphabetically and takes the first. `.txt` is filtered out. If the filtered list is empty the load fails. HIGH for upstream RetroArch master. Onion ships RetroArch 1.22.2-1 (Makefile), a fork; I assume the same logic, MED.
  - https://raw.githubusercontent.com/libretro/RetroArch/master/tasks/task_content.c
- Footguns that DO matter inside a zip:
  1. Two files with a valid extension (e.g. both `.gba` and `.bin`, or a patch + a ROM): alphabetical first wins.
  2. For gpSP, `.bin` is a valid extension, so a stray `*.bin` in the zip that sorts before the `.gba` would be loaded instead. (Not the case for a `.txt`.)
  3. Save/state naming of zipped content: Onion's own migration script names saves after the *zip's* basename (`saves/<Core>/<romname>.srm`, where `romname` is basename minus extension of the file in the list). Upstream RetroArch's comment in `task_content.c` says extraction keeps the member's own basename; I did not verify which name RetroArch uses for the `.srm` when the content is an archive. MED-LOW. Another reason to avoid zips: name the zip exactly like the inner file, or do not zip.
- FAQ: "Ensure you are using supported rom extensions" (SITE/docs/faq.html). HIGH.
- Apostrophe note: Onion docs say **sub**folder names cannot contain an apostrophe (`'`) (SITE/docs/emulators.html). That is about folder names on the SD card, not names inside a zip, so "Vimm's Lair.txt" is not a problem by itself. Do not put the apostrophe in any SD-card folder name. HIGH (docs).

### Recommended layout for GBA ROMs
```
SD:/Roms/GBA/Advance Wars 2 - Black Hole Rising (USA).gba      <- plain, un-zipped, no apostrophes/quotes/$
SD:/Roms/GBA/<optional ONE level of subfolder>/<rom>.gba       <- e.g. Roms/GBA/Hacks/ (only one level deep)
SD:/BIOS/gba_bios.bin                                          <- optional for mGBA, safest for gpSP
```
- Folder name `GBA` is case sensitive. (SITE/docs/emulators/folders.html. HIGH.)
- After copying: Games tab, highlight GBA, **SELECT** (context menu), **Refresh all roms**; or **Y**, **Refresh list**. (SITE/docs/faq.html and game-list-options doc. HIGH.)
- Prefer extracting the `.gba` and discarding `Vimm's Lair.txt`.

### Black-screen boot-loop recovery (if the device keeps auto-resuming into the black screen)
FAQ: hold POWER 10+ seconds, or delete `.tmp_update/cmd_to_run.sh` from the SD card on a PC. SITE/docs/faq.html. HIGH.

---

## 4. Advance Wars / Advance Wars 2 compatibility notes

AW2 USA header code is **`AW2E`** (Europe `AW2P`; Advance Wars 1 `AWRE`/`AWRP`).

### gpSP: `gba_over.h` (per-game database; there is no `game_config.txt` in current gpSP)
https://raw.githubusercontent.com/libretro/gpsp/master/gba_over.h (entries at lines ~12-48)
```
"AWRP"/"AWRE"  flags = FLAGS_SERIAL | FLAGS_SERIAL_AW1  idle_loop_target_pc = 0x803880a
"AW2P"/"AW2E"  flags = FLAGS_SERIAL | FLAGS_SERIAL_AW2  idle_loop_target_pc = 0x8036e2a
```
- Meaning (read in `gba_memory.c` `load_game_config_over`): the game is known; the idle-loop skip is armed at ROM address 0x8036e2a; the link-cable serial mode defaults to "Advance Wars 2 mode" (user can change it in core option `gpsp_serial`; not in Onion's shipped `gpSP.opt`). Save type is not forced here; it is auto-detected from the ROM's `FLASH512_V`/`SRAM_V`/`EEPROM_V` signature strings (`detect_backup_subcircuit`). HIGH (source).
- The libretro gpSP docs have a compatibility table; Advance Wars is **not** in the known-issues list. https://raw.githubusercontent.com/libretro/docs/master/docs/library/gpsp.md HIGH.
- Risk for a **modified** ROM that keeps code `AW2E`: the idle-loop address is matched by game code, not by ROM contents, so a hack that moves or changes code around 0x08036E2A can hang/black-screen. gpSP has no option to disable idle-loop skipping; the only way around is mGBA or changing the header code. MED (inference from source; not tested).

### mGBA: `src/gba/overrides.c`
https://raw.githubusercontent.com/libretro/mgba/master/src/gba/overrides.c lines 14-21:
```
{ "AWRE", GBA_SAVEDATA_FLASH512, HW_NONE, 0x8038810 },
{ "AWRP", GBA_SAVEDATA_FLASH512, HW_NONE, 0x8038810 },
{ "AW2E", GBA_SAVEDATA_FLASH512, HW_NONE, 0x8036E08 },
{ "AW2P", GBA_SAVEDATA_FLASH512, HW_NONE, 0x803719C },
```
- Meaning: for code `AW2E` mGBA forces **64 KB Flash (FLASH512)** save type and arms an idle-loop address 0x08036E08. The idle-loop address is only *used* when core option `mgba_idle_optimization` is "Remove Known" (Onion's shipped default) or "Detect and Remove". HIGH (source).
- Same modded-ROM risk as above; for mGBA it is solvable by setting `mgba_idle_optimization = "Don't Remove"` (per-game `.opt`, section 5) and, if the mod uses a different save type than Flash 64K, note that the override forces FLASH512 for any `AW2E` ROM. MED.

### Other notes
- gpSP's info file states plainly: "this core has problems with some games. For most users, mGBA or VBA-M will be better options" (https://raw.githubusercontent.com/libretro/libretro-core-info/master/gpsp_libretro.info). mGBA's: "generally a good first choice". HIGH. So gpSP is the fallback for speed, not the compatibility choice.
- I found no libretro/Onion text that mentions a known black/white screen for Advance Wars 2 on either core. LOW-negative evidence only (issue trackers were not reachable).

---

## 5. Per-game overrides in Onion, and save file naming

### Where RetroArch settings live on the SD card (HIGH, from retroarch.cfg)
Source: https://raw.githubusercontent.com/OnionUI/Onion/main/static/configs/RetroArch/.retroarch/retroarch.cfg
- `rgui_config_directory = "/mnt/SDCARD/Saves/CurrentProfile/config"` (the "Config" directory)
- `savefile_directory = "/mnt/SDCARD/Saves/CurrentProfile/saves"`, `sort_savefiles_enable = "true"` -> saves in a subfolder per core
- `savestate_directory = "/mnt/SDCARD/Saves/CurrentProfile/states"`, `sort_savestates_enable = "true"` -> states in a subfolder per core
- `savestate_auto_load = "true"`, `savestate_auto_save = "true"`, `savestate_auto_index = "true"`
- `auto_overrides_enable = "true"`, `auto_remaps_enable = "true"`, `game_specific_options = "true"`, `global_core_options = "false"`
- `input_remapping_directory = "/mnt/SDCARD/Saves/CurrentProfile/config/remaps"`

### Per-game RetroArch files (path + format)
Using RetroArch's standard layout (LRDOCS/docs/guides/overrides.md: `/config/<name-of-core>/name-of-game.cfg`, `.opt`, `name-of-content-directory.cfg`) with Onion's config dir:

| Purpose | Path on SD card | Format |
|---|---|---|
| Per-game core options | `Saves/CurrentProfile/config/mGBA/<game>.opt` (or `.../gpSP/<game>.opt`) | `key = "value"` lines. A *full* options file, loaded instead of the global one. |
| Per-game config override | `Saves/CurrentProfile/config/mGBA/<game>.cfg` (or `.../gpSP/`) | `key = "value"` lines of `retroarch.cfg` settings; only the differing ones. |
| Per content-directory override | `Saves/CurrentProfile/config/mGBA/GBA.cfg` | same; applies to everything loaded from `Roms/GBA/`. |
| Core-wide options / override (shipped) | `Saves/CurrentProfile/config/mGBA/mGBA.opt`, `mGBA.cfg` | as in section 1 |
| Per-game input remap | `Saves/CurrentProfile/config/remaps/mGBA/<game>.rmp` | RetroArch remap format |
| Per-game core selection (Onion-specific) | `Roms/GBA/.game_config/<game>.cfg` | `core = "gpsp_libretro"` (one line) |

- `<name-of-core>` directory is the core's library name: **`mGBA`**, **`gpSP`** (case as shown by the shipped folders in `static/configs/Saves/CurrentProfile/config/`). HIGH (shipped files); libretro docs confirm "The gpSP core's directory name is 'gpSP'".
- `<game>` = the content name without extension, e.g. `Advance Wars 2 - Black Hole Rising (USA)`. For zipped content this is presumably the zip's name; MED-LOW. Another reason to ship un-zipped.
- Onion's own wording of the same feature: FAQ "How do I save RetroArch settings?" says use Quick Menu, Overrides, Save Game/Core/Content Overrides (SITE/docs/faq.html). HIGH. The file locations above are my inference from `rgui_config_directory` plus the libretro docs: MED-HIGH. Easiest way to confirm on the device: make one change, do Save Game Overrides, then look in `Saves/CurrentProfile/config/<Core>/` with the File Explorer app.
- "Content Overrides are recommended over Core Overrides since some cores serve multiple systems" (FAQ). HIGH.

Ready-to-ship per-game file ideas for the aw2bhr mod (untested; verify the key names on-device):
```
# Saves/CurrentProfile/config/mGBA/<rom name>.opt  (a FULL options file; copy mGBA.opt and change these)
mgba_idle_optimization = "Don't Remove"
mgba_force_gbp = "OFF"
mgba_use_bios = "OFF"        # optional, to rule out a bad gba_bios.bin
```
```
# Roms/GBA/.game_config/<rom name>.cfg  (force a core for this rom)
core = "gpsp_libretro"
```
Key names are from the mGBA core's `libretro_core_options.h` and Onion's shipped `mGBA.opt` (HIGH). Because `.opt` replaces rather than layers, copy the full shipped `mGBA.opt` first (HIGH, libretro docs: "full configurations ... loaded instead of").

### Save and state file naming (original vs modded ROM keep separate saves?)
- Battery saves: `Saves/CurrentProfile/saves/<Core>/<content name>.srm` (mGBA writes via libretro_saves; gpSP via `gpsp_save_method`). Source: Onion's `transfer_mgba_save.sh` uses exactly `saves/mGBA/$romname.srm` and `saves/gpSP/$romname.srm`; libretro gpSP docs: "'content-name'.srm". HIGH.
- States: `Saves/CurrentProfile/states/<Core>/<content name>.state<N>`; the Onion auto-save is slot -1: `<content name>.state.auto` (seen in `transfer_mgba_save.sh`: `states/gpSP/$romname.state.auto`; FAQ: "Save state slot -1 is reserved for Onion's Auto-Save feature"). HIGH.
- Key point: names come from the ROM **file name**, so an original and a modded ROM with **different file names** automatically get separate `.srm` and `.state*` files. They share a save only if the file names (minus extension) are identical, or if both sit in different folders with the same name. Also saves are per core folder: an mGBA save is not seen by gpSP until you use GLO **Transfer mGBA save** (GLO shows it only when core = gpSP and an mGBA `.srm` exists). HIGH.
- Save states from one core/version are not compatible with another; FAQ says delete `Saves/CurrentProfile/states/[system]` when a game misbehaves after changing cores/versions. HIGH. Do this for the original ROM too: delete `states/mGBA/<name>.state*` and `states/gpSP/<name>.state*`.
- Copying an existing save in: name it `<rom name>.srm` into `Saves/CurrentProfile/saves/<Core>/` and delete the states for that game (FAQ). HIGH.
- The mod's flash type: both cores force Flash 64K for `AW2E`. A save from another emulator should be a 64 KB `.srm`/`.sav` (rename `.sav` to `.srm`; FAQ). MED.

---

## 6. Adding a homebrew / hack `.gba` ("Ports" or otherwise)

- A `.gba` hack or homebrew needs nothing special: put it in `Roms/GBA/` like any GBA ROM, **Refresh list**, launch. It is launched by the same Emu/GBA entry and core. HIGH (by construction; Onion has no per-ROM registration for the GBA system).
- The "Ports Collection" (`Roms/PORTS/`) is for native/standalone game ports (Quake, Duke3D, ...), not for GBA ROMs; it uses `.port` shortcut scripts with fields `GameName`, `GameDir`, `GameExecutable`, `Core`, `RomDir`, `RomFile`, etc. (SITE/docs/ports.html). You do **not** need it. HIGH.
- If you ever want a separate "hacks" list: either use a single subfolder `Roms/GBA/Hacks/` (one level deep only, no apostrophe in the folder name; browsing subfolders is incompatible with `miyoogamelist.xml`), or build a custom Emu folder with its own `config.json` + `launch.sh` (SITE/docs/advanced/combining-systems.html). HIGH.
- Per-game core for a hack: GLO **Game core**, or hand-made `.game_config/<name>.cfg` (section 1).
- Avoid `"`, `` ` `` and `$` in file names; `launch_game` has special handling for `$`, but it is a fragile area of the shell scripts. MED (code reading).
- Header caveat for hacks: if the mod keeps the original header code `AW2E`, the AW2E overrides in section 4 apply to it. If the hack changed the header (bad logo/checksum byte `0x96` at 0xB2 or `0xEA` at byte 3), gpSP treats it as a non-standard header ("is_hack" path) and selects built-in BIOS for official-BIOS mode; this is harmless for AW2. MED.

---

## Step-by-step fix guide for the user (draft)

1. Unzip on the PC. Keep only `Advance Wars 2 - Black Hole Rising (USA).gba`. Delete `Vimm's Lair.txt` and the zip. Put the `.gba` in `SD:\Roms\GBA\`.
2. Delete stale state files for that game: on the SD card, remove `Saves\CurrentProfile\states\mGBA\Advance Wars 2*` and `Saves\CurrentProfile\states\gpSP\Advance Wars 2*` (keep `.srm` in `saves\` if you have real progress).
3. Check `SD:\BIOS\gba_bios.bin`. If present, it must be the official 16 KB file (md5 `a860e8c0b6d573d191e4ec7db1b1e4f6`). If unsure, rename it to `gba_bios.bin.off` and retest.
4. On the Miyoo: Games, GBA, SELECT, **Refresh all roms**. Highlight the game, press **Y**, **Reset game** (starts without auto state).
5. Still black/white: **Y**, **Game core**, choose **gpSP** (install gpSP from Package Manager's Expert section first if it is not listed; enable Tweaks, User interface, Show expert mode to see the Expert tab). Launch again.
6. Still failing: boot-loop recovery per FAQ (hold POWER 10 s, or delete `.tmp_update\cmd_to_run.sh`), then re-test with another known-good ROM to rule out the SD card or Onion install (FAQ recommends reputable SanDisk/Samsung cards; the stock Miyoo card is "very prone to data corruption").
7. If it only fails on the **modified** ROM: use mGBA with `mgba_idle_optimization = "Don't Remove"` (section 5).

---

## Source index (every URL fetched that mattered)

Onion repo (`https://raw.githubusercontent.com/OnionUI/Onion/main/...`):
- `Makefile` (version 4.4.0-beta-20260120, RA 1.22.2-1, packaging dirs) HIGH
- `static/build/.tmp_update/script/game_list_options.sh` (GLO; core switching, `.game_config`, reset game, save transfer) HIGH
- `static/build/.tmp_update/script/transfer_mgba_save.sh` (save/state paths and names) HIGH
- `static/build/.tmp_update/script/build_ext_cache.sh` (core extension cache from `.info` files) HIGH
- `static/build/.tmp_update/runtime.sh` (launch, `override_game_core`, `--appendconfig`, reset/auto-load handling) HIGH
- `static/build/.tmp_update/script/patch_ra_cfg.sh`, `static/packages/common/Emu/advcommand.sh`, `static/packages/common/auto_advmenu_rc.sh`, `static/packages/common/apply.sh` (supporting)
- `static/configs/RetroArch/.retroarch/retroarch.cfg` (directories, auto-load, overrides) HIGH
- `static/configs/Saves/CurrentProfile/config/mGBA/mGBA.opt`, `.../mGBA/mGBA.cfg`, `.../gpSP/gpSP.opt`, `.../gpSP/gpSP.cfg` HIGH
- `website/docs/04-emulators/03-handhelds/05-nintendo-gba.md` HIGH
- `website/docs/07-apps/01-included-in-onion/game-list-options.md`, `retroarch.md`, `shortcuts.md`, `game-switcher.md` HIGH
- `website/docs/01-features/index.md` (links) HIGH

Built docs site (`https://raw.githubusercontent.com/OnionUI/OnionUI.github.io/main/...`, version 4.3):
- `sitemap.xml`, `docs/emulators/gba.html`, `docs/emulators.html`, `docs/emulators/folders.html`, `docs/faq.html`, `docs/ports.html`, `docs/installation/fresh.html`, `docs/apps/package-manager.html`, `docs/apps/game-list-options.html`, `docs/advanced/combining-systems.html` HIGH

libretro:
- `https://raw.githubusercontent.com/libretro/libretro-core-info/master/gpsp_libretro.info`, `mgba_libretro.info`, `vba_next_libretro.info`, `vbam_libretro.info`
- `https://raw.githubusercontent.com/libretro/gpsp/master/README.md`, `libretro/libretro.c`, `libretro/libretro_core_options.h`, `gba_over.h`, `gba_memory.c`
- `https://raw.githubusercontent.com/libretro/mgba/master/src/platform/libretro/libretro.c`, `libretro_core_options.h`, `src/gba/overrides.c`, `src/gba/gba.c`
- `https://raw.githubusercontent.com/libretro/RetroArch/master/tasks/task_content.c`
- `https://raw.githubusercontent.com/libretro/docs/master/docs/guides/overrides.md`, `docs/library/gpsp.md`, `docs/library/mgba.md`

Caveats: upstream libretro `master` was read, not the exact core builds shipped in Onion; the built docs are 4.3 while the repo is 4.4.0-beta; the shipped GBA `launch.sh`/`config.json` could not be located.
