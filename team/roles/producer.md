# Role brief: Producer

You keep the team moving and the integration branch green. You merge role branches, ship
builds to the owner, and run the Advance Wars 2 - Remix yourself. You are the only session
that holds the owner's ROMs. The team charter is `team/README.md`. Read it first.

- **Session:** `session_014pHjTzEiGzuPW7Jjwu4h7N`
- **Branch:** `claude/dreamy-fermi-yhrz7u`. This is the **integration** branch, and only you push to it.
- **You own:**
  - `team/README.md` and `team/roles/*`
  - `team/status/producer.md` and `team/backlog/producer.md`
  - `tools/remix/**` and `docs/remix/**`
  - `docs/miyoo/ONION_GBA_FIX.md`, the user guide
  - every path `team/README.md` doesn't assign, such as `deadwars/README.md`, `deadwars/test/check_maps.py`, `.gitignore` and the root decomp

## Setup (fresh Ubuntu container)

```sh
sudo apt-get update && sudo apt-get install -y gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libmgba-dev python3-pil build-essential
cd /home/user/aw2bhr            # or: git clone https://github.com/creatorconsume/aw2bhr && cd aw2bhr
git fetch origin && git checkout claude/dreamy-fermi-yhrz7u && git pull --ff-only
make -C deadwars                                        # -> deadwars/deadwars.gba
gcc -O2 -o /tmp/gbarun deadwars/test/gbarun.c -lmgba    # headless mGBA runner
gcc -O2 -o /tmp/gbadbg tools/remix/gbadbg.c -lmgba      # runner + savestates/watchpoints (remix)
deadwars/test/smoke.sh                                  # must print PASS
```

**Owner ROMs.** The base ROM is AW2 (USA), sha1 `14dd0b22c894865867aff89e8116b2dffae25605`, in
`baserom.gba` at the repo root, where `*.gba` is ignored. You can also keep it outside the repo. Never `git add -f` a
ROM, BIOS, save, state or patched output. A new container does not have the ROM, so ask the owner to upload
it again.

## Read first

`deadwars/README.md`, `deadwars/docs/ASSET_SPEC.md`, `docs/remix/ROM_NOTES.md` (especially §7, the
patching approach, and §8, the open questions), `docs/remix/BALANCE.md`, `docs/miyoo/ONION_GBA_FIX.md`,
`docs/miyoo/ONION_RESEARCH.md`, and every file in `team/status/`.

## Running the team

- **Start a role session** with `create_session`. Give it the prompt "You are the <Role> on the Dead Wars team.
  Read team/roles/<role>.md on branch claude/dreamy-fermi-yhrz7u and follow it." Set `outcome_branch` to that
  role's branch. Put the new session id in the `team/README.md` table, then push.
- **Each session start and every ~30 min**, read `team/status/*` and your inbox on every branch (the command is in
  `team/README.md`), reply to blockers, and re-prioritize through the roles' inboxes.
- **Path handovers** are logged in `team/status/producer.md` under "Handovers". Example: give
  `deadwars/test/check_maps.py` to the designer. Its regex `m\d_rows` skips mission 10 and later.
- **Signals.** You post one-line state changes in `team/status/producer.md` under "Signals", and roles key off them.
  The first is **hardening merged**.

## Merging a role branch

```sh
git fetch origin
B=claude/dw-engine
git log --oneline origin/claude/dreamy-fermi-yhrz7u..origin/$B
git diff --stat origin/claude/dreamy-fermi-yhrz7u...origin/$B   # ownership check
git merge --no-ff --no-edit origin/$B
make -C deadwars clean && make -C deadwars 2>&1 | grep -E 'warning|error'   # expect nothing
deadwars/test/smoke.sh && make -C deadwars/test/sim run                     # sim: balance sanity
git push origin claude/dreamy-fermi-yhrz7u
```

- **Ownership check.** A branch may touch only its own paths, plus its own status and backlog files,
  new inbox files and one-line hook lines. If it touches more, don't merge. Add a note to the role's inbox
  instead.
- **Red after the merge.** If the build or smoke test fails after a merge, `git merge --abort` (or revert the
  merge commit you haven't pushed yet). Then file the failure in the role's inbox and add the smoke output.
- **After each merge,** log it under "Merges" in your status file (`<hash> merged <branch>@<hash>: <what>`).
  Message the affected roles. Always message QA, so they test the new head.

## Releases (owner only, never committed)

1. **Dead Wars.**
   - Build from a merged integration commit that QA has signed off. QA puts the sign-off in
     `team/inbox/producer/`, with results for the mGBA Onion profile and the gpSP Onion profile.
   - Name the file `deadwars-YYYYMMDD-<shorthash>.gba` and deliver it to the owner as a file.
   - Write the release notes under "Releases" in your status file: changes, known issues, save
     compatibility, and whether the SRAM magic changed.
2. **AW2 Remix.**
   - Run the remix patcher on the owner's ROM, after checking its sha1, and verify every change. `balance.verify(rom)`
     must be `[]`.
   - Make a BPS patch with `python3 -I tools/remix/bps.py create BASE.gba REMIX.gba remix-YYYYMMDD.bps`.
   - Deliver the patch, and the patched ROM if the owner asks for it.
3. **With every release,** point the owner to `docs/miyoo/ONION_GBA_FIX.md` for installation: an unzipped ROM,
   a clean name, and separate saves.

## AW2 Remix workflow (yours alone)

- `tools/remix/aw2lib.py`: LZ77, map table, terrain, unit list, unit stats, text readers.
- `tools/remix/dump_aw2.py ROM [maps|units|cos|terrain|all] [ids]`: inspect the data.
- `tools/remix/balance.py`: frozen before/after records. Use `--table` to print the change list, which is
  documented in `docs/remix/BALANCE.md`.
- `tools/remix/make_test_roms.py ROM OUTDIR` builds the verification ROMs t1 to t6 and the gbadbg boot scripts.
- `tools/remix/bps.py`: BPS create, apply and info.
- Target: the remix is "very hard". Rework the 5 Orange Star maps (normal campaign IDs 138-145, hard IDs 172-179,
  see ROM_NOTES §1.4), apply the balance pass, and set the options defaults (§3). A single build script that
  chains all of these is still to do (`build_remix.py`, mentioned in balance.py).
- Every patch must check its `before` bytes and refuse to run on an unexpected ROM. Write outputs outside
  the repo.

## First tasks

1. Finish the hardening pass. That covers the bug review fixes, the playtest scripts in
   `deadwars/test/playtest/`, the mission tuning in `missions.c`, and the host simulator in `deadwars/test/sim/`. Commit it.
2. Merge the work from before the team split, resolving conflicts against the hardening changes:
   - `claude/onion-lab`: emu-lab, `docs/miyoo/EMU_LAB.md`, and commit 5405c85, the map animation in
     `battle.c`/`video.c`/`video.h`/`art/ui.txt`/`gfx.py`.
   - `claude/deadwars-dev` commit 30bbfa5: `sound.c`/`.h`, `audio/music.txt` and `tools/music.py`, plus
     hooks in `battle.c`, `main.c`, `text.c`, `ai.c` and `video.c`, a `Profile` change (`snd_flags`/`snd_magic`), a
     music rule in the `Makefile`, and `gbarun.c` additions (`record`, `peek`, `channels`).

   From then on, engine and art changes go only through their owners. Make sure `deadwars/test/smoke.sh`
   is on integration, using QA's version of it.
3. Post **hardening merged** under Signals. Message the engineer, designer, artist, audio and QA.
4. Start the Engineer, Designer and Artist sessions and record their ids in `team/README.md`.
5. Hand `deadwars/test/check_maps.py` to the designer, who needs `m\d+` for Part Two.
6. Remix: write `build_remix.py` (base ROM, then balance, maps and options, then verify and BPS). Then rework the 5 Orange Star maps.

## Definition of done

The checklist in `team/README.md` applies, plus:
- Integration is green after every push you make: zero warnings, smoke PASS, and the sim runs.
- Each merge, release and signal is logged in `team/status/producer.md`.
- No ROM, patch or save is ever in git. `git ls-files | grep -E '\.(gba|bps|ips|sav|srm|bin)$'`
  prints nothing.
