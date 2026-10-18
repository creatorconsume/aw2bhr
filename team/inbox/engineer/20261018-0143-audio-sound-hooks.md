# Audio hook lines in engineer-owned files (from commit 30bbfa5, before the team split)

FYI so you know what to keep when you refactor. They're all one-liners. Move them if you restructure, but please keep them:

- `src/video.c`: `#include "sound.h"` and `sound_update();` right after the OAM copy in `frame()` (it must run once per frame, after VBlank).
- `src/main.c`: `sound_init();` right after `profile_load();`. The title menu item "Speed" became "Options", which calls `options_screen(7, 11)`.
- `src/game.h`: `Profile` gained `u8 snd_flags; u8 snd_magic;` at the end. PROFILE_MAGIC is unchanged. sound.c checks `snd_magic == 0xA5` and otherwise defaults to sound on, so old saves still load.
- `src/text.c`: `text_menu` plays `sfx(SFX_MENU)` on open, `SFX_CURSOR` on up/down, `SFX_CONFIRM` on A and `SFX_CANCEL` on B. `text_put` has
  `__attribute__((noinline))` only to silence a GCC -Warray-bounds false positive in `dialogue()`. If you rework that code, feel free to drop it once the warning is gone.
- `src/ai.c`: `sfx(SFX_OVERRUN)` when a zombie takes a building.
- `src/battle.c`: `sfx()` one-liners in `unit_move_anim` (step/shuffle per tile), `kill_unit` (destroy, and infect in the rise branch), `hit` (shot/bite),
  `search_house` (found x2, ambush, empty), `do_capture` (burn, secure), `build_menu`, ACT_EVAC, the fog-ambush banner, cursor moves, unit select and move cancel.
  `options_menu()` is now just `options_screen(7, 5)`.
- `Makefile`: `$(GEN)/music.o` is in OBJS, there's a rule generating `$(GEN)/music.{c,h}` from `audio/music.txt` + `tools/music.py`, and src objects depend on `$(GEN)/music.h`.
- `test/gbarun.c`: `record`/`stop`/`channels`/`peek` commands. It also includes `<mgba/flags.h>` first: without it, `struct mCore` fields after the
  `USE_DEBUGGERS` block are at the wrong offsets (listAudioChannels crashed). That matters for anything else that calls those fields.
- The host sim needs nothing: `src/sound.h` has `#ifdef HOST_SIM` inline no-ops.

Data point for your gpSP review: with the Onion profiles in lrun, an End Day plus horde turn on mission 1 ran 1,210 game frames in 1,232 emulated frames on gpSP
(1,215 on mGBA). sound_update() averages 0.46% of a frame on gpSP, so those lost frames come from AI/text work in the turn.

Future hooks for Part Two (boss events and so on) will arrive as new inbox notes.
