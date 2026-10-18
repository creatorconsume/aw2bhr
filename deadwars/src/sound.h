// Dead Wars - PSG music sequencer + sound effects
#ifndef SOUND_H
#define SOUND_H
#include "gba.h"
#include "music.h"

enum {
	SFX_CURSOR, SFX_CONFIRM, SFX_CANCEL, SFX_MENU, SFX_STEP, SFX_SHUFFLE,
	SFX_SHOT, SFX_BITE, SFX_DESTROY, SFX_INFECT, SFX_SECURE, SFX_OVERRUN,
	SFX_BURN, SFX_FOUND, SFX_EMPTY, SFX_AMBUSH, SFX_NIGHTFALL, SFX_DAWN,
	SFX_BUILD, SFX_EVAC, SFX_COUNT
};

#ifndef HOST_SIM
void sound_init(void);    // after profile_load()
void sound_update(void);  // once per frame, right after VBlank
void sfx(int id);
void music_play(int id);  // force a song (-1 = silence); MUS_AUTO hands control back to the director
// options (stored in Profile)
bool sound_music_on(void);
bool sound_sfx_on(void);
void sound_set(bool music, bool effects);
void options_screen(int x, int y);  // Speed / Music / SFX / Sound Test menu, saves the profile
#else // host balance simulator (test/sim): no sound
static inline void sound_init(void) {}
static inline void sound_update(void) {}
static inline void sfx(int id) { (void)id; }
static inline void music_play(int id) { (void)id; }
static inline bool sound_music_on(void) { return false; }
static inline bool sound_sfx_on(void) { return false; }
static inline void sound_set(bool music, bool effects) { (void)music; (void)effects; }
static inline void options_screen(int x, int y) { (void)x; (void)y; }
#endif
#define MUS_AUTO 0x7F

#endif
