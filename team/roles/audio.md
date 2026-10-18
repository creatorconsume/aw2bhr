# Role brief: Audio (Dead Wars)

You own the sound of Dead Wars: original music and sound effects that cost almost nothing on the CPU, because
the game must run at full speed on **gpSP** on the Miyoo Mini Plus. Read `team/README.md`, the charter, first.
It covers the inbox protocol, branch flow and definition of done.

- **Session:** `session_0168cF2mwktk5orSxuVfkrjt`, an existing session. A replacement session takes the same branch.
- **Branch:** `claude/deadwars-dev`. Keep this name.
- **You own:** `deadwars/src/sound.c`, `deadwars/src/sound.h`, `deadwars/audio/**` and
  `deadwars/tools/*audio*`/`*music*` scripts (today that is `tools/music.py`). Your own `team/status/audio.md` and
  `team/backlog/audio.md` are yours too.
- **Not yours (any more):** the `Makefile`, `gbarun.c`, `game.h` (`Profile`), and the hook lines in `battle.c`, `main.c`,
  `text.c`, `ai.c` and `video.c` all belong to the engineer. Your commit 30bbfa5 edited them before the team split, and the
  producer will merge it as it is. From now on, touch those files only to add or move a one-line `sfx()`/`music_play()`
  hook, and drop a note in `team/inbox/engineer/` each time. A new Makefile rule or `Profile` field is a request to the engineer.

## Setup (fresh Ubuntu container)

```sh
sudo apt-get update && sudo apt-get install -y gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libmgba-dev python3-pil build-essential
cd /home/user/aw2bhr            # or: git clone https://github.com/creatorconsume/aw2bhr && cd aw2bhr
git fetch origin
BR=claude/deadwars-dev
git checkout -B $BR origin/$BR          # the branch already exists; an existing session just stays on it
git merge --no-edit origin/claude/dreamy-fermi-yhrz7u   # once "hardening merged" is posted
make -C deadwars                                        # also runs tools/music.py -> build/gen/music.{c,h}
gcc -O2 -o /tmp/gbarun deadwars/test/gbarun.c -lmgba    # runner; your version adds record/peek/channels
deadwars/test/smoke.sh                                  # must print PASS
```

**Audio checks.** Your `gbarun` can record: `record NAME` writes `OUTPREFIX-NAME.wav` (32768 Hz, stereo, s16), `stop`
closes it, and `channels MASK` solos channels. You can't listen to the result, so analyse it in Python instead: peak and RMS
per song and per SFX, clipping, silence gaps, and loop seams. For gpSP CPU cost, use QA's emu-lab `lrun` with
`count frame_count` (see `team/roles/engineer.md`, the gpSP profiling note).

## Read first

- `deadwars/README.md`: the game and its moments (night falls, infection, nests, ambush, overrun, evac).
- Your own `deadwars/src/sound.h`, `deadwars/src/sound.c` and `deadwars/tools/music.py` (the format is in its docstring),
  and `deadwars/audio/music.txt` (songs: title, day, night, horde, victory, defeat).
- `docs/miyoo/ONION_RESEARCH.md`: the core options Onion ships, including the audio settings for mGBA and gpSP.

## Design rules

- PSG only, as now: two squares, wave and noise, with one sequencer tick per frame in `sound_update()` and no software mixing.
  If you ever want Direct Sound samples, propose it to the producer first with gpSP numbers.
- Budget: on the gpSP Onion core, `sound_update()` takes less than about 2% of a frame on average and never causes a missed frame.
- SFX borrow a channel and give it back. The music must recover cleanly, with no stuck notes after a menu, a suspend or a reset.
- Small speaker: keep melodies in the mid range, avoid heavy bass, and make sure SFX cut through the music.
  The Music and SFX options must work and persist (`options_screen`, `Profile.snd_flags`).
- All music is original. Don't transcribe or quote existing game or film themes.

## First tasks

The detailed, prioritized version of this list is in `team/backlog/audio.md`. It is yours to keep current.

1. **Land your work.** After **hardening merged**, merge integration into your branch. Resolve the conflicts in
   `battle.c`/`video.c` with care, because the map animation and hardening changes also touch them. Keep only hook lines,
   then push and drop a note in `team/inbox/producer/`.
2. **Keep the host simulator building.** `make -C deadwars/test/sim` compiles `battle.c` and `ai.c` without `sound.c`.
   Give it `#ifdef HOST_SIM` no-op inline versions of `sfx`/`music_play`/`sound_update` in `sound.h`, which is your file,
   so the designer's `stubs.c` doesn't need to change.
3. **Measure.** Record CPU cost on gpSP and mGBA, and loudness per song and SFX. Put the numbers in your status file.
4. **Document.** Write `deadwars/audio/README.md` with the song list and the state that triggers each song, the SFX list and where
   each one fires, and how to add a song.
5. **Part Two.** Read the designer's `deadwars/docs/design/PART_TWO.md` and `BOSS_PATIENT_ZERO.md` (on
   `origin/claude/dw-design`) and `team/inbox/audio/`. Write a Patient Zero boss theme and stingers for the new events.

## Definition of done (every push)

- `make -C deadwars clean && make -C deadwars 2>&1 | grep -E 'warning|error'` prints nothing for your files.
  `tools/music.py` errors are fatal, so they must be clear.
- `deadwars/test/smoke.sh` prints PASS, and `make -C deadwars/test/sim` builds.
- The gpSP cost and loudness numbers in `team/status/audio.md` are current. Inbox items are under Inbox done.
- Generated `build/gen/music.*` and recorded wavs are never committed.

## Coordination

- Read your inbox on every branch before each task. The command is in `team/README.md`. Merge integration before each task and
  every 30-45 minutes.
- The designer tells you which moments need sound. The engineer owns where hooks live and the per-frame call order. QA
  reports audio bugs such as stuck notes or silence on gpSP.
- Commits: `Dead Wars: audio: <what>`, ending with your session's two attribution lines.
