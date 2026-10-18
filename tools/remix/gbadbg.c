// Headless GBA runner + memory/debug helpers (libmgba 0.10), for AW2 remix research.
// build: gcc -O2 -o gbadbg gbadbg.c -lmgba
// usage: gbadbg ROM SCRIPT OUTPREFIX
// SCRIPT lines (one per line, '#' comments):
//   wait N | press KEYS N | hold KEYS | release | shot NAME   (as gbarun; KEYS from A B L R S T U D < >)
//   save FILE | load FILE                (mGBA savestate)
//   dump ADDR LEN FILE                   (raw bus read -> file)
//   poke8|poke16|poke32 ADDR VAL         (bus write)
//   watch ADDR LEN [w|r|rw]              (watchpoint on each byte; hits logged to stdout)
//   break ADDR                           (hardware breakpoint; hits logged w/ regs)
//   maxhits N                            (stop logging after N hits, default 200)
//   regs                                 (print registers)
//   echo TEXT
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba/debugger/debugger.h>
#include <mgba/internal/arm/arm.h>
#include <mgba-util/vfs.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { GBA_KEY_A, GBA_KEY_B, GBA_KEY_SELECT, GBA_KEY_START, GBA_KEY_RIGHT, GBA_KEY_LEFT,
       GBA_KEY_UP, GBA_KEY_DOWN, GBA_KEY_R, GBA_KEY_L };

static void quietLog(struct mLogger* l, int c, enum mLogLevel lv, const char* f, va_list a) {
	(void) l; (void) c; (void) lv; (void) f; (void) a;
}
static struct mLogger logger = { .log = quietLog };
static unsigned parseKeys(const char* s) {
	unsigned k = 0;
	for (; *s; ++s) switch (*s) {
		case 'A': k |= 1 << GBA_KEY_A; break;      case 'B': k |= 1 << GBA_KEY_B; break;
		case 'S': k |= 1 << GBA_KEY_SELECT; break; case 'T': k |= 1 << GBA_KEY_START; break;
		case '>': k |= 1 << GBA_KEY_RIGHT; break;  case '<': k |= 1 << GBA_KEY_LEFT; break;
		case 'U': k |= 1 << GBA_KEY_UP; break;     case 'D': k |= 1 << GBA_KEY_DOWN; break;
		case 'R': k |= 1 << GBA_KEY_R; break;      case 'L': k |= 1 << GBA_KEY_L; break;
	}
	return k;
}
static color_t* buf;
static unsigned W, H;
static struct mCore* core;
static struct mDebugger dbg;
static int useDbg = 0, hits = 0, maxhits = 200;
static long frames = 0;

static void shot(const char* prefix, const char* name) {
	char path[512];
	snprintf(path, sizeof path, "%s-%s.ppm", prefix, name);
	FILE* f = fopen(path, "wb");
	fprintf(f, "P6\n%u %u\n255\n", W, H);
	for (unsigned i = 0; i < W * H; ++i) {
		uint32_t c = buf[i];
		unsigned char px[3] = { c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF };
		fwrite(px, 1, 3, f);
	}
	fclose(f);
}
static void printRegs(void) {
	struct ARMCore* cpu = core->cpu;
	printf("  r0=%08X r1=%08X r2=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X\n  r8=%08X r9=%08X r10=%08X r11=%08X r12=%08X sp=%08X lr=%08X pc=%08X\n",
		cpu->gprs[0], cpu->gprs[1], cpu->gprs[2], cpu->gprs[3], cpu->gprs[4], cpu->gprs[5], cpu->gprs[6], cpu->gprs[7],
		cpu->gprs[8], cpu->gprs[9], cpu->gprs[10], cpu->gprs[11], cpu->gprs[12], cpu->gprs[13], cpu->gprs[14], cpu->gprs[15]);
}
static void entered(struct mDebugger* d, enum mDebuggerEntryReason r, struct mDebuggerEntryInfo* info) {
	struct ARMCore* cpu = core->cpu;
	d->state = DEBUGGER_RUNNING;
	if (hits++ >= maxhits) return;
	int thumb = cpu->cpsr.t;
	uint32_t pc = cpu->gprs[15] - (thumb ? 4 : 8);
	if (r == DEBUGGER_ENTER_WATCHPOINT) {
		printf("WATCH f=%ld addr=%08X old=%X new=%X acc=%d pc~%08X lr=%08X\n", frames, info->address,
			info->type.wp.oldValue, info->type.wp.newValue, info->type.wp.accessType, pc, cpu->gprs[14]);
	} else if (r == DEBUGGER_ENTER_BREAKPOINT) {
		printf("BREAK f=%ld addr=%08X\n", frames, info ? info->address : pc);
		printRegs();
	} else {
		printf("ENTER reason=%d pc~%08X\n", r, pc);
	}
}
static void ensureDbg(void) {
	if (useDbg) return;
	memset(&dbg, 0, sizeof dbg);
	dbg.type = DEBUGGER_CUSTOM;
	dbg.entered = entered;
	mDebuggerAttach(&dbg, core);
	dbg.state = DEBUGGER_RUNNING;
	useDbg = 1;
}
static void frame(unsigned keys) {
	core->setKeys(core, keys);
	if (useDbg) mDebuggerRunFrame(&dbg); else core->runFrame(core);
	++frames;
}
int main(int argc, char** argv) {
	if (argc < 4) { fprintf(stderr, "usage: gbadbg ROM SCRIPT OUTPREFIX\n"); return 2; }
	mLogSetDefaultLogger(&logger);
	core = mCoreFind(argv[1]);
	if (!core || !core->init(core)) { fprintf(stderr, "no core\n"); return 1; }
	mCoreInitConfig(core, NULL);
	core->desiredVideoDimensions(core, &W, &H);
	buf = calloc(W * H, sizeof(color_t));
	core->setVideoBuffer(core, buf, W);
	if (!mCoreLoadFile(core, argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
	core->reset(core);
	FILE* s = strcmp(argv[2], "-") ? fopen(argv[2], "r") : stdin;
	char line[512];
	unsigned held = 0;
	setvbuf(stdout, NULL, _IOLBF, 0);
	while (fgets(line, sizeof line, s)) {
		char cmd[32] = "", a1[256] = "", a2[256] = "", a3[256] = "";
		if (sscanf(line, "%31s %255s %255s %255s", cmd, a1, a2, a3) < 1 || cmd[0] == '#') continue;
		if (!strcmp(cmd, "wait")) {
			int n = atoi(a1);
			for (int i = 0; i < n; ++i) frame(held);
		} else if (!strcmp(cmd, "press")) {
			unsigned k = parseKeys(a1);
			int n = atoi(a2); if (n <= 0) n = 2;
			for (int i = 0; i < n; ++i) frame(held | k);
			for (int i = 0; i < 2; ++i) frame(held);
		} else if (!strcmp(cmd, "hold")) { held |= parseKeys(a1);
		} else if (!strcmp(cmd, "release")) { held = 0;
		} else if (!strcmp(cmd, "shot")) { shot(argv[3], a1);
		} else if (!strcmp(cmd, "save")) {
			struct VFile* vf = VFileOpen(a1, O_CREAT | O_TRUNC | O_RDWR);
			if (!vf || !mCoreSaveStateNamed(core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC)) fprintf(stderr, "save failed\n");
			if (vf) vf->close(vf);
		} else if (!strcmp(cmd, "load")) {
			struct VFile* vf = VFileOpen(a1, O_RDONLY);
			if (!vf || !mCoreLoadStateNamed(core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC)) fprintf(stderr, "load failed\n");
			if (vf) vf->close(vf);
		} else if (!strcmp(cmd, "dump")) {
			uint32_t a = strtoul(a1, 0, 0), n = strtoul(a2, 0, 0);
			FILE* f = fopen(a3, "wb");
			for (uint32_t i = 0; i < n; ++i) fputc(core->busRead8(core, a + i), f);
			fclose(f);
		} else if (!strcmp(cmd, "poke8")) { core->busWrite8(core, strtoul(a1, 0, 0), strtoul(a2, 0, 0));
		} else if (!strcmp(cmd, "poke16")) { core->busWrite16(core, strtoul(a1, 0, 0), strtoul(a2, 0, 0));
		} else if (!strcmp(cmd, "poke32")) { core->busWrite32(core, strtoul(a1, 0, 0), strtoul(a2, 0, 0));
		} else if (!strcmp(cmd, "watch")) {
			ensureDbg();
			uint32_t a = strtoul(a1, 0, 0), n = strtoul(a2, 0, 0);
			enum mWatchpointType t = WATCHPOINT_WRITE;
			if (!strcmp(a3, "r")) t = WATCHPOINT_READ; else if (!strcmp(a3, "rw")) t = WATCHPOINT_RW;
			for (uint32_t i = 0; i < (n ? n : 1); ++i) {
				struct mWatchpoint wp = { .address = a + i, .segment = -1, .type = t };
				dbg.platform->setWatchpoint(dbg.platform, &wp);
			}
		} else if (!strcmp(cmd, "break")) {
			ensureDbg();
			struct mBreakpoint bp = { .address = strtoul(a1, 0, 0), .segment = -1, .type = BREAKPOINT_HARDWARE };
			dbg.platform->setBreakpoint(dbg.platform, &bp);
		} else if (!strcmp(cmd, "maxhits")) { maxhits = atoi(a1); hits = 0;
		} else if (!strcmp(cmd, "regs")) { printRegs();
		} else if (!strcmp(cmd, "echo")) { printf("%s", line + 5);
		}
	}
	fprintf(stderr, "ran %ld frames\n", frames);
	core->deinit(core);
	return 0;
}
