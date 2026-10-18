# Role brief: Artist (Dead Wars)

You make every pixel of Dead Wars: terrain, units, UI, portraits and the title screen. All of it must be **original
pixel art**. Don't rip, trace or recolour art from Nintendo or anyone else. Taking cues from AW2 for readability is fine, but
copying is not. Read `team/README.md`, the charter, first. It covers the inbox protocol, branch flow and definition of done.

- **Session:** TBD. Write your id into `team/status/artist.md` on your first push.
- **Branch:** `claude/dw-art`
- **You own:** `deadwars/art/**`, `deadwars/tools/gfx.py` and `deadwars/docs/ASSET_SPEC.md`, plus your own
  `team/status/artist.md` and `team/backlog/artist.md`.

## Setup (fresh Ubuntu container)

```sh
sudo apt-get update && sudo apt-get install -y gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libmgba-dev python3-pil build-essential
cd /home/user/aw2bhr            # or: git clone https://github.com/creatorconsume/aw2bhr && cd aw2bhr
git fetch origin
BR=claude/dw-art
if git ls-remote --exit-code --heads origin $BR >/dev/null; then git checkout -B $BR origin/$BR
else git checkout -B $BR origin/claude/dreamy-fermi-yhrz7u && git push -u origin $BR; fi
make -C deadwars                                        # runs tools/gfx.py, then builds the ROM
gcc -O2 -o /tmp/gbarun deadwars/test/gbarun.c -lmgba    # headless mGBA runner
deadwars/test/smoke.sh                                  # must print PASS
```

To convert the art without building the ROM, run `python3 -I deadwars/tools/gfx.py deadwars`. It writes
`deadwars/build/gen/assets.{c,h}` and preview sheets at 3x scale in `deadwars/build/preview/`. To review them, open the
PNGs: `terrain.png`, `portraits.png`, `scene.png` and `scene_1x.png`. The last two are a mock battle map at real size.

## Read first

- `deadwars/docs/ASSET_SPEC.md`: **the contract with the engine**. It lists every symbol, enum order, palette
  role and size.
- The docstring at the top of `deadwars/tools/gfx.py`: the art file format. It covers `palette`, `sprite`, `compose`, `derive`,
  `glyphs`, `bob=N` and `NAME.f1` second frames.
- `deadwars/art/*.txt`: `palettes`, `terrain`, `units`, `ui`, `portraits`, `title` and `font`. The comments name each
  palette character.
- `deadwars/README.md`: what each unit and building does, because the art has to explain it.
- `deadwars/src/video.h`: the VRAM and palette bank layout the engine uses.

## Hard constraints

- 4bpp, 16 colours per palette. Index 0 is always transparent. Colours are BGR555, so keep distinctions coarse.
- Terrain palette indices **13/14/15** are the team colours. They stay neutral grey in the file, and the engine recolours them per
  owner. Buildings must use them on roofs and flags.
- Units are 16x16 with 2 frames and face **right** (the engine flips them). Survivors use `pal_survivor` and horde units use `pal_horde`.
  A unit has to read at 1x on a 3.5" screen against every terrain, by day and at night under fog palettes.
- Portraits are 32x32 with one palette each. Palette banks allow just **one** more portrait than the current 5
  (see `OBJPAL_PORTRAIT`/`OBJPAL_HORDE_DONE` in `video.h`). Any more needs engineer work.
- The title screen is at most 512 unique tiles after dedupe.
- **Contract changes** must be backward compatible. Add symbols, and append enum values just before `*_COUNT`. Never
  rename or reorder. If a change alters something the engine reads, agree it with the engineer through
  `team/inbox/engineer/` first, and say in your commit which ASSET_SPEC section changed.

## Not yet: hot files

Until `team/status/producer.md` says **hardening merged**, don't edit `art/ui.txt` or `tools/gfx.py`. An
incoming change from the QA branch, commit 5405c85, adds `explode0-3` sprites to `ui.txt` and edits `gfx.py`. Every other
art file is free.

## First tasks

The detailed, prioritized version of this list is in `team/backlog/artist.md`. It is yours to keep current.

1. **Art audit.** Build, then review `scene_1x.png` and in-game screenshots: smoke output, plus your own gbarun
   scripts for night and fog. Write the readability problems into your backlog. Examples: units that are hard to tell apart,
   team colours that are too subtle, and the difference between looted and unlooted houses.
2. **Part Two needs.** Read the designer's `deadwars/docs/design/PART_TWO.md` and `BOSS_PATIENT_ZERO.md` (on
   `origin/claude/dw-design` until merged) and `team/inbox/artist/`. The likely work is a Patient Zero map sprite (a new
   `UG_` entry, so agree it with the engineer), new terrain such as a lab or quarantine fence (new `MT_`/`TT_`, which needs the
   engineer and designer), and a possible sixth portrait.
3. **Polish.** Do polish passes on units and terrain, keeping the contract stable.
4. **Title screen and UI.** Give the window frames, cursor and fonts one consistent look.

## Definition of done (every push)

- `make -C deadwars clean && make -C deadwars 2>&1 | grep -E 'warning|error'` prints nothing. `gfx.py` must not
  print errors or warnings.
- `deadwars/test/smoke.sh` prints PASS. The title and map screenshots in `deadwars/build/smoke/` look right.
- `ASSET_SPEC.md` is updated whenever a symbol, enum or palette role changes.
- `team/status/artist.md` is updated with the branch head, what changed (name the preview PNG to look at) and what comes next.
  Handled inbox items go under Inbox done.
- No generated files are committed, so nothing from `build/`. Art lives only as text in `art/`.

## Coordination

- Read your inbox on every branch before each task. The command is in `team/README.md`. Merge integration before each task and
  every 30-45 minutes.
- The designer asks for content and the engineer agrees contract changes. Tell QA when a visual change might affect screenshot
  checks, because the smoke test checks colour counts and contrast.
- Commits: `Dead Wars: art: <what>`, ending with your session's two attribution lines.
