# Part One: writing review

Status: these are proposals. I'll apply them to `missions.c` after "hardening merged", re-run
`docs/design/tools/check_dialogue.py`, and check overflow in the emulator.

`check_dialogue.py` currently finds **1 overflow**: M4, Doc's line "...I have his files." loses
"files." (the same bug QA logged in `test/playtest/m4_text_overflow.txt`).

## Problems and fixes

| # | Where | Problem | Fix |
|---|---|---|---|
| 1 | M4 intro, Doc line 3 | It overflows the 5×23 box, so "files." is cut. | "The first patient came through here. It spread from this building. I have his files." |
| 2 | M1 intro, last line | "Stay close to the light" points at a mechanic that doesn't exist. Night is fog, +1 zombie move and +15% bite, and the radio towers reveal. | "Every few nights it gets dark. They move faster in the dark, and you won't see them coming. Stay together." (M1 has no radio tower, so don't promise one.) |
| 3 | M1 intro | It never says *what* supplies are for. | Line 4: "Search houses for supplies. The workshop turns supplies into people. Anyone bitten gets back up." |
| 4 | M3 intro | "Seven days" next to the objective "by Day 8" reads like a contradiction. | Holt: "Evac bird lands at the Route 9 helipad. It waits until Day 8. Not one day more." |
| 5 | M3 intro, June | "The bus fits two if you load it" is unclear: the Bus *and* its passenger both count. | "The bus counts as two if somebody's riding in it. Use that." |
| 6 | M4 outro, Doc | "That's what Medics are for." is clunky, and it breaks the scene. | "Thank you. I can keep your people standing. Medics heal whoever is next to them." |
| 7 | M5 outro | "Thanks for playing the Dead Wars pilot!" will be wrong once Part Two ships. | "END OF PART ONE." Leave the thanks to the Part Two ending. |
| 8 | M5 intro, Holt | "Nobody gets bitten" is a weak rally line before the hardest mission. | "Seven days. Walls, guns, and each other. Nobody gets bitten." |
| 9 | M2 outro | "Pick a radio channel and keep listening." is OK. Optionally sharpen it. | "Don't get used to it. Find us a radio channel. Somebody else has to be out there." |

## Continuity

- **Mara** is the fire captain of Station 9. Holt calls her "Captain", which is consistent.
- Part One mentions **Patient Zero** in M4 (files) and M5 ("he's coming for us"). Part Two
  gives him a name (Walter Kessling) and a place (the reservoir pumping station). Nothing in
  Part One contradicts that.
- **Doc** is Dr. Ellis Park. Zero calls him "Ellis" in M10, which sets up the earlier idea that
  he saw him on the ward.
- **M1's "Four of us. That's who's left."** comes back as the last Mara line of M10. Keep the two
  in sync.

## Objectives (all fine)

The objective strings are short and accurate. When M3's goal changes, also change M3's
objective "Evac 3 units by Day 8." if the sim retune moves the deadline.
