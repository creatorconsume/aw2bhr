# Onion OS box art

`python3 -I boxart/boxart.py .` (run from `deadwars/`) regenerates:

| file | game | source |
|---|---|---|
| `deadwars.png` | Dead Wars | the game's own sprites from `art/*.txt` |
| `aw2remix.png` | Advance Wars 2 - Remix | original sprites in `boxart/remix.txt` (no Nintendo art) |

Both are 250x360 PNGs (Onion's recommended maximum for game-list images), drawn as
integer-scaled pixel art so they stay crisp.

**Install:** Onion looks up `Roms/GBA/Imgs/<ROM file name without extension>.png`.
Rename to match the ROM, e.g. `deadwars.gba` -> `Roms/GBA/Imgs/deadwars.png`.
