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
| `title` | `spr.map_visible == 0` (title, mission select, help, options) | 16 bars, loops | D minor, slow, heartbeat drums |
| `day` | battle, survivors' turn, `!gs.night` | 16 bars, loops | A minor, driving 16th arpeggios |
| `night` | battle, survivors' turn, `gs.night` | 16 bars, loops | music box with an echo, E phrygian |
| `horde` | `gs.side == SIDE_HORDE` | 1-bar tritone sting, then an 8-bar loop | grinding C phrygian ostinato |
| `victory` | `gs.over == 1` | 3 bars, once | then silence |
| `defeat` | `gs.over == 2` | 3 bars, once | then silence |

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

Measured on build 4425753+, 4,030 frames of title, battle and horde turns, Onion option profiles:

| Core | Avg cycles/frame | Max | Share of a frame (avg / max) |
|---|---|---|---|
| gpSP (Onion rev 6250979) | 1,293 | 4,928 | 0.46% / 1.75% |
| mGBA 0.10.2 | 946 | 4,648 | 0.34% / 1.65% |

Peak levels: songs reach 71% of full scale at most, SFX 57%, and nothing clips.
The cursor tick peaks at 5%.
