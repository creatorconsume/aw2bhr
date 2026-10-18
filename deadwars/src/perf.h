// Dead Wars - optional cycle profiling (build with `make PERF=1`)
// Timers 2+3 cascade into a free-running 32-bit CPU cycle counter. Results
// collect in `perf` (EWRAM); dump it with test/gbarun.c (see docs/tech/PERF.md).
#ifndef PERF_H
#define PERF_H
#include "gba.h"

#ifdef PERF
typedef struct { u32 n, sum, max; } PerfStat;
typedef struct {
	u32 magic;           // "PERF" once perf_init ran
	PerfStat frame_work; // cycles of game work between two vblank waits
	PerfStat paths;      // find_paths
	PerfStat think;      // one zombie choosing its move (find_paths + scoring)
	PerfStat field;      // build_field
	u32 dropped;         // frames whose work overran one vblank period
	u32 turn_frame_max;  // worst frame_work since the horde turn started
} PerfData;
extern PerfData perf;

#define REG_TM2D  REG16(0x04000108)
#define REG_TM2CNT REG16(0x0400010A)
#define REG_TM3D  REG16(0x0400010C)
#define REG_TM3CNT REG16(0x0400010E)

static inline u32 perf_now(void)
{
	u32 hi, lo;
	do {
		hi = REG_TM3D;
		lo = REG_TM2D;
	} while (hi != REG_TM3D);
	return hi << 16 | lo;
}
static inline void perf_add(PerfStat *s, u32 t0)
{
	u32 c = perf_now() - t0;
	s->n++;
	s->sum += c;
	if (c > s->max)
		s->max = c;
}
#define PERF_TURN_START() (perf.turn_frame_max = 1)
void perf_init(void);
void perf_stress(void);
#define PERF_T0(v) u32 v = perf_now()
#define PERF_ADD(stat, t0) perf_add(&perf.stat, t0)
#else
#define PERF_T0(v) do {} while (0)
#define PERF_ADD(stat, t0) do {} while (0)
#define PERF_TURN_START() do {} while (0)
#endif

#endif
