// Host-side stand-ins for video.c, text.c, save.c and the generated art, so the
// real rules code (map.c, battle.c, ai.c, data.c, missions.c) links on a PC.
// Nothing here draws anything; frame() only counts, so sim.c can catch stalls.
#include "game.h"
#include "video.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

u16 host_vram[0x18000 / 2], host_pal[0x400 / 2], host_oam[0x400 / 2], host_io[0x400 / 2];
u8 host_sram[0x10000];

// art: map.c reads these when it redraws tiles into (host) VRAM
const uint16_t terrain_meta[MT_COUNT][4];
const uint16_t overlay_meta[OV_COUNT][4];

SpriteState spr;
s16 cam_px, cam_py;
u16 keys_down, keys_held, keys_rep;
u32 frame_count;
Profile profile;

// watchdog: sim.c resets this every turn and checks it
u32 sim_frames;
int sim_verbose;

void frame(void) { frame_count++; sim_frames++; }
void wait_frames(int n) { frame_count += n; sim_frames += n; }
void input_poll(void) {}
void video_init(void) {}
void video_load_game_gfx(void) {}
void video_show_title(void) {}
void palettes_apply(void) {}
void sprites_draw(int hide_index) {}

void text_clear(void) {}
void text_put(int x, int y, const char *s, int pal) {}
void text_putn(int x, int y, s32 n, int pal) {}
void text_window(int x, int y, int w, int h) {}
void text_clear_rect(int x, int y, int w, int h) {}
void text_printf(int x, int y, int pal, const char *fmt, ...) {}
int text_menu(int x, int y, int w, const char *const *items, const u8 *enabled, int count, int start) { return -1; }
void dialogue(const Line *lines) {}
void banner(const char *l1, const char *l2, int frames)
{
	if (sim_verbose >= 2)
		printf("    [banner] %s%s%s\n", l1, l2 ? " / " : "", l2 ? l2 : "");
}

void profile_load(void) { memset(&profile, 0, sizeof profile); profile.unlocked = 1; }
void profile_save(void) {}
bool suspend_load(void) { return false; }
void suspend_save(void) {}
void suspend_clear(void) {}
