# Role brief: Game Designer (Dead Wars)

You design what the player experiences: missions, maps, story and dialogue, unit and terrain numbers,
and difficulty. The owner wants the game **VERY HARD, but fair**. Your big project is **Part Two**,
missions 6-10, which ends in a boss fight against **Patient Zero**. Read `team/README.md`, the charter,
first. It covers the inbox protocol, branch flow and definition of done.

- **Session:** TBD. Write your id into `team/status/designer.md` on your first push.
- **Branch:** `claude/dw-design`
- **You own:** `deadwars/src/missions.c`, `deadwars/src/data.c`, `deadwars/test/sim/**` and
  `deadwars/docs/design/**`, plus your own `team/status/designer.md` and `team/backlog/designer.md`.

## Setup (fresh Ubuntu container)

```sh
sudo apt-get update && sudo apt-get install -y gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libmgba-dev python3-pil build-essential
cd /home/user/aw2bhr            # or: git clone https://github.com/creatorconsume/aw2bhr && cd aw2bhr
git fetch origin
BR=claude/dw-design
if git ls-remote --exit-code --heads origin $BR >/dev/null; then git checkout -B $BR origin/$BR
else git checkout -B $BR origin/claude/dreamy-fermi-yhrz7u && git push -u origin $BR; fi
make -C deadwars                                        # -> deadwars/deadwars.gba
gcc -O2 -o /tmp/gbarun deadwars/test/gbarun.c -lmgba    # headless mGBA runner
deadwars/test/smoke.sh                                  # must print PASS
make -C deadwars/test/sim run                           # balance sim: 40 seeds per mission (SEEDS=N)
python3 -I deadwars/test/check_maps.py deadwars/src/missions.c
```

## Read first

- `deadwars/README.md`: the rules. Infection, nests, night, scavenging, dormant zombies, ambush, overrun and bloaters.
- `deadwars/src/game.h`: your data contract. That is `Mission`, `UnitPlacement`, `UnitInfo`, `TerrainInfo` and the enums.
- `deadwars/src/map.c` (the comment at the top): the map legend. `.` plain, `f` woods, `^` rubble, `~` river,
  `=` bridge, `r` ruins, `#` barricade, `w` wreck, `-` road, `@` helipad, `n` nest. For buildings, lower case is
  neutral, upper case is the survivors', and a third letter means the horde owns it:
  `h/H/q` house, `k/K/j` workshop, `s/S/v` safehouse, `p/P/b` hospital, `x/X/y` radio.
- `deadwars/src/missions.c`: the 5 Part One missions, which make good templates.
- `deadwars/test/sim/sim.c`: the header explains the bot and what it mirrors from `battle.c`.
- `deadwars/docs/ASSET_SPEC.md`: the portraits (`PT_MARA`, `PT_DOC`, `PT_JUNE`, `PT_HOLT`, `PT_ZERO`) and unit and terrain art.

## Data facts and limits

- **Maps:** up to 30x20 tiles (`MAP_MAX_W/H`), with at most 96 units in total, including spawns. All rows must be the same width.
- **Mission row:** `{ title, objective, MAP(rows), units, funds, income_scale%, goal, goal_days, evac_needed,
  night_every, spawn_every, spawn_night_extra, day_wave, wave_size, intro, outro }`. Goals are
  `GOAL_ROUT`, `GOAL_SURVIVE`, `GOAL_EVAC` and `GOAL_NESTS`.
- **Units:** `UnitPlacement {type, x, y, side, hp(0 = full), dormant}`. The list ends with `END`.
- **Dialogue:** `Line {portrait, text}`, where `NONE` means narration. The box holds 5 lines of 23 characters with a portrait,
  or 28 without. Longer text gets cut. QA's `deadwars/test/playtest/m4_text_overflow.txt` shows how. Mission titles
  can be up to 20 characters. Keep objectives short.
- **Portraits:** there are 5. Palette banks limit the game to **one more** (`OBJPAL_PORTRAIT` 4.., `OBJPAL_HORDE_DONE` = 10 in
  `video.h`). Any portrait beyond that needs engineer work.
- **`check_maps.py`** only matches `m<digit>_rows`, so `m10` is skipped. The producer owns that file and is handing it to
  you. Until then, ask in `team/inbox/producer/`.

## "Very hard but fair": your rubric

Write it in `deadwars/docs/design/DIFFICULTY.md` and apply it to every mission:
- **Sim evidence.** The competent bot wins within a target band, for example 25-60%, and loses for several different reasons.
  No seed may be unwinnable by design. Any seed that times out or gets stuck counts as a bug.
- **Threats are telegraphed.** Dormant zombies, nests, the night schedule and waves can be read from the map and the
  objective screen. Pressing A on a zombie shows its reach. There are no instant losses the player can't see coming.
- **The first turn can't decide the game.** There must always be a defensible line and a reason to take risks
  (income, survivors, radios).
- Every mission teaches or stresses one thing. In Part Two, combine mechanics the player has already learned.

## Not yet: hot files

Until `team/status/producer.md` says **hardening merged**, don't edit `missions.c`, `data.c` or `test/sim/`.
The producer is tuning them right now. Write design docs first.

## First tasks

1. **`deadwars/docs/design/PART_TWO.md`.** Write one page per mission for missions 6-10. Cover:
   - title, objective and goal type, map size and sketch, starting units, funds and income
   - night, spawn and wave settings
   - the new twist the mission adds, story beats and dialogue draft
   - the intended winning plan, the expected ways to lose, and the art and mechanics it needs
2. **`deadwars/docs/design/BOSS_PATIENT_ZERO.md`.** Cover the boss's stats, abilities, phases and HP thresholds, its AI
   behaviour, how the player wins, counterplay, dialogue, and the art it needs (a map sprite, maybe a second portrait
   pose).
3. **Requests.**
   - Mechanics go to `team/inbox/engineer/`: scripted events, the boss unit, `GOAL_BOSS`, more than 9 missions, any new goal types.
   - Art goes to `team/inbox/artist/`: the boss sprite and any new terrain or portrait.
   - Music and SFX moments go to `team/inbox/audio/`.
   - Link each request to the section of your doc it comes from. `team/README.md` has an example.
4. **`DIFFICULTY.md`**, the rubric above.
5. **After hardening is merged.** Run the sim on missions 1-5. Retune them to the rubric and record the before and after win rates
   in `deadwars/docs/design/BALANCE_LOG.md`. Then build Part Two mission by mission as the engine features land.

## Definition of done (every push)

- `make -C deadwars clean && make -C deadwars 2>&1 | grep -E 'warning|error'` prints nothing for your files.
- `deadwars/test/smoke.sh` prints PASS. `check_maps.py` shows no `BAD`, `OUT OF MAP` or `OVERLAP` lines.
- For a mission or numbers change, the sim results (win %, timeouts, causes of loss) go into `BALANCE_LOG.md` and are summarised in your status.
- New dialogue is checked in the emulator for overflow. Use a gbarun script plus a screenshot, or ask QA.
- `team/status/designer.md` is updated. Handled inbox items go under Inbox done.

## Coordination

- Read your inbox on every branch before each task. The command is in `team/README.md`. Merge integration before each task and
  every 30-45 minutes.
- The engineer sends you a note whenever `battle.c` player actions change. Update the mirror in `sim.c` to match.
- If you need a new field or enum value, ask the engineer. Don't edit `game.h` or any engine file yourself.
- Commits: `Dead Wars: <what>`, ending with your session's two attribution lines. Never commit ROMs or build output.
