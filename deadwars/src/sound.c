// Dead Wars - PSG audio: a 4-track sequencer for music (audio/music.txt, compiled by
// tools/music.py) and an SFX layer that borrows a channel and hands it back to the music.
// Everything runs from sound_update() once per frame: a few register writes, no mixing.
#include "game.h"
#include "video.h"
#include "sound.h"
#include <string.h>

#define REG_SND1SWEEP REG16(0x04000060)
#define REG_SND1CNT   REG16(0x04000062)
#define REG_SND1FREQ  REG16(0x04000064)
#define REG_SND2CNT   REG16(0x04000068)
#define REG_SND2FREQ  REG16(0x0400006C)
#define REG_SND3SEL   REG16(0x04000070)
#define REG_SND3CNT   REG16(0x04000072)
#define REG_SND3FREQ  REG16(0x04000074)
#define REG_SND4CNT   REG16(0x04000078)
#define REG_SND4FREQ  REG16(0x0400007C)
#define REG_SNDDMGCNT REG16(0x04000080)
#define REG_SNDDSCNT  REG16(0x04000082)
#define WAVE_RAM      ((volatile u16 *)0x04000090)
#define TRIGGER 0x8000

#define SND_MAGIC   0xA5
#define SND_NOMUSIC 0x01
#define SND_NOSFX   0x02

enum { CH_SQ1, CH_SQ2, CH_WAVE, CH_NOISE, CH_COUNT };

typedef struct {
	const u8 *pos, *loop;  // event stream, loop point (NULL = play once)
	u8 wait;               // frames left on the current event
	u8 inst, vol;          // instrument, volume override (0xFF = none)
	u8 on, gate, vib;      // note sounding, frames until staccato cut, vibrato phase
	u16 freq;
} Track;

typedef struct {
	u8 frames, duty, env, sweep; // frames = 0 ends the voice
	u16 freq;                    // square: frequency register, noise: NR43
	s16 slide;                   // added to freq every frame after the first
} SfxStep;

typedef struct {
	const SfxStep *step;
	u8 left, prio;
	u16 freq;
} Voice;

static Track trk[CH_COUNT], saved[CH_COUNT];
static Voice vox[CH_COUNT];
static s8 cur_song = -1, saved_song = -1, forced = MUS_AUTO, wave_loaded = -1;
static u8 last_day, last_night, last_visible;
static u8 in_dialogue, battle_live, jingle_started, boss_seen;

// ---------------------------------------------------------------- hardware

static void hw_play(int ch, int duty, int env, u16 freq)
{
	switch (ch) {
	case CH_SQ1: REG_SND1CNT = duty | env << 8; REG_SND1FREQ = freq | TRIGGER; break;
	case CH_SQ2: REG_SND2CNT = duty | env << 8; REG_SND2FREQ = freq | TRIGGER; break;
	case CH_WAVE: REG_SND3CNT = env << 8; REG_SND3FREQ = freq | TRIGGER; break;
	case CH_NOISE: REG_SND4CNT = env << 8; REG_SND4FREQ = freq | TRIGGER; break;
	}
}

static void hw_freq(int ch, u16 freq)  // retune without restarting the envelope
{
	switch (ch) {
	case CH_SQ1: REG_SND1FREQ = freq; break;
	case CH_SQ2: REG_SND2FREQ = freq; break;
	case CH_WAVE: REG_SND3FREQ = freq; break;
	case CH_NOISE: REG_SND4FREQ = freq; break;
	}
}

static void hw_off(int ch)
{
	if (ch == CH_WAVE)
		REG_SND3CNT = 0;
	else
		hw_play(ch, 0, 0, ch == CH_NOISE ? 0 : 0x7FF);
	if (ch == CH_SQ1)
		REG_SND1SWEEP = 0;
}

static void load_wave(int w)
{
	if (w == wave_loaded)
		return;
	REG_SND3SEL = 0x40; // stop; bank 1 plays so the CPU sees bank 0
	const u16 *src = (const u16 *)mus_waves[w];
	for (int i = 0; i < 8; i++)
		WAVE_RAM[i] = src[i];
	REG_SND3SEL = 0x80; // play bank 0
	wave_loaded = w;
}

// ---------------------------------------------------------------- music

static void track_sound(int ch)
{
	Track *t = &trk[ch];
	const MusInst *in = &mus_inst[t->inst];
	int env = in->env;
	if (t->vol != 0xFF && in->kind != 1)
		env = (env & 0x0F) | t->vol << 4;
	if (in->kind == 1)
		load_wave(in->param);
	if (ch == CH_SQ1)
		REG_SND1SWEEP = in->sweep;
	hw_play(ch, in->kind == 0 ? in->param : 0, env, t->freq);
}

static void track_tick(int ch)
{
	Track *t = &trk[ch];
	bool own = !vox[ch].step; // an SFX holds the channel: keep time, stay quiet
	if (!t->pos)
		return;
	for (int guard = 0; !t->wait; guard++) {
		u8 e = *t->pos++;
		if (guard > 64) { // a loop with no length: give up rather than hang
			t->pos = NULL;
			return;
		}
		if (e < MEV_REST) {
			t->wait = *t->pos++;
			if (ch == CH_NOISE)
				t->inst = e;
			const MusInst *in = &mus_inst[t->inst];
			t->freq = ch == CH_NOISE ? in->param : mus_freq[ch == CH_WAVE ? e + 12 : e];
			t->on = 1;
			t->gate = in->gate;
			t->vib = 0;
			if (own)
				track_sound(ch);
			continue;
		}
		switch (e) {
		case MEV_REST:
			t->wait = *t->pos++;
			t->on = 0;
			if (own)
				hw_off(ch);
			break;
		case MEV_TIE: t->wait = *t->pos++; break;
		case MEV_INST: t->inst = *t->pos++; break;
		case MEV_VOL: t->vol = *t->pos++; break;
		case MEV_LOOP: t->loop = t->pos; break;
		default:
			if (t->loop) {
				t->pos = t->loop;
				break;
			}
			t->pos = NULL;
			t->on = 0;
			if (own)
				hw_off(ch);
			return;
		}
	}
	t->wait--;
	if (!t->on || !own)
		return;
	if (t->gate && !--t->gate) {
		t->on = 0;
		hw_off(ch);
		return;
	}
	const MusInst *in = &mus_inst[t->inst];
	if (in->vib_depth && ch != CH_NOISE) {
		t->vib += in->vib_speed * 4;
		int tri = (t->vib & 0x80 ? 255 - t->vib : t->vib) - 64;
		hw_freq(ch, t->freq + (((2048 - t->freq) * in->vib_depth * tri) >> 14));
	}
}

static void start_song(int id)
{
	if (cur_song == MUS_DAY || cur_song == MUS_NIGHT) {
		memcpy(saved, trk, sizeof trk);
		saved_song = cur_song;
	}
	cur_song = id;
	if (id >= 0 && id == saved_song) {
		// back from the horde's turn: pick the battle theme up where it left off
		memcpy(trk, saved, sizeof trk);
		saved_song = -1;
		for (int ch = 0; ch < CH_COUNT; ch++)
			if (!vox[ch].step) {
				if (trk[ch].on)
					track_sound(ch);
				else
					hw_off(ch);
			}
		return;
	}
	if (id != MUS_HORDE)
		saved_song = -1;
	for (int ch = 0; ch < CH_COUNT; ch++) {
		Track *t = &trk[ch];
		memset(t, 0, sizeof *t);
		t->vol = 0xFF;
		t->pos = id >= 0 ? mus_songs[id][ch] : NULL;
		if (!vox[ch].step)
			hw_off(ch);
	}
}

void music_play(int id)
{
	forced = id;
}

void sound_dialogue(bool on)
{
	in_dialogue = on;
}

static bool song_done(void)
{
	for (int ch = 0; ch < CH_COUNT; ch++)
		if (trk[ch].pos)
			return false;
	return true;
}

// Is a boss (Patient Zero) on the map? Engine support arrives with UF_BOSS.
static bool boss_alive(void)
{
#ifdef UF_BOSS
	for (int i = 0; i < MAX_UNITS; i++) {
		const Unit *u = &gs.units[i];
		if (u->alive && u->side == SIDE_HORDE && (unit_info[u->type].flags & UF_BOSS))
			return true;
	}
#endif
	return false;
}

// Pick the song from the game state, so gameplay code never has to ask for music.
static int director(void)
{
	if (!spr.map_visible) {
		battle_live = jingle_started = 0;
		return in_dialogue ? MUS_BRIEFING : MUS_TITLE;
	}
	if (gs.over) {
		// jingle once, then the briefing bed under the outro (or silence)
		int j = gs.over == 1 ? MUS_VICTORY : MUS_DEFEAT;
		if (!jingle_started) {
			jingle_started = 1;
			return j;
		}
		if (cur_song == j && !song_done())
			return j;
		return in_dialogue || cur_song == MUS_BRIEFING ? MUS_BRIEFING : -1;
	}
	jingle_started = 0;
	if (in_dialogue && !battle_live)
		return MUS_BRIEFING; // mission intro
	battle_live = 1;
	if (!(frame_count & 15))
		boss_seen = boss_alive();
	if (boss_seen)
		return MUS_ZERO;
	if (gs.side == SIDE_HORDE)
		return MUS_HORDE;
	const Mission *m = &missions[gs.mission];
	if (m->goal == GOAL_SURVIVE && m->goal_days && gs.day + 1 >= m->goal_days)
		return MUS_LASTSTAND;
	return gs.night ? MUS_NIGHT : MUS_DAY;
}

// ---------------------------------------------------------------- sound effects

#define HZ(f)          (2048 - 131072 / (f))
#define NZ(s, w, r)    ((s) << 4 | (w) << 3 | (r))  // noise: shift, 7-bit width, divider
#define ENV(v, d, st)  ((v) << 4 | (d) << 3 | (st)) // volume, 1 = fade in, step
#define DUTY(d)        ((d) << 6)
#define SWEEP(t, d, s) ((t) << 4 | (d) << 3 | (s))  // time, 1 = down, shift
#define END            { 0, 0, 0, 0, 0, 0 }
#define SQ(n, d, e, f)    { n, DUTY(d), e, 0, f, 0 }
#define NOISE(n, e, nz)   { n, 0, e, 0, nz, 0 }

static const SfxStep s_cursor[] = { SQ(2, 0, ENV(2, 0, 1), HZ(1760)), END };
static const SfxStep s_confirm[] = { SQ(3, 2, ENV(7, 0, 1), HZ(988)), SQ(6, 2, ENV(7, 0, 2), HZ(1319)), END };
static const SfxStep s_cancel[] = { SQ(3, 2, ENV(6, 0, 1), HZ(659)), SQ(6, 2, ENV(6, 0, 2), HZ(440)), END };
static const SfxStep s_menu[] = { SQ(3, 1, ENV(5, 0, 1), HZ(784)), SQ(5, 1, ENV(5, 0, 2), HZ(1047)), END };
static const SfxStep s_step[] = { NOISE(2, ENV(3, 0, 1), NZ(1, 0, 2)), END };
static const SfxStep s_shuffle[] = { NOISE(3, ENV(3, 0, 1), NZ(5, 0, 3)), END };
static const SfxStep s_shot_n[] = { NOISE(2, ENV(15, 0, 1), NZ(1, 0, 0)), NOISE(14, ENV(12, 0, 2), NZ(3, 0, 1)), END };
static const SfxStep s_shot_t[] = { { 6, DUTY(2), ENV(10, 0, 1), SWEEP(1, 1, 2), HZ(400), 0 }, END };
static const SfxStep s_bite_n[] = { NOISE(3, ENV(13, 0, 1), NZ(4, 0, 5)), NOISE(10, ENV(11, 0, 3), NZ(6, 0, 4)), END };
static const SfxStep s_bite_t[] = { { 18, DUTY(2), ENV(9, 0, 3), 0, HZ(110), -3 }, END };
static const SfxStep s_destroy_n[] = { NOISE(4, ENV(15, 0, 2), NZ(5, 0, 2)), NOISE(30, ENV(13, 0, 4), NZ(7, 0, 3)), END };
static const SfxStep s_destroy_t[] = { { 20, DUTY(2), ENV(12, 0, 3), SWEEP(2, 1, 1), HZ(300), 0 }, END };
static const SfxStep s_infect_t[] = { { 36, DUTY(2), ENV(2, 1, 3), 0, HZ(98), 8 }, { 24, DUTY(2), ENV(10, 0, 3), 0, HZ(123), -4 }, END };
static const SfxStep s_infect_n[] = { NOISE(40, ENV(2, 1, 5), NZ(2, 0, 1)), END };
static const SfxStep s_secure[] = {
	SQ(4, 2, ENV(9, 0, 1), HZ(523)), SQ(4, 2, ENV(9, 0, 1), HZ(659)), SQ(4, 2, ENV(9, 0, 1), HZ(784)),
	SQ(12, 2, ENV(9, 0, 3), HZ(1047)), END };
static const SfxStep s_overrun[] = {
	SQ(6, 2, ENV(10, 0, 2), HZ(311)), SQ(6, 2, ENV(10, 0, 2), HZ(294)), SQ(18, 2, ENV(10, 0, 4), HZ(208)), END };
static const SfxStep s_burn_n[] = { NOISE(10, ENV(4, 1, 2), NZ(3, 0, 2)), NOISE(36, ENV(13, 0, 5), NZ(4, 0, 1)), END };
static const SfxStep s_burn_t[] = { { 30, DUTY(3), ENV(8, 0, 4), SWEEP(3, 1, 3), HZ(700), 0 }, END };
static const SfxStep s_found[] = {
	SQ(4, 2, ENV(9, 0, 1), HZ(784)), SQ(4, 2, ENV(9, 0, 1), HZ(988)), SQ(4, 2, ENV(9, 0, 1), HZ(1175)),
	SQ(14, 2, ENV(9, 0, 3), HZ(1568)), END };
static const SfxStep s_empty[] = { SQ(8, 2, ENV(7, 0, 2), HZ(330)), SQ(14, 2, ENV(7, 0, 3), HZ(262)), END };
static const SfxStep s_ambush_a[] = { SQ(28, 1, ENV(13, 0, 4), HZ(523)), END };
static const SfxStep s_ambush_b[] = { SQ(28, 1, ENV(12, 0, 4), HZ(740)), END };
static const SfxStep s_ambush_n[] = { NOISE(28, ENV(12, 0, 4), NZ(3, 0, 4)), END };
static const SfxStep s_night_a[] = { { 56, DUTY(2), ENV(9, 0, 6), 0, HZ(659), -3 }, END };
static const SfxStep s_night_b[] = { { 56, DUTY(2), ENV(6, 0, 6), 0, HZ(622), -3 }, END };
static const SfxStep s_dawn[] = {
	SQ(6, 2, ENV(8, 0, 2), HZ(523)), SQ(6, 2, ENV(8, 0, 2), HZ(659)), SQ(6, 2, ENV(8, 0, 2), HZ(784)),
	SQ(26, 2, ENV(8, 0, 5), HZ(1047)), END };
static const SfxStep s_build_n[] = {
	NOISE(4, ENV(8, 0, 1), NZ(2, 1, 1)), NOISE(4, ENV(8, 0, 1), NZ(2, 1, 1)), NOISE(4, ENV(8, 0, 1), NZ(2, 1, 1)), END };
static const SfxStep s_build_t[] = { SQ(12, 1, 0, HZ(392)), SQ(5, 1, ENV(9, 0, 1), HZ(392)), SQ(12, 1, ENV(9, 0, 3), HZ(784)), END };
#define CHOP NOISE(4, ENV(12, 0, 1), NZ(5, 0, 4))
static const SfxStep s_evac_n[] = { CHOP, CHOP, CHOP, CHOP, CHOP, CHOP, CHOP, CHOP, CHOP, CHOP, CHOP, CHOP, END };
static const SfxStep s_evac_t[] = { { 48, DUTY(2), ENV(6, 0, 7), 0, HZ(110), 2 }, END };
static const SfxStep s_heal[] = {
	SQ(3, 2, ENV(6, 0, 1), HZ(659)), SQ(3, 2, ENV(6, 0, 1), HZ(831)), SQ(3, 2, ENV(6, 0, 1), HZ(988)),
	SQ(10, 2, ENV(6, 0, 2), HZ(1319)), END };
static const SfxStep s_burst_n[] = { NOISE(3, ENV(15, 0, 1), NZ(2, 0, 3)), NOISE(22, ENV(13, 0, 3), NZ(5, 0, 6)), END };
static const SfxStep s_burst_t[] = { { 16, DUTY(2), ENV(11, 0, 2), SWEEP(1, 1, 3), HZ(220), 0 }, END };
static const SfxStep s_acid_n[] = { NOISE(24, ENV(9, 0, 3), NZ(1, 0, 4)), END };
static const SfxStep s_acid_t[] = { { 12, DUTY(3), ENV(7, 0, 2), 0, HZ(300), 12 }, END };
static const SfxStep s_wave_t[] = {
	SQ(8, 2, ENV(10, 0, 0), HZ(587)), SQ(8, 2, ENV(10, 0, 0), HZ(440)), SQ(8, 2, ENV(10, 0, 0), HZ(587)),
	SQ(8, 2, ENV(10, 0, 0), HZ(440)), SQ(8, 2, ENV(10, 0, 0), HZ(587)), SQ(16, 2, ENV(10, 0, 3), HZ(440)), END };
static const SfxStep s_wave_n[] = { NOISE(56, ENV(10, 0, 7), NZ(6, 0, 5)), END };
static const SfxStep s_buzz[] = { SQ(8, 3, ENV(6, 0, 1), HZ(110)), END };
static const SfxStep s_blip[] = { NOISE(1, ENV(3, 0, 1), NZ(2, 1, 3)), END };
static const SfxStep s_call_a[] = { { 44, DUTY(2), ENV(4, 1, 4), 0, HZ(147), 3 }, END };
static const SfxStep s_call_b[] = { { 44, DUTY(2), ENV(3, 1, 4), 0, HZ(208), 4 }, END };
static const SfxStep s_call_n[] = { NOISE(44, ENV(3, 1, 6), NZ(6, 0, 4)), END };
static const SfxStep s_link_n[] = { NOISE(6, ENV(15, 0, 2), NZ(4, 0, 1)), NOISE(70, ENV(15, 0, 7), NZ(6, 0, 4)), END };
static const SfxStep s_link_a[] = { { 70, DUTY(2), ENV(13, 0, 7), SWEEP(7, 1, 1), HZ(440), 0 }, END };
static const SfxStep s_link_b[] = { { 70, DUTY(2), ENV(10, 0, 7), 0, HZ(466), -2 }, END };
static const SfxStep s_roar_n[] = { NOISE(32, ENV(14, 0, 4), NZ(5, 0, 2)), END };
static const SfxStep s_roar_t[] = { { 32, DUTY(3), ENV(12, 0, 4), 0, HZ(98), -2 }, END };

typedef struct {
	u8 prio;
	struct { u8 ch; const SfxStep *steps; } v[3];
} SfxDef;

// prio: 0 ambient ticks, 1 UI, 2 combat, 3 stings
static const SfxDef sfx_tab[SFX_COUNT] = {
	[SFX_CURSOR] = { 0, { { CH_SQ2, s_cursor } } },
	[SFX_CONFIRM] = { 1, { { CH_SQ2, s_confirm } } },
	[SFX_CANCEL] = { 1, { { CH_SQ2, s_cancel } } },
	[SFX_MENU] = { 1, { { CH_SQ2, s_menu } } },
	[SFX_STEP] = { 0, { { CH_NOISE, s_step } } },
	[SFX_SHUFFLE] = { 0, { { CH_NOISE, s_shuffle } } },
	[SFX_SHOT] = { 2, { { CH_NOISE, s_shot_n }, { CH_SQ1, s_shot_t } } },
	[SFX_BITE] = { 2, { { CH_NOISE, s_bite_n }, { CH_SQ2, s_bite_t } } },
	[SFX_DESTROY] = { 2, { { CH_NOISE, s_destroy_n }, { CH_SQ1, s_destroy_t } } },
	[SFX_INFECT] = { 3, { { CH_SQ1, s_infect_t }, { CH_NOISE, s_infect_n } } },
	[SFX_SECURE] = { 2, { { CH_SQ1, s_secure } } },
	[SFX_OVERRUN] = { 2, { { CH_SQ1, s_overrun } } },
	[SFX_BURN] = { 3, { { CH_NOISE, s_burn_n }, { CH_SQ1, s_burn_t } } },
	[SFX_FOUND] = { 2, { { CH_SQ1, s_found } } },
	[SFX_EMPTY] = { 2, { { CH_SQ1, s_empty } } },
	[SFX_AMBUSH] = { 3, { { CH_SQ1, s_ambush_a }, { CH_SQ2, s_ambush_b }, { CH_NOISE, s_ambush_n } } },
	[SFX_NIGHTFALL] = { 3, { { CH_SQ1, s_night_a }, { CH_SQ2, s_night_b } } },
	[SFX_DAWN] = { 3, { { CH_SQ1, s_dawn } } },
	[SFX_BUILD] = { 2, { { CH_NOISE, s_build_n }, { CH_SQ1, s_build_t } } },
	[SFX_EVAC] = { 3, { { CH_NOISE, s_evac_n }, { CH_SQ1, s_evac_t } } },
	[SFX_HEAL] = { 1, { { CH_SQ2, s_heal } } },
	[SFX_BURST] = { 2, { { CH_NOISE, s_burst_n }, { CH_SQ1, s_burst_t } } },
	[SFX_ACID] = { 2, { { CH_NOISE, s_acid_n }, { CH_SQ2, s_acid_t } } },
	[SFX_WAVE] = { 3, { { CH_SQ1, s_wave_t }, { CH_NOISE, s_wave_n } } },
	[SFX_BUZZ] = { 1, { { CH_SQ2, s_buzz } } },
	[SFX_BLIP] = { 0, { { CH_NOISE, s_blip } } },
	[SFX_CALL] = { 3, { { CH_SQ1, s_call_a }, { CH_SQ2, s_call_b }, { CH_NOISE, s_call_n } } },
	[SFX_LINK] = { 3, { { CH_NOISE, s_link_n }, { CH_SQ1, s_link_a }, { CH_SQ2, s_link_b } } },
	[SFX_ROAR] = { 3, { { CH_NOISE, s_roar_n }, { CH_SQ1, s_roar_t } } },
};

void sfx(int id)
{
	if (!sound_sfx_on() || id < 0 || id >= SFX_COUNT)
		return;
	const SfxDef *d = &sfx_tab[id];
	for (int i = 0; i < 3 && d->v[i].steps; i++) {
		Voice *v = &vox[d->v[i].ch];
		if (v->step && v->prio > d->prio)
			continue;
		v->step = d->v[i].steps;
		v->left = 0;
		v->prio = d->prio;
	}
}

static void voice_tick(int ch)
{
	Voice *v = &vox[ch];
	if (!v->step)
		return;
	const SfxStep *s = v->step;
	if (!v->left) {
		if (!s->frames) {
			// done: give the channel back to the music
			v->step = NULL;
			if (trk[ch].pos && trk[ch].on)
				track_sound(ch);
			else
				hw_off(ch);
			return;
		}
		v->left = s->frames;
		v->freq = s->freq;
		if (ch == CH_SQ1)
			REG_SND1SWEEP = s->sweep;
		hw_play(ch, s->duty, s->env, v->freq);
	} else if (s->slide) {
		v->freq += s->slide;
		hw_freq(ch, v->freq);
	}
	if (!--v->left)
		v->step++;
}

// ---------------------------------------------------------------- frame update, options

void sound_init(void)
{
	if (profile.snd_magic != SND_MAGIC) { // profile from before sound existed
		profile.snd_magic = SND_MAGIC;
		profile.snd_flags = 0;
	}
	REG_SOUNDCNT_X = 0x80;   // master on (must come first)
	REG_SNDDMGCNT = 0xFF77;  // all 4 PSG channels to both speakers, full volume
	REG_SNDDSCNT = 0x0002;   // PSG at 100%, DirectSound off
	for (int ch = 0; ch < CH_COUNT; ch++)
		hw_off(ch);
}

#ifdef SOUND_PROFILE
u32 snd_prof_max, snd_prof_total; // CPU cycles in sound_update (build with CC="... -DSOUND_PROFILE")
#endif

void sound_update(void)
{
#ifdef SOUND_PROFILE
	REG16(0x0400010A) = 0;
	REG16(0x04000108) = 0;
	REG16(0x0400010A) = 0x80; // timer 2, 1 tick per cycle
#endif
	int want = forced != MUS_AUTO ? forced : director();
	if (!sound_music_on())
		want = -1;
	if (want != cur_song)
		start_song(want);
	// day <-> night sting when a new day starts mid-battle
	if (spr.map_visible && last_visible && gs.day != last_day && gs.night != last_night)
		sfx(gs.night ? SFX_NIGHTFALL : SFX_DAWN);
	last_visible = spr.map_visible;
	last_day = gs.day;
	last_night = gs.night;
	for (int ch = 0; ch < CH_COUNT; ch++) {
		track_tick(ch);
		voice_tick(ch);
	}
#ifdef SOUND_PROFILE
	u32 c = REG16(0x04000108);
	snd_prof_total += c;
	if (c > snd_prof_max)
		snd_prof_max = c;
#endif
}

bool sound_music_on(void) { return !(profile.snd_flags & SND_NOMUSIC); }
bool sound_sfx_on(void) { return !(profile.snd_flags & SND_NOSFX); }

void sound_set(bool music, bool effects)
{
	profile.snd_flags = (music ? 0 : SND_NOMUSIC) | (effects ? 0 : SND_NOSFX);
	if (!effects)
		for (int ch = 0; ch < CH_COUNT; ch++)
			if (vox[ch].step) {
				vox[ch].step = NULL;
				hw_off(ch);
				if (trk[ch].pos && trk[ch].on)
					track_sound(ch);
			}
}

static const char *const sfx_names[] = {
	"Cursor", "Confirm", "Cancel", "Menu", "Step", "Shuffle", "Shot", "Bite", "Destroy", "Infect",
	"Secure", "Overrun", "Burn", "Found", "Empty", "Ambush", "Nightfall", "Dawn", "Build", "Evac",
	"Heal", "Burst", "Acid", "Wave", "Buzz", "Blip", "Call", "Link", "Roar",
};
_Static_assert(sizeof sfx_names / sizeof sfx_names[0] == SFX_COUNT, "one Sound Test name per SFX");

// Left/Right picks a song or effect, A plays it, B leaves.
static void sound_test(void)
{
	int row = 0, pick[2] = { 0, 0 };
	const int count[2] = { MUS_COUNT, SFX_COUNT };
	text_window(5, 3, 20, 6);
	text_put(7, 4, "Sound Test", BGPAL_TEXT_YEL);
	spr.pointer_show = 1;
	for (;;) {
		text_printf(8, 6, BGPAL_UI, "Music %-10s", mus_names[pick[0]]);
		text_printf(8, 7, BGPAL_UI, "SFX   %-10s", sfx_names[pick[1]]);
		spr.pointer_x = 6 * 8;
		spr.pointer_y = (6 + row) * 8;
		frame();
		if (keys_rep & (KEY_UP | KEY_DOWN))
			row ^= 1;
		int d = ((keys_rep & KEY_RIGHT) ? 1 : 0) - ((keys_rep & KEY_LEFT) ? 1 : 0);
		pick[row] = (pick[row] + d + count[row]) % count[row];
		if (keys_down & KEY_A) {
			if (row == 0)
				music_play(pick[0]);
			else
				sfx(pick[1]);
		}
		if (keys_down & KEY_B)
			break;
	}
	spr.pointer_show = 0;
	music_play(MUS_AUTO);
	text_clear_rect(5, 3, 20, 6);
}

void options_screen(int x, int y)
{
	char l[4][20];
	const char *items[6] = { l[0], l[3], l[1], l[2], "Sound Test", "Done" };
	int sel = 0;
	for (;;) {
		strcpy(l[0], profile.anim_speed ? "Unit speed Normal" : "Unit speed Fast");
		strcpy(l[1], sound_music_on() ? "Music      On" : "Music      Off");
		strcpy(l[2], sound_sfx_on() ? "SFX        On" : "SFX        Off");
		strcpy(l[3], profile.battle_off ? "Battle anims Off" : "Battle anims On");
		sel = text_menu(x, y, 20, items, NULL, 6, sel);
		if (sel < 0 || sel == 5)
			break;
		if (sel == 4)
			sound_test();
		else if (sel == 0)
			profile.anim_speed ^= 1;
		else if (sel == 1)
			profile.battle_off ^= 1;
		else
			sound_set(sel == 2 ? !sound_music_on() : sound_music_on(), sel == 3 ? !sound_sfx_on() : sound_sfx_on());
	}
	profile_save();
}
