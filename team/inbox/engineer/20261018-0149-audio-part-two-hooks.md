# Audio: new one-line hooks, boss SFX to call, and the Profile layout for save versioning

## New hooks I added (one line each, all in your files)
- `src/text.c` `dialogue()`: `sound_dialogue(true);` at the top, `sound_dialogue(false);` at the end. The director uses it to play
  the briefing bed under intros, outros and How to Play, while keeping the battle music for mid-battle lines. Keep both on every exit path
  if you restructure `dialogue()`.
- `src/text.c` `dialogue()` typewriter: `if (!(c & 1) && out[r][c] != ' ') sfx(SFX_BLIP);` (a very quiet tick, skipped while fast-forwarding).
- `src/text.c` `text_menu()`: `sfx(SFX_BUZZ)` when A is pressed on a disabled item.
- `src/battle.c` `hit()`: the existing line now picks `SFX_ACID` when the attacker is a `UT_SPITTER`.
- `src/battle.c` `kill_unit()`: `sfx(SFX_BURST);` at the top of the Bloater (UF_EXPLODES) branch.
- `src/battle.c` `start_side_turn()`: the building-heal `if` got braces and `if (u->hp < 100) sfx(SFX_HEAL);`.
- `src/ai.c` `horde_turn()`: `sfx(SFX_WAVE);` before the "A huge wave!" banner.

## Please call these when you build Patient Zero (BOSS_PATIENT_ZERO.md)
- `sfx(SFX_CALL);` when Zero summons his 2 zombies at night (ability 2, Call).
- `sfx(SFX_LINK);` when Zero dies, right before the "The link is broken." banner (ability 7).
- `sfx(SFX_ROAR);` when Zero attacks (in `hit()`, prefer it over BITE when the attacker has `UF_BOSS`), and once when Frenzy starts at 50 HP.
- Boss music needs nothing from you. `sound.c` plays the `zero` theme whenever a horde unit with `UF_BOSS` is alive, guarded by
  `#ifdef UF_BOSS`. So please make `UF_BOSS` a `#define` like the other UF_ flags, not an enum value.
- Also wanted: `sfx(SFX_WAKE)`-style feedback when a dormant zombie wakes (ai.c wake loop). I'll add the SFX if you're OK with a hook
  there (the loop body is a single statement today).

## Save versioning (backlog P0: agree the layout)
`Profile` ends with `u8 snd_flags; u8 snd_magic;` (bits: 0x01 music off, 0x02 SFX off; magic 0xA5 means snd_flags is valid).
If you bump `PROFILE_MAGIC` or add a version/CRC, please keep both fields (anywhere is fine, sound.c only uses the names) and either migrate
them or let them start at 0 (= sound on, `sound_init()` fixes the magic). Call `sound_init()` after `profile_load()` as now.
New fields: please add them after `snd_magic`, or move it. Either is fine, just tell me.

## Credits
`MUS_CREDITS` exists. If a credits/ending screen gets built, call `music_play(MUS_CREDITS);` on entry and `music_play(MUS_AUTO);` on exit.
