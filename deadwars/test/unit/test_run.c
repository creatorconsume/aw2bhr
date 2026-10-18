// Dead Wars: The Last Boat - host tests of the run layer (run.c, runsave.c): meals and hunger, power
// cuts against the A/B save, the scenes a resumed run still owes, what the stop choices cost.
// run.c is included so its static helpers can be called. The screens are stubbed below; the
// storylets are a recorder (story_fire) that can also cut the power in the middle of a scene; the
// player is frame_hook (stubs.c): it presses queued keys, and with none left the game is waiting
// for input, so the test takes over there. See Makefile.
#include "../../src/run.c"
#include "unit.h"
#include <setjmp.h>

int checks, fails;
const char *test_name = "";
extern int sram_cut;              // runsave.c (HOST_SIM): power off after this many byte writes
extern void (*frame_hook)(void);  // stubs.c

// ---------------------------------------------------------------- UI stand-ins
#define BANNERS 16
static char banner_l1[BANNERS][32];
static int banners;

void banner(const char *l1, const char *l2, int frames)
{
	if (banners < BANNERS)
		snprintf(banner_l1[banners], sizeof banner_l1[0], "%s", l1 ? l1 : "");
	banners++;
}

void text_clear(void) {}
void text_put(int x, int y, const char *s, int pal) {}
void text_window(int x, int y, int w, int h) {}
void text_clear_rect(int x, int y, int w, int h) {}
void text_printf(int x, int y, int pal, const char *fmt, ...) {}
void dialogue(const Line *lines) {}
void video_show_title(void) {}

static bool bannered(const char *l1)
{
	for (int i = 0; i < banners && i < BANNERS; i++)
		if (!strcmp(banner_l1[i], l1))
			return true;
	return false;
}

// ---------------------------------------------------------------- the player and the power
static jmp_buf waiting, power;
static u16 press[8];
static int npress;

static void player(void)
{
	keys_down = npress ? press[0] : 0;
	if (!npress)
		longjmp(waiting, 1); // the game waits for the player: the test takes over
	memmove(press, press + 1, sizeof press - sizeof press[0]);
	npress--;
}

static void keys(int n, ...)
{
	va_list ap;
	va_start(ap, n);
	for (npress = 0; npress < n && npress < 8; npress++)
		press[npress] = va_arg(ap, int);
	va_end(ap);
}

// true: fn returned; false: it stopped where the game waits for input
static bool play(void (*fn)(void))
{
	if (setjmp(waiting))
		return false;
	fn();
	return true;
}

// ---------------------------------------------------------------- storylets: a recorder
static u8 fired_phase[32];
static s8 fired_subject[32];
static int nfired;
static int cut_phase = -1;        // the power goes as a scene of this phase plays

bool story_fire(int phase, int subject)
{
	if (phase == cut_phase) {
		cut_phase = -1;
		longjmp(power, 1);
	}
	if (nfired < 32) {
		fired_phase[nfired] = phase;
		fired_subject[nfired] = subject;
	}
	nfired++;
	return false;
}

void story_init_run(void) {}
const char *story_log_text(const LogRec *r) { return NULL; }

static int fired(int phase, int subject)
{
	int n = 0;
	for (int i = 0; i < nfired && i < 32; i++)
		n += fired_phase[i] == phase && (subject < -1 || fired_subject[i] == subject);
	return n;
}

// ---------------------------------------------------------------- the mission: tac_run
enum { MARA, DOC, JUNE, HOLT };
static int mission_result = 1;
static bool mission_hurts;        // Doc is killed, June is bitten

int tac_run(const TacMission *m, const u8 *deploy, int n)
{
	if (mission_hurts) {
		run.roster[DOC].alive = 0;
		run.roster[DOC].hp = 0;
		chronicle(LOG_DEATH, DOC, DC_ZOMBIE);
		person_bitten(&run.roster[JUNE]); // as tac_rules.c: the timer by difficulty
		chronicle(LOG_BITTEN, JUNE, 0);
	}
	return mission_result;
}

// ---------------------------------------------------------------- helpers
static void fresh(void)
{
	memset(host_sram, 0, sizeof host_sram);
	frame_hook = player;
	npress = 0;
	sram_cut = -1;
	cut_phase = -1;
	mission_result = 1;
	mission_hurts = false;
	run_new(1);
	run.ship_day = 200;
	run_save();
	banners = nfired = 0;
}

// The power comes back: RAM is gone, the game boots and the player picks Continue.
static bool reboot(void)
{
	memset(&run, 0x5A, sizeof run);
	run_phase = 0xEE;
	gfx_ready = false;
	sram_cut = -1;
	banners = nfired = 0;
	if (!run_load())
		return false;
	sanitize();
	return true;
}

static int total_hp(void)
{
	int h = 0;
	for (int i = 0; i < run.nroster; i++)
		h += run.roster[i].alive ? run.roster[i].hp : 0;
	return h;
}

static void set_hp(const u8 *hp)
{
	for (int i = 0; i < 4; i++) {
		run.roster[i].hp = hp[i];
		run.roster[i].alive = hp[i] != 0;
	}
}

// ---------------------------------------------------------------- meals and hunger
static void test_meals(void)
{
	test_name = "meals";
	static const u8 doc_low[4] = { 10, 1, 8, 10 };

	// full meals for 4 with 3 food: half meals for everyone, nobody starves, 1 food is kept
	fresh();
	set_hp(doc_low);
	run.res[RES_FOOD] = 3;
	run.morale = 6;
	CHECK(day_pass());
	EQ(run.res[RES_FOOD], 1);
	EQ(run.roster[DOC].alive, 1);
	EQ(run.roster[DOC].hp, 1);
	EQ(total_hp(), 29);
	EQ(run.morale, 5);                        // short meals: -1
	EQ(ate, false);                           // the cooks have nothing to stretch
	CHECK(bannered("Not enough food."));

	// 1 food feeds two: the two healthiest go hungry, Doc (1 HP) eats
	fresh();
	set_hp(doc_low);
	run.res[RES_FOOD] = 1;
	run.morale = 6;
	CHECK(day_pass());
	EQ(run.res[RES_FOOD], 0);
	EQ(run.roster[MARA].hp, 9);
	EQ(run.roster[DOC].hp, 1);
	EQ(run.roster[JUNE].hp, 8);
	EQ(run.roster[HOLT].hp, 9);
	EQ(alive_count(), 4);
	EQ(run.morale, 5);
	CHECK(bannered("The food runs out."));

	// half meals for 3 (Holt gone) with 1 food: two eat, one goes without
	fresh();
	static const u8 three[4] = { 2, 1, 3, 0 };
	set_hp(three);
	run.rations = 0;
	run.res[RES_FOOD] = 1;
	CHECK(day_pass());
	EQ(run.res[RES_FOOD], 0);
	EQ(total_hp(), 5);
	EQ(run.roster[JUNE].hp, 2);
	EQ(alive_count(), 3);

	// no food at all: everyone goes hungry, 1 HP starves
	fresh();
	set_hp(doc_low);
	run.res[RES_FOOD] = 0;
	CHECK(day_pass());
	EQ(run.roster[DOC].alive, 0);
	EQ(total_hp(), 9 + 7 + 9);
	EQ(fired(SP_DEATH, DOC), 1);
	CHECK(bannered("The food is gone."));

	// enough: full meals
	fresh();
	set_hp(doc_low);
	run.res[RES_FOOD] = 10;
	run.morale = 6;
	CHECK(day_pass());
	EQ(run.res[RES_FOOD], 6);
	EQ(total_hp(), 29);
	EQ(run.morale, 6);
	EQ(ate, true);

	// more food never ends worse: fewer or the same deaths, as much HP or more; HP is only lost
	// when every crumb is eaten, and never while there is enough for half meals
	static const u8 hp0[4] = { 3, 1, 2, 5 };
	for (int rations = 0; rations < 2; rations++)
		for (int alive = 1; alive <= 4; alive++) {
			int deaths0 = 99, hp_prev = -1;
			for (int food = 0; food <= 9; food++) {
				fresh();
				set_hp(hp0);
				for (int i = alive; i < 4; i++)
					run.roster[i].alive = 0;
				int before = total_hp(), n = alive_count();
				run.rations = rations;
				run.res[RES_FOOD] = food;
				day_pass();
				int deaths = n - alive_count(), hp = total_hp(), left = run.res[RES_FOOD];
				if (deaths > deaths0 || hp < hp_prev || left > food || ((left || 2 * food >= n) && hp < before))
					FAILF("rations %d, %d alive, food %d: %d dead, HP %d -> %d, food left %d (food %d: %d dead, HP %d)",
					      rations, alive, food, deaths, before, hp, left, food - 1, deaths0, hp_prev);
				checks++;
				deaths0 = deaths;
				hp_prev = hp;
			}
		}
}

// ---------------------------------------------------------------- the A/B save and power cuts
static void test_power_cuts(void)
{
	test_name = "autosave cut";
	static u8 snap[0x8000];
	// a cut at any byte of an autosave leaves the run as it was before it, or as it is after it
	fresh();
	run.day = 3;
	run_save();
	memcpy(snap, host_sram, sizeof snap);
	int total = sizeof(RunState) + 20, bad = 0; // magic zeroed, the data, the header, the magic
	for (int cut = 0; cut <= total + 1; cut++) {
		memcpy(host_sram, snap, sizeof snap);
		run_new(1);
		run.day = 4;
		sram_cut = cut;
		run_save();
		bool ok = reboot();
		if (!ok || run.day != (cut >= total ? 4 : 3))
			bad++;
	}
	EQ(bad, 0);

	// erasing a finished run: a cut between the two slots never brings back the run before it
	// ended, whichever slot the final save went to
	test_name = "erase cut";
	for (int extra = 0; extra < 2; extra++) {
		fresh();                                   // slot A: seq 1
		for (int k = 0; k <= extra; k++) {
			run.day = 4 + k;
			run_save();                            // B (seq 2), then A (seq 3)
		}
		run.day = 7;
		run.over = 2;
		run_save();                                // the over state: A, then B
		memcpy(snap, host_sram, sizeof snap);
		for (int cut = 0; cut <= 32; cut++) { // two 16-byte headers
			memcpy(host_sram, snap, sizeof snap);
			sram_cut = cut;
			run_erase();
			sram_cut = -1;
			if (reboot() && run.over != 2)
				FAILF("final save in slot %c, cut after %d bytes: Continue loads day %d, over %d",
				      extra ? 'B' : 'A', cut, run.day, run.over);
			checks++;
		}
		EQ(run_exists(), false);                   // the last cut point wrote everything
	}
}

// ---------------------------------------------------------------- scenes a resumed run owes
static void go_main(void) { arrive(N_MAIN); }
static void go_gas(void) { arrive(N_GAS); }

// Arrive at `go` with the power cut as a `cut` scene plays (-1: no cut), then Continue.
static void arrival(void (*go)(void), int cut, int roads)
{
	fresh();
	cut_phase = cut;
	if (!setjmp(power)) {
		CHECK(play(go));
		CHECK(cut < 0);                           // not cut off
	}
	CHECK(reboot());
	EQ(run_phase & PH_MASK, PH_STOP);
	CHECK(!play(run_loop));                     // Continue: up to the stop menu
	EQ(fired(SP_STOP, -1), cut >= 0);           // played again only if it was cut off
	EQ(fired(SP_ROAD, -1), cut >= 0 && roads);
	EQ(run_phase, PH_STOP);
	EQ(stop_ids[0], go == go_main ? ACT_MISSION : ACT_REST);
	CHECK(reboot());                            // and once is enough
	CHECK(!play(run_loop));
	EQ(nfired, 0);
	EQ(run_phase, PH_STOP);
}

static void test_arrival(void)
{
	test_name = "arrival cut";
	arrival(go_main, SP_STOP, 0);
	arrival(go_gas, SP_STOP, 1);
	arrival(go_gas, SP_ROAD, 1);
	test_name = "arrival";
	arrival(go_main, -1, 0);
}

static void go_farm(void) { mission(node_mission(N_FARM)); }

// Lose at Hensley Farm (Doc killed, June bitten) with the power cut as a `cut` scene plays.
static void after_mission(int cut)
{
	fresh();
	run.node = N_FARM;
	run_phase = PH_STOP;
	run_save();
	mission_result = 2;
	mission_hurts = true;
	cut_phase = cut;
	keys(1, KEY_START);                         // deploy the first four
	if (!setjmp(power)) {
		CHECK(play(go_farm));
		CHECK(cut < 0);                           // not cut off
	}
	int played = nfired;
	CHECK(reboot());
	EQ(run.roster[DOC].alive, 0);               // the outcome stays
	EQ(!!(run.roster[JUNE].flags & PF_INFECTED), 1);
	CHECK(!play(run_loop));                     // Continue: up to the job board
	int death = cut == SP_DEATH, bite = death || cut == SP_BITE, end = cut >= 0;
	EQ(fired(SP_DEATH, DOC), death);
	EQ(fired(SP_BITE, JUNE), bite);
	EQ(fired(SP_MISSION_END, -1), end);
	EQ(nfired, death + bite + end);
	EQ(bannered("No way through."), end);
	if (cut < 0)
		EQ(played, 4);                            // mission start, death, bite, mission end
	EQ(run_phase, PH_CAMP | PH_DAWN_DAY);
	EQ(run.roster[DOC].flags & (PF_DEATH_SCENE | PF_BITE_SCENE), 0);
	EQ(run.roster[JUNE].flags & (PF_DEATH_SCENE | PF_BITE_SCENE), 0);
	CHECK(reboot());                            // and once is enough
	CHECK(!play(run_loop));
	EQ(nfired, 0);
}

static void test_after_mission(void)
{
	test_name = "after-mission cut";
	after_mission(SP_DEATH);
	after_mission(SP_BITE);
	after_mission(SP_MISSION_END);
	test_name = "after mission";
	after_mission(-1);
}

// ---------------------------------------------------------------- who went, and the bite timer
// PF_DEPLOYED marks exactly who went in (storylets: `deployed`); a bite's timer is set once, at the
// bite (person_bitten, by difficulty), and the mission adds nothing to it.
static void test_who_went(void)
{
	test_name = "who went";
	fresh();
	run.difficulty = 0;                         // Easy: a bite turns 2 days later
	run.roster[MARA].flags |= PF_DEPLOYED;      // went on an earlier mission
	run.node = N_FARM;
	run_phase = PH_STOP;
	run_save();
	mission_hurts = true;                       // Doc killed, June bitten
	keys(2, KEY_A, KEY_START);                  // Mara stays with the cars
	CHECK(play(go_farm));
	EQ(run.roster[MARA].flags & PF_DEPLOYED, 0);
	EQ(run.roster[DOC].flags & PF_DEPLOYED, PF_DEPLOYED);
	EQ(run.roster[JUNE].flags & PF_DEPLOYED, PF_DEPLOYED);
	EQ(run.roster[HOLT].flags & PF_DEPLOYED, PF_DEPLOYED);
	EQ(fired(SP_BITE, JUNE), 1);
	EQ(run.roster[JUNE].turn_in, 3 + run.roster[JUNE].grit / 2 + 2);
	CHECK(reboot());                            // it is saved with the people
	EQ(run.roster[MARA].flags & PF_DEPLOYED, 0);
	EQ(run.roster[HOLT].flags & PF_DEPLOYED, PF_DEPLOYED);
}

// ---------------------------------------------------------------- what the stop choices cost
static void stop_menu(void) { stop(); }

static bool offers(int node, int act)
{
	fresh();
	run.node = node;
	run_phase = PH_STOP;
	memset(stop_ids, 0xFF, sizeof stop_ids);
	CHECK(!play(stop_menu));                    // the menu waits
	for (int i = 0; i < ACT_COUNT; i++)
		if (stop_ids[i] == act)
			return true;
	return false;
}

static void fail_farm(void)
{
	mission(node_mission(N_FARM));
	camp();
}

static void bypass_farm(void)
{
	bypass();
	camp();
}

static void test_stop_prices(void)
{
	test_name = "stop choices";
	// skipping a scavenge run is free (going in and straight out is); the farm can be bypassed
	CHECK(!offers(N_MAIN, ACT_BYPASS));
	CHECK(offers(N_MAIN, ACT_CAMP));
	CHECK(offers(N_MAIN, ACT_MISSION));
	CHECK(offers(N_FARM, ACT_BYPASS));
	CHECK(!offers(N_FARM, ACT_CAMP));
	CHECK(!offers(N_BRIDGE, ACT_BYPASS));
	CHECK(!offers(N_BRIDGE, ACT_CAMP));
	CHECK(offers(N_GAS, ACT_CAMP));

	// trying the farm and failing at once is never cheaper than going around it
	test_name = "farm detour";
	int fuel[2], day[2];
	for (int k = 0; k < 2; k++) {
		fresh();
		run.node = N_FARM;
		run.day = 4;
		run.res[RES_FUEL] = 9;
		run_phase = PH_STOP;
		mission_result = 2;
		if (k)
			keys(2, KEY_START, KEY_A);              // camp: the job board, the night summary
		else
			keys(3, KEY_START, KEY_START, KEY_A);   // deploy too
		CHECK(play(k ? bypass_farm : fail_farm));
		EQ(run_phase, PH_ROAD);
		fuel[k] = run.res[RES_FUEL];
		day[k] = run.day;
	}
	EQ(day[0], 5);
	EQ(day[1], 5);
	EQ(fuel[1], 9 - BYPASS_FUEL);
	EQ(fuel[0], fuel[1]);
}

int main(void)
{
	test_meals();
	test_power_cuts();
	test_arrival();
	test_after_mission();
	test_who_went();
	test_stop_prices();
	printf("run layer: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
