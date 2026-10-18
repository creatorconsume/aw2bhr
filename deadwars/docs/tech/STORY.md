# Storylets: authoring guide

How to write storylets for *Dead Wars: The Last Boat* (design: `docs/design/LAST_BOAT.md` section 11).
You write rows in plain tab-separated tables; `tools/storyc.py` checks them and turns them into C.
You never touch C. If something is wrong, the build stops and tells you the file, the line and the problem.

- Tables: `deadwars/data/story/*.tsv`
- Compiler: `deadwars/tools/storyc.py` (Python 3, standard library only)
- Runtime: `deadwars/src/story.c` (the game side: binding, dialogue, menu, effects)
- Tests: `deadwars/test/story/` (bad-data fixtures, edge-case data, a fuzzer)

## 1. What a storylet is

A storylet is a short scene that the game plays at a **trigger point** (a phase): at camp, on the road, at a stop, at the start or end of a mission, after a death or after a bite.

Each storylet has:

- **roles** (A, B, ...): the people it is about. The engine fills them with real people from the roster when the scene plays. A dead person can never fill a role, except the dead person of a death scene (section 4).
- **conditions** that must hold: on the world (food, day, a story variable) or on the people in the roles (a trait, a skill, a relationship).
- **text**: lines of dialogue, spoken by a role or narrated.
- **choices** (optional, 2 to 4) that the player picks from a menu. A choice can need its own conditions; when they fail it shows greyed out.
- **effects**: what changes (food -2, morale +1, Holt loses Hothead, ...), always or only for one choice.

At each trigger point the engine collects the storylets of that phase that can play right now (conditions hold and roles can be filled), picks one by weight, plays it, applies its effects and writes a line to the Chronicle (the run's journal).

**Golden rule:** a storylet never names a character directly. It says "A: alive, has Caring", and the engine finds who that is. So a storylet can't contradict the game: a dead character never speaks, and nobody is "the medic" unless they have the skill.

## 2. Working with the tables

### Format

- One file per table, all required: `phase.tsv var.tsv storylet.tsv role.tsv choice.tsv cond.tsv text.tsv effect.tsv`.
- Columns are separated by **one tab**. The first non-comment line is the **header row** (column names). The column order is free. Optional columns can be left out of the header.
- Lines starting with `#` are comments. Blank lines are ignored. Empty cells take the column's default.
- Save as UTF-8 (a BOM and Windows line endings are fine). **Dialogue text must be plain ASCII**: the game font has no curly quotes, no em dashes and no accents. Type `'` `"` `-` `...`.
- In text: `\n` forces a line break, `\\` is a backslash. There are no other escapes.
- Any spreadsheet that saves "tab-separated values" works. So does a text editor that keeps tabs as tabs.

### Commands (run from `deadwars/`)

```
python3 -I tools/storyc.py --check data/story        # check only, prints problems
python3 -I tools/storyc.py data/story /tmp/story-gen # check + write story_data.c/.h (the ROM build does this into build/gen)
python3 -I tools/storyc.py --selftest                # the compiler's own tests (also checks data/story)
make -C test/story run OUT=/tmp/story-test           # selftest + fuzz the runtime on thousands of random runs
```

`make` in `deadwars/` runs the compiler automatically whenever a `.tsv` changes. A problem stops the build:

```
data/story/text.tsv:14: line does not fit the dialogue box (with portrait, 23 wide x 5 rows, names at their longest): needs more than 5 rows. Worst case: "WWWWWWWWW, you sleep | through your watch ..."
data/story/cond.tsv:9: 'grumpy' is not a trait (brave cowardly hothead ...)
data/story/effect.tsv:30: role 'A' is the dead subject: the dead take no effects
```

Warnings (a var nobody reads, a storylet with weight 0) are printed but don't stop the build.

The fuzzer prints how often each storylet and each choice fired over thousands of random runs. Use it to spot storylets that never fire or fire all the time. A storylet that never fires fails the run.

## 3. The tables

### phase.tsv: trigger points

| column | | meaning |
|---|---|---|
| `id` | required | `camp` `road` `stop` `mission_start` `mission_end` `death` `bite` |
| `quiet` | required | weight of "nothing happens" (0..255), see section 7 |
| `doc` | optional | your notes |

List every phase that has storylets. When the game fires each one:

| phase | when | subject |
|---|---|---|
| `camp` | every night, after the camp jobs | none |
| `road` | travelling between stops | none |
| `stop` | arriving at a stop | none |
| `mission_start` | before choosing who goes | none |
| `mission_end` | after a mission, after its death and bite scenes | none |
| `death` | once per person who died (mission, starvation, raid, ...) | the dead person |
| `bite` | once per person bitten in a mission who is still alive | the bitten person |

### var.tsv: story variables

World state that storylets remember: "we robbed the stranger", "arguments this week".

| column | | meaning |
|---|---|---|
| `id` | required | lower-case name, e.g. `stranger_fate`. Must not clash with a builtin input (section 5). |
| `type` | required | `bool` (0/1) or `int` |
| `min` `max` | required | range; must fit -32768..32767; a bool is 0..1 |
| `default` | required | value at the start of a run |
| `doc` | optional | what the numbers mean. Start it with `[game]` when the C code sets the var (e.g. `mission_won`, written by run.c after each mission): storyc then doesn't warn that no effect changes it. |

At most 64 vars. Values are always kept inside `min..max`. **Append new vars at the end** (the row order is where they live in the save).

### storylet.tsv: one row per storylet

| column | default | meaning |
|---|---|---|
| `id` | required | lower-case name, unique, e.g. `camp_holt_temper` |
| `phase` | required | a phase from phase.tsv |
| `weight` | required | 0..255: how likely, relative to the others. 0 = retired (never fires) |
| `priority` | 0 | 0..9: if any storylet of a higher priority can play, only those compete (section 7) |
| `once` | 0 | 1 = at most once per run |
| `cooldown` | 0 | days before it can play again (0 = no wait) |
| `cool_group` | empty | storylets with the same group share one cooldown clock (each still uses its own `cooldown` days) |
| `excl_group` | empty | once one storylet of the group has fired, the others never fire (section 7) |
| `log` | empty | the Chronicle line, 25 characters max, e.g. `{A.name} cooled off` (section 6) |
| `doc` | empty | your notes |

**Append new storylets at the end, never reorder or delete rows.** The row order is the storylet's number in the save file and in the Chronicle. To retire one, set its weight to 0.

### role.tsv: who a storylet is about

| column | default | meaning |
|---|---|---|
| `storylet` | required | the storylet id |
| `role` | required | an upper-case name: `A`, `B`, ... or `FRIEND` (1-8 letters/digits) |
| `kind` | `alive` | `alive`, `subject` or `subject_dead` (section 4) |
| `cast` | empty | empty = anyone; `mara` `doc` `june` `holt` `rosa` `tess`; `named` = any named character; `unnamed` = an unnamed survivor |
| `doc` | empty | notes |

At most 4 roles per storylet.

### cond.tsv: conditions

| column | default | meaning |
|---|---|---|
| `storylet` | required | the storylet id |
| `choice` | empty | empty = the storylet needs it; a number = only that choice needs it |
| `role` | empty | the person the input is about (empty for world inputs) |
| `input` | required | what to test (section 5) |
| `other` | empty | the second role, only for `rel` |
| `op` | required | `=` `!=` `<` `<=` `>` `>=`, or `has` / `lacks` for traits |
| `value` | required | a number, a trait name or a cast id |
| `doc` | empty | notes |

All the conditions of a storylet must hold together (AND). For an OR, write two storylets.

### choice.tsv: the menu

| column | | meaning |
|---|---|---|
| `storylet` | required | the storylet id |
| `choice` | required | 1, 2, 3, 4, numbered without gaps |
| `label` | required | the menu text, 26 characters max with names at their longest. Placeholders are allowed |
| `doc` | optional | notes |

Either no choices, or 2 to 4.

### text.tsv: the lines

| column | | meaning |
|---|---|---|
| `storylet` | required | the storylet id |
| `choice` | empty | empty = the intro, before the menu; a number = shown after that choice was picked |
| `speaker` | empty | a role, or empty for narration |
| `body` | required | the line, with placeholders (section 6) |

The rows play in file order, at most 8 in the intro and 8 per choice. Every storylet needs at least one intro line. **With choices, the last intro line is the question**: it stays on screen while the menu shows above it.

### effect.tsv: what changes

| column | default | meaning |
|---|---|---|
| `storylet` | required | the storylet id |
| `choice` | empty | empty = always; a number = only if that choice was picked |
| `op` | required | see the table in section 8 |
| `role` | empty | the person it changes (person ops only) |
| `other` | empty | the second role (`rel_add` only) |
| `target` | empty | a resource, var, trait or skill, depending on the op |
| `value` | empty | the amount |
| `chance` | 100 | percent chance to happen (rolled for each effect on its own) |
| `doc` | empty | notes |

## 4. Roles and binding

When the engine considers a storylet, it fills the roles one by one with people from the roster:

- every role gets a **different** person;
- `alive` roles take living people only (and never the death/bite subject);
- a `subject` role is the bitten person in a `bite` storylet (alive). Only phase `bite` has it;
- a `subject_dead` role is the dead person in a `death` storylet. Only phase `death` has it. They can be named in text (`{A.name}`, `{A.his}`) and appear in conditions (`rel`, `trait`, ...), but **they never speak and never take effects**;
- the `cast` column filters by character;
- each role's conditions must hold, and with choices at least one choice must be available for the people chosen.

When several people qualify, one is chosen at random from the run's random seed. A storylet with no possible filling simply doesn't play. At most one role per storylet is a subject.

A death or bite storylet without a subject role is allowed. It plays for any death or bite, but it can't name the person.

**The actor** is the person the Chronicle line is about: the subject role if there is one, otherwise the first role listed in role.tsv.

## 5. Condition inputs

World inputs (leave `role` empty):

| input | range | meaning |
|---|---|---|
| `day` | 0..255 | the day of the run |
| `days_left` | -255..255 | days until the boat sails (negative = it sailed) |
| `food` `fuel` `ammo` `meds` `parts` | 0..9999 | caravan stock |
| `morale` | 0..10 | group morale |
| `alive_count` | 0..16 | people alive |
| any var from var.tsv | its min..max | |

Person inputs (set `role`):

| input | values | meaning |
|---|---|---|
| `trait` | `has` / `lacks` + a trait | brave cowardly hothead calm loner loyal caring selfish greedy reckless paranoid optimist cynic devout addict haunted stubborn curious fragile glutton pacifist clever weary |
| `cast` | `=` / `!=` + a cast id or `unnamed` | who they are. Prefer the role's `cast` column; use this for "anyone but Tess" |
| `firearms` `melee` `medicine` `mechanics` `stealth` `scavenge` `survival` `leadership` | 0..5 | skill level |
| `hp` | 0..99 | hit points |
| `hp_pct` | 0..100 | hp as a percent of max hp (rounded down) |
| `infected` `amputee` | 0 / 1 | flags: use `=` or `!=` |
| `grit` `aim` `speed` `nerve` | 0..5 | attributes |

Pair input (set `role` and `other`):

| input | range | meaning |
|---|---|---|
| `rel` | -3..3 | relationship: -3 enemies, 0 neutral, +3 bonded ("together"). It is symmetric: `rel A B` = `rel B A` |

The compiler rejects mistakes that would make a storylet dead or pointless: a value out of range, `morale < 0` (never true), `day >= 0` (always true), `mood >= 4` together with `mood <= 2`, "has Calm" with "lacks Calm", three required traits (people have two), a cast condition on a role that already has a cast filter, and a choice that can never be available.

## 6. Text

### The dialogue box

The box fills the bottom of the screen: 5 rows of text. The compiler wraps every line exactly like the game does, with **every name at its longest**, and rejects lines that don't fit.

| who speaks | layout | width |
|---|---|---|
| a character with a portrait (Mara, Doc, June, Holt) | portrait on the left, their name above the text | 23 per row |
| anyone else (Rosa, Tess, unnamed survivors) | `Name: ` in front of the line | 28 per row, minus the name on the first row |
| narration (no speaker) | plain | 28 per row |

- The 5th row is one character shorter: the blinking "next" arrow sits in its last column.
- A role without a `cast` filter can be anyone, so its lines are checked in both layouts, with a 9-character name (the longest an unnamed survivor can have).
- A role with a `cast` filter uses that character's real name length and pronouns.
- Words are never split. A single word longer than a row is an error.
- The Chronicle `log` line: 25 characters. Menu labels: 26.

If a line doesn't fit, the error shows the worst-case wrap with `|` between rows. Shorten it or split it into two rows of text.tsv.

### Placeholders

| placeholder | becomes |
|---|---|
| `{A.name}` | the person's name |
| `{A.he}` `{A.him}` `{A.his}` `{A.self}` | he/she/they, him/her/them, his/her/their, himself/herself/themself |
| `{A.He}` `{A.Him}` `{A.His}` | capitalised, to start a sentence |
| `{A.is}` `{A.was}` `{A.has}` | is/are, was/were, has/have, agreeing with the pronoun |
| `{A.s}` `{A.es}` | verb endings after a pronoun: `{A.He} walk{A.s}` gives "She walks" / "They walk"; `{A.He} watch{A.es}` gives "He watches" / "They watch" |
| `{res.food}` `{res.fuel}` `{res.ammo}` `{res.meds}` `{res.parts}` | the current stock |
| `{day}` `{days_left}` | the day, days until the boat sails |

Only roles declared for this storylet can be used. After a name, the verb is always singular ("{A.name} walks"); the agreement helpers are for pronouns. The Chronicle `log` line can only name the actor (section 4) and has no `{res...}` or `{day}`: the journal shows it long after the scene.

**Never type a character's name** (`Mara`, `Doc`, `June`, `Holt`, `Rosa`, `Tess`) in text: the compiler rejects it. Bind a role with `cast=mara` and write `{A.name}`. This means "June" the month is out too: say "early summer".

## 7. How the engine picks

1. It takes the storylets of the phase with weight > 0 that are not used up:
   - `once` and already fired → out;
   - its `excl_group` was used by *another* storylet → out (the one that fired can repeat unless `once`);
   - on cooldown: fired less than `cooldown` days ago (same day included), counting the shared clock of its `cool_group` → out.
2. It drops those whose world conditions fail or whose roles can't be filled.
3. **Priority**: only the highest priority still present competes. Use priority 1+ for story beats that must happen as soon as they can (a priority 1+ storylet always plays when it fits).
4. **Weight and quiet**: with priority 0, one is picked with chance `weight / (sum of weights + quiet)`. The phase's `quiet` weight is the chance that nothing happens at all. With `quiet` 0, something plays whenever anything can.
5. It plays: intro lines, the question with the menu, the reply lines of the choice, then the effects, then a banner summing up what changed ("Food -3  Morale +1", "Holt +Calm"). Then it writes the Chronicle record.

At most one storylet plays per trigger.

## 8. Effects

| op | role | other | target | value | what it does |
|---|---|---|---|---|---|
| `res_add` | | | `food` `fuel` `ammo` `meds` `parts` | ±1..999 | add; never below 0 |
| `morale_add` | | | | ±1..10 | add; kept in 0..10 |
| `var_set` | | | a var | min..max | set |
| `var_add` | | | an int var | ± up to max-min | add; kept in min..max |
| `hp_add` | yes | | | ±1..99 | heal (never above max hp) or hurt. **Reaching 0 kills**: the person dies, and the Chronicle records it |
| `infect` | yes | | | | infected (turns in 3 + grit/2 days), like a bite; nothing if already infected |
| `cure` | yes | | | | no longer infected |
| `trait_add` | yes | | a trait | | fills an empty trait slot; with two traits already, **the second one is replaced**. Nothing if they have it |
| `trait_remove` | yes | | a trait | | removes it |
| `rel_add` | yes | yes | | ±1..6 | changes the relationship both ways; kept in -3..3 |
| `skill_xp` | yes | | a skill | 1..100 | skill practice (levels up at 10/25/45/70/100 XP) |

Order: the storylet's own effects (empty `choice`) first, then the picked choice's effects, each in file order. Each effect with a `chance` rolls on its own.

- To swap a trait precisely, `trait_remove` the old one first, then `trait_add` the new one.
- **The dead take no effects.** If an `hp_add` kills someone, the later effects on them are skipped, and so is a `rel_add` with them.
- A death caused by a storylet does not trigger a death scene by itself, and it costs no morale unless you add a `morale_add`. Guard dangerous effects with conditions, e.g. `hp >= 4` on the choice before an `hp_add -2`.
- An `infect` from a storylet does not trigger a bite scene.

## 9. Limits

| what | limit |
|---|---|
| storylets | 256 (the Chronicle stores the number in a byte) |
| story vars | 64 |
| storylets with a cooldown (a `cool_group` counts once) | 64 |
| storylets + excl groups | 512 |
| roles per storylet | 4 |
| choices | 2..4 |
| lines in the intro / after one choice | 8 |
| dialogue line | 5 rows; 23 wide with a portrait, 28 without (section 6) |
| menu label | 26 characters |
| Chronicle line | 25 characters |
| a name | 9 characters |

## 10. A worked example

"Holt picks a fight with someone he doesn't like", from `data/story`:

storylet.tsv
```
id	phase	weight	priority	once	cooldown	cool_group	excl_group	log	doc
camp_holt_temper	camp	6	0	0	2			{A.name} picked a fight	Holt picks a fight
```
role.tsv: A is Holt; B is anyone else alive.
```
storylet	role	kind	cast	doc
camp_holt_temper	A	alive	holt
camp_holt_temper	B	alive		someone Holt doesn't get on with
```
cond.tsv: Holt is still a Hothead and doesn't like B; choice 3 needs good morale.
```
storylet	choice	role	input	other	op	value	doc
camp_holt_temper		A	trait		has	hothead
camp_holt_temper		A	rel	B	<=	0
camp_holt_temper	3		morale		>=	6
```
choice.tsv
```
storylet	choice	label	doc
camp_holt_temper	1	Back {A.name}
camp_holt_temper	2	Back {B.name}
camp_holt_temper	3	Laugh it off	needs good morale
```
text.tsv: the intro (A, then B; B's line is the question), then a reply per choice.
```
storylet	choice	speaker	body
camp_holt_temper		A	{B.name}, you sleep through your watch again and I leave you at the next stop.
camp_holt_temper		B	Say it louder, Sergeant. Wake up the whole county.
camp_holt_temper	1		{B.He} turn{B.s} {B.his} back on the fire. {B.He} {B.has} nothing more to say.
camp_holt_temper	2		{A.name} storms off to check the cars. Again.
camp_holt_temper	3		Somebody snorts. Then everybody is laughing, even {A.name}. Even {B.name}.
```
effect.tsv: every fight counts; then each choice.
```
storylet	choice	op	role	other	target	value	chance	doc
camp_holt_temper		var_add			arguments	1
camp_holt_temper	1	rel_add	A	B		-1
camp_holt_temper	1	var_add			arguments	1
camp_holt_temper	2	rel_add	A	B		-1
camp_holt_temper	2	morale_add				-1
camp_holt_temper	3	rel_add	A	B		1
camp_holt_temper	3	morale_add				1
```
Its sequel, `camp_holt_cools_off` (priority 1, once), needs `arguments >= 2`, removes Hothead, adds Calm and resets `arguments` to 0.

## 11. The example set (data/story)

The first 14 rows are the engineer's examples below, which together use every feature. The designer's
Act 1 content (62 more storylets and 5 vars) follows them; `docs/design/SLICE_NUMBERS.md` part 6 lists
it.

| storylet | phase | shows |
|---|---|---|
| camp_thin_stew | camp | world + person conditions, a choice that needs a trait (Optimist), cool_group `camp_food` |
| camp_hunt_dusk | camp | choice conditions (ammo, skill), `hp_add` with `chance`, skill XP, shared cooldown |
| camp_holt_temper | camp | a cast role (Holt, portrait), `rel`, `var_add` |
| camp_holt_cools_off | camp | priority 1 + once, a var condition, trait swap, a sequel |
| camp_new_face | camp | `unnamed` and `named` roles, `cast !=`, `stranger_fate` read |
| road_radio_boat | road | June, `days_left`, `{days_left}` in text, `var_set` of a bool |
| road_stranger_help / _rob | road | `excl_group stranger`: only one of the two ever happens |
| stop_pharmacy_cross | stop | `amputee`, `infected`, `infect` at 20%, guarded `hp_add` |
| mission_end_shaken | mission_end | `hp_pct`, healing |
| death_grief_bonded | death | `subject_dead`, `rel >= 2`, `trait_add haunted` |
| death_rival_guilt | death | `subject_dead`, `rel <= -2`, choices after a death |
| bite_confession | bite | `subject`, `grit`, `cure` at 50% |
| bite_caring_stays | bite | `subject` + a Caring helper, a choice that needs Medicine 2 |

## 12. For programmers

- API (`src/story.h`): `story_init_run()` at `run_new()` sets the vars to their defaults and clears flags and cooldowns. `story_fire(phase, subject)` plays at most one storylet and returns true if one played. `subject` is the roster index for `SP_DEATH` and `SP_BITE`, else -1. `story_log_text(const LogRec *)` (defined in story.c, declared weak in run.c) gives the journal line of a `LOG_STORY` record, or NULL.
- Screen: `story_fire` draws over rows 6-19: the `dialogue()` box (rows 12-19), the menu above it, then a `banner()`. Redraw your screen afterwards. It leaves `spr.portrait` at -1.
- After `story_fire`, people may have died (`alive` 1 → 0, a `LOG_DEATH` record with arg 4 = killed by a storylet), been infected (`LOG_BITTEN`), or changed traits, rel, skills, res, morale and vars. Check for game over.
- Save state in `RunState`:
  - `vars[SV_*]`: SV_* come from `build/gen/story_data.h`.
  - `story_flags`: bit *i* means storylet *i* has fired. Bits 511, 510, ... mean that excl group has fired.
  - `story_cool[slot]` holds the day of the last fire + 1 (0 = never). storyc assigns the slots: one per storylet with a cooldown, or one per cool_group.
  - `STORY_SIG` (story_data.h) changes when storylets, vars or groups are added, removed or reordered. runsave.c can store it to spot a save from older content.
- Records: `chronicle(LOG_STORY, actor, storylet index)` is written when a storylet plays. `actor` is the actor role's roster index (the subject if no role, else 0xFF).
- Cost: all of a phase's storylets are scanned. Binding tries the qualifying people per role with backtracking, capped at 1024 people per storylet (a storylet that hits the cap counts as unbindable). Hundreds of storylets take well under a frame.
- Generated data: `story_data.h` has the SV_ enum, limits, the condition/effect/placeholder codes and the table types. `story_data.c` has the tables. Text is stored with placeholders as 3-byte codes (`\001`, PH_*, argument).
- Host tests: `test/story/story_fuzz.c` builds story.c with `-DHOST_SIM` and stubbed text.c. It runs every phase on random runs and checks every invariant after each call. It also compares each call with a brute-force reference search, and runs with and without the sanitizers. `test/story/edge/` is valid data with every feature at its limits (fuzzed with `-d`: the people a storylet kills take no later effect). `test/story/min/` plus `bad_*/` are the compiler's negative tests.
