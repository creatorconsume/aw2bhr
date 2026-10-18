// Headless GBA runner for automated testing (uses libmgba).
// usage: gbarun ROM SCRIPT OUTPREFIX
// SCRIPT lines: "wait N" | "press KEYS N" (hold KEYS for N frames, then release 2 frames)
//               | "shot NAME" (writes OUTPREFIX-NAME.ppm) | "hold KEYS" | "release"
//               | "dump HEXADDR LEN NAME" (raw bus read -> OUTPREFIX-NAME.bin)
// KEYS: any of A B L R S(elect) T(start) U D < >   e.g. "press A 4"
#include <mgba/core/core.h>
#include <mgba/core/log.h>
// GBA keypad bit order
enum { GBA_KEY_A, GBA_KEY_B, GBA_KEY_SELECT, GBA_KEY_START, GBA_KEY_RIGHT, GBA_KEY_LEFT,
       GBA_KEY_UP, GBA_KEY_DOWN, GBA_KEY_R, GBA_KEY_L };
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void shot(const char* prefix, const char* name) {
	char path[512];
	snprintf(path, sizeof path, "%s-%s.ppm", prefix, name);
	FILE* f = fopen(path, "wb");
	fprintf(f, "P6\n%u %u\n255\n", W, H);
	for (unsigned i = 0; i < W * H; ++i) {
		uint32_t c = buf[i];  // XBGR8
		unsigned char px[3] = { c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF };
		fwrite(px, 1, 3, f);
	}
	fclose(f);
}

int main(int argc, char** argv) {
	if (argc < 4) { fprintf(stderr, "usage: gbarun ROM SCRIPT OUTPREFIX\n"); return 2; }
	mLogSetDefaultLogger(&logger);
	struct mCore* core = mCoreFind(argv[1]);
	if (!core || !core->init(core)) { fprintf(stderr, "no core\n"); return 1; }
	mCoreInitConfig(core, NULL);
	core->desiredVideoDimensions(core, &W, &H);
	buf = calloc(W * H, sizeof(color_t));
	core->setVideoBuffer(core, buf, W);
	if (!mCoreLoadFile(core, argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
	core->reset(core);
	FILE* s = fopen(argv[2], "r");
	char line[256];
	unsigned held = 0;
	long frames = 0;
	while (fgets(line, sizeof line, s)) {
		char cmd[32] = "", a1[64] = "";
		int n = 0;
		if (sscanf(line, "%31s %63s %d", cmd, a1, &n) < 1 || cmd[0] == '#') continue;
		if (!strcmp(cmd, "wait")) {
			n = atoi(a1);
			for (int i = 0; i < n; ++i, ++frames) { core->setKeys(core, held); core->runFrame(core); }
		} else if (!strcmp(cmd, "press")) {
			unsigned k = parseKeys(a1);
			if (n <= 0) n = 2;
			for (int i = 0; i < n; ++i, ++frames) { core->setKeys(core, held | k); core->runFrame(core); }
			for (int i = 0; i < 2; ++i, ++frames) { core->setKeys(core, held); core->runFrame(core); }
		} else if (!strcmp(cmd, "hold")) {
			held |= parseKeys(a1);
		} else if (!strcmp(cmd, "release")) {
			held = 0;
		} else if (!strcmp(cmd, "dump")) {
			char name[64] = "", path[512];
			unsigned addr = 0, len = 0;
			sscanf(line, "%*s %x %u %63s", &addr, &len, name);
			snprintf(path, sizeof path, "%s-%s.bin", argv[3], name);
			FILE* f = fopen(path, "wb");
			for (unsigned i = 0; i < len; ++i)
				fputc(core->busRead8(core, addr + i), f);
			fclose(f);
		} else if (!strcmp(cmd, "shot")) {
			shot(argv[3], a1);
		}
	}
	fprintf(stderr, "ran %ld frames\n", frames);
	core->deinit(core);
	return 0;
}
