// Dead Wars - profiling build helpers (only compiled in with PERF=1)
#include "game.h"
#include "perf.h"
#include <string.h>

#ifdef PERF
PerfData perf EWRAM_BSS;

void perf_init(void)
{
	memset(&perf, 0, sizeof perf);
	perf.magic = 0x46524550; // "PERF"
	REG_TM2CNT = 0;
	REG_TM3CNT = 0;
	REG_TM2D = 0;
	REG_TM3D = 0;
	REG_TM3CNT = 0x0084; // enable, cascade
	REG_TM2CNT = 0x0080; // enable, 1 cycle
}

// Worst case the map format allows: a 30x20 open field, a ring of survivors in
// the middle and 60 awake zombies (mostly runners) all around them.
void perf_stress(void)
{
	gs.w = MAP_MAX_W;
	gs.h = MAP_MAX_H;
	memset(gs.owner, SIDE_NEUTRAL, sizeof gs.owner);
	memset(gs.units, 0, sizeof gs.units);
	for (int y = 0; y < gs.h; y++)
		for (int x = 0; x < gs.w; x++) {
			u32 r = rng_next() % 16;
			gs.terrain[y][x] = r < 10 ? TT_PLAIN : r < 13 ? TT_FOREST : r < 14 ? TT_RUINS : TT_ROAD;
		}
	gs.terrain[10][15] = TT_SAFEHOUSE;
	gs.owner[10][15] = SIDE_SURVIVOR;
	static const s8 ring[8][2] = { { 0, -2 }, { 2, 0 }, { 0, 2 }, { -2, 0 }, { 1, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 } };
	for (int i = 0; i < 8; i++)
		spawn_unit(i & 1 ? UT_HEAVY : UT_SURVIVOR, SIDE_SURVIVOR, 15 + ring[i][0], 10 + ring[i][1], 100);
	int n = 0;
	for (int y = 0; y < gs.h && n < 60; y += 2)
		for (int x = 0; x < gs.w && n < 60; x += 3) {
			if (ABS(x - 15) + ABS(y - 10) < 6)
				continue;
			spawn_unit(n % 4 ? UT_RUNNER : UT_WALKER, SIDE_HORDE, x + (y & 2 ? 1 : 0), y, 100);
			n++;
		}
	gs.cur_x = 15;
	gs.cur_y = 10;
}
#endif
