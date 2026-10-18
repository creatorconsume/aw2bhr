# Team charter: Dead Wars, AW2 Remix, Onion compatibility

Work on this repo (`creatorconsume/aw2bhr`) is split across Claude Code cloud sessions that act
like a small game dev team. Each role has its own session, its own branch and its own files.
The producer integrates the role branches and ships builds to the owner. This file is the source
of truth for who owns what. Each role's onboarding brief is in `team/roles/<role>.md`.

## Projects

1. **Dead Wars**: an original GBA homebrew zombie tactics game, in `deadwars/`.
2. **Advance Wars 2 - Remix**: a very hard ROM hack. A Python patcher (`tools/remix/`,
   `docs/remix/`) builds it from the owner's own ROM. The ROM is never committed. Only the producer works on this.
3. **Miyoo Mini Plus / Onion OS compatibility**: `docs/miyoo/`, `tools/emu-lab/`.

**Target device:** Miyoo Mini Plus running Onion OS, with the GBA cores **mGBA** and **gpSP**. mGBA is the default
in Onion 4.3.x. QA found that 4.4 beta switches the default to gpSP (see `docs/miyoo/EMU_LAB.md` on `claude/onion-lab`).
Every build must boot and run at full speed on both cores. gpSP is the weaker of the two, so it sets the CPU budget.

## Team

| Role | Session | Branch | Owns |
|---|---|---|---|
| Producer | `session_014pHjTzEiGzuPW7Jjwu4h7N` | `claude/dreamy-fermi-yhrz7u` (integration) | `team/README.md`, `team/roles/*`, merges, releases, `tools/remix/**`, `docs/remix/**`, `docs/miyoo/ONION_GBA_FIX.md`, and any path not listed in this table |
| Gameplay engineer | TBD - see status | `claude/dw-engine` | `deadwars/src/**` except `missions.c`, `data.c`, `sound.c`, `sound.h`; plus `deadwars/Makefile`, `deadwars/gba.ld`, `deadwars/tools/gbafix.py`, `deadwars/test/gbarun.c` |
| Game designer | TBD - see status | `claude/dw-design` | `deadwars/src/missions.c`, `deadwars/src/data.c`, `deadwars/test/sim/**`, `deadwars/docs/design/**` |
| Artist | TBD - see status | `claude/dw-art` | `deadwars/art/**`, `deadwars/tools/gfx.py`, `deadwars/docs/ASSET_SPEC.md` |
| Audio | `session_0168cF2mwktk5orSxuVfkrjt` | `claude/deadwars-dev` | `deadwars/src/sound.c`, `deadwars/src/sound.h`, `deadwars/audio/**`, `deadwars/tools/*audio*` and `*music*` scripts |
| QA & platform | `session_01N4mmbhZhKTnaSCQXTpVved` | `claude/onion-lab` | `tools/emu-lab/**`, `deadwars/test/playtest/**`, `deadwars/test/smoke.sh`, `docs/qa/**`, `docs/miyoo/**` except `ONION_GBA_FIX.md` |

Once a new session starts, the `Session:` line in `team/status/<role>.md` is the authoritative
id. The producer copies it into this table.

The producer owns every path the table does not list, for example `deadwars/README.md`,
`deadwars/test/check_maps.py`, `.gitignore` and the decomp at the repo root. Ask through
the producer's inbox before you change one. The producer can hand a path to a role by writing
the handover in `team/status/producer.md`.

Each role edits its own `team/status/<role>.md` and `team/backlog/<role>.md`, and anyone can
add new files to any inbox.

## Files in `team/`

| Path | Who edits | What |
|---|---|---|
| `team/README.md` | producer | This charter |
| `team/roles/<role>.md` | producer | Onboarding brief for a fresh session in that role |
| `team/status/<role>.md` | that role only | Session id, branch head, current focus, last done, next, blockers, inbox done. Update it with every push |
| `team/backlog/<role>.md` | that role only | Prioritized checklist. On your first day, create it from the "First tasks" in your brief |
| `team/inbox/<role>/` | anyone, append-only | Requests to that role, one new file per request |

## Inbox protocol (no merge conflicts)

- To ask another role for something, create a **new** file in `team/inbox/<their-role>/` named
  `YYYYMMDD-HHMM-<from>-<slug>.md`, using UTC time (`date -u +%Y%m%d-%H%M`). `<from>` is your role
  name, for example `20261018-1530-designer-boss-unit.md`.
- Never edit or delete a file someone else created. To follow up, add a new file.
- The recipient handles the request, then records it under **Inbox done** in its own status file:
  `` `<filename>` -> outcome (commit / branch) ``. The request file stays where it is.
- Requests travel with your branch, so they reach the other role when the producer merges your branch. If
  you need an answer sooner, also message the target session (`send_message` with the session id from
  the table): "New request in team/inbox/engineer/20261018-1530-designer-boss-unit.md on
  claude/dw-design: <one line>".
- Read your inbox at the start of every task. That means your own branch, the integration branch, and the
  other branches, because requests can sit on a branch that has not been merged yet:
  ```sh
  git fetch -q origin
  for b in $(git branch -r --format='%(refname:short)' | grep '^origin/claude/'); do
    git ls-tree -r --name-only "$b" team/inbox/<you>/ | sed "s|^|$b |"; done | sort -k2 -u
  git show origin/<branch>:team/inbox/<you>/<file>     # read one request
  ```

### Example: asking for something

The designer needs a boss unit for mission 10 and adds
`team/inbox/engineer/20261018-1530-designer-boss-unit.md`:

```markdown
# Boss unit "Patient Zero" for mission 10
From: designer (claude/dw-design @ 1a2b3c4)   Priority: P1, blocks the M10 playtest
Spec: deadwars/docs/design/BOSS_PATIENT_ZERO.md (sections 2-3)
Need: unit type UT_ZERO (horde side, never dormant, 2-tile reach, spawns 1 Walker next to it
      every 2 horde turns); mission goal "kill unit X" (GOAL_BOSS) with a portrait line when it drops below 50 HP.
I will fill in: unit_info / base_damage rows in data.c and the M10 mission data, once the enum exists.
Art: requested separately in team/inbox/artist/20261018-1531-designer-zero-sprite.md
Done when: UT_ZERO and GOAL_BOSS are on claude/dw-engine, with a note on how to place them.
```

The designer then pings the engineer's session with `send_message`. When the work lands, the engineer adds a line to
`team/status/engineer.md` under Inbox done:
`` `20261018-1530-designer-boss-unit.md` -> done in 9f8e7d6 on claude/dw-engine ``.

## Branch flow

- Work only on your own branch. Commit small and push often: `git push -u origin <branch>`. If a push
  fails because of the network, retry up to 4 times, waiting 2s, 4s, 8s and 16s between tries.
- Before you start each task, and at least every 30-45 minutes, merge the integration branch:
  `git fetch origin claude/dreamy-fermi-yhrz7u && git merge --no-edit origin/claude/dreamy-fermi-yhrz7u`.
  Resolve any conflicts in your own files. For conflicts in someone else's files, keep their version and send them a note.
- Never rewrite published history. No force pushes, no rebasing pushed commits.
- Merge only the integration branch into yours. If you need another role's unmerged work, ask the
  producer to merge that branch into integration first.
- You may edit a file you don't own only to add tiny hook lines, such as a `sfx(SFX_SHOT);` call
  or one `#include`. Each time you do, drop a note in the owner's inbox. Anything bigger is a request.
- Never push to another role's branch or to the integration branch. Only the producer pushes to integration.
- Do not open pull requests unless the owner asks for them.

### Until "hardening merged"

The producer is finishing a hardening pass, with a bug review, playtest fixes and the host balance simulator in
`deadwars/test/sim/`. It touches `missions.c`, `deadwars/test/sim/*` and engine files. Two role branches also
carry work from before the team split that touches files they no longer own, and the producer will merge both
alongside the hardening pass:
- QA's commit 5405c85, the map animation, touches `battle.c`, `video.c`/`.h`, `art/ui.txt` and `gfx.py`.
- Audio's commit 30bbfa5, PSG music and SFX, touches `battle.c`, `main.c`, `text.c`, `ai.c`, `video.c`, `game.h` (`Profile`),
  the `Makefile` and `gbarun.c`.

Until `team/status/producer.md` says **hardening merged**:

- **Engineer:** do not touch existing `deadwars/src/*.c`/`.h` files. New files and design work are fine.
- **Designer:** do not touch `missions.c`, `data.c` or `test/sim/`. Design docs are fine.
- **Artist:** do not touch `art/ui.txt` or `tools/gfx.py`. All other art is fine.

## Definition of done (every push)

1. `make -C deadwars clean && make -C deadwars` finishes with zero errors and zero warnings in files you own:
   `make -C deadwars 2>&1 | grep -E 'warning|error'`.
2. `deadwars/test/smoke.sh` exits 0. It builds the ROM, boots it headless to mission 1, and checks the screens
   and the game state in RAM. Screenshots go to `deadwars/build/smoke/shot-*.png`. QA owns the script. If
   your checkout doesn't have it yet, merge integration. If integration doesn't have it either, run the
   interim check from your role brief.
3. Docs for what you changed are updated, and so is your `team/status/<role>.md`, with the new branch head.
4. Nothing generated or copyrighted is committed. That means no ROMs (`*.gba`), BIOS, saves (`*.sav`, `*.srm`), save states,
   or `build/`, `cores/` and `out/` folders.

## Merge and release flow

1. A role pushes a green branch, updates its status, and when it matters adds a note to `team/inbox/producer/`.
2. The producer reviews the diff. The branch may change only that role's paths, plus `team/status/<role>.md`,
   `team/backlog/<role>.md`, new inbox files and hook lines. The producer then merges it into integration with
   `--no-ff`, rebuilds, runs the smoke test, pushes, logs the merge in `team/status/producer.md`, and messages
   the roles it affects.
3. QA tests the integration head on the Onion profiles (mGBA-libretro with Onion options, and gpSP) and files a
   sign-off for that commit in `team/inbox/producer/`.
4. Only the producer delivers releases to the owner. These are the Dead Wars ROMs and the AW2 Remix patch or ROMs.
   Every release is built from a QA-signed integration commit and noted in `team/status/producer.md`. Releases are
   delivered directly to the owner and never committed.

## Commits

- Keep messages short and in the imperative mood, with a prefix: `Dead Wars: ...`, `emu-lab: ...`, `AW2 remix: ...`,
  `team: ...`.
- End every commit message with the two attribution lines your session's instructions give you. They are
  a `Co-Authored-By:` line and `Claude-Session: https://claude.ai/code/<your own session id>`. Each session
  uses its own session URL.
- Never put model identifiers in commit messages, docs or code comments.

## Read first (everyone)

- `deadwars/README.md`: the game, rules, controls, build and test harness.
- `deadwars/docs/ASSET_SPEC.md`: the art-to-engine contract.
- `docs/miyoo/ONION_GBA_FIX.md` and `docs/miyoo/ONION_RESEARCH.md`: the target device, its cores and their quirks.
- `docs/remix/ROM_NOTES.md`: the AW2 ROM data map. Only the producer needs this in depth.
