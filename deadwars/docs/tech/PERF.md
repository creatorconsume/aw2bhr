# Performance (gpSP / mGBA on Miyoo Mini Plus)

The GBA gives us 280,896 CPU cycles per frame (16.78 MHz / 59.73 Hz), of
which ~197k fall in the visible part. Game code runs as Thumb from ROM
(WAITCNT 3/1 + prefetch), so expect ~3–6 cycles per instruction.

Emulators on the Miyoo emulate every cycle whether we use it or not, so our
CPU budget doesn't change their host load much. What matters to the player is
whether **we** hold the screen still: any stretch of work longer than a frame
freezes animation (a "hitch"). We idle in `VBlankIntrWait` (BIOS halt), which
both mGBA and gpSP detect and fast-forward.

## How to measure

```sh
make -C deadwars PERF=1                 # -> deadwars-perf.gba (separate build-perf/)
gcc -O2 -o /tmp/gbarun deadwars/test/gbarun.c -lmgba
cd somewhere && /tmp/gbarun .../deadwars-perf.gba .../deadwars/test/perf_horde.txt out
python3 .../deadwars/test/perf_report.py out
```

`PERF=1` cascades timers 2+3 into a 32-bit cycle counter (`src/perf.h`) and
collects `n/sum/max` for: work per frame, `find_paths`, a zombie's decision
(`act()` up to the move), and `build_field`. It also replaces mission 1 with a
stress map: **30x20 (the format's maximum), 8 survivors in the middle, 60 awake
zombies (45 runners, 15 walkers)**. The script ends Day 1 and dumps `perf`
before and after the horde turn (the dump address in the script must match
`nm build-perf/deadwars-perf.elf | grep perf`).

Timing is mGBA's (cycle-accurate enough). gpSP doesn't model waitstates
exactly; it runs ROM code faster than real hardware, so these numbers are an
upper bound for it.

## Results: one horde turn, 60 zombies

| | before | after |
|---|---|---|
| `find_paths` avg / max | 129k / 185k | 44k / 53k |
| zombie decision avg / max | 500k / 730k (**2.6 frames**) | 109k / 145k (0.5 frames) |
| `build_field` (once per turn) | 790k | 419k |
| total AI compute in the turn | ~31M cycles (~110 frames, 1.8 s) | ~7M (~25 frames) |
| frames that overran | 61 | 10 |

Steady-state frame work (OAM build for 96 units + copy) is ~45k cycles, 16% of
a frame. The worst single frame in the turn (2.3 frames) is turn start:
spawning, `fog_update` and `build_field` back to back, right after the banner,
where a still screen is invisible.

The AI's choices are unchanged: the host simulator (`test/sim`, 20 seeds x 5
missions) prints identical results before and after.

## What was slow and what changed

1. **`unit_at` was a 96-unit scan**, called for every tile relaxation in
   `find_paths` and 2–3 times per reachable tile in `act()`. Now there is an
   occupancy grid (`occ_begin()/occ_end()` in `map.c`). It is *scoped*: it is
   built (~3k cycles) at the start of a hot section and used only while
   nothing moves, spawns or dies. Outside a scope `unit_at` falls back to the
   scan, so no other code has to keep a grid in sync.
2. **Division in BFS queues.** `c % MAP_MAX_W` and `c / MAP_MAX_W` compile to
   `__aeabi_idivmod` calls in Thumb (no hardware divide). Queues now hold
   packed `x | y << 8`.
3. **`act()` scored every survivor slot for every tile of the whole map.** It
   now builds a prey list once and only scans the zombie's move box.

## Rules of thumb for new code

- No `/` or `%` by non-powers-of-two in loops that run per tile or per unit.
- Don't call `unit_at` per tile outside an `occ_begin()` scope; never move or
  spawn units inside one.
- Keep any work you do between two `frame()` calls under ~150k cycles; if
  something must be heavier, do it behind a banner or a fade.
- Not done yet (not needed at current sizes): moving `find_paths` and
  `build_field` to IWRAM as ARM code (typically 2–3x faster), and caching
  `damage_estimate` divisions.
