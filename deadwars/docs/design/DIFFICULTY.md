# Difficulty rubric: "very hard but fair"

The owner is an Advance Wars veteran who wants every mission to feel like a hard Advance
Campaign map. Every mission must pass all five checks before it ships.

## 1. Sim evidence

`make -C deadwars/test/sim run` (40 seeds by default, 200 for sign-off) must show:

| Missions | Bot win-rate band |
|---|---|
| 1 | 60–80% (tutorial pressure) |
| 2–4 | 35–60% |
| 5 | 25–45% |
| 6–7 | 35–55% |
| 8–9 | 30–50% / 30–45% |
| 10 | 20–40% |

- **Varied losses.** At least two different loss reasons each make up ≥15% of the losses. If
  every loss is "the Brute ate the bus", the mission is a gimmick, not a challenge.
- **No timeouts or stalls.** A seed that reaches `DAY_CAP` or `FRAME_CAP` is a bug.
- **No hopeless seeds.** Look at the 5 worst seeds by hand (verbose). If a seed is lost before
  the player's second turn because of where things spawned or how the dice rolled, it fails.
- The bot is "competent", not expert. A strong human should beat the band by about 20–30
  points. Keep that in mind when a mission sits at the top of its band.

## 2. Threats are telegraphed

- Dormant zombies, nests, the night schedule (`night_every`), the wave day and the deadline can
  all be read from the map, the objective screen or the intro dialogue.
- The intro or objective names every new rule (VIP, a safehouse you can lose, permanent night,
  boss regen).
- No hidden spawns next to the player beyond the house-search trap, which is documented and opt-in.

## 3. The first turn can't decide the game

- At the start there is always a defensible position within one move of your units.
- There is always a reason to take risks: income buildings, houses to search, radio towers, or a
  shorter route.

## 4. One idea per mission

Each mission teaches or stresses one thing. Part Two combines mechanics the player already
knows instead of adding new rules, except for the VIP (M6) and the boss (M10).

## 5. Tuning order (smallest lever first)

1. Dormant flags and placement of 1–3 horde units.
2. `spawn_every` / `spawn_night_extra`.
3. `day_wave` / `wave_size`.
4. Starting funds and `income_scale`.
5. Starting survivor roster.
6. Map edits.
7. `data.c` numbers. These affect every mission, so re-run all of them and record the results in
   `BALANCE_LOG.md`.
