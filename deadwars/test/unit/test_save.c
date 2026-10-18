// Dead Wars - host tests of the battery save (save.c): the profile and the classic suspend.
// save.c is included so its SUSPEND_* constants are visible; SRAM is host_sram (gba.h, HOST_SIM).
#include "../../src/save.c"
#include "unit.h"

int checks, fails;
const char *test_name = "";
u8 host_sram[0x10000];
GameState gs;

static u32 sram_u32(int off)
{
	u32 v;
	sram_read(off, &v, 4);
	return v;
}

static void boot(void)
{
	memset(&profile, 0, sizeof profile);
	profile_load();
}

static void test_fresh(void)
{
	test_name = "fresh SRAM";
	memset(host_sram, 0xFF, sizeof host_sram);
	boot();
	EQ(profile.magic, PROFILE_MAGIC);
	EQ(profile.unlocked, 1);
	EQ(profile.has_suspend, 0);
}

static void test_resume(void)
{
	test_name = "suspend and resume";
	memset(host_sram, 0xFF, sizeof host_sram);
	boot();
	memset(&gs, 0, sizeof gs);
	gs.mission = 3;
	gs.day = 5;
	suspend_save();
	EQ(profile.has_suspend, 1);
	memset(&gs, 0, sizeof gs);
	boot();
	EQ(profile.has_suspend, 1);              // a good suspend survives the reboot
	CHECK(suspend_load());
	EQ(gs.mission, 3);
	EQ(gs.day, 5);
	suspend_clear();
	EQ(profile.has_suspend, 0);
	EQ(sram_u32(SUSPEND_OFFSET), 0);
}

// A classic suspend written by an older build ("DWS1", smaller GameState) can't be resumed. The
// profile's has_suspend must not survive the update, or "Continue" stays the default title entry
// and drops straight back to the title on every boot.
static void test_old_suspend(void)
{
	test_name = "old-build suspend";
	memset(host_sram, 0xFF, sizeof host_sram);
	boot();
	profile.has_suspend = 1;
	profile_save();
	u32 old = 0x44575331;                    // "DWS1"
	sram_write(SUSPEND_OFFSET, &old, 4);
	SRAM[SUSPEND_OFFSET + 4] = 1;            // its mission
	boot();
	EQ(profile.has_suspend, 0);
	Profile saved;
	sram_read(0, &saved, sizeof saved);
	EQ(saved.has_suspend, 0);                // cleared in SRAM too, not just this boot
	EQ(saved.unlocked, 1);                   // the rest of the profile is kept
	// and loading it neither succeeds nor overwrites the live state
	SRAM[SUSPEND_OFFSET] = 0x31;
	memset(&gs, 0, sizeof gs);
	gs.mission = 7;
	CHECK(!suspend_load());
	EQ(gs.mission, 7);
}

int main(void)
{
	test_fresh();
	test_resume();
	test_old_suspend();
	printf("save: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
