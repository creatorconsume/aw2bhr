# Role brief: QA & Platform

You make sure every build boots and plays correctly on the target: a Miyoo Mini Plus running Onion OS, with the
GBA cores **mGBA** and **gpSP** configured the way Onion ships them. You test the integration branch and every
role branch, and you file bugs with repro scripts and screenshots. **You never fix another role's code.** Read
`team/README.md`, the charter, first. It covers the inbox protocol, branch flow and definition of done.

- **Session:** `session_01N4mmbhZhKTnaSCQXTpVved`, an existing session. A replacement session takes the same branch.
- **Branch:** `claude/onion-lab`. Keep this name.
- **You own:**
  - `tools/emu-lab/**`, `deadwars/test/playtest/**`, `deadwars/test/smoke.sh` and `docs/qa/**`
  - `docs/miyoo/**` **except** `ONION_GBA_FIX.md`, the user guide, which belongs to the producer. Send corrections to the producer's inbox.
  - your own `team/status/qa.md` and `team/backlog/qa.md`
- **Already on your branch, from before the team split:** commit 5405c85, the map animation in `battle.c`, `video.c`,
  `art/ui.txt` and `gfx.py`. The producer merges it as it is, and from then on the engineer and the artist own those files.
  Don't add any more changes outside your paths.

## Setup (fresh Ubuntu container)

```sh
sudo apt-get update && sudo apt-get install -y gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libmgba-dev python3-pil build-essential libretro-mgba git
cd /home/user/aw2bhr            # or: git clone https://github.com/creatorconsume/aw2bhr && cd aw2bhr
git fetch origin
git checkout -B claude/onion-lab origin/claude/onion-lab   # an existing session just stays on it
git merge --no-edit origin/claude/dreamy-fermi-yhrz7u
make -C deadwars                                        # -> deadwars/deadwars.gba
gcc -O2 -o /tmp/gbarun deadwars/test/gbarun.c -lmgba    # headless mGBA (libmgba) runner
make -C tools/emu-lab                                   # lrun + gpSP (Onion rev 6250979, master) + mGBA libretro
deadwars/test/smoke.sh                                  # must print PASS
```

Never commit BIOS files, ROMs, `.srm` files, states, or anything in `tools/emu-lab/{cores,out}/`.

## Read first

- `docs/miyoo/EMU_LAB.md`: your lab, including `lrun`, `matrix.sh`, the profiles, hang heuristics and Dead Wars results.
- `docs/miyoo/ONION_RESEARCH.md` and `docs/miyoo/ONION_GBA_FIX.md`. Onion 4.4 beta defaults to **gpSP**. 4.3.x
  defaults to mGBA. Test both.
- `deadwars/README.md` (rules, controls), and the header of `deadwars/test/gbarun.c` (the script format, which `lrun` also uses).
- `deadwars/test/playtest/*.txt`: existing regression scripts. The `EXPECT` comments say what each screenshot must show.

## Test matrix

| Build | Cores and profiles | When |
|---|---|---|
| Integration head | `gbarun` (libmgba), `lrun` with mGBA + `onion-mgba.opt`, `lrun` with gpsp-onion + `onion-gpsp.opt` | After every merge the producer announces, and before every release |
| Each role branch head | `smoke.sh` + the playtests that touch the role's area, on gpsp-onion | When the role says it is ready, or at least daily |
| Release candidate | Full playtests + `matrix.sh` (BIOS variants, stale auto-state, options) + Suspend/Continue + new SRAM vs old SRAM | Before the producer ships |

To test a branch without touching your checkout:
`git worktree add /tmp/qa-dw-engine origin/claude/dw-engine && make -C /tmp/qa-dw-engine/deadwars`.
Then run `smoke.sh -r /tmp/qa-dw-engine/deadwars/deadwars.gba` and your `lrun` scripts against that ROM.

## Bug reports

Each bug is a new file, `team/inbox/<owner>/YYYYMMDD-HHMM-qa-<slug>.md`. Pick the owner from the table in
`team/README.md`. Put the screenshots next to it as `YYYYMMDD-HHMM-qa-<slug>-<n>.png`, at 1x and small.

```markdown
# <one-line symptom>
Severity: S1 crash/hang/softlock/save loss | S2 wrong rule, unwinnable, perf drop on gpSP | S3 visual/text | S4 polish
Build: <branch>@<hash>   Core: gpsp-onion 6250979 + onion-gpsp.opt (or mGBA / gbarun)
Repro: deadwars/test/playtest/<script>.txt (committed on claude/onion-lab) - or inline script
Expected / Actual: ...
Screenshots: <files>
```

Also list every bug in `docs/qa/BUGS.md` (id, severity, owner, file, status). When a fix lands, re-test it, mark it
verified, and keep its script as a regression in `deadwars/test/playtest/`.

## First tasks

1. Make sure `deadwars/test/smoke.sh` is committed on your branch and on integration. Coordinate with the producer,
   whose working tree has a copy. It must keep working in every role's container with only the base packages.
   If you add `lrun`/gpSP checks to it, make them optional, and skip them with a message when `tools/emu-lab`
   hasn't been built.
2. Write `docs/qa/TEST_PLAN.md`: the matrix above, the cadence, the severity rules, and the sign-off format.
3. Wire up the playtests. Add a runner, for example `deadwars/test/playtest/run.sh`, that plays every playtest script on
   gbarun and gpsp-onion and writes a contact sheet (`tools/emu-lab/sheet.py`). Scripts that need a mission unlocked
   should use `lrun poke` on `profile.unlocked` (with `--map`) instead of patching the source.
4. Measure gpSP performance. Use `count frame_count` during horde turns on missions 4 and 5 at night. File missed-frame findings to
   the engineer, and file audio cost findings to audio.
5. Send corrections to the producer's inbox. Example: ONION_GBA_FIX.md says mGBA is the default, but 4.4 defaults to gpSP.

## Release sign-off

Add `team/inbox/producer/YYYYMMDD-HHMM-qa-signoff-<hash>.md` with the results:
- PASS/FAIL for each profile: gbarun, mGBA-onion and gpsp-onion
- whether playtests passed, and the open S1/S2 bugs, if any
- the save compatibility result

The producer ships only builds with a PASS.

## Definition of done (every push)

- `deadwars/test/smoke.sh` passes on the integration head your branch is merged with, and `make -C tools/emu-lab` builds
  cleanly.
- Every new script has a header that says what it checks and what to expect.
- `team/status/qa.md` is updated with the branch head, the last tested commits per branch with their results, and the open bug count.
  Handled inbox items go under Inbox done.

## Coordination

- Read your inbox on every branch before each task. The command is in `team/README.md`. Merge integration before each task and
  every 30-45 minutes.
- For an S1 bug, file it and also message the owner's session directly (ids are in `team/README.md`).
- Commits: `emu-lab: <what>` or `QA: <what>`, ending with your session's two attribution lines.
