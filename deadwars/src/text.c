// Dead Wars - text, windows, menus, dialogue (BG0 = text, BG3 = window frames)
#include "game.h"
#include "video.h"
#include "sound.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TXT(x, y) SCREENBLOCK(SB_TEXT)[(y) * 32 + (x)]
#define WIN(x, y) SCREENBLOCK(SB_WINDOW)[(y) * 32 + (x)]

void text_clear(void)
{
	for (int i = 0; i < 32 * 20; i++) {
		SCREENBLOCK(SB_TEXT)[i] = 0;
		SCREENBLOCK(SB_WINDOW)[i] = 0;
	}
}

void text_clear_rect(int x, int y, int w, int h)
{
	for (int j = y; j < y + h; j++)
		for (int i = x; i < x + w; i++)
			if (i >= 0 && i < 32 && j >= 0 && j < 32)
				TXT(i, j) = WIN(i, j) = 0;
}

__attribute__((noinline)) void text_put(int x, int y, const char *s, int pal)
{
	for (; *s && x < 30; s++, x++)
		if (x >= 0)
			TXT(x, y) = (TILE_FONT + ((u8)*s - 32)) | SE_PAL(pal);
}

void text_putn(int x, int y, s32 n, int pal)
{
	char buf[12];
	snprintf(buf, sizeof buf, "%ld", (long)n);
	text_put(x, y, buf, pal);
}

void text_printf(int x, int y, int pal, const char *fmt, ...)
{
	char buf[40];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	text_put(x, y, buf, pal);
}

void text_window(int x, int y, int w, int h)
{
	for (int j = 0; j < h; j++) {
		int row = j == 0 ? 0 : j == h - 1 ? 2 : 1;
		for (int i = 0; i < w; i++) {
			if (x + i < 0 || x + i > 31 || y + j < 0 || y + j > 31)
				continue; // never wrap into the next row
			int col = i == 0 ? 0 : i == w - 1 ? 2 : 1;
			WIN(x + i, y + j) = TILE_WINDOW + row * 3 + col;
			TXT(x + i, y + j) = 0;
		}
	}
}

// Vertical list menu. Returns chosen index, or -1 if cancelled with B.
int text_menu(int x, int y, int w, const char *const *items, const u8 *enabled, int count, int start)
{
	int sel = start;
	text_window(x, y, w, count + 2);
	for (int i = 0; i < count; i++)
		text_put(x + 2, y + 1 + i, items[i], (enabled && !enabled[i]) ? BGPAL_TEXT_GREY : BGPAL_UI);
	spr.pointer_show = 1;
	sfx(SFX_MENU);
	for (;;) {
		spr.pointer_x = x * 8 + 4;
		spr.pointer_y = (y + 1 + sel) * 8;
		frame();
		if (keys_rep & (KEY_UP | KEY_DOWN))
			sfx(SFX_CURSOR);
		if (keys_rep & KEY_UP)
			sel = (sel + count - 1) % count;
		if (keys_rep & KEY_DOWN)
			sel = (sel + 1) % count;
		if ((keys_down & KEY_A) && (!enabled || enabled[sel])) {
			sfx(SFX_CONFIRM);
			break;
		}
		if (keys_down & KEY_A)
			sfx(SFX_BUZZ);
		if (keys_down & KEY_B) {
			sfx(SFX_CANCEL);
			sel = -1;
			break;
		}
	}
	spr.pointer_show = 0;
	text_clear_rect(x, y, w, count + 2);
	return sel;
}

static const char *const speaker_names[PORTRAIT_COUNT] = { "Mara", "Doc", "June", "Holt", "???", "???" };

// Word-wrapped dialogue box with portraits, typewriter text.
void dialogue(const Line *lines)
{
	const int bx = 0, by = 12, bw = 30, bh = 8;
	sound_dialogue(true);
	for (; lines && lines->text; lines++) {
		text_window(bx, by, bw, bh);
		int tx = bx + 1, tw = bw - 2;
		if (lines->portrait < PORTRAIT_COUNT) {
			spr.portrait = lines->portrait;
			spr.portrait_x = 8;
			spr.portrait_y = by * 8 + 12;
			spr.portrait_flip = 0;
			tx = bx + 6;
			tw = bw - 7;
			text_put(tx, by + 1, speaker_names[lines->portrait], lines->portrait >= PT_ZERO ? BGPAL_TEXT_RED : BGPAL_TEXT_YEL);
		} else {
			spr.portrait = -1;
		}
		// layout words into lines
		const char *s = lines->text;
		int cx = 0, cy = 0, rows = bh - 3;
		char out[6][32];
		memset(out, 0, sizeof out);
		while (*s && cy < rows) {
			const char *w = s;
			while (*w && *w != ' ' && *w != '\n')
				w++;
			int len = w - s;
			if (cx + len > tw) {
				cy++;
				cx = 0;
				if (cy >= rows)
					break;
			}
			memcpy(&out[cy][cx], s, MIN(len, tw));
			cx += len;
			s = w;
			if (*s == '\n') {
				cy++;
				cx = 0;
				s++;
			} else if (*s == ' ') {
				if (cx < tw)
					out[cy][cx++] = ' ';
				s++;
			}
		}
		// typewriter
		bool skip = false;
		for (int r = 0; r < rows; r++) {
			char tmp[32] = { 0 };
			for (int c = 0; out[r][c]; c++) {
				tmp[c] = out[r][c];
				text_put(tx, by + 2 + r, tmp, BGPAL_UI);
				if (!skip) {
					if (!(c & 1) && out[r][c] != ' ')
						sfx(SFX_BLIP);
					frame();
					if (keys_down & KEY_START)
						goto done; // skip the whole scene
					if (keys_down & (KEY_A | KEY_B))
						skip = true;
				}
			}
		}
		// wait for A, blink an arrow
		do {
			frame();
			text_put(bw - 2, by + bh - 2, (frame_count & 16) ? "v" : " ", BGPAL_TEXT_YEL);
		} while (!(keys_down & (KEY_A | KEY_B | KEY_START)));
		if (keys_down & KEY_START)
			break; // skip the whole scene
	}
done:
	spr.portrait = -1;
	text_clear_rect(bx, by, bw, bh);
	sound_dialogue(false);
}

void banner(const char *l1, const char *l2, int frames)
{
	int w = MIN(MAX((int)strlen(l1), l2 ? (int)strlen(l2) : 0) + 4, 30);
	int x = MAX(0, (30 - w) / 2), h = l2 ? 4 : 3, y = (20 - h) / 2;
	text_window(x, y, w, h);
	text_put(x + MAX(0, (w - (int)strlen(l1)) / 2), y + 1, l1, BGPAL_TEXT_YEL);
	if (l2)
		text_put(x + MAX(0, (w - (int)strlen(l2)) / 2), y + 2, l2, BGPAL_UI);
	for (int i = 0; i < frames; i++) {
		frame();
		if (keys_down & (KEY_A | KEY_B))
			break;
	}
	text_clear_rect(x, y, w, h);
}
