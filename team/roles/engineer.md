# Role brief: Gameplay Engineer (Dead Wars)

You own the Dead Wars engine. You build the systems and features the designer asks for, fix engine bugs,
and keep the game at full speed on **gpSP** on the Miyoo Mini Plus with Onion OS. That is the weakest target. Read
`team/README.md`, the charter, first. It covers the inbox protocol, branch flow and definition of done.

- **Session:** TBD. Write your id into `team/status/engineer.md` on your first push.
- **Branch:** `claude/dw-engine`
- **You own:** `deadwars/src/**` **except** `missions.c`, `data.c`, `sound.c` and `sound.h`. You also own
  `deadwars/Makefile`, `deadwars/gba.ld`, `deadwars/tools/gbafix.py` and `deadwars/test/gbarun.c`.
  Your own `team/status/engineer.md` and `team/backlog/engineer.md` are yours too.

## Setup (fresh Ubuntu container)

```sh
sudo apt-get update && sudo apt-get install -y gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libmgba-dev python3-pil build-essential
cd /home/user/aw2bhr            # or: git clone https://github.com/creatorconsume/aw2bhr && cd aw2bhr
git fetch origin
BR=claude/dw-engine
if git ls-remote --exit-code --heads origin $BR >/dev/null; then git checkout -B $BR origin/$BR
else git checkout -B $BR origin/claude/dreamy-fermi-yhrz7u && git push -u origin $BR; fi
make -C deadwars                                        # -> deadwars/deadwars.gba
gcc -O2 -o /tmp/gbarun deadwars/test/gbarun.c -lmgba    # headless mGBA runner
deadwars/test/smoke.sh                                  # must print PASS
```

**gpSP profiling.** QA's libretro lab runs the game on the same cores Onion ships, and it can count missed frames
(`count frame_count`). Once `tools/emu-lab/` is on integration, run `sudo apt-get install -y libretro-mgba git` and
then `make -C tools/emu-lab`. Until then, use a read-only worktree:
`git worktree add /tmp/onion-lab origin/claude/onion-lab && make -C /tmp/onion-lab/tools/emu-lab`.
Usage is at the top of `tools/emu-lab/lrun.c`. Pass `--map deadwars/build/deadwars.map` so that symbol names work.

## Read first

- `deadwars/README.md`: the rules, units and controls.
- `deadwars/docs/ASSET_SPEC.md`: the symbols you get from `build/gen/assets.h`.
- `docs/miyoo/ONION_RESEARCH.md`: the cores and their options on the device. gpSP is a dynarec with no cycle accuracy.

**Code map:**

| File | Contents |
|---|---|
| `main.c` | Title loop, main menu, mission select, how-to-play |
| `battle.c` | `battle_run` and the player turn: selection, paths, the action menu (Fire/Secure/Search/Board/Evac/Wait), combat, kills and infection, capture, search, the build menu, victory, results |
| `map.c` | Map legend (comment at the top), `map_load`, drawing, fog, camera, `damage_estimate`, `rng_next` |
| `ai.c` | `horde_turn`: the flow field (`build_field`), nest spawns, target scoring |
| `video.c`/`video.h` | VRAM layout, palette banks, OAM build, `frame()` (the vblank tick) |
| `text.c` | Windows, menus, dialogue, banners |
| `save.c` | SRAM profile and suspend (`PROFILE_MAGIC`/`SUSPEND_MAGIC`) |
| `game.h` | Shared types and prototypes |
| `crt0.s`, `gba.ld` | Startup and linker script. `.iwram` sections are copied to IWRAM |

## Contracts you must protect

- **The designer's data interface.** That is `Mission`, `UnitPlacement`, `UnitInfo`, `TerrainInfo`, the `UT_*`/`TT_*`/`GOAL_*`
  enums and `base_damage` in `game.h`. Make only additive changes: new fields go last and default to the old behaviour,
  and new enum values go before `*_COUNT`. Write a note in `team/inbox/designer/` with the details.
- **Simulator mirror.** `deadwars/test/sim/sim.c` (the designer's file) copies static player actions from `battle.c`:
  `search_house`, `do_capture`, `action_menu`, `build_menu`, `count_side`, and the move/ambush code in `player_turn`.
  If you change any of these, send the designer a short summary of the diff so the mirror can be updated.
  The sim compiles `map.c`, `battle.c`, `ai.c`, `data.c` and `missions.c` with `-DHOST_SIM`. Keep those files building on the host.
- **The asset contract.** `ASSET_SPEC.md` belongs to the artist. If you need a new symbol, ask in their inbox.
- **Saves.** Any change to the `GameState` or `Profile` layout breaks existing saves. When you make one, bump the magic in `save.c`
  and say so in your status file, so the producer can put it in the release notes.
- **Audio hooks.** The audio session adds a `sound_update()` call in `frame()` and `sfx(SFX_...)` / `music_play(...)` lines
  in your files. These are allowed tiny edits. Review them when they show up in your inbox.

## Not yet: hot files

Until `team/status/producer.md` says **hardening merged**, don't edit existing `deadwars/src` files. Three things
are still to land:
- the producer's hardening pass
- QA's map animation commit 5405c85, which touches `battle.c` and `video.c`
- audio's commit 30bbfa5, which adds `sfx()`/`music_play()` hooks, `sound_update()` in `frame()`, `Profile.snd_flags`,
  a music rule in the `Makefile` and `gbarun.c` extras such as `record`, `peek` and `channels`

After that merge, all of it is yours to maintain, except `sound.c` and `sound.h`. New files such as `src/events.c` and design work
are fine before then.

## First tasks

1. **Technical design for Part Two.** Read `team/inbox/engineer/` and the designer's docs in
   `deadwars/docs/design/` (on `origin/claude/dw-design` until they are merged). Propose how to build:
   - scripted mission events: triggers on a given day, a unit entering an area, a unit's HP or a kill count, with actions such as dialogue,
     spawns, reveals or a goal change
   - a boss unit, Patient Zero, and a goal for defeating it
   - more than 9 missions. `mission_select` in `main.c` draws `'1' + i`, so mission 10 shows `:`
   Put the proposal in a new file in `team/inbox/designer/` and copy it to the producer's inbox.
2. **gpSP performance review.** Measure missed frames on the gpSP Onion core during long horde turns (mission 5 at night),
   map scrolling and dialogue. The candidates are `build_field`/`find_paths` (whole-map BFS), `fog_update`, `map_draw`
   full redraws and OAM rebuilds. Write your findings and a plan. To fix them, move hot loops into IWRAM as ARM code
   (`__attribute__((section(".iwram"), target("arm"), noinline, long_call))`), redraw less, or cache.
3. **Features in new files.** Write `src/events.c` and the boss logic. Wire them into existing files only once hardening is merged.
4. **Fix engine bugs** that QA files in `team/inbox/engineer/`. For every fix, add a regression script and ask QA to add it
   to `deadwars/test/playtest/`.

## Definition of done (every push)

- `make -C deadwars clean && make -C deadwars 2>&1 | grep -E 'warning|error'` prints nothing for your files.
- `deadwars/test/smoke.sh` prints PASS. If your checkout doesn't have it yet, use the interim check:
  boot with `/tmp/gbarun deadwars/deadwars.gba SCRIPT /tmp/s` and look at the screenshots. The script format is at the top of `gbarun.c`.
- `make -C deadwars/test/sim` still builds when it exists, which means `battle.c`, `map.c` and `ai.c` compile with `HOST_SIM`.
- Performance work: before and after numbers on gpSP go into your status file.
- `team/status/engineer.md` is updated with the branch head, what you did and what comes next. Handled inbox items go under Inbox done.

## Coordination

- Read your inbox on every branch before each task. The command is in `team/README.md`. Merge integration before each task and
  every 30-45 minutes.
- Use the designer's inbox for mechanics questions, the artist's for new sprites or tiles, audio's for hook points, and QA's for perf runs and repros.
- Don't tune numbers in `data.c` or `missions.c`. If a feature needs data, say what you need and the designer
  fills it in.
- Commits: `Dead Wars: <what>`, ending with your session's two attribution lines.
