// Dead Wars - battery save (SRAM): profile + one suspended game
#include "game.h"
#include <string.h>

#define PROFILE_MAGIC 0x44575031 // "DWP1"
#define SUSPEND_MAGIC 0x44575332 // "DWS2": GameState grew to 32x32 maps
#define SUSPEND_OFFSET 256

// emulators look for this string to pick the save type
__attribute__((used, aligned(4))) static const char save_tag[] = "SRAM_V113";

Profile profile EWRAM_BSS;

static void sram_read(int off, void *dst, int n)
{
	u8 *d = dst;
	for (int i = 0; i < n; i++)
		d[i] = SRAM[off + i];
}

static void sram_write(int off, const void *src, int n)
{
	const u8 *s = src;
	for (int i = 0; i < n; i++)
		SRAM[off + i] = s[i];
}

void profile_load(void)
{
	(void)*(volatile const char *)save_tag;
	sram_read(0, &profile, sizeof profile);
	if (profile.magic != PROFILE_MAGIC) {
		memset(&profile, 0, sizeof profile);
		profile.magic = PROFILE_MAGIC;
		profile.unlocked = 1;
		profile.anim_speed = 1; // Normal: AW2 pace
		profile_save();
	}
}

void profile_save(void)
{
	sram_write(0, &profile, sizeof profile);
}

void suspend_save(void)
{
	gs.magic = SUSPEND_MAGIC;
	sram_write(SUSPEND_OFFSET, &gs, sizeof gs);
	profile.has_suspend = 1;
	profile_save();
}

bool suspend_load(void)
{
	sram_read(SUSPEND_OFFSET, &gs, sizeof gs);
	return gs.magic == SUSPEND_MAGIC;
}

void suspend_clear(void)
{
	u32 zero = 0;
	sram_write(SUSPEND_OFFSET, &zero, 4);
	profile.has_suspend = 0;
	profile_save();
}
