// Dead Wars: The Last Boat - storylet runtime (design: LAST_BOAT.md 11). Content: data/story/*.tsv,
// checked and compiled by tools/storyc.py into story_data.{c,h}. Authoring guide: docs/tech/STORY.md.
//
// story_fire(phase, subject):
//  1. candidates: the phase's storylets with weight > 0 that are not used up (once, excl group,
//     cooldown), whose world conditions hold and whose roles bind to the roster;
//  2. only the highest priority present competes; one is picked by weight against the phase's
//     quiet weight (priority 0 only: a priority >= 1 storylet always plays when it fits);
//  3. it plays: Chronicle record, dialogue, the choice menu, effects, a summary banner.
// Roles bind to distinct people: alive ones, or exactly the subject for the subject kinds. When a
// subject is given, plain roles never bind it. Each role takes a random one (run.rng) of the people
// who qualify for it given the roles before it, so a run replays the same way.
//
// Save state (RunState): vars[SV_*]; story_flags bit i = storylet i has fired, bits 511, 510, ... =
// that excl group has fired; story_cool[slot] = run.day of the last fire + 1 (0 = never). storyc
// gives each storylet with a cooldown its own slot, or one slot per cool_group.
#include "lb.h"
#include "story.h"
#include "story_data.h"
#include "video.h"
#include <string.h>

_Static_assert(SV_COUNT <= STORY_VARS, "more story vars than RunState.vars holds");
_Static_assert(STORY_COUNT + STORY_EXCL_COUNT <= STORY_FLAG_BYTES * 8, "RunState.story_flags too small");
_Static_assert(STORY_COOL_SLOTS <= sizeof(((RunState *)0)->story_cool), "RunState.story_cool too small");
_Static_assert(STORY_COUNT <= 256, "LogRec.arg holds the storylet index");

#define BIND_BUDGET 1024   // people tried per storylet: caps the cost of a role mix that can't bind
#define LINE_CAP    160    // an expanded line: 5 rows x 28 + "Name: ", with room to spare
#define LABEL_CAP   32
#define NOTE_MAX    6      // person changes listed in the summary banner
#define DEATH_STORY 4      // LOG_DEATH arg: killed by a storylet (run.c: 0 bled, 1 zombie, 2 starved, 3 raid)

typedef struct { u8 idx, bind[STORY_ROLES]; } Cand;
static Cand cand[STORY_COUNT + 1] EWRAM_BSS;
static char line_buf[STORY_LINES][LINE_CAP] EWRAM_BSS;
static Line line_list[STORY_LINES + 1] EWRAM_BSS;
static const char *line_name[STORY_LINES + 1] EWRAM_BSS;
static char label_buf[STORY_CHOICES][LABEL_CAP] EWRAM_BSS;
static char note_buf[NOTE_MAX][24] EWRAM_BSS;
static char sum[2][27] EWRAM_BSS;

static const Storylet *cur;          // storylet being bound or played
static u8 bind[STORY_ROLES];         // roster index per role
static int subj = -1, nroster, nalive, budget, nnote;

#ifdef HOST_SIM // test/story/story_fuzz.c checks what played
int story_dbg_idx = -1, story_dbg_choice = -1;
u8 story_dbg_bind[STORY_ROLES];
u32 story_dbg_budget_out;
#endif

static const char *const res_short[RES_COUNT] = { "Food", "Fuel", "Ammo", "Meds", "Parts" };
static const char *const pron[][3] = { // PH_HE .. PH_ES, by Person.he
	{ "he", "she", "they" }, { "him", "her", "them" }, { "his", "her", "their" },
	{ "himself", "herself", "themself" }, { "He", "She", "They" }, { "Him", "Her", "Them" },
	{ "His", "Her", "Their" }, { "is", "is", "are" }, { "was", "was", "were" },
	{ "has", "has", "have" }, { "s", "s", "" }, { "es", "es", "" },
};
_Static_assert(PH_ES - PH_HE + 1 == sizeof pron / sizeof pron[0], "pronoun table");

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

// xorshift on run.rng, so story picks replay with the run
static u32 srng(void)
{
	u32 x = run.rng ? run.rng : 0x2545F491u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return run.rng = x;
}

// 0..n-1 without a divide (n <= 65535)
static int srng_below(int n)
{
	return n > 0 ? (int)(((srng() >> 16) * (u32)n) >> 16) : 0;
}

// ---------------------------------------------------------------- conditions

static bool cmp(int a, int op, int b)
{
	switch (op) {
	case SO_EQ: return a == b;
	case SO_NE: return a != b;
	case SO_LT: return a < b;
	case SO_LE: return a <= b;
	case SO_GT: return a > b;
	case SO_GE: return a >= b;
	}
	return false;
}

// hp_pct = hp * 100 / maxhp rounded down, compared without a divide: pct >= v <=> hp * 100 >= v * maxhp
static bool pct_ge(const Person *p, int v)
{
	return p->maxhp ? p->hp * 100 >= v * p->maxhp : v <= 0;
}

static bool pct_cmp(const Person *p, int op, int v)
{
	bool eq = pct_ge(p, v) && !pct_ge(p, v + 1);
	switch (op) {
	case SO_EQ: return eq;
	case SO_NE: return !eq;
	case SO_LT: return !pct_ge(p, v);
	case SO_LE: return !pct_ge(p, v + 1);
	case SO_GT: return pct_ge(p, v + 1);
	case SO_GE: return pct_ge(p, v);
	}
	return false;
}

static int cast_of(const Person *p)
{
	return p->cast < CAST_COUNT ? p->cast : STORY_CAST_NONE;
}

static bool has_trait(const Person *p, int t)
{
	return t != TR_NONE && (p->trait[0] == t || p->trait[1] == t);
}

// a person input of a bound role; false = not a person input
static bool person_value(const StoryCond *c, int *v)
{
	const Person *p = &run.roster[bind[c->role]];
	switch (c->in) {
	case SI_CAST: *v = cast_of(p); break;
	case SI_SKILL: *v = c->arg < SK_COUNT ? p->skill[c->arg] : 0; break;
	case SI_HP: *v = p->hp; break;
	case SI_INFECTED: *v = (p->flags & PF_INFECTED) != 0; break;
	case SI_AMPUTEE: *v = (p->flags & PF_AMPUTEE) != 0; break;
	case SI_DEPLOYED: *v = (p->flags & PF_DEPLOYED) != 0; break;
	case SI_GRIT: *v = p->grit; break;
	case SI_AIM: *v = p->aim; break;
	case SI_SPEED: *v = p->speed; break;
	case SI_NERVE: *v = p->nerve; break;
	case SI_REL:
		if (c->arg >= STORY_ROLES)
			return false;
		*v = run.rel[bind[c->role]][bind[c->arg]];
		break;
	default: return false;
	}
	return true;
}

static bool cond_ok(const StoryCond *c)
{
	int v;
	switch (c->in) {
	case SI_DAY: v = run.day; break;
	case SI_DAYS_LEFT: v = run.ship_day - run.day; break;
	case SI_RES: v = c->arg < RES_COUNT ? run.res[c->arg] : 0; break;
	case SI_MORALE: v = run.morale; break;
	case SI_ALIVE: v = nalive; break;
	case SI_VAR: v = c->arg < SV_COUNT ? run.vars[c->arg] : 0; break;
	default:
		if (c->role >= STORY_ROLES)
			return false;
		if (c->in == SI_TRAIT) {
			bool has = has_trait(&run.roster[bind[c->role]], c->val);
			return c->op == SO_HAS ? has : !has;
		}
		if (c->in == SI_HP_PCT)
			return pct_cmp(&run.roster[bind[c->role]], c->op, c->val);
		if (!person_value(c, &v))
			return false;
	}
	return cmp(v, c->op, c->val);
}

static bool conds_ok(const StoryCond *c, int n)
{
	for (int i = 0; i < n; i++)
		if (!cond_ok(&c[i]))
			return false;
	return true;
}

static bool choice_ok(const StoryChoice *ch)
{
	return conds_ok(&story_cond[ch->cond0], ch->ncond);
}

static bool any_choice(void)
{
	if (!cur->nchoice)
		return true;
	for (int i = 0; i < cur->nchoice; i++)
		if (choice_ok(&story_choice[cur->choice0 + i]))
			return true;
	return false;
}

// ---------------------------------------------------------------- binding

static bool cast_ok(const Person *p, int want)
{
	if (want == STORY_CAST_ANY)
		return true;
	if (want == STORY_CAST_NAMED)
		return p->cast < CAST_COUNT;
	return cast_of(p) == want;
}

static bool taken(int i, int k)
{
	for (int r = 0; r < k; r++)
		if (bind[r] == i)
			return true;
	return false;
}

// Bind roles k.. (0..k-1 are bound): true once every role holds a person, every role condition
// passes and at least one choice is available. Role k tries the people who qualify for it in a
// random rotation, backtracking when a later role can't be bound.
static bool bind_from(int k)
{
	if (k == cur->nrole)
		return any_choice();
	int kind = cur->role_kind[k];
	if (kind != RK_ALIVE && subj < 0)
		return false;
	const StoryCond *c = &story_cond[cur->cond0 + cur->cend[k]];
	int nc = cur->cend[k + 1] - cur->cend[k], n = 0;
	u8 list[ROSTER_MAX];
	for (int i = kind == RK_ALIVE ? 0 : subj; i < nroster; i++) {
		const Person *p = &run.roster[i];
		bool ok = kind == RK_SUBJECT_DEAD ? !p->alive : p->alive;
		if (ok && !(kind == RK_ALIVE && i == subj) && !taken(i, k) && cast_ok(p, cur->role_cast[k])) {
			if (--budget < 0)
				return false;
			bind[k] = i;
			if (conds_ok(c, nc))
				list[n++] = i;
		}
		if (kind != RK_ALIVE)
			break; // only the subject
	}
	for (int j = 0, at = srng_below(n); j < n; j++, at = at + 1 < n ? at + 1 : 0) {
		bind[k] = list[at];
		if (bind_from(k + 1))
			return true;
		if (budget < 0)
			return false;
	}
	return false;
}

static bool flag(int bit)
{
	return run.story_flags[bit >> 3] & (1 << (bit & 7));
}

// not used up (once, excl group, cooldown) and its subject role has a subject
static bool available(int idx, const Storylet *st)
{
	if (!st->weight || (st->once && flag(idx)))
		return false;
	if (st->excl_bit != STORY_NO_EXCL && flag(st->excl_bit) && !flag(idx))
		return false; // another storylet of the group fired
	if (st->cool_slot < STORY_COOL_SLOTS) {
		int stamp = run.story_cool[st->cool_slot];
		if (stamp && run.day + 1 < stamp + st->cool_days)
			return false;
	}
	if (st->nrole && st->role_kind[0] != RK_ALIVE && subj < 0)
		return false; // storyc puts the subject role first
	return true;
}

// ---------------------------------------------------------------- text

// append at o[n] (cap = size of o), always 0-terminated; returns the new length
static int put_str(char *o, int n, int cap, const char *s, int max)
{
	while (*s && max-- > 0 && n < cap - 1)
		o[n++] = *s++;
	o[n] = 0;
	return n;
}

static int put_num(char *o, int n, int cap, int v)
{
	char t[8];
	int k = 0;
	u32 u = v < 0 ? -v : v;
	if (v < 0 && n < cap - 1)
		o[n++] = '-';
	do {
		t[k++] = '0' + u % 10;
		u /= 10;
	} while (u && k < 7);
	while (k && n < cap - 1)
		o[n++] = t[--k];
	o[n] = 0;
	return n;
}

static const Person *person_at(int i)
{
	return i >= 0 && i < nroster && i < ROSTER_MAX ? &run.roster[i] : NULL;
}

static int put_name(char *o, int n, int cap, const Person *p)
{
	return put_str(o, n, cap, p ? person_name(p) : "someone", sizeof run.roster[0].name - 1);
}

// Expand storyc's placeholders (\001, PH_*, argument) with `who` holding each role's person.
static int expand(char *o, int n, int cap, const char *s, const u8 *who)
{
	while (*s && n < cap - 1) {
		u8 ch = *s++;
		if (ch != 1) {
			o[n++] = ch;
			continue;
		}
		u8 ph = *s ? *s++ : 0, arg = *s ? *s++ : 0;
		if (!ph || !arg)
			break;
		const Person *p = arg <= STORY_ROLES ? person_at(who[arg - 1]) : NULL;
		if (ph == PH_NAME)
			n = put_name(o, n, cap, p);
		else if (ph >= PH_HE && ph <= PH_ES)
			n = put_str(o, n, cap, pron[ph - PH_HE][p && p->he < 3 ? p->he : 2], 8);
		else if (ph == PH_RES)
			n = put_num(o, n, cap, arg <= RES_COUNT ? run.res[arg - 1] : 0);
		else if (ph == PH_DAY)
			n = put_num(o, n, cap, run.day);
		else if (ph == PH_DAYS_LEFT)
			n = put_num(o, n, cap, run.ship_day - run.day);
	}
	o[n] = 0;
	return n;
}

// A compiled line as a dialogue Line: a speaker with a portrait shows it (dialogue() prints the
// name); one without gets "Name: " in front.
static void make_line(int k, const StoryLine *sl)
{
	Line *ln = &line_list[k];
	char *b = line_buf[k];
	int n = 0;
	ln->portrait = 0xFF;
	line_name[k] = NULL;
	if (sl->speaker < STORY_ROLES) {
		const Person *p = &run.roster[bind[sl->speaker]];
		int pt = p->cast < CAST_COUNT ? cast_info[p->cast].portrait : 0xFF;
		line_name[k] = person_name(p);
		if (pt < PORTRAIT_COUNT) {
			ln->portrait = pt;
		} else {
			n = put_name(b, n, LINE_CAP, p);
			n = put_str(b, n, LINE_CAP, ": ", 2);
		}
	}
	expand(b, n, LINE_CAP, sl->text, bind);
	ln->text = b;
}

static void say(int line0, int count)
{
	int n = MIN(count, STORY_LINES);
	for (int k = 0; k < n; k++)
		make_line(k, &story_line[line0 + k]);
	line_list[n].text = NULL;
	if (n)
		dialogue(line_list);
}

// The question stays on screen under the menu: dialogue()'s box and word wrap, drawn at once.
static void prompt(const Line *ln, const char *name)
{
	const int by = 12, rows = 5;
	char out[5][32];
	int tx = 1, tw = 28, cx = 0, cy = 0;
	text_window(0, by, 30, 8);
	if (ln->portrait < PORTRAIT_COUNT) {
		spr.portrait = ln->portrait;
		spr.portrait_x = 8;
		spr.portrait_y = by * 8 + 12;
		spr.portrait_flip = 0;
		tx = 6;
		tw = 23;
		if (name)
			text_put(tx, by + 1, name, BGPAL_TEXT_YEL);
	}
	memset(out, 0, sizeof out);
	for (const char *s = ln->text; *s && cy < rows;) {
		const char *w = s;
		while (*w && *w != ' ' && *w != '\n')
			w++;
		int len = w - s;
		if (cx + len > tw) {
			cy++;
			cx = 0;
			if (cy >= rows)
				break;
		}
		memcpy(&out[cy][cx], s, MIN(len, tw));
		cx += len;
		s = w;
		if (*s == '\n') {
			cy++;
			cx = 0;
			s++;
		} else if (*s == ' ') {
			if (cx < tw)
				out[cy][cx++] = ' ';
			s++;
		}
	}
	for (int r = 0; r < rows; r++)
		text_put(tx, by + 2 + r, out[r], BGPAL_UI);
}

// The caller's screen (the journey map, its labels) shows above the dialogue box. A menu or a
// banner clears its rectangle when it closes: put back what was under it.
static u16 keep_buf[2][6 * 30];

static void keep(int x, int y, int w, int h, bool restore)
{
#ifndef HOST_SIM
	int k = 0;
	for (int j = y; j < y + h && j < 20; j++)
		for (int i = x; i < x + w && i < 30 && k < 6 * 30; i++, k++) {
			volatile u16 *t = &SCREENBLOCK(SB_TEXT)[j * 32 + i], *wn = &SCREENBLOCK(SB_WINDOW)[j * 32 + i];
			if (restore) {
				*t = keep_buf[0][k];
				*wn = keep_buf[1][k];
			} else {
				keep_buf[0][k] = *t;
				keep_buf[1][k] = *wn;
			}
		}
#endif
}

// Show the last intro line with the choices above it; unavailable ones are greyed out.
static const StoryChoice *ask(const StoryLine *q)
{
	const char *items[STORY_CHOICES];
	u8 en[STORY_CHOICES];
	int n = MIN(cur->nchoice, STORY_CHOICES), w = 0, first = -1;
	for (int i = 0; i < n; i++) {
		const StoryChoice *ch = &story_choice[cur->choice0 + i];
		w = MAX(w, expand(label_buf[i], 0, LABEL_CAP, ch->label, bind));
		items[i] = label_buf[i];
		en[i] = choice_ok(ch);
		if (en[i] && first < 0)
			first = i;
	}
	if (first < 0)
		return NULL; // can't happen: binding needs an available choice
	make_line(0, q);
	prompt(&line_list[0], line_name[0]);
	w = MIN(w + 3, 30);
	int sel;
	keep(30 - w, 12 - (n + 2), w, n + 2, false);
	do
		sel = text_menu(30 - w, 12 - (n + 2), w, items, en, n, first);
	while (sel < 0); // B doesn't skip the decision
	keep(30 - w, 12 - (n + 2), w, n + 2, true);
	text_clear_rect(0, 12, 30, 8);
	spr.portrait = -1;
#ifdef HOST_SIM
	story_dbg_choice = sel;
#endif
	return &story_choice[cur->choice0 + sel];
}

// ---------------------------------------------------------------- effects

static void note(const Person *p, const char *what, const char *what2)
{
	if (nnote >= NOTE_MAX)
		return;
	char *b = note_buf[nnote++];
	int n = put_name(b, 0, sizeof note_buf[0], p);
	n = put_str(b, n, sizeof note_buf[0], what, 12);
	put_str(b, n, sizeof note_buf[0], what2 ? what2 : "", 10);
}

static void hp_add(Person *p, int i, int d)
{
	int v = p->hp + d;
	if (d > 0) {
		if (v > p->maxhp)
			v = MAX(p->maxhp, p->hp); // never above max, never lowered by a heal
		if (v <= p->hp)
			return;
	}
	if (v <= 0) {
		p->hp = 0;
		p->alive = 0;
		chronicle(LOG_DEATH, i, DEATH_STORY);
		note(p, " dies", NULL);
		return;
	}
	p->hp = MIN(v, 255);
	note(p, d < 0 ? " hurt" : " healed", NULL);
}

static void trait_add(Person *p, int t)
{
	if (t <= TR_NONE || t >= TR_COUNT || has_trait(p, t))
		return;
	int s = p->trait[0] == TR_NONE ? 0 : 1; // two traits already: the second one goes
	if (s && p->trait[1] != TR_NONE && p->trait[1] < TR_COUNT)
		note(p, " -", trait_names[p->trait[1]]);
	p->trait[s] = t;
	note(p, " +", trait_names[t]);
}

static void trait_remove(Person *p, int t)
{
	if (!has_trait(p, t))
		return;
	for (int s = 0; s < 2; s++)
		if (p->trait[s] == t)
			p->trait[s] = TR_NONE;
	if (p->trait[0] == TR_NONE) {
		p->trait[0] = p->trait[1];
		p->trait[1] = TR_NONE;
	}
	note(p, " -", trait_names[t]);
}

static void effect(const StoryEffect *e)
{
	if (e->chance < 100 && srng_below(100) >= e->chance)
		return;
	Person *p = NULL;
	int i = 0;
	if (e->role < STORY_ROLES) {
		i = bind[e->role];
		p = &run.roster[i];
		if (!p->alive)
			return; // the dead take no effects (an earlier effect may have killed them)
	}
	switch (e->op) {
	case SE_RES_ADD:
		if (e->arg < RES_COUNT)
			run.res[e->arg] = clampi(run.res[e->arg] + e->val, 0, 0xFFFF);
		break;
	case SE_MORALE_ADD:
		run.morale = clampi(run.morale + e->val, 0, 10);
		break;
	case SE_VAR_SET:
	case SE_VAR_ADD:
		if (e->arg < SV_COUNT)
			run.vars[e->arg] = clampi((e->op == SE_VAR_ADD ? run.vars[e->arg] : 0) + e->val,
			                          story_var_min[e->arg], story_var_max[e->arg]);
		break;
	case SE_HP_ADD:
		if (p)
			hp_add(p, i, e->val);
		break;
	case SE_INFECT:
		if (p && !(p->flags & PF_INFECTED)) {
			person_bitten(p); // the same timer as a bite in a mission, by difficulty
			chronicle(LOG_BITTEN, i, 0);
			note(p, " infected", NULL);
		}
		break;
	case SE_CURE:
		if (p && (p->flags & PF_INFECTED)) {
			p->flags &= ~PF_INFECTED;
			p->turn_in = 0;
			note(p, " cured", NULL);
		}
		break;
	case SE_TRAIT_ADD:
		if (p)
			trait_add(p, e->arg);
		break;
	case SE_TRAIT_REMOVE:
		if (p)
			trait_remove(p, e->arg);
		break;
	case SE_REL_ADD:
		if (p && e->arg < STORY_ROLES) {
			int j = bind[e->arg];
			if (j != i && run.roster[j].alive)
				run.rel[i][j] = run.rel[j][i] = clampi(run.rel[i][j] + e->val, -3, 3);
		}
		break;
	case SE_SKILL_XP:
		if (p && e->arg < SK_COUNT && e->val > 0)
			skill_use(p, e->arg, e->val);
		break;
	}
}

static void effects(int e0, int n)
{
	for (int k = 0; k < n; k++)
		effect(&story_effect[e0 + k]);
}

// Append a token to the two summary rows (26 wide: the banner's limit).
static void sum_add(const char *tok)
{
	int len = strlen(tok);
	for (int r = 0; r < 2; r++) {
		int n = strlen(sum[r]);
		if (n + (n ? 2 : 0) + len <= 26) {
			if (n) {
				sum[r][n++] = ' ';
				sum[r][n++] = ' ';
			}
			memcpy(sum[r] + n, tok, len + 1);
			return;
		}
		if (!n)
			return;
	}
}

static void sum_delta(const char *name, int d)
{
	char t[16];
	int n = put_str(t, 0, sizeof t, name, 6);
	t[n++] = ' ';
	t[n++] = d < 0 ? '-' : '+';
	put_num(t, n, sizeof t, d < 0 ? -d : d);
	sum_add(t);
}

// "Food -3  Morale +1" and who changed, as a banner
static void summary(const u16 *res0, int morale0)
{
	memset(sum, 0, sizeof sum);
	for (int r = 0; r < RES_COUNT; r++)
		if (run.res[r] != res0[r])
			sum_delta(res_short[r], run.res[r] - res0[r]);
	if (run.morale != morale0)
		sum_delta("Morale", run.morale - morale0);
	for (int k = 0; k < nnote; k++)
		sum_add(note_buf[k]);
	if (sum[0][0]) {
		keep(0, 8, 30, 4, false); // banner() rows: 8..11 at most
		banner(sum[0], sum[1][0] ? sum[1] : NULL, 150);
		keep(0, 8, 30, 4, true);
	}
}

// ---------------------------------------------------------------- play

static void play(const Cand *c)
{
	int idx = c->idx;
	const Storylet *st = cur = &story_list[idx];
	memcpy(bind, c->bind, sizeof bind);
	run.story_flags[idx >> 3] |= 1 << (idx & 7);
	if (st->excl_bit != STORY_NO_EXCL)
		run.story_flags[st->excl_bit >> 3] |= 1 << (st->excl_bit & 7);
	if (st->cool_slot < STORY_COOL_SLOTS)
		run.story_cool[st->cool_slot] = MIN(run.day, 254) + 1;
	int actor = st->actor < st->nrole ? bind[st->actor] : subj >= 0 ? subj : 0xFF;
	chronicle(LOG_STORY, actor, idx);
#ifdef HOST_SIM
	story_dbg_idx = idx;
	story_dbg_choice = -1;
	memcpy(story_dbg_bind, bind, sizeof bind);
#endif

	u16 res0[RES_COUNT];
	memcpy(res0, run.res, sizeof res0);
	int morale0 = run.morale;
	nnote = 0;
	const StoryChoice *pick = NULL;
	if (st->nchoice) {
		say(st->line0, st->nline - 1); // the last line is the question, shown under the menu
		pick = ask(&story_line[st->line0 + st->nline - 1]);
	} else {
		say(st->line0, st->nline);
	}
	if (pick)
		say(pick->line0, pick->nline);
	effects(st->eff0, st->neff);
	if (pick)
		effects(pick->eff0, pick->neff);
	summary(res0, morale0);
}

void story_init_run(void)
{
	memset(run.vars, 0, sizeof run.vars);
	for (int i = 0; i < SV_COUNT; i++)
		run.vars[i] = story_var_def[i];
	memset(run.story_flags, 0, sizeof run.story_flags);
	memset(run.story_cool, 0, sizeof run.story_cool);
}

bool story_fire(int phase, int subject)
{
	if (phase < 0 || phase >= SP_COUNT)
		return false;
	nroster = MIN(run.nroster, ROSTER_MAX);
	subj = subject >= 0 && subject < nroster ? subject : -1;
	nalive = 0;
	for (int i = 0; i < nroster; i++)
		nalive += run.roster[i].alive != 0;

	int ncand = 0, best = -1;
	u32 total = 0;
	for (int k = story_phase_start[phase]; k < story_phase_start[phase + 1]; k++) {
		int idx = story_phase_list[k];
		const Storylet *st = &story_list[idx];
		if (st->priority < best || !available(idx, st))
			continue;
		cur = st;
		if (!conds_ok(&story_cond[st->cond0], st->cend[0]))
			continue;
		budget = BIND_BUDGET;
		if (!bind_from(0)) {
#ifdef HOST_SIM
			if (budget < 0)
				story_dbg_budget_out++;
#endif
			continue;
		}
		if (st->priority > best) {
			best = st->priority;
			ncand = 0;
			total = 0;
		}
		cand[ncand].idx = idx;
		memcpy(cand[ncand].bind, bind, sizeof bind);
		ncand++;
		total += st->weight;
	}
	if (!ncand)
		return false;
	int roll = srng_below(total + (best > 0 ? 0 : story_phase_quiet[phase]));
	if (roll >= (int)total)
		return false; // nothing happens
	int c = 0;
	while (roll >= story_list[cand[c].idx].weight)
		roll -= story_list[cand[c++].idx].weight;
	play(&cand[c]);
	return true;
}

// The journal line of a LOG_STORY record, or NULL.
const char *story_log_text(const LogRec *r)
{
	static char b[26];
	if (!r || r->event != LOG_STORY || r->arg >= STORY_COUNT || !story_list[r->arg].log)
		return NULL;
	const Storylet *st = &story_list[r->arg];
	u8 who[STORY_ROLES];
	memset(who, 0xFF, sizeof who);
	if (st->actor < STORY_ROLES)
		who[st->actor] = r->actor;
	nroster = MIN(run.nroster, ROSTER_MAX);
	expand(b, 0, sizeof b, st->log, who);
	return b;
}
