// Dead Wars: The Last Boat - ironman run save (LAST_BOAT.md 16, 17, 19.6).
// Two SRAM slots written in turn: a save always overwrites the OLDER slot, so a power cut in
// the middle of a write leaves the newer one intact. Each slot is a header + the RunState bytes;
// the valid slot (magic, version, size, CRC) with the highest sequence number wins.
// SRAM map: profile at 0, the classic suspend at 256 (GameState), slot A 0x2000, slot B 0x4000.
#include "lb.h"
#include "story_data.h"
#include <stddef.h>

extern u8 run_phase; // run.c: where a loaded run resumes; kept in the slot header

typedef struct {
	u32 magic;
	u16 version, size;  // RUN_VERSION, sizeof(RunState)
	u32 seq;            // higher = newer
	u8 phase, pad;      // run_phase
	u16 crc;            // CRC-16/CCITT over STORY_SIG, version..pad, then the RunState bytes
} RunHdr;

#define CLASSIC_SUSPEND 256 // save.c SUSPEND_OFFSET
#define SRAM_SIZE 0x8000
_Static_assert(sizeof(RunHdr) == 16, "run save header layout");
_Static_assert(sizeof(RunHdr) + sizeof(RunState) <= 0x2000, "a run slot is 8 KB");
_Static_assert(CLASSIC_SUSPEND + sizeof(GameState) <= RUN_SLOT_A, "the classic suspend runs into slot A");
_Static_assert(RUN_SLOT_A + 0x2000 <= RUN_SLOT_B && RUN_SLOT_B + 0x2000 <= SRAM_SIZE, "slots overlap");

static const u32 slot_base[2] = { RUN_SLOT_A, RUN_SLOT_B };

// CRC-16/CCITT (poly 0x1021, init 0xFFFF), a nibble at a time
static const u16 crc_tab[16] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
	0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
};

static u16 crc_byte(u16 c, u8 b)
{
	c = (u16)((c << 4) ^ crc_tab[(c >> 12) ^ (b >> 4)]);
	return (u16)((c << 4) ^ crc_tab[(c >> 12) ^ (b & 15)]);
}

// The storylet content signature is part of the CRC: story_flags, story_cool and vars are indexed
// by the TSV row order, so a save from other story content reads as invalid, like a version change.
static u16 crc_hdr(const RunHdr *h)
{
	const u8 *b = (const u8 *)h;
	u16 c = 0xFFFF;
	for (int i = 0; i < 32; i += 8)
		c = crc_byte(c, (u8)((u32)STORY_SIG >> i));
	for (u32 i = offsetof(RunHdr, version); i < offsetof(RunHdr, crc); i++)
		c = crc_byte(c, b[i]);
	return c;
}

static void sram_get(u32 off, void *dst, u32 n)
{
	u8 *d = dst;
	for (u32 i = 0; i < n; i++)
		d[i] = SRAM[off + i];
}

static void sram_put(u32 off, const void *src, u32 n)
{
	const u8 *s = src;
	for (u32 i = 0; i < n; i++)
		SRAM[off + i] = s[i];
}

// Is slot k complete and intact? The data is checked straight from SRAM, so `run` is untouched.
static bool slot_valid(int k, RunHdr *h)
{
	u32 base = slot_base[k];
	sram_get(base, h, sizeof *h);
	if (h->magic != RUN_MAGIC || h->version != RUN_VERSION || h->size != sizeof(RunState))
		return false;
	u16 c = crc_hdr(h);
	for (u32 i = 0; i < sizeof(RunState); i++)
		c = crc_byte(c, SRAM[base + sizeof *h + i]);
	return c == h->crc;
}

// The newest valid slot (0 = A, 1 = B) or -1; its header in *out.
static int newest(RunHdr *out)
{
	RunHdr h[2];
	bool ok[2] = { slot_valid(0, &h[0]), slot_valid(1, &h[1]) };
	int k = -1;
	if (ok[0] && ok[1])
		k = (s32)(h[1].seq - h[0].seq) > 0 ? 1 : 0;
	else if (ok[0] || ok[1])
		k = ok[0] ? 0 : 1;
	if (k >= 0 && out)
		*out = h[k];
	return k;
}

void run_save(void)
{
	RunHdr cur, h;
	int k = newest(&cur);
	int t = k == 0 ? 1 : 0; // overwrite the other (older or broken) slot; A when there is none
	u32 base = slot_base[t];
	run.magic = RUN_MAGIC;
	run.version = RUN_VERSION;
	run.size = sizeof(RunState);
	h.magic = RUN_MAGIC;
	h.version = RUN_VERSION;
	h.size = sizeof(RunState);
	h.seq = k >= 0 ? cur.seq + 1 : 1;
	h.phase = run_phase;
	h.pad = 0;
	u16 c = crc_hdr(&h);
	const u8 *b = (const u8 *)&run;
	for (u32 i = 0; i < sizeof(RunState); i++)
		c = crc_byte(c, b[i]);
	h.crc = c;
	// invalidate the slot first and write its magic last: a cut write never looks complete
	u32 zero = 0;
	sram_put(base, &zero, sizeof zero);
	sram_put(base + sizeof h, &run, sizeof(RunState));
	sram_put(base + sizeof h.magic, (const u8 *)&h + sizeof h.magic, sizeof h - sizeof h.magic);
	sram_put(base, &h.magic, sizeof h.magic);
}

bool run_load(void)
{
	RunHdr h;
	int k = newest(&h);
	if (k < 0)
		return false;
	sram_get(slot_base[k] + sizeof h, &run, sizeof(RunState));
	// the copy in RAM must still match (and carry its own layout stamp)
	u16 c = crc_hdr(&h);
	const u8 *b = (const u8 *)&run;
	for (u32 i = 0; i < sizeof(RunState); i++)
		c = crc_byte(c, b[i]);
	if (c != h.crc || run.magic != RUN_MAGIC || run.version != RUN_VERSION || run.size != sizeof(RunState))
		return false;
	run_phase = h.phase;
	return true;
}

bool run_exists(void)
{
	return newest(NULL) >= 0;
}

// Permadeath: both slots go, so an older slot can never bring a finished run back.
void run_erase(void)
{
	static const RunHdr zero;
	sram_put(RUN_SLOT_A, &zero, sizeof zero);
	sram_put(RUN_SLOT_B, &zero, sizeof zero);
}
