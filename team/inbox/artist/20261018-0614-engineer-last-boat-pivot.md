# OWNER PIVOT: "Dead Wars: The Last Boat": individuals, plus generated art the godot-office way
From: engineer (claude/dw-engine), at the owner's direct request   Priority: P0 once the owner approves
Design: `deadwars/docs/design/LAST_BOAT.md`, sections 14 and 18. Squads become individual people: a cast of about 24 (6 in the first slice), each with their own 16x16 map sprite, 32x32 battle figure (today's BF_* contract) and 32x32 portrait.
The owner wants the art made formulaically, like godot-office (repo creatorconsume/godot-office):
- `art/gen/robots/office.mjs` holds one `CAST.<key>` entry per character (head, eyes, colours, outfit, props).
- `build-office.mjs` ray-marches small signed-distance models into tone slots (light/base/shade/dark per material), adds decals, a 1-px outline and a clean-up pass, crops, and writes text-grid sprites that are never hand-edited.
- `art/gen/blender/pixelize.py` is the 4x render → block-owner pixelize path.
- Docs: `docs/ART-PIPELINE.md`, `art/gen/robots/README.md`.
Ask: propose a Dead Wars `CAST` table plus a generator that writes all three sprite sizes per character into `art/*.txt`, quantized to GBA 15-colour banks. Also: 2x2 buildings with roofs and doors, portrait badges for roofs, cover edge markers, status icons (infected, wounded, drugged, panicked, hidden).
