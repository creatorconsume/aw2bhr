// lrun - headless libretro frontend for testing GBA cores the way Onion runs them.
// usage: lrun --core CORE.so --rom ROM [--bios-dir DIR] [--save-dir DIR] [--opts FILE]
//             [--script FILE] [--out DIR] [--autostate FILE] [--map FILE] [--hang-secs N] [-v]
// SCRIPT lines (same as deadwars/test/gbarun.c, plus extras):
//   wait N | press KEYS [N] | hold KEYS | release | shot NAME | dump ADDR LEN NAME
//   savestate FILE | loadstate FILE | sram FILE | reset
//   peek ADDR LEN | poke ADDR B0 [B1 ..] | expect ADDR B0 [B1 ..] | count ADDR
//   until ADDR BYTE [MAXFRAMES]   (run until the RAM byte equals BYTE; counts as a FAIL on timeout)
// KEYS: any of A B L R S(elect) T(start) U D < >   e.g. "press A 4"
// FILE and NAME paths are relative to --out unless absolute. ADDR is hex, or SYMBOL[+OFFSET]
// when --map points at the game's GNU ld map file. Bytes are hex.
// "expect" checks RAM bytes and prints PASS/FAIL. "count" reads a u32 frame counter and prints how
// many game frames ran since the last count (fewer than emulated frames = the game lags).
// Exit status: 0 ok, 1 setup/core error, 4 an expect failed, 3 suspect: a uniform (black/white)
// screenshot, no screen change for --hang-secs after an input, or the script's last input never
// changed the screen.
#define _POSIX_C_SOURCE 200809L
#include "libretro.h"
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

// ---- core symbols ----
static struct {
	void (*init)(void);
	void (*deinit)(void);
	void (*get_system_info)(struct retro_system_info*);
	void (*get_system_av_info)(struct retro_system_av_info*);
	void (*set_environment)(retro_environment_t);
	void (*set_video_refresh)(retro_video_refresh_t);
	void (*set_audio_sample)(retro_audio_sample_t);
	void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
	void (*set_input_poll)(retro_input_poll_t);
	void (*set_input_state)(retro_input_state_t);
	void (*set_controller_port_device)(unsigned, unsigned);
	void (*reset)(void);
	void (*run)(void);
	size_t (*serialize_size)(void);
	bool (*serialize)(void*, size_t);
	bool (*unserialize)(const void*, size_t);
	bool (*load_game)(const struct retro_game_info*);
	void (*unload_game)(void);
	void* (*get_memory_data)(unsigned);
	size_t (*get_memory_size)(unsigned);
} core;

static const char *bios_dir = ".", *save_dir, *out_dir = ".", *map_path;
static int verbose;

// ---- core options: "key = "value"" file (RetroArch .opt format) ----
#define MAX_OPTS 128
static struct opt { char key[64], val[128], def[128]; int in_file, asked; } opts[MAX_OPTS];
static int nopts;

static struct opt* opt_get(const char* key, int create) {
	for (int i = 0; i < nopts; ++i)
		if (!strcmp(opts[i].key, key)) return &opts[i];
	if (!create || nopts == MAX_OPTS) return NULL;
	struct opt* o = &opts[nopts++];
	snprintf(o->key, sizeof o->key, "%s", key);
	return o;
}

static void load_opts(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) { fprintf(stderr, "lrun: cannot open opts %s\n", path); exit(1); }
	char line[256], key[64], val[128];
	while (fgets(line, sizeof line, f))
		if (sscanf(line, " %63[^ =] = \"%127[^\"]\"", key, val) == 2) {
			struct opt* o = opt_get(key, 1);
			snprintf(o->val, sizeof o->val, "%s", val);
			o->in_file = 1;
		}
	fclose(f);
}

// legacy SET_VARIABLES: "Description; default|other|..." -> remember the default
static void set_variables(const struct retro_variable* v) {
	for (; v->key; ++v) {
		struct opt* o = opt_get(v->key, 1);
		const char* p = strchr(v->value, ';');
		if (!o || !p) continue;
		for (++p; *p == ' '; ++p) {}
		size_t n = strcspn(p, "|");
		snprintf(o->def, sizeof o->def, "%.*s", (int) n, p);
	}
}

// ---- input ----
static unsigned keys; // libretro joypad bitmask

static unsigned parse_keys(const char* s) {
	unsigned k = 0;
	for (; *s; ++s) switch (*s) {
		case 'A': k |= 1 << RETRO_DEVICE_ID_JOYPAD_A; break;
		case 'B': k |= 1 << RETRO_DEVICE_ID_JOYPAD_B; break;
		case 'S': k |= 1 << RETRO_DEVICE_ID_JOYPAD_SELECT; break;
		case 'T': k |= 1 << RETRO_DEVICE_ID_JOYPAD_START; break;
		case 'U': k |= 1 << RETRO_DEVICE_ID_JOYPAD_UP; break;
		case 'D': k |= 1 << RETRO_DEVICE_ID_JOYPAD_DOWN; break;
		case '<': k |= 1 << RETRO_DEVICE_ID_JOYPAD_LEFT; break;
		case '>': k |= 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT; break;
		case 'L': k |= 1 << RETRO_DEVICE_ID_JOYPAD_L; break;
		case 'R': k |= 1 << RETRO_DEVICE_ID_JOYPAD_R; break;
	}
	return k;
}

static void input_poll(void) {}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
	(void) index;
	if (port || device != RETRO_DEVICE_JOYPAD) return 0;
	if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t) keys;
	return id < 16 ? (keys >> id) & 1 : 0;
}

// ---- video: keep the last frame as RGB888 ----
static enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static unsigned W, H;
static uint8_t rgb[256 * 256 * 3];
static uint32_t frame_hash;
static long frame_no, last_change, last_input = -1, dupes;

static void video_refresh(const void* data, unsigned w, unsigned h, size_t pitch) {
	if (!data) { ++dupes; return; }
	if (w > 256 || h > 256) return;
	W = w, H = h;
	uint32_t hash = 2166136261u;
	for (unsigned y = 0; y < h; ++y) {
		const uint8_t* row = (const uint8_t*) data + y * pitch;
		for (unsigned x = 0; x < w; ++x) {
			uint8_t* p = &rgb[(y * w + x) * 3];
			if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
				uint32_t c = ((const uint32_t*) row)[x];
				p[0] = c >> 16, p[1] = c >> 8, p[2] = c;
			} else {
				unsigned c = ((const uint16_t*) row)[x];
				if (pixfmt == RETRO_PIXEL_FORMAT_RGB565)
					p[0] = (c >> 11) << 3, p[1] = ((c >> 5) & 63) << 2, p[2] = (c & 31) << 3;
				else
					p[0] = ((c >> 10) & 31) << 3, p[1] = ((c >> 5) & 31) << 3, p[2] = (c & 31) << 3;
			}
			hash = (hash ^ p[0] ^ (p[1] << 8) ^ (p[2] << 16)) * 16777619u;
		}
	}
	if (hash != frame_hash) frame_hash = hash, last_change = frame_no;
}

// ---- audio: just count ----
static long audio_frames;
static void audio_sample(int16_t l, int16_t r) { (void) l, (void) r; ++audio_frames; }
static size_t audio_batch(const int16_t* d, size_t n) { (void) d; audio_frames += n; return n; }

// ---- environment ----
static const struct retro_memory_map* memmap;
static struct retro_memory_descriptor memdesc[64];
static unsigned nmemdesc;

static void core_log(enum retro_log_level level, const char* fmt, ...) {
	if (level < RETRO_LOG_WARN && !verbose) return;
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "[core] ");
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static bool environment(unsigned cmd, void* data) {
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char**) data = bios_dir; return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char**) data = save_dir; return true;
	case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY: *(const char**) data = save_dir; return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: pixfmt = *(enum retro_pixel_format*) data; return true;
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned*) data = 0; return true;
	case RETRO_ENVIRONMENT_SET_VARIABLES: set_variables(data); return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable* v = data;
		struct opt* o = opt_get(v->key, 1);
		v->value = NULL;
		if (!o) return false;
		o->asked = 1;
		v->value = o->in_file ? o->val : o->def[0] ? o->def : NULL;
		return v->value != NULL;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool*) data = false; return true;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback*) data)->log = core_log;
		return true;
	case RETRO_ENVIRONMENT_SET_MESSAGE:
		fprintf(stderr, "[osd] %s\n", ((const struct retro_message*) data)->msg);
		return true;
	case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
		fprintf(stderr, "[osd] %s\n", ((const struct retro_message_ext*) data)->msg);
		return true;
	case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool*) data = true; return true;
	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
	case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: *(int*) data = 3; return true;
	case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned*) data = RETRO_LANGUAGE_ENGLISH; return true;
	case RETRO_ENVIRONMENT_GET_FASTFORWARDING: *(bool*) data = false; return true;
	case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
		memmap = data;
		nmemdesc = memmap->num_descriptors < 64 ? memmap->num_descriptors : 64;
		memcpy(memdesc, memmap->descriptors, nmemdesc * sizeof *memdesc);
		return true;
	}
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
	case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
	case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
	case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
	case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
	case RETRO_ENVIRONMENT_SET_GEOMETRY:
	case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
		return true;
	default:
		if (verbose) fprintf(stderr, "lrun: unhandled env %u\n", cmd);
		return false;
	}
}

// ---- helpers ----
static void* load_file(const char* path, size_t* size) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	void* buf = malloc(n > 0 ? n : 1);
	*size = fread(buf, 1, n, f);
	fclose(f);
	return buf;
}

static int save_file(const char* path, const void* data, size_t size) {
	FILE* f = fopen(path, "wb");
	if (!f) return 0;
	int ok = fwrite(data, 1, size, f) == size;
	return fclose(f) == 0 && ok;
}

static uint8_t* gba_addr(uint32_t addr) {
	for (unsigned i = 0; i < nmemdesc; ++i) {
		const struct retro_memory_descriptor* d = &memdesc[i];
		if (d->ptr && addr >= d->start && addr - d->start < d->len)
			return (uint8_t*) d->ptr + d->offset + (addr - d->start);
	}
	size_t n = core.get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
	uint8_t* ram = core.get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
	if (ram && addr >= 0x02000000 && addr - 0x02000000 < n) return ram + (addr - 0x02000000);
	return NULL;
}

// "0x3001234" / "3001234" or a symbol from the ld map file ("                0x030012d8                frame_count")
static uint32_t parse_addr(const char* arg) {
	char s[256], *end, *plus;
	snprintf(s, sizeof s, "%s", arg);
	uint32_t off = 0, a = strtoul(s, &end, 16);
	if (!*end) return a;
	if ((plus = strchr(s, '+'))) *plus = 0, off = strtoul(plus + 1, NULL, 0); // SYMBOL+OFFSET
	FILE* f = map_path ? fopen(map_path, "r") : NULL;
	char line[512], name[256];
	unsigned long v;
	a = 0;
	while (f && fgets(line, sizeof line, f))
		if (sscanf(line, " 0x%lx %255s", &v, name) == 2 && !strcmp(name, s) && strspn(line, " ") > 8) {
			a = v;
			break;
		}
	if (f) fclose(f);
	if (!a) fprintf(stderr, "lrun: unknown symbol %s (pass --map)\n", s);
	return a ? a + off : 0;
}

static void mkdirs(const char* dir) {
	char tmp[1024];
	snprintf(tmp, sizeof tmp, "%s", dir);
	for (char* p = tmp + 1; *p; ++p)
		if (*p == '/') *p = 0, mkdir(tmp, 0777), *p = '/';
	mkdir(tmp, 0777);
}

// script file arguments live in the output directory unless absolute
static const char* out_path(const char* name, const char* ext) {
	static char path[1024];
	snprintf(path, sizeof path, "%s%s%s%s", name[0] == '/' ? "" : out_dir, name[0] == '/' ? "" : "/", name, ext);
	return path;
}

// minimal PNG: stored (uncompressed) deflate blocks
static uint32_t crc_table[256];
static uint32_t crc(uint32_t c, const uint8_t* p, size_t n) {
	if (!crc_table[1])
		for (uint32_t i = 0; i < 256; ++i) {
			uint32_t k = i;
			for (int j = 0; j < 8; ++j) k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1;
			crc_table[i] = k;
		}
	c = ~c;
	while (n--) c = crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
	return ~c;
}

static void put32(uint8_t* p, uint32_t v) { p[0] = v >> 24, p[1] = v >> 16, p[2] = v >> 8, p[3] = v; }

static void png_chunk(FILE* f, const char* type, const uint8_t* data, size_t n) {
	uint8_t hdr[8];
	put32(hdr, n);
	memcpy(hdr + 4, type, 4);
	fwrite(hdr, 1, 8, f);
	fwrite(data, 1, n, f);
	uint32_t c = crc(crc(0, hdr + 4, 4), data, n);
	put32(hdr, c);
	fwrite(hdr, 1, 4, f);
}

static void write_png(const char* path) {
	size_t raw_n = H * (W * 3 + 1), nblk = raw_n / 65535 + 1;
	uint8_t* raw = malloc(raw_n);
	uint8_t* z = malloc(2 + raw_n + nblk * 5 + 4);
	for (unsigned y = 0; y < H; ++y) {
		raw[y * (W * 3 + 1)] = 0;
		memcpy(raw + y * (W * 3 + 1) + 1, rgb + y * W * 3, W * 3);
	}
	size_t zn = 0, pos = 0;
	uint32_t a = 1, b = 0;
	z[zn++] = 0x78, z[zn++] = 0x01;
	do {
		size_t len = raw_n - pos > 65535 ? 65535 : raw_n - pos;
		z[zn++] = pos + len == raw_n;
		z[zn++] = len, z[zn++] = len >> 8, z[zn++] = ~len, z[zn++] = ~len >> 8;
		memcpy(z + zn, raw + pos, len);
		zn += len, pos += len;
	} while (pos < raw_n);
	for (size_t i = 0; i < raw_n; ++i) a = (a + raw[i]) % 65521, b = (b + a) % 65521;
	put32(z + zn, b << 16 | a), zn += 4;
	uint8_t ihdr[13] = { 0 };
	put32(ihdr, W), put32(ihdr + 4, H), ihdr[8] = 8, ihdr[9] = 2;
	FILE* f = fopen(path, "wb");
	if (!f) fprintf(stderr, "lrun: cannot write %s\n", path);
	else {
		fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
		png_chunk(f, "IHDR", ihdr, 13);
		png_chunk(f, "IDAT", z, zn);
		png_chunk(f, "IEND", NULL, 0);
		fclose(f);
	}
	free(raw), free(z);
}

// classify the current frame: uniform colour screens are the "black/white screen" symptom
static int uniform_count;
static void shot(const char* name) {
	write_png(out_path(name, ".png"));
	unsigned n = W * H, same = 0;
	for (unsigned i = 0; i < n; ++i) same += !memcmp(rgb + i * 3, rgb, 3);
	const char* kind = "ok";
	if (n && same == n) {
		++uniform_count;
		kind = rgb[0] + rgb[1] + rgb[2] < 48 ? "BLACK" : rgb[0] + rgb[1] + rgb[2] > 700 ? "WHITE" : "UNIFORM";
	}
	printf("shot %-14s frame %6ld  %s  (unchanged for %ld frames)\n", name, frame_no, kind,
	       frame_no - last_change);
}

// ---- running ----
static double host_secs, worst_ms;
static long hang_frames = 600, hang_reported, hangs;
static long count_frame = -1, fails;
static uint32_t count_value;

static void run_frames(long n) {
	for (long i = 0; i < n; ++i) {
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		core.run();
		clock_gettime(CLOCK_MONOTONIC, &t1);
		double s = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
		host_secs += s;
		if (s * 1000 > worst_ms) worst_ms = s * 1000;
		++frame_no;
		// input given, then the screen never changed again: likely hung
		if (last_input >= last_change && frame_no - last_input >= hang_frames && hang_reported != last_input) {
			hang_reported = last_input, ++hangs;
			printf("HANG? no screen change for %ld frames after input at frame %ld\n",
			       frame_no - last_input, last_input);
		}
	}
}

static void press(unsigned k, long n) {
	unsigned held = keys;
	keys = held | k;
	last_input = frame_no;
	run_frames(n);
	keys = held;
	run_frames(2);
}

static int state_save(const char* path) {
	size_t n = core.serialize_size();
	void* buf = malloc(n);
	int ok = n && core.serialize(buf, n) && save_file(path, buf, n);
	printf("savestate %s: %s (%zu bytes)\n", path, ok ? "ok" : "FAILED", n);
	free(buf);
	return ok;
}

static int state_load(const char* path) {
	size_t n = 0;
	void* buf = load_file(path, &n);
	int ok = buf && core.unserialize(buf, n);
	printf("loadstate %s: %s (%zu bytes, core expects %zu)\n", path, ok ? "ok" : "FAILED", n,
	       core.serialize_size());
	free(buf);
	return ok;
}

static void sram_write(const char* path) {
	size_t n = core.get_memory_size(RETRO_MEMORY_SAVE_RAM);
	const uint8_t* p = core.get_memory_data(RETRO_MEMORY_SAVE_RAM);
	if (!p || !n) { printf("sram: core exposes no save RAM\n"); return; }
	size_t used = 0;
	for (size_t i = 0; i < n; ++i) used += p[i] != 0xFF && p[i] != 0x00;
	printf("sram %s: %zu bytes, %zu non-blank, %s\n", path, n, used,
	       save_file(path, p, n) ? "written" : "WRITE FAILED");
}

static void run_script(const char* path) {
	FILE* s = fopen(path, "r");
	if (!s) { fprintf(stderr, "lrun: cannot open script %s\n", path); exit(1); }
	char line[512];
	while (fgets(line, sizeof line, s)) {
		char cmd[32] = "", a1[256] = "";
		int n = 0;
		if (sscanf(line, "%31s %255s %d", cmd, a1, &n) < 1 || cmd[0] == '#') continue;
		if (!strcmp(cmd, "wait")) run_frames(atol(a1));
		else if (!strcmp(cmd, "press")) press(parse_keys(a1), n > 0 ? n : 2);
		else if (!strcmp(cmd, "hold")) keys |= parse_keys(a1), last_input = frame_no;
		else if (!strcmp(cmd, "release")) keys = 0;
		else if (!strcmp(cmd, "shot")) shot(a1);
		else if (!strcmp(cmd, "savestate")) state_save(out_path(a1, ""));
		else if (!strcmp(cmd, "loadstate")) state_load(out_path(a1, ""));
		else if (!strcmp(cmd, "sram")) sram_write(out_path(a1, ""));
		else if (!strcmp(cmd, "reset")) core.reset(), printf("reset at frame %ld\n", frame_no);
		else if (!strcmp(cmd, "poke") || !strcmp(cmd, "expect")) {
			// poke writes the bytes (e.g. to force a game state), expect compares them
			uint32_t addr = parse_addr(a1);
			char* p = line;
			int bad = 0;
			for (int k = 0; k < 2; ++k) p += strspn(p, " \t"), p += strcspn(p, " \t\n");
			for (char* end;; p = end) {
				unsigned long v = strtoul(p, &end, 16);
				if (end == p) break;
				uint8_t* m = gba_addr(addr++);
				if (!m) printf("%s %08x: not mapped\n", cmd, addr - 1), bad = 1;
				else if (cmd[0] == 'p') *m = v;
				else if (*m != v) printf("expect %s: byte %08x is %02x, want %02lx\n", a1, addr - 1, *m, v), bad = 1;
			}
			if (cmd[0] == 'e') printf("expect %s: %s (frame %ld)\n", a1, bad ? "FAIL" : "PASS", frame_no), fails += bad;
		} else if (!strcmp(cmd, "until")) {
			// wait for a game state instead of a fixed frame count, so scripts survive timing changes
			unsigned want = 0;
			long max = 3600, start = frame_no;
			sscanf(line, "%*s %*s %x %ld", &want, &max);
			const uint8_t* m = gba_addr(parse_addr(a1));
			while (m && *m != want && frame_no - start < max) run_frames(1);
			if (!m || *m != want) printf("until %s == %02x: FAIL after %ld frames\n", a1, want, frame_no - start), ++fails;
			else if (verbose) printf("until %s == %02x: %ld frames\n", a1, want, frame_no - start);
		} else if (!strcmp(cmd, "count")) {
			uint32_t addr = parse_addr(a1), v = 0;
			for (int i = 0; i < 4; ++i) {
				const uint8_t* p = gba_addr(addr + i);
				v |= (uint32_t) (p ? *p : 0) << (8 * i);
			}
			if (count_frame >= 0 && frame_no > count_frame)
				printf("count %s: game ran %u frames in %ld emulated frames (%.1f%%)\n", a1, v - count_value,
				       frame_no - count_frame, 100.0 * (v - count_value) / (frame_no - count_frame));
			count_value = v, count_frame = frame_no;
		} else if (!strcmp(cmd, "dump") || !strcmp(cmd, "peek")) {
			char name[64] = "";
			unsigned len = 0;
			sscanf(line, "%*s %*s %u %63s", &len, name);
			uint32_t addr = parse_addr(a1);
			FILE* f = NULL;
			if (cmd[0] == 'd' && !(f = fopen(out_path(name, ".bin"), "wb"))) {
				fprintf(stderr, "lrun: cannot write dump %s\n", name);
				continue;
			}
			if (!f) printf("peek %08x:", addr);
			for (unsigned i = 0; i < len; ++i) {
				const uint8_t* p = gba_addr(addr + i);
				if (f) fputc(p ? *p : 0, f);
				else if (p) printf(" %02x", *p);
				else printf(" ??");
			}
			if (f) fclose(f);
			else printf("\n");
		} else fprintf(stderr, "lrun: unknown script command '%s'\n", cmd);
	}
	fclose(s);
}

#define SYM(name) \
	if (!(*(void**) &core.name = dlsym(lib, "retro_" #name))) { fprintf(stderr, "lrun: missing retro_" #name "\n"); return 1; }

int main(int argc, char** argv) {
	const char *core_path = NULL, *rom = NULL, *opts_path = NULL, *script = NULL, *autostate = NULL;
	double hang_secs = 10;
	for (int i = 1; i < argc; ++i) {
		const char* a = argv[i];
		const char* v = i + 1 < argc ? argv[i + 1] : NULL;
		if (!strcmp(a, "-v")) { verbose = 1; continue; }
		if (!v) { fprintf(stderr, "lrun: %s needs a value\n", a); return 1; }
		++i;
		if (!strcmp(a, "--core")) core_path = v;
		else if (!strcmp(a, "--rom")) rom = v;
		else if (!strcmp(a, "--bios-dir")) bios_dir = v;
		else if (!strcmp(a, "--save-dir")) save_dir = v;
		else if (!strcmp(a, "--opts")) opts_path = v;
		else if (!strcmp(a, "--script")) script = v;
		else if (!strcmp(a, "--out")) out_dir = v;
		else if (!strcmp(a, "--autostate")) autostate = v;
		else if (!strcmp(a, "--map")) map_path = v;
		else if (!strcmp(a, "--hang-secs")) hang_secs = atof(v);
		else { fprintf(stderr, "lrun: unknown option %s\n", a); return 1; }
	}
	if (!core_path || !rom) {
		fprintf(stderr, "usage: lrun --core CORE.so --rom ROM [--bios-dir DIR] [--save-dir DIR] [--opts FILE]\n"
		                "            [--script FILE] [--out DIR] [--autostate FILE] [--map FILE] [--hang-secs N] [-v]\n");
		return 1;
	}
	mkdirs(out_dir);
	if (!save_dir) save_dir = out_dir;
	mkdirs(save_dir);
	if (opts_path) load_opts(opts_path);

	void* lib = dlopen(core_path, RTLD_NOW | RTLD_LOCAL);
	if (!lib) { fprintf(stderr, "lrun: %s\n", dlerror()); return 1; }
	SYM(init) SYM(deinit) SYM(get_system_info) SYM(get_system_av_info) SYM(set_environment)
	SYM(set_video_refresh) SYM(set_audio_sample) SYM(set_audio_sample_batch) SYM(set_input_poll)
	SYM(set_input_state) SYM(set_controller_port_device) SYM(reset) SYM(run) SYM(serialize_size)
	SYM(serialize) SYM(unserialize) SYM(load_game) SYM(unload_game) SYM(get_memory_data)
	SYM(get_memory_size)

	core.set_environment(environment);
	core.init();
	core.set_video_refresh(video_refresh);
	core.set_audio_sample(audio_sample);
	core.set_audio_sample_batch(audio_batch);
	core.set_input_poll(input_poll);
	core.set_input_state(input_state);

	struct retro_system_info si = { 0 };
	core.get_system_info(&si);
	printf("core: %s %s (need_fullpath=%d)\n", si.library_name, si.library_version, si.need_fullpath);

	struct retro_game_info gi = { .path = rom };
	void* romdata = NULL;
	size_t romsize = 0;
	if (!si.need_fullpath) {
		if (!(romdata = load_file(rom, &romsize))) { fprintf(stderr, "lrun: cannot read %s\n", rom); return 1; }
		gi.data = romdata, gi.size = romsize;
	}
	if (!core.load_game(&gi)) { printf("ERROR: core refused to load %s\n", rom); return 1; }
	core.set_controller_port_device(0, RETRO_DEVICE_JOYPAD);

	struct retro_system_av_info av = { 0 };
	core.get_system_av_info(&av);
	double fps = av.timing.fps > 0 ? av.timing.fps : 59.7275;
	hang_frames = (long) (hang_secs * fps);

	// battery save: <save dir>/<rom name>.srm, loaded after load_game like RetroArch does
	char srm[1024];
	const char* base = strrchr(rom, '/') ? strrchr(rom, '/') + 1 : rom;
	const char* dot = strrchr(base, '.');
	snprintf(srm, sizeof srm, "%s/%.*s.srm", save_dir, (int) (dot ? dot - base : (long) strlen(base)), base);
	size_t sram_n = core.get_memory_size(RETRO_MEMORY_SAVE_RAM), file_n = 0;
	void* sram_file = load_file(srm, &file_n);
	if (sram_file && sram_n) {
		memcpy(core.get_memory_data(RETRO_MEMORY_SAVE_RAM), sram_file, file_n < sram_n ? file_n : sram_n);
		printf("sram: loaded %s (%zu bytes into %zu)\n", srm, file_n, sram_n);
	}
	free(sram_file);
	if (autostate && !state_load(autostate)) printf("autostate: core rejected the state, running from boot\n");

	if (script) run_script(script);
	else run_frames((long) (10 * fps));
	shot("last");
	// the last input never changed the screen: the game is frozen or ignores input
	if (last_input >= 0 && last_change <= last_input && frame_no - last_input >= 60 && hang_reported != last_input)
		++hangs, printf("NO-RESPONSE: screen unchanged since frame %ld, last input at frame %ld\n", last_change,
		                last_input);

	sram_write(srm);
	for (int i = 0; i < nopts; ++i) {
		struct opt* o = &opts[i];
		if (o->in_file && !o->asked) printf("option %s: in opts file but never read by core\n", o->key);
		else if (o->asked && !o->in_file) printf("option %s: not in opts file, core default \"%s\"\n", o->key, o->def);
		else if (verbose && o->asked) printf("option %s = \"%s\"\n", o->key, o->val);
	}
	double emu_secs = frame_no / fps;
	printf("timing: %ld frames (%.1f emulated s) in %.2f host s: %.0fx realtime, %.3f ms/frame avg, %.2f max\n",
	       frame_no, emu_secs, host_secs, host_secs > 0 ? emu_secs / host_secs : 0,
	       frame_no ? host_secs * 1000 / frame_no : 0, worst_ms);
	printf("av: %.4f fps, %.0f Hz audio, %.1f audio frames/video frame (expect %.1f), %ld duped frames\n",
	       fps, av.timing.sample_rate, frame_no ? (double) audio_frames / frame_no : 0,
	       av.timing.sample_rate / fps, dupes);

	core.unload_game();
	core.deinit();
	dlclose(lib);
	free(romdata);
	if (fails) printf("RESULT: FAIL (%ld expect failures)\n", fails);
	else if (hangs || uniform_count) printf("RESULT: SUSPECT (%ld hang reports, %d uniform screenshots)\n", hangs, uniform_count);
	else printf("RESULT: OK\n");
	return fails ? 4 : hangs || uniform_count ? 3 : 0;
}
