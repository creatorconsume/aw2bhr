# Dead Wars audio

All music and sound effects are original. They use only the four GBA PSG channels:

| Channel | Music role | Borrowed by SFX for |
|---|---|---|
| sq1 (square, with sweep) | lead melody | most game-event SFX |
| sq2 (square) | harmony / arpeggio | UI sounds, bite growl, two-voice stings |
| wave | bass / drone | (never) |
| noise | drums | footsteps, shots, bites, explosions, fire, the helicopter |

`sound_update()` (in `src/sound.c`) is called once per frame from `frame()` right after VBlank. It
advances the sequencer one tick and writes a few registers. Nothing is mixed in software.

## Songs (`audio/music.txt`)

The song is chosen automatically every frame by `director()` in `src/sound.c`, so gameplay code never
asks for music:

| Song | Plays when | Length | Notes |
|---|---|---|---|
| `title` | `spr.map_visible == 0` (title, mission select, options) | 16 bars, loops | D minor, slow, heartbeat drums |
| `briefing` | a `dialogue()` on the title (How to Play), before a battle starts (mission intro), or after the victory jingle (outro) | 8 bars, loops | quiet pad and music box, no drums (the noise channel is left for text blips) |
| `day` | battle, survivors' turn, `!gs.night` | 16 bars, loops | A minor, driving 16th arpeggios |
| `night` | battle, survivors' turn, `gs.night` | 16 bars, loops | music box with an echo, E phrygian |
| `laststand` | survivors' turn of the last 2 days of a `GOAL_SURVIVE` mission (`day + 1 >= goal_days`) | 8 bars, loops | E minor, fast |
| `horde` | `gs.side == SIDE_HORDE` | 1-bar tritone sting, then an 8-bar loop | grinding C phrygian ostinato |
| `zero` | a living horde unit with `UF_BOSS` (Patient Zero, M10), both turns | 16 bars, loops | D phrygian, tritone stabs. Switches on by itself once the engine defines `UF_BOSS` |
| `victory` | `gs.over == 1` | 3 bars, once | then `briefing` under the outro |
| `defeat` | `gs.over == 2` | 3 bars, once | then silence |
| `credits` | only via `music_play(MUS_CREDITS)` (no credits screen yet) | 16 bars, loops | C major, hopeful |

- Mid-battle dialogue (for example a boss line) keeps the battle music. `dialogue()` brackets itself with
  `sound_dialogue(true/false)`, which is how the director knows a dialogue is open.
- The boss check scans the units every 16 frames, which keeps the per-frame cost flat.
- When the horde's turn ends, `day`/`night` resume from where they left off. If day and night swapped
  (a new day started), the new theme starts from the top.
- `music_play(id)` forces a song (the Sound Test uses it). `music_play(MUS_AUTO)` hands control back to
  the director.
- Music Off (`Profile.snd_flags`) silences the music but keeps the SFX.

## Sound effects (`sfx(SFX_...)`, tables at the bottom of `src/sound.c`)

| SFX | Fires in | Channels |
|---|---|---|
| `CURSOR` | map cursor moves (`battle.c` player_turn), menu up/down (`text.c` text_menu) | sq2 |
| `CONFIRM` | menu A, selecting a unit | sq2 |
| `CANCEL` | menu B, backing out of a move | sq2 |
| `MENU` | any `text_menu` opening | sq2 |
| `STEP` / `SHUFFLE` | each tile a survivor / zombie moves (`unit_move_anim`) | noise |
| `SHOT` / `BITE` | every hit, survivor / zombie attacker (`hit`) | noise + sq1 / sq2 |
| `DESTROY` | `kill_unit` | noise + sq1 |
| `INFECT` | a killed survivor rises as a Walker (`kill_unit`) | sq1 + noise |
| `SECURE` | survivors capture a building (`do_capture`) | sq1 |
| `OVERRUN` | zombies take a building (`ai.c` act) | sq1 |
| `BURN` | a nest is burned (`do_capture`) | noise + sq1 |
| `FOUND` / `EMPTY` / `AMBUSH` | search results (`search_house`); `AMBUSH` also for a fog ambush in player_turn | sq1 (+ sq2 + noise) |
| `NIGHTFALL` / `DAWN` | automatically, when a new day flips `gs.night` (in `sound_update`) | sq1 (+ sq2) |
| `BUILD` | recruiting at a Workshop (`build_menu`) | noise + sq1 |
| `EVAC` | a unit evacuates at a helipad | noise + sq1 |
| `HEAL` | start of the survivors' turn, a hurt unit heals on an owned building (`start_side_turn`) | sq2 |
| `BURST` | a Bloater bursts (`kill_unit`) | noise + sq1 |
| `ACID` | a Spitter hits (`hit`) | noise + sq2 |
| `WAVE` | "A huge wave!" (`ai.c` horde_turn) | sq1 + noise |
| `BUZZ` | A on a disabled menu item (`text_menu`) | sq2 |
| `BLIP` | dialogue typewriter, every other letter (`dialogue`) | noise, very quiet |
| `CALL` / `LINK` / `ROAR` | Patient Zero: summons at night / dies ("The link is broken.") / attacks or frenzies. **Not hooked yet**: they wait for the engine's boss code (requested in `team/inbox/engineer/`) | sq1 + sq2 + noise |

Priorities are 0 (cursor/steps), 1 (UI), 2 (combat), 3 (stings). A new SFX takes a channel unless a
higher-priority one is playing there. When an SFX ends, the channel goes back to the music, which
retriggers its current note.

## Adding a song

1. Write it in `audio/music.txt`. The format is documented at the top of `tools/music.py`.
   Give every channel the same length. Put `L` where the loop starts, at the same point in every
   channel, or leave it out for a one-shot jingle. The compiler refuses mismatched lengths and loop points.
2. `make` regenerates `build/gen/music.{c,h}`. The song becomes `MUS_<NAME>` and shows up in the Sound Test.
3. To have it play automatically, add the game state that selects it to `director()` in `src/sound.c`.
   For scripted moments, call `music_play(MUS_<NAME>)` and then `music_play(MUS_AUTO)`.

Keep melodies in octaves 4-6 and bass in octaves 2-3: the Miyoo speaker is small.

## Adding an SFX

Add an enum value before `SFX_COUNT` in `src/sound.h`. Then add its step table, its `sfx_tab` entry
and its Sound Test name in `src/sound.c`. Steps are register snapshots (square frequency register
or noise NR43, duty, envelope, sweep) held for N frames, with an optional per-frame frequency slide.

## Checking audio

`test/gbarun.c` can `record NAME` (WAV, 32768 Hz stereo), `stop`, solo channels with `channels MASK`, and
`peek` memory. The Sound Test (Options > Sound Test) plays any song or SFX, which makes it easy to
record each one on its own. Build with `make CC="arm-none-eabi-gcc -DSOUND_PROFILE"` to count the
CPU cycles spent in `sound_update` (`snd_prof_total`, `snd_prof_max`).

Measured over 4,030 frames of title, battle and horde turns with the Onion option profiles (10 songs, 29 SFX build):

| Core | Avg cycles/frame | Max | Share of a frame (avg / max) |
|---|---|---|---|
| gpSP (Onion rev 6250979) | 1,292 | 4,981 | 0.46% / 1.77% |
| mGBA 0.10.2 | 1,113 | 4,760 | 0.40% / 1.69% |

Peak levels: songs reach 72% of full scale at most, SFX 74% (the Link death sting), and nothing clips.
The cursor tick peaks at 4%.
