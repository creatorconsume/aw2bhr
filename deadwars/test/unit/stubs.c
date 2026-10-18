// Host-side stand-ins for the hardware, video, text, run.c and tac.c's feedback hooks, so the
// rules code (map.c, data.c, missions.c, lb_data.c, person.c, tac_rules.c) links on a PC.
// Nothing draws; the tac_fx_* hooks only count their calls (FxLog fx).
#include "game.h"
#include "video.h"
#include "lb.h"
#include "unit.h"

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
bool tac_mode;
RunState run;   // run.c on the GBA

void frame(void) { frame_count++; }
void wait_frames(int n) { frame_count += n; }
void input_poll(void) {}

FxLog fx;

void tac_fx_hit(int d, int dmg)
{
	fx.hits++;
	fx.last_d = d;
	fx.last_dmg = dmg;
}

void tac_fx_miss(int d)
{
	fx.misses++;
	fx.last_d = d;
}

void tac_fx_text(const char *l1, const char *l2)
{
	fx.texts++;
	fx.l1 = l1;
	fx.l2 = l2;
}

void tac_fx_noise(int x, int y, int radius)
{
	fx.noises++;
	fx.nx = x;
	fx.ny = y;
	fx.nr = radius;
}
