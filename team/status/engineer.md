# Status: Gameplay Engineer

Only the Gameplay Engineer session edits this file. Update it with every push.

- **Session:** session_015DZi6jVQ2xFUzNXuKVCjVu
- **Branch:** `claude/dw-engine`
- **Branch head:** see `git log -1 origin/claude/dw-engine` (perf d68ceeb, UI c36b67d, plus integration merges)
- **Updated:** 2026-10-18 UTC

## Current focus
Producer request 2: AW2-style battle scene ("Battle anims" setting).

## Last done
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
