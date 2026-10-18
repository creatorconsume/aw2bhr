# Status: Gameplay Engineer

Only the Gameplay Engineer session edits this file. Update it with every push.

- **Session:** session_015DZi6jVQ2xFUzNXuKVCjVu
- **Branch:** `claude/dw-engine`
- **Branch head:** see `git log -1 origin/claude/dw-engine` (perf d68ceeb, UI c36b67d, plus integration merges)
- **Updated:** 2026-10-18 UTC

## Current focus
The Last Boat demo landed and is GREEN (`deadwars/test/all.sh`: ALL PASS). It's playable from the title menu (last entry). Next: an adversarial logic review of the new code, owner-requested.

## Last done
- **Logic review (b44ecc0):**
  - 6 lenses, each finding checked by 3 skeptics: 23 defects confirmed and fixed with regression tests, 5 rejected.
  - Test counts now: rules 5169, flow 206, run 330, save 15, 96 storyc bad fixtures; all.sh ALL PASS.
  - Open design calls for the designer: in docs/tech/LAST_BOAT_ENGINE.md and SLICE_NUMBERS.md (point-blank shots through walls, Main Street "Make camp", meal rationing).
- **The Last Boat demo (218790e)**, built by role agents in this session (rules, tactics, run, story, designer, artist, audio, integrator):
  - Act 1 slice: journey map → stops (choose one) → deploy → three mission types (scavenge, get through, take the bridge) → camp jobs → storylets.
  - Hit chances, directional cover, noise and the Horde meter with waves, 2x2 buildings with doors and barricades, infection, an A/B ironman save.
  - The storylet compiler is relational (SQLite with FK/CHECK) and has a selftest and fuzzers.
  - Art is generated (people, roofs, icons, journey map).
  - Engine guide: docs/tech/LAST_BOAT_ENGINE.md. One command runs every test: deadwars/test/all.sh.
- **Battle scene uses the artist's art** (contract in ASSET_SPEC "Battle scene").
  - Backdrop per terrain on BG2 (CBB2 tiles 128+, SB26), mirrored for the left panel.
  - 32x32 figures in the artist's 10 slots, with the IDLE/ATK poses.
  - Effects: MUZZLE, SPARK, SLASH, ACID and RISE.
  - HP windows on rows 13-19. Night tint comes from the existing palettes.
  - The map is restored untouched afterwards.
- **AW2 battle scene (producer request 2), new `src/scene.c`:**
  - A full-screen split: survivors on the left, the horde on the right, N figures = displayed HP (placeholder: map sprites at 2x, affine), and a ground strip from each unit's terrain.
  - Sequence: lunge and flash, then the HP bar counts down, then the counter, then "Destroyed" or the "Rises again..." beat. A skips.
  - Setting "Battle anims On/Off" (`Profile.battle_off`, which reuses the old pad byte, so saves stay compatible; default On).
  - Off, during hold-B in the horde turn, or in the sim: the map hit, as before.
  - New `can_counter()` is shared by attack(), the scene and the forecast.
  - Artist contract: `team/inbox/artist/20261018-0320-engineer-battle-scene-contract.md`.
- **Paced horde turn (producer request 1).** At Normal speed (now the default for new profiles), each visible zombie gets the cursor, a 20-frame beat, its walk, an 8-frame beat, then the cursor on its target for 16 frames, the attack and a 12-frame beat. Fast speed and holding B skip the beats. Zombies hidden in fog still move unseen. Options label: "Unit speed".
- **Fog-cancel exploit (producer request 3).** If a move un-fogs any tile, B in the action menu buzzes and shows "No going back" instead of undoing the move.
- **Perf (d68ceeb):** `make PERF=1` adds cycle counters and a 30x20 stress map with 60 zombies. Script: `test/perf_horde.txt` + `test/perf_report.py`. Writeup: `docs/tech/PERF.md`.
  Changes: scoped occupancy grid (`occ_begin`/`occ_end`, map.c), packed x|y BFS queues with no idivmod, and `act()` scanning only the move box against a prey list.
  mGBA cycles for one 60-zombie horde turn, before -> after:
  - zombie decision: avg 500k -> 109k, max 730k (2.6 frames) -> 145k (0.5 frames)
  - `find_paths`: avg 129k -> 44k
  - `build_field`: 790k -> 419k
  - total AI compute: ~110 frames -> ~25 frames
  - frames overrun: 61 -> 10
  AI choices are unchanged: sim output is identical over 20 seeds x 5 missions. gpSP numbers through emu-lab are still to come.
- **UI (c36b67d), new `src/ui.c`:**
  - R opens a unit card (stats, move type, range, traits, damage dealt and taken vs each enemy) or a terrain card.
  - Target forecast shows our hit and the counter as luck ranges.
  - Path arrow while choosing a move.
  - The day banner is now a summary: supplies, healed HP, lost, turned.
  - `damage_calc(a, d, luck)` is new in map.c. `damage_estimate` wraps it with the same rng use.

## Next
- After hardening: Part Two mechanics (designer request), the Zero gfx hookup (artist), and the P0 technical design note.
- gpSP numbers through emu-lab `lrun`.

## Blockers
- none

## Inbox done
<!-- `<inbox filename>` -> outcome (commit / branch) -->
- `20261018-0143-audio-sound-hooks.md` -> noted. I'll keep all the hooks when resolving the merge.
