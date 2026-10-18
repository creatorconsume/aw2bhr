# Balance simulator

A headless, host-side (x86 gcc) simulator that plays every mission many times against the
real horde AI and reports win rates. It compiles the actual game rules:
`src/map.c`, `src/battle.c`, `src/ai.c`, `src/data.c` and `src/missions.c`, built with
`-DHOST_SIM` so `src/gba.h` points the hardware registers at plain arrays. `stubs.c` replaces
video, text, input and save with no-ops.

```sh
make -C deadwars            # once, so build/gen/assets.h exists
make -C deadwars/test/sim run          # 40 seeds of every mission (SEEDS=200 for sign-off)
deadwars/build/sim/sim -m 3 -n 1 -v    # one game of mission 3 with an ASCII map per day
```

Options: `-m N` picks one mission, `-n` sets the number of seeds and `-s` the first seed.
`-d` sets the day cap (default 30). `-v` prints the map and every action; `-v -v` adds plan
scores and banners; `-v -v -v` dumps candidate tiles. `-Wknob=N` overrides a bot weight (see
`W_*` in `sim.c`).

Output columns: win %, outcome counts, then averages for the final day, kills, units lost, the
horde's peak size and goal progress (nests burned or units evacuated). The outcomes are:
`overrun` (Safehouse taken), `wiped` (no units and no way to recruit), `deadline` (evac too
late), `timeout` (still running at the day cap) and `STALL` (a horde turn went past
`FRAME_CAP` frames; a compute loop with no frames trips a 20 s alarm).

## The bot

`battle.c`'s `player_turn()` is interactive, so the player side is a greedy bot written in
`sim.c`. Each step it scores every (unit, tile, action) for the units that haven't moved and
carries out the plan that improves most on standing still. Scores weigh:

- damage dealt, kills, the expected counterattack, and Bloater bursts;
- expected damage over the next horde turn. Each zombie's bites are split among the units it can
  reach, and nearby friends that haven't moved yet count as backup. Death risk ramps up as that
  damage approaches the unit's HP;
- an objective field, which is a weighted BFS that charges extra for tiles held by zombies:
  nests (nests and rout missions), the helipad (evac) or the Safehouse (survival);
- healing on owned buildings, Medic adjacency, terrain stars, and a home guard for the Safehouse.

It searches houses, secures buildings, burns nests, boards the bus in the evac mission, and
recruits at the end of its turn (Pickup for evac, then Armor, Heavy, or a Survivor if it is
short of units). Fog is respected for targets and destinations, so ambushes happen. For
threats it remembers every zombie.

Player actions that are `static` in `battle.c` are mirrored line for line in `sim.c`:
`search_house`, `do_capture`, the move/ambush part of `player_turn`, `action_menu`
(Fire/Secure/Search/Board/Evac) and `build_menu`. If those rules change, update the mirrors.

**Known limits.** The bot holds a line well, but it attacks badly. It moves one unit at a time,
so it rarely makes the coordinated push a human would. On the nest, rout and evac missions
(2–4) it grinds slowly and often reaches the day cap, so on those maps its win rate is a lower
bound and a human playtest is needed. On the survival missions (1, 5) it plays close to how a
careful human would.
