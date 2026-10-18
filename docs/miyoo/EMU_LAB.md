# Emu lab: run GBA ROMs the way Onion OS runs them (no device needed)

`tools/emu-lab/` is a small headless libretro frontend (`lrun`) plus the GBA cores and core
options that Onion OS ships for the Miyoo Mini Plus. Use it to test Dead Wars, the modded
AW2 ROM, or the user's Advance Wars 2 dump on mGBA and gpSP, without the device.

Everything below was run on 2026-10-18 on x86_64 Ubuntu 24.04 unless marked **untested**.

## What Onion actually runs (checked in Onion's git tree)

| | Onion 4.3.x (`v4.3.1` tag) | Onion 4.4 beta (`main`) |
|---|---|---|
| Default GBA core | mGBA | **gpSP**: `static/packages/Emu/Nintendo - GBA (gpSP)/Emu/GBA/launch.sh` has `default_core=gpsp`, added in commit 27cc13d "Update gpSP core (new GBA default, has netplay)" |
| gpSP build | (older) | `gpsp_libretro.so` has the version string `v1.1.0-6250979`, i.e. libretro/gpsp commit 6250979 "Fix I/O write mirroring", 2025-11-23 |
| mGBA build | `mgba_libretro.so` has `0.10-dev ec5ecb26d` (last updated in Onion in Dec 2022) | same |

The 4.4 launch script falls back to mGBA for a game only if an mGBA `.state.auto` exists and no
gpSP one does. A per-game `Roms/GBA/.game_config/<rom>.cfg` overrides the core (see ONION_RESEARCH.md).
**The research notes and the fix guide say "mGBA is the default". That is only true for 4.3.x.**

The lab builds these cores (`make -C tools/emu-lab`):
- `cores/gpsp-onion_libretro.so`: gpSP at 6250979, the same source revision as Onion 4.4 (built for x86_64, so the dynarec is the x86 one and not ARM).
- `cores/gpsp_libretro.so`: gpSP master (5819380, 2026-09-19 at build time).
- `cores/mgba_libretro.so`: Ubuntu's `libretro-mgba` 0.10.2. That is close to, but not the same as, Onion's 0.10-dev build. **Untested:** behaviour that differs between the two.

Core options come from Onion's shipped files, copied unchanged into `tools/emu-lab/profiles/`
(each file names its source URL): `onion-mgba.opt` (`mgba_use_bios = "ON"`,
`mgba_skip_bios = "OFF"`, `mgba_force_gbp = "ON"`, `mgba_idle_optimization = "Remove Known"`)
and `onion-gpsp.opt` (`gpsp_bios = "auto"`, `gpsp_boot_mode = "game"`, `gpsp_drc = "enabled"`).
`lrun` reports which options the core never reads. `gpsp_save_method` is not read by
either gpSP build; the key does not exist in Onion's own binary either (checked with `strings`).

## Build

```sh
sudo apt-get install -y build-essential libretro-mgba python3-pil   # python3-pil only for the contact sheet
make -C tools/emu-lab          # builds lrun, clones and builds both gpSP revisions into tools/emu-lab/cores/
```
`cores/`, `out/`, ROMs, BIOS files, `.srm` and states are git-ignored. Never commit them.

## lrun

```sh
tools/emu-lab/lrun --core tools/emu-lab/cores/gpsp-onion_libretro.so --rom ROM.gba \
    --opts tools/emu-lab/profiles/onion-gpsp.opt --bios-dir DIR_WITH_gba_bios.bin \
    --script tools/emu-lab/scripts/boot.txt --out OUTDIR [--save-dir DIR] [--autostate FILE] \
    [--map deadwars/build/deadwars.map] [--hang-secs 10] [-v]
```
- `--bios-dir` is RetroArch's system dir (Onion: `/mnt/SDCARD/BIOS`). The cores look for `gba_bios.bin` there.
- `--save-dir` (default `--out`) holds `<rom name>.srm`. Like RetroArch, it is loaded after `retro_load_game` and written on exit.
- `--autostate FILE` loads a state before the first frame, as Onion's `savestate_auto_load` does with `<rom>.state.auto`.
- Scripts use the `deadwars/test/gbarun.c` format: `wait N`, `press KEYS [N]`, `hold KEYS`, `release`,
  `shot NAME`, `dump ADDR LEN NAME`. Extras: `savestate FILE`, `loadstate FILE`, `sram FILE`, `reset`,
  `peek ADDR LEN`, `poke ADDR BYTES..`, and `count ADDR` (game frame counter vs emulated frames).
  KEYS are any of `A B L R S(elect) T(start) U D < >`. ADDR is hex, or a symbol when `--map` is given.
  Relative FILE/NAME paths go into `--out`.
- Screenshots are PNG. Each `shot` line says `ok`, `BLACK`, `WHITE` or `UNIFORM`, plus how many frames the screen has not changed.
- Hang heuristics. A screen that stays unchanged for `--hang-secs` after an input prints `HANG?`. If the script's last input never changed the screen, it prints `NO-RESPONSE`. Any uniform screenshot also makes the run `RESULT: SUSPECT` (exit 3). A core error is exit 1, and a core crash kills lrun (the matrix reports `CRASH`).
- At the end it prints timing (host ms/frame and times-realtime), audio samples per frame vs the core's rate, and SRAM size and use.

## Test matrix (for the real AW2 ROM)

```sh
make -C tools/emu-lab
tools/emu-lab/matrix.sh "/path/Advance Wars 2 - Black Hole Rising (USA).gba" [/path/gba_bios.bin]
# optional: also test a modded build and a stale state from the original carried over to it
MOD_ROM=/path/aw2bhr.gba tools/emu-lab/matrix.sh "/path/Advance Wars 2 ... (USA).gba" [/path/gba_bios.bin]
# optional: another input script (default tools/emu-lab/scripts/boot.txt: 10 s boot, START, A)
SCRIPT=my.txt tools/emu-lab/matrix.sh ROM
```
Output is the table on stdout, plus `tools/emu-lab/out/matrix/<rom>/summary.txt`, `<case>/log.txt`,
`<case>/*.png`, and `sheet.png` (all cases side by side).

| Case | What it reproduces |
|---|---|
| `gpsp-onion`, `mgba-onion` | Onion 4.4 / 4.3 defaults, no BIOS |
| `gpsp-master` | newer gpSP than Onion ships |
| `mgba-realbios`, `mgba-realbios-skip`, `gpsp-realbios` | only with a BIOS argument: the user's BIOS file, with mGBA's BIOS intro and with it skipped |
| `mgba-zerobios`, `mgba-junkbios`, `mgba-shortbios` | a 16 KB all-zero file / a corrupt dump that looks like a BIOS / a wrong-size file named `gba_bios.bin` |
| `mgba-junk-off` | the corrupt BIOS with `mgba_use_bios = OFF` (the suggested fix) |
| `gpsp-junkbios`, `gpsp-zerobios` | the same bad files on gpSP |
| `mgba-nogbp`, `mgba-noidle` | `mgba_force_gbp = OFF`; `mgba_idle_optimization = "Don't Remove"` |
| `state-gpsp2mgba`, `state-mgba2gpsp` | a `.state.auto` left by the other core (the 4.3 to 4.4 core switch) |
| `state-trunc-mgba` | a truncated / corrupt `.state.auto` |
| `state-self-*` | control: a good state from the same core and ROM |
| `mod-*` (with `MOD_ROM`) | the modded ROM on both cores, with idle-loop removal off, and with the **original ROM's** state auto-loaded into it |

### Results on Dead Wars (the only ROM available here)

Stand-in for "real BIOS": the open-source Cult-of-GBA BIOS (`git clone https://github.com/Cult-of-GBA/BIOS`,
`bios.bin`, MIT). Stand-in for "modded ROM": the previous Dead Wars build (commit 91dcb87).

| Case | Result |
|---|---|
| defaults, `gpsp-master`, `nogbp`, `noidle`, zero BIOS, short BIOS, good states | boots, reaches the title and menus |
| `mgba-junkbios` | **white screen from frame 1 onward.** mGBA logs "BIOS checksum incorrect" and still uses the file. This reproduces suspect #2 of ONION_RESEARCH.md. `mgba-junk-off` (use_bios OFF) boots normally. mGBA only accepts a file that starts with 7 ARM branch instructions (`GBAIsBIOS`), so a zero-filled or wrong-size file is ignored, while a corrupt dump or a wrong-version BIOS is used. |
| `gpsp-junkbios` | the Onion gpSP build (6250979, dynarec on) **segfaults** with the junk BIOS used in matrix.sh. gpSP's only check is `bios_rom[0] == 0x18`. With the dynarec off, or on gpSP master, the same file gives a **white screen**. On the device, expect a crash back to the menu or a white screen. All-zero BIOS: gpSP falls back to its built-in BIOS and boots. |
| Cult BIOS on mGBA | "checksum incorrect", then "Invalid logo, skipping BIOS" (the Dead Wars header logo is blank), then boots |
| `state-gpsp2mgba`, `state-mgba2gpsp`, `state-trunc-mgba` | rejected by the core and the game boots normally. Wrong-core and truncated states are harmless. |
| `mod-oldstate-mgba` | **frozen screen**: the state from the other ROM version loads with only a warning ("Savestate is for a different version of the game"), then "Illegal opcode" loops. `NO-RESPONSE`. |
| `mod-oldstate-gpsp` | **frozen screen** (gpSP's built-in BIOS splash, frozen). gpSP does not check which ROM a state came from. `NO-RESPONSE`. |

So the two black/white-screen mechanisms that the lab reproduces are (1) a corrupt `gba_bios.bin`
with mGBA (or gpSP) and (2) an auto state from a different build of the same-named ROM. The
second one is exactly what happens when the user replaces the AW2 ROM (or installs a modded
build) under the same file name. The fix for both is in ONION_GBA_FIX.md: rename or delete
`gba_bios.bin`, and use "Reset game" or delete `states/<core>/<rom>.state.auto`.

**Untested here:** the real Nintendo BIOS and the real AW2 ROM. The coordinator should run the
matrix with them.

## Dead Wars on Onion's cores (smoke test)

```sh
make -C deadwars && make -C tools/emu-lab
L=tools/emu-lab
$L/lrun --core $L/cores/gpsp-onion_libretro.so --rom deadwars/deadwars.gba --opts $L/profiles/onion-gpsp.opt \
  --bios-dir /nonexistent --out out-dw --save-dir out-dw/saves --map deadwars/build/deadwars.map \
  --script $L/scripts/deadwars-smoke.txt
$L/lrun ... --save-dir out-dw/saves --out out-dw2 --script $L/scripts/deadwars-continue.txt   # second boot
```
`deadwars-smoke.txt` does: title, Campaign, mission 1 briefing (skipped), select a unit, move it 2 tiles,
Wait, End Day, horde turn, Day 2, Suspend. `deadwars-continue.txt` boots again from the `.srm` and picks Continue.

| Check | mGBA 0.10.2 | gpSP 6250979 (Onion) | gpSP master |
|---|---|---|---|
| boot / title / menu / mission 1 / move / end day / day 2 (night) | ok | ok | ok |
| SRAM detected (via the `SRAM_V113` tag) | yes, 32 KB `.srm` | `.srm` is always 128 KB (gpSP exposes its whole backup buffer) | same as 6250979 |
| Suspend, then reboot with Continue (same core) | resumes on Day 2 | resumes on Day 2 | resumes on Day 2 |
| mGBA `.srm` copied to gpSP (what Onion's "Transfer mGBA save" does) | n/a | resumes on Day 2 | not run |
| game frames / emulated frames (`count frame_count`) | 1889/1892 (99.8%) | 1889/1892 | 1889/1892 |
| audio samples/frame | 548.4 (32768 Hz) | 1097 (65536 Hz) | 548.5 (32768 Hz) |
| host time (x86, not the Miyoo's Cortex-A7) | ~0.15 ms/frame | ~0.12-0.17 ms/frame | ~0.15 ms/frame |
| BIOS: none, all-zero, wrong size | ok | ok (built-in BIOS) | ok |

The 3 frames the game "lost" over 1892 are the mission-load lag frames, the same count on every core, so the
game runs at full speed with no emulator-specific slowdown. **Untested:** speed on the real ARM device.

### Dead Wars compatibility notes and suggested fixes (for the Dead Wars sessions)
1. **Blank Nintendo logo in the header** (`gbafix: logo source not found`). Harmless on Onion: mGBA detects
   the invalid logo and skips the BIOS intro, and gpSP's `boot_mode = "game"` never runs the intro. gpSP
   only checks bytes 0x03 = 0xEA and 0xB2 = 0x96, which Dead Wars has. On real hardware or with an
   emulator that runs the real BIOS intro, the logo check would fail (**untested**). Fix: build with
   `make LOGO_ROM=<a ROM you own>`, or have gbafix.py fill in the logo as devkitPro's gbafix does.
2. **Auto state after an update.** When a new Dead Wars build replaces the old file with the same name, Onion's
   `.state.auto` from the old build freezes the game on both cores (reproduced above, with the previous build).
   The engine cannot prevent this. Fix: put the version in the ROM file name (e.g. `Dead Wars v0.3.gba`) so
   each build gets its own states, and say "Reset game" in the README.
3. SRAM, waitstates (`WAITCNT = 0x4317`), crt0 and the IRQ handler with `VBlankIntrWait`: no problems on any
   core, including mGBA running the Cult-of-GBA BIOS (real BIOS code for the SWIs instead of HLE; gpSP rejects that
   BIOS because its first byte is not 0x18 and uses its built-in one).
4. Pre-existing compiler warning, not from this lab: `src/text.c:29: array subscript 2 is outside array bounds
   of 'char[2]'`.
