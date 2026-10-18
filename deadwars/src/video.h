// Dead Wars - VRAM layout and sprite state
#ifndef VIDEO_H
#define VIDEO_H
#include "gba.h"

// BG tile indices in charblock 0
#define TILE_FONT     0      // 96 glyphs, ASCII 32..127
#define TILE_WINDOW   96     // 9-slice
#define TILE_OVERLAY  112
#define TILE_TERRAIN  128

// screenblocks
#define SB_TERRAIN 8   // 4 blocks, 512x512
#define SB_OVERLAY 12  // 4 blocks
#define SB_TEXT    16
#define SB_WINDOW  17
#define SB_TITLE   24

// BG palette banks
#define BGPAL_UI        0
#define BGPAL_TERRAIN   1  // +team (0 neutral,1 survivor,2 horde) +3 if fogged -> banks 1..6
#define BGPAL_OVERLAY   7
#define BGPAL_TEXT_RED  8
#define BGPAL_TEXT_YEL  9
#define BGPAL_TEXT_BLUE 10
#define BGPAL_TEXT_GREY 11
#define BGPAL_TITLE     12

// OBJ tile indices
#define OBJT_UNITS    0
#define OBJT_CURSOR   96
#define OBJT_DIGITS   104
#define OBJT_POINTER  114
#define OBJT_PORTRAIT 128
#define OBJT_EXPLODE  256    // 4 frames x 4 tiles

// OBJ palette banks
#define OBJPAL_SURVIVOR      0
#define OBJPAL_HORDE         1
#define OBJPAL_SURVIVOR_DONE 2
#define OBJPAL_CURSOR        3
#define OBJPAL_PORTRAIT      4  // .. 4 + PORTRAIT_COUNT - 1
#define OBJPAL_HORDE_DONE    10
#define OBJPAL_FLASH         11

typedef struct {
	u8 cursor_show, map_visible, pointer_show, portrait_flip;
	s8 anim_face;   // moving unit faces: 0 default, -1 left, 1 right
	u8 boom_timer;  // map explosion frames left (counts down from BOOM_FRAMES)
	s16 hide_unit, anim_unit, flash_unit, portrait;
	s16 anim_px, anim_py, pointer_x, pointer_y, portrait_x, portrait_y;
	s16 popup_x, popup_y, popup_value, popup_timer;
	s16 boom_x, boom_y;
} SpriteState;
#define BOOM_FRAMES 32
extern SpriteState spr;

void video_load_game_gfx(void);
void video_show_title(void);

#endif
