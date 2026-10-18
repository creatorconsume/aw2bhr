// Dead Wars - host fuzz of the storylet runtime (src/story.c), see Makefile.
// Random runs (rosters, traits, relationships, resources, story state) go through story_fire()
// for every phase, many times in a row. After each call it checks that:
//  - a call that plays nothing changes nothing but run.rng;
//  - what played is of that phase, was available (once, excl, cooldown), bound distinct people
//    of the right kinds and casts, and its conditions held (re-checked here independently);
//  - a brute-force search here agrees on what could play: nothing eligible = nothing plays; an
//    eligible priority >= 1 storylet or a phase without quiet weight = something plays; what
//    plays is eligible and of the top priority present;
//  - the dead are untouched; every Person field, res, morale, rel and var stays in range, and
//    only the bound people changed;
//  - every dialogue line, menu, banner and journal line fits the screen (text.c's word wrap);
//  - a replay from the same state does exactly the same;
//  - with -d (data where a lethal hp_add is the first effect on its role, like test/story/edge):
//    whoever a storylet kills takes no later effect.
// The stubs below stand in for text.c: they check layout instead of drawing.
#include "game.h"
#include "video.h"
#include "lb.h"
#include "story.h"
#include "story_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RunState run;
SpriteState spr;
u16 keys_down, keys_held, keys_rep;
u32 frame_count;

extern int story_dbg_idx, story_dbg_choice;
extern u8 story_dbg_bind[STORY_ROLES];
extern u32 story_dbg_budget_out;
const char *story_log_text(const LogRec *r);

static int fails, checks;
static bool strict_death; // -d
static const char *ctx = "";
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; if (fails < 40) { \
	printf("FAIL [%s] %s:%d: %s: ", ctx, __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static u32 hseed = 1;
static u32 hr(void)
{
	hseed ^= hseed << 13;
	hseed ^= hseed >> 17;
	hseed ^= hseed << 5;
	return hseed;
}
static int rnd(int n) { return n > 0 ? (int)(hr() % (u32)n) : 0; }

// ---------------------------------------------------------------- text.c stand-ins

static int lines_shown, menus_shown, banners_shown, b_presses;
static long fired[STORY_COUNT + 1], picked[STORY_COUNT + 1][STORY_CHOICES];

// text.c dialogue(): 5 rows of 28 (23 with a portrait); the blinking arrow takes the 5th row's last column
static void check_wrap(const char *s, int tw)
{
	int cx = 0, cy = 0, rows = 5, last = 0;
	while (*s && cy < rows) {
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
		CHECK(len <= tw, "word longer than a row: %.*s", len, s);
		cx += len;
		if (cy == rows - 1)
			last = cx;
		s = w;
		if (*s == '\n') {
			cy++;
			cx = 0;
			s++;
		} else if (*s == ' ') {
			if (cx < tw)
				cx++;
			s++;
		}
	}
	while (*s == ' ' || *s == '\n')
		s++;
	CHECK(!*s, "text overflows the box: ...%s", s);
	CHECK(last <= tw - 1, "5th row reaches the arrow column (%d of %d)", last, tw);
}

static void check_chars(const char *s, int max)
{
	int n = 0;
	for (; *s; s++, n++)
		CHECK((*s >= 32 && *s <= 126) || *s == '\n', "bad character %d", (u8)*s);
	CHECK(n <= max, "text of %d characters, max %d", n, max);
}

void dialogue(const Line *lines)
{
	int n = 0;
	CHECK(lines != NULL, "no lines");
	for (; lines && lines->text; lines++, n++) {
		CHECK(lines->portrait == 0xFF || lines->portrait < PORTRAIT_COUNT, "portrait %d", lines->portrait);
		check_chars(lines->text, 200);
		check_wrap(lines->text, lines->portrait < PORTRAIT_COUNT ? 23 : 28);
		lines_shown++;
	}
	CHECK(n >= 1 && n <= STORY_LINES, "%d lines", n);
	spr.portrait = -1;
}

int text_menu(int x, int y, int w, const char *const *items, const u8 *enabled, int count, int start)
{
	static bool cancelled;
	int any = 0;
	menus_shown++;
	CHECK(count >= 1 && count <= STORY_CHOICES, "count %d", count);
	CHECK(x >= 0 && y >= 0 && x + w <= 30 && y + count + 2 <= 12, "menu at %d,%d %dx%d", x, y, w, count + 2);
	CHECK(enabled && start >= 0 && start < count && enabled[start], "start %d", start);
	for (int i = 0; i < count; i++) {
		check_chars(items[i], 26);
		CHECK((int)strlen(items[i]) <= w - 3, "label '%s' wider than the menu (%d)", items[i], w);
		any += enabled[i] != 0;
	}
	CHECK(any > 0, "no choice available");
	if (!any)
		return 0;
	if (!cancelled && !rnd(4)) { // B: the menu must come back
		cancelled = true;
		b_presses++;
		return -1;
	}
	cancelled = false;
	int k = rnd(any);
	for (int i = 0; i < count; i++)
		if (enabled[i] && !k--)
			return i;
	return start;
}

void banner(const char *l1, const char *l2, int frames)
{
	banners_shown++;
	CHECK(l1 && *l1, "empty banner");
	check_chars(l1, 26);
	if (l2)
		check_chars(l2, 26);
}

void text_window(int x, int y, int w, int h)
{
	CHECK(x >= 0 && y >= 0 && x + w <= 30 && y + h <= 20, "window %d,%d %dx%d", x, y, w, h);
}

void text_put(int x, int y, const char *s, int pal)
{
	CHECK(x >= 0 && y >= 0 && y < 20 && x + (int)strlen(s) <= 30, "text at %d,%d: %s", x, y, s);
}

void text_clear_rect(int x, int y, int w, int h)
{
	CHECK(x >= 0 && y >= 0 && x + w <= 30 && y + h <= 20, "clear %d,%d %dx%d", x, y, w, h);
}

// ---------------------------------------------------------------- random runs

static const char *const syll[] = { "ra", "mo", "li", "ke", "da", "su", "vo", "ni", "an", "tor" };

static void random_person(Person *p, int cast)
{
	memset(p, 0, sizeof *p);
	p->cast = cast;
	p->alive = rnd(100) < 85;
	p->grit = 1 + rnd(5);
	p->aim = 1 + rnd(5);
	p->speed = 1 + rnd(5);
	p->nerve = 1 + rnd(5);
	p->maxhp = 6 + p->grit;
	p->hp = p->alive ? 1 + rnd(p->maxhp) : 0;
	int nt = rnd(3);
	for (int t = 0; t < nt; t++) {
		int v = 1 + rnd(TR_COUNT - 1);
		if (t == 0 || v != p->trait[0])
			p->trait[t] = v;
	}
	for (int s = 0; s < SK_COUNT; s++) {
		p->skill[s] = rnd(6);
		p->skill_xp[s] = rnd(120);
	}
	if (rnd(5) == 0) {
		p->flags |= PF_INFECTED;
		p->turn_in = 1 + rnd(6);
	}
	if (rnd(10) == 0)
		p->flags |= PF_AMPUTEE;
	p->weapon = rnd(WP_COUNT);
	p->he = cast < CAST_COUNT ? cast_info[cast].he : rnd(3);
	p->xp = rnd(500);
	if (cast < CAST_COUNT) {
		strncpy(p->name, cast_info[cast].name, sizeof p->name - 1);
	} else {
		int n = 0, k = 1 + rnd(4);
		for (int i = 0; i < k && n < 9; i++)
			for (const char *s = syll[rnd(10)]; *s && n < 9; s++)
				p->name[n++] = *s;
		p->name[0] -= 32;
	}
}

static void random_run(void)
{
	memset(&run, 0, sizeof run);
	run.day = rnd(60);
	run.ship_day = 30 + rnd(21);
	run.morale = rnd(11);
	for (int r = 0; r < RES_COUNT; r++)
		run.res[r] = rnd(5) == 0 ? 0 : rnd(12) == 0 ? 0xFFFF - rnd(3) : rnd(40);
	run.nroster = rnd(10) == 0 ? rnd(2) : 2 + rnd(ROSTER_MAX - 1);
	u8 casts[CAST_COUNT];
	for (int c = 0; c < CAST_COUNT; c++)
		casts[c] = c;
	for (int c = CAST_COUNT - 1; c > 0; c--) {
		int k = rnd(c + 1);
		u8 t = casts[c];
		casts[c] = casts[k];
		casts[k] = t;
	}
	int nc = 0;
	for (int i = 0; i < run.nroster; i++)
		random_person(&run.roster[i], (nc < CAST_COUNT && rnd(10) < 6) ? casts[nc++] : 0xFF);
	for (int i = 0; i < ROSTER_MAX; i++)
		for (int j = 0; j < i; j++)
			run.rel[i][j] = run.rel[j][i] = rnd(7) - 3;
	story_init_run();
	for (int v = 0; v < SV_COUNT; v++)
		if (rnd(2))
			run.vars[v] = story_var_min[v] + rnd(story_var_max[v] - story_var_min[v] + 1);
	for (int i = 0; i < STORY_COUNT; i++)
		if (!rnd(6))
			run.story_flags[i >> 3] |= 1 << (i & 7);
	run.rng = hr() | 1;
}

// ---------------------------------------------------------------- independent re-checks

static bool ref_cond_b(const RunState *rs, const u8 *bind, const StoryCond *c)
{
	int nalive = 0, v = 0;
	for (int i = 0; i < rs->nroster && i < ROSTER_MAX; i++)
		nalive += rs->roster[i].alive != 0;
	const Person *p = c->role < STORY_ROLES ? &rs->roster[bind[c->role]] : NULL;
	switch (c->in) {
	case SI_DAY: v = rs->day; break;
	case SI_DAYS_LEFT: v = rs->ship_day - rs->day; break;
	case SI_RES: v = rs->res[c->arg]; break;
	case SI_MORALE: v = rs->morale; break;
	case SI_ALIVE: v = nalive; break;
	case SI_VAR: v = rs->vars[c->arg]; break;
	case SI_CAST: v = p->cast < CAST_COUNT ? p->cast : 0xFF; break;
	case SI_TRAIT: {
		bool has = p->trait[0] == c->val || p->trait[1] == c->val;
		return c->op == SO_HAS ? has : !has;
	}
	case SI_SKILL: v = p->skill[c->arg]; break;
	case SI_HP: v = p->hp; break;
	case SI_HP_PCT: v = p->maxhp ? p->hp * 100 / p->maxhp : 0; break;
	case SI_INFECTED: v = !!(p->flags & PF_INFECTED); break;
	case SI_AMPUTEE: v = !!(p->flags & PF_AMPUTEE); break;
	case SI_GRIT: v = p->grit; break;
	case SI_AIM: v = p->aim; break;
	case SI_SPEED: v = p->speed; break;
	case SI_NERVE: v = p->nerve; break;
	case SI_REL: v = rs->rel[bind[c->role]][bind[c->arg]]; break;
	default: return false;
	}
	switch (c->op) {
	case SO_EQ: return v == c->val;
	case SO_NE: return v != c->val;
	case SO_LT: return v < c->val;
	case SO_LE: return v <= c->val;
	case SO_GT: return v > c->val;
	case SO_GE: return v >= c->val;
	}
	return false;
}

static bool ref_cond(const RunState *rs, const Storylet *st, const StoryCond *c)
{
	return ref_cond_b(rs, story_dbg_bind, c);
}

static bool bit(const RunState *rs, int b) { return rs->story_flags[b >> 3] & (1 << (b & 7)); }

// ---- the reference: can storylet st bind on rs? (exhaustive; a condition is checked once the
// last of its roles is bound)
static int cond_level(const StoryCond *c)
{
	if (c->role >= STORY_ROLES)
		return -1;
	return c->in == SI_REL && c->arg > c->role ? c->arg : c->role;
}

static bool ref_bind(const RunState *rs, const Storylet *st, int subject, int k, u8 *b)
{
	const StoryCond *sc = &story_cond[st->cond0];
	int ns = st->cend[STORY_ROLES], n = rs->nroster < ROSTER_MAX ? rs->nroster : ROSTER_MAX;
	if (k == st->nrole) {
		for (int i = 0; i < ns; i++)
			if (cond_level(&sc[i]) == -1 && !ref_cond_b(rs, b, &sc[i]))
				return false;
		if (!st->nchoice)
			return true;
		for (int c = 0; c < st->nchoice; c++) {
			const StoryChoice *ch = &story_choice[st->choice0 + c];
			bool ok = true;
			for (int i = 0; i < ch->ncond; i++)
				ok = ok && ref_cond_b(rs, b, &story_cond[ch->cond0 + i]);
			if (ok)
				return true;
		}
		return false;
	}
	for (int i = 0; i < n; i++) {
		const Person *p = &rs->roster[i];
		int kind = st->role_kind[k], want = st->role_cast[k], cast = p->cast < CAST_COUNT ? p->cast : 0xFF;
		if (kind == RK_ALIVE ? (!p->alive || i == subject) : (i != subject || (kind == RK_SUBJECT) != (p->alive != 0)))
			continue;
		if (want != STORY_CAST_ANY && (want == STORY_CAST_NAMED ? cast == 0xFF : cast != want))
			continue;
		bool dup = false, ok = true;
		for (int q = 0; q < k; q++)
			dup |= b[q] == i;
		if (dup)
			continue;
		b[k] = i;
		for (int c = 0; c < ns && ok; c++)
			if (cond_level(&sc[c]) == k)
				ok = ref_cond_b(rs, b, &sc[c]);
		if (ok && ref_bind(rs, st, subject, k + 1, b))
			return true;
	}
	return false;
}

static bool ref_eligible(const RunState *rs, int idx, int subject)
{
	const Storylet *st = &story_list[idx];
	if (!st->weight || (st->once && bit(rs, idx)))
		return false;
	if (st->excl_bit != STORY_NO_EXCL && bit(rs, st->excl_bit) && !bit(rs, idx))
		return false;
	if (st->cool_slot != STORY_NONE) {
		int stamp = rs->story_cool[st->cool_slot];
		if (stamp && rs->day < stamp - 1 + st->cool_days)
			return false;
	}
	u8 b[STORY_ROLES] = { 0 };
	return ref_bind(rs, st, subject, 0, b);
}

static long oracle_checks;

static void check_oracle(const RunState *b, int phase, int subject, bool played, u32 budget_out)
{
	if (phase < 0 || phase >= SP_COUNT)
		return;
	int n = b->nroster < ROSTER_MAX ? b->nroster : ROSTER_MAX, best = -1;
	if (subject < 0 || subject >= n)
		subject = -1;
	static bool elig[STORY_COUNT + 1];
	for (int k = story_phase_start[phase]; k < story_phase_start[phase + 1]; k++) {
		int idx = story_phase_list[k];
		elig[idx] = ref_eligible(b, idx, subject);
		if (elig[idx] && story_list[idx].priority > best)
			best = story_list[idx].priority;
	}
	oracle_checks++;
	if (played) {
		int idx = story_dbg_idx;
		CHECK(elig[idx], "%s played but the reference says it can't", story_ids[idx]);
		CHECK(story_list[idx].priority == best, "%s played at priority %d, top eligible is %d", story_ids[idx],
		      story_list[idx].priority, best);
	} else if (budget_out == story_dbg_budget_out) {
		CHECK(best < 0 || (best == 0 && story_phase_quiet[phase] > 0),
		      "phase %d: nothing played though a storylet of priority %d could (quiet %d)", phase, best,
		      story_phase_quiet[phase]);
	}
}

// what can change: res within the storylet's res_add totals
static int res_bound(const Storylet *st, int r)
{
	int b = 0;
	for (int k = 0; k < st->neff; k++)
		if (story_effect[st->eff0 + k].op == SE_RES_ADD && story_effect[st->eff0 + k].arg == r)
			b += abs(story_effect[st->eff0 + k].val);
	int cb = 0;
	for (int c = 0; c < st->nchoice; c++) {
		const StoryChoice *ch = &story_choice[st->choice0 + c];
		int x = 0;
		for (int k = 0; k < ch->neff; k++)
			if (story_effect[ch->eff0 + k].op == SE_RES_ADD && story_effect[ch->eff0 + k].arg == r)
				x += abs(story_effect[ch->eff0 + k].val);
		cb = cb > x ? cb : x;
	}
	return b + cb;
}

static void check_played(const RunState *b, int phase, int subject)
{
	int idx = story_dbg_idx;
	CHECK(idx >= 0 && idx < STORY_COUNT, "idx %d", idx);
	if (idx < 0 || idx >= STORY_COUNT)
		return;
	const Storylet *st = &story_list[idx];
	fired[idx]++;
	if (story_dbg_choice >= 0 && story_dbg_choice < STORY_CHOICES)
		picked[idx][story_dbg_choice]++;
	CHECK(st->phase == phase, "%s is not of phase %d", story_ids[idx], phase);
	CHECK(st->weight > 0, "%s has weight 0", story_ids[idx]);
	CHECK(!(st->once && bit(b, idx)), "%s is once and had fired", story_ids[idx]);
	CHECK(st->excl_bit == STORY_NO_EXCL || !bit(b, st->excl_bit) || bit(b, idx), "%s: excl group used", story_ids[idx]);
	if (st->cool_slot != STORY_NONE) {
		int stamp = b->story_cool[st->cool_slot];
		CHECK(!stamp || b->day >= stamp - 1 + st->cool_days, "%s on cooldown (day %d, last %d)", story_ids[idx], b->day, stamp - 1);
		CHECK(run.story_cool[st->cool_slot] == (b->day < 254 ? b->day : 254) + 1, "cooldown stamp");
	}
	CHECK(bit(&run, idx), "fired bit not set");
	int n = b->nroster < ROSTER_MAX ? b->nroster : ROSTER_MAX;
	for (int r = 0; r < st->nrole; r++) {
		int who = story_dbg_bind[r];
		CHECK(who < n, "%s role %d bound to %d of %d", story_ids[idx], r, who, n);
		if (who >= n)
			return;
		const Person *p = &b->roster[who];
		for (int q = 0; q < r; q++)
			CHECK(story_dbg_bind[q] != who, "%s: roles %d and %d both bound to %d", story_ids[idx], q, r, who);
		switch (st->role_kind[r]) {
		case RK_ALIVE:
			CHECK(p->alive && who != subject, "%s role %d: alive non-subject", story_ids[idx], r);
			break;
		case RK_SUBJECT:
			CHECK(p->alive && who == subject, "%s role %d: the alive subject", story_ids[idx], r);
			break;
		case RK_SUBJECT_DEAD:
			CHECK(!p->alive && who == subject, "%s role %d: the dead subject", story_ids[idx], r);
			break;
		}
		int want = st->role_cast[r], cast = p->cast < CAST_COUNT ? p->cast : 0xFF;
		CHECK(want == STORY_CAST_ANY || (want == STORY_CAST_NAMED ? cast != 0xFF : cast == want),
		      "%s role %d cast %d, want %d", story_ids[idx], r, cast, want);
	}
	for (int k = 0; k < st->cend[STORY_ROLES]; k++)
		CHECK(ref_cond(b, st, &story_cond[st->cond0 + k]), "%s condition %d failed", story_ids[idx], k);
	if (st->nchoice) {
		CHECK(story_dbg_choice >= 0 && story_dbg_choice < st->nchoice, "choice %d", story_dbg_choice);
		if (story_dbg_choice >= 0 && story_dbg_choice < st->nchoice) {
			const StoryChoice *ch = &story_choice[st->choice0 + story_dbg_choice];
			for (int k = 0; k < ch->ncond; k++)
				CHECK(ref_cond(b, st, &story_cond[ch->cond0 + k]), "%s choice %d condition %d failed",
				      story_ids[idx], story_dbg_choice + 1, k);
		}
	}
	for (int r = 0; r < RES_COUNT; r++)
		CHECK(abs(run.res[r] - b->res[r]) <= res_bound(st, r), "%s res %d: %d -> %d", story_ids[idx], r, b->res[r], run.res[r]);
	// the Chronicle: LOG_STORY first, then deaths and bites of bound people
	int added = run.log_count - b->log_count;
	CHECK(added >= 1, "no Chronicle record");
	if (added >= 1 && b->log_count < LOG_MAX) {
		const LogRec *lr = &run.log[(run.log_head + b->log_count) % LOG_MAX];
		CHECK(lr->event == LOG_STORY && lr->arg == idx && lr->day == run.day, "LOG_STORY record");
		const char *t = story_log_text(lr);
		if (story_list[idx].log) {
			CHECK(t != NULL, "%s: no journal line", story_ids[idx]);
			if (t)
				check_chars(t, 25);
		}
	}
}

static void check_state(const RunState *b, bool played)
{
	CHECK(run.morale <= 10, "morale %d", run.morale);
	for (int v = 0; v < SV_COUNT; v++)
		CHECK(run.vars[v] >= story_var_min[v] && run.vars[v] <= story_var_max[v], "var %d = %d", v, run.vars[v]);
	for (int v = SV_COUNT; v < STORY_VARS; v++)
		CHECK(run.vars[v] == b->vars[v], "unused var %d changed", v);
	int nbound = 0;
	u8 who[STORY_ROLES];
	if (played && story_dbg_idx >= 0 && story_dbg_idx < STORY_COUNT) {
		nbound = story_list[story_dbg_idx].nrole;
		memcpy(who, story_dbg_bind, sizeof who);
	}
	for (int i = 0; i < ROSTER_MAX; i++) {
		const Person *p = &run.roster[i], *o = &b->roster[i];
		bool is_bound = false;
		for (int r = 0; r < nbound; r++)
			is_bound |= who[r] == i;
		if (!o->alive || !is_bound) {
			CHECK(!memcmp(p, o, sizeof *p), "person %d changed (alive %d, bound %d)", i, o->alive, is_bound);
			continue;
		}
		CHECK(p->alive <= 1, "alive %d", p->alive);
		if (strict_death && !p->alive) { // killed by this storylet: nothing else may touch them
			Person a = *p, c = *o;
			a.alive = c.alive = a.hp = c.hp = 0;
			CHECK(!memcmp(&a, &c, sizeof a), "person %d changed after dying", i);
			for (int j = 0; j < ROSTER_MAX; j++)
				CHECK(run.rel[i][j] == b->rel[i][j], "rel %d %d changed after dying", i, j);
		}
		if (p->alive)
			CHECK(p->hp >= 1 && p->hp <= (o->hp > p->maxhp ? o->hp : p->maxhp), "hp %d/%d", p->hp, p->maxhp);
		else
			CHECK(p->hp == 0, "dead with hp %d", p->hp);
		CHECK(p->trait[0] < TR_COUNT && p->trait[1] < TR_COUNT, "trait out of range");
		CHECK(p->trait[1] == TR_NONE || p->trait[0] != p->trait[1], "duplicate trait %d", p->trait[0]);
		if (o->trait[0] != TR_NONE || o->trait[1] == TR_NONE)
			CHECK(p->trait[0] != TR_NONE || p->trait[1] == TR_NONE, "trait gap");
		for (int s = 0; s < SK_COUNT; s++)
			CHECK(p->skill[s] <= 5 && p->skill[s] >= o->skill[s] && p->skill_xp[s] >= o->skill_xp[s], "skill %d", s);
		CHECK(!(p->flags & ~(o->flags | PF_INFECTED)), "flags %x -> %x", o->flags, p->flags);
		if ((p->flags & PF_INFECTED) && !(o->flags & PF_INFECTED))
			CHECK(p->turn_in >= 3, "infected, turn_in %d", p->turn_in);
		if (!(p->flags & PF_INFECTED) && (o->flags & PF_INFECTED))
			CHECK(p->turn_in == 0, "cured, turn_in %d", p->turn_in);
		Person a = *p, c = *o; // the rest never changes
		a.alive = c.alive = a.hp = c.hp = a.flags = c.flags = a.turn_in = c.turn_in = 0;
		memset(a.trait, 0, 2);
		memset(c.trait, 0, 2);
		memset(a.skill, 0, sizeof a.skill + sizeof a.skill_xp);
		memset(c.skill, 0, sizeof c.skill + sizeof c.skill_xp);
		CHECK(!memcmp(&a, &c, sizeof a), "person %d: an untouchable field changed", i);
	}
	for (int i = 0; i < ROSTER_MAX; i++)
		for (int j = 0; j < ROSTER_MAX; j++) {
			CHECK(run.rel[i][j] >= -3 && run.rel[i][j] <= 3 && run.rel[i][j] == run.rel[j][i], "rel %d %d", i, j);
			if (run.rel[i][j] != b->rel[i][j]) {
				bool bi = false, bj = false;
				for (int r = 0; r < nbound; r++) {
					bi |= who[r] == i;
					bj |= who[r] == j;
				}
				CHECK(bi && bj && i != j, "rel %d %d changed between unbound people", i, j);
			}
		}
	for (int k = 0; k < (int)sizeof run.story_cool; k++)
		if (run.story_cool[k] != b->story_cool[k])
			CHECK(played && k == story_list[story_dbg_idx].cool_slot, "cool slot %d changed", k);
}

// ---------------------------------------------------------------- driver

static RunState before;

static bool fire(int phase, int subject)
{
	before = run;
	story_dbg_idx = -1;
	u32 budget_out = story_dbg_budget_out;
	bool played = story_fire(phase, subject);
	check_oracle(&before, phase, subject, played, budget_out);
	if (!played) {
		RunState a = run;
		a.rng = before.rng;
		CHECK(!memcmp(&a, &before, sizeof a), "nothing played but the run changed (phase %d)", phase);
		CHECK(story_dbg_idx == -1, "story_dbg_idx set");
		return false;
	}
	check_played(&before, phase, subject);
	check_state(&before, true);
	return true;
}

static int pick(bool dead)
{
	int n = 0, c[ROSTER_MAX];
	for (int i = 0; i < run.nroster && i < ROSTER_MAX; i++)
		if (!run.roster[i].alive == dead)
			c[n++] = i;
	return n ? c[rnd(n)] : -1;
}

int main(int argc, char **argv)
{
	int runs = 2000, rounds = 12;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-n") && i + 1 < argc)
			runs = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-s") && i + 1 < argc)
			hseed = (u32)atoi(argv[++i]) * 2654435761u | 1;
		else if (!strcmp(argv[i], "-d"))
			strict_death = true;
		else {
			printf("usage: story_fuzz [-n runs] [-s seed] [-d]\n");
			return 2;
		}
	}
	long calls = 0, plays = 0, deaths = 0, phase_plays[SP_COUNT] = { 0 };

	ctx = "init";
	random_run();
	story_init_run();
	for (int v = 0; v < SV_COUNT; v++)
		CHECK(run.vars[v] == story_var_def[v], "var %d default", v);
	for (int k = 0; k < STORY_FLAG_BYTES; k++)
		CHECK(!run.story_flags[k], "flags not cleared");

	ctx = "bad args";
	random_run();
	for (int k = 0; k < 50; k++) {
		CHECK(!fire(-1, -1) && !fire(SP_COUNT, -1) && !fire(99, 3), "a bad phase played");
		fire(rnd(SP_COUNT), rnd(40) - 20);
		run.nroster = 17 + rnd(200); // corrupt: clamped to ROSTER_MAX
		fire(rnd(SP_COUNT), rnd(ROSTER_MAX));
		run.nroster = rnd(ROSTER_MAX + 1);
	}

	ctx = "fuzz";
	for (int n = 0; n < runs; n++) {
		random_run();
		for (int round = 0; round < rounds; round++) {
			for (int ph = 0; ph < SP_COUNT; ph++) {
				int subject = -1;
				if (ph == SP_DEATH)
					subject = pick(true);
				else if (ph == SP_BITE && (subject = pick(false)) >= 0) {
					run.roster[subject].flags |= PF_INFECTED;
					if (!run.roster[subject].turn_in)
						run.roster[subject].turn_in = 3;
				}
				if (ph == SP_DEATH && subject < 0)
					continue;
				calls++;
				if (fire(ph, subject)) {
					plays++;
					phase_plays[ph]++;
					for (int i = 0; i < ROSTER_MAX; i++)
						deaths += before.roster[i].alive && !run.roster[i].alive;
				}
			}
			run.day += rnd(3);
			if (!rnd(4)) // someone dies between rounds, so the death phase gets fresh graves
				for (int i = 0; i < run.nroster; i++)
					if (run.roster[i].alive && !rnd(6)) {
						run.roster[i].alive = 0;
						run.roster[i].hp = 0;
						break;
					}
		}
	}

	ctx = "replay";
	for (int n = 0; n < 300; n++) {
		random_run();
		int ph = rnd(SP_COUNT), subject = ph == SP_DEATH ? pick(true) : ph == SP_BITE ? pick(false) : -1;
		RunState start = run;
		u32 seed = hseed;
		story_fire(ph, subject);
		RunState first = run;
		int idx = story_dbg_idx;
		run = start;
		hseed = seed;
		story_fire(ph, subject);
		CHECK(!memcmp(&first, &run, sizeof run) && idx == story_dbg_idx, "replay differs (phase %d)", ph);
	}

	ctx = "journal";
	for (int idx = 0; idx < STORY_COUNT; idx++) {
		LogRec r = { 3, LOG_STORY, (u8)rnd(ROSTER_MAX), (u8)idx };
		random_run();
		const char *t = story_log_text(&r);
		if (story_list[idx].log) {
			CHECK(t != NULL, "%s: no journal line", story_ids[idx]);
			if (t)
				check_chars(t, 25);
		} else
			CHECK(t == NULL, "%s: a journal line without a log", story_ids[idx]);
		r.actor = 0xFF;
		t = story_log_text(&r);
		if (t)
			check_chars(t, 25);
	}
	LogRec bad = { 1, LOG_STORY, 0, 255 };
	CHECK(STORY_COUNT > 255 || story_log_text(&bad) == NULL, "journal line for a bad index");
	bad.event = LOG_DEATH;
	CHECK(story_log_text(&bad) == NULL, "journal line for a death");

	printf("story_fuzz: %d runs x %d rounds: %ld calls, %ld played (", runs, rounds, calls, plays);
	for (int ph = 0; ph < SP_COUNT; ph++)
		printf("%s%ld", ph ? " " : "", phase_plays[ph]);
	printf(" by phase), %d lines, %d menus (%d B presses), %d banners, %ld deaths\n", lines_shown, menus_shown,
	       b_presses, banners_shown, deaths);
	int never = 0;
	for (int i = 0; i < STORY_COUNT; i++) {
		printf("  %-24s %6ld", story_ids[i], fired[i]);
		for (int c = 0; c < story_list[i].nchoice; c++)
			printf("  c%d %5ld", c + 1, picked[i][c]);
		printf("\n");
		if (!fired[i] && story_list[i].weight) {
			printf("    NEVER FIRED\n");
			never++;
		}
		for (int c = 0; c < story_list[i].nchoice; c++)
			if (fired[i] && !picked[i][c])
				printf("    choice %d never picked\n", c + 1);
	}
	if (story_dbg_budget_out)
		printf("  binding budget ran out %u times\n", story_dbg_budget_out);
	printf("  %ld calls agreed with the reference search\n", oracle_checks);
	printf("story_fuzz: %d checks, %d failures%s\n", checks, fails, never ? " (some storylets never fired)" : "");
	return fails || never ? 1 : 0;
}
