// Dead Wars - entry point, title screen, mission select
#include "game.h"
#include "video.h"
#include "perf.h"
#include <string.h>

static int mission_select(void)
{
	const char *items[16];
	char lines[16][28];
	int n = MIN(profile.unlocked, mission_count);
	for (int i = 0; i < n; i++) {
		// "N Title.............. *": star at col 25, inside the frame
		memset(lines[i], ' ', sizeof lines[i]);
		lines[i][0] = '1' + i;
		int tl = MIN((int)strlen(missions[i].title), 18);
		memcpy(lines[i] + 2, missions[i].title, tl);
		lines[i][21] = profile.best_days[i] ? '*' : ' ';
		lines[i][22] = 0;
		items[i] = lines[i];
	}
	return text_menu(2, 2, 26, items, NULL, n, n - 1);
}

static void how_to_play(void)
{
	static const Line help[] = {
		{ 0xFF, "Command your survivors with the D-Pad and A. B cancels. START opens the menu, SELECT jumps to your next ready unit." },
		{ 0xFF, "Select a unit, pick a blue tile, then choose an action. Fire attacks. Secure takes buildings for income and healing." },
		{ 0xFF, "Search an unlooted house for supplies or survivors... or something worse. Spend supplies at a Workshop to recruit." },
		{ 0xFF, "Anyone killed by a zombie rises as one. Nests keep breeding the dead: Burn them with foot units." },
		{ 0xFF, "Every few days night falls: you only see what your units can see, and zombies move 1 extra tile. Radio towers light up the dark." },
		{ 0xFF, "Press A on a zombie to see how far it can reach. Lose your Safehouse and it's over." },
		{ 0, NULL },
	};
	dialogue(help);
}

int main(void)
{
	video_init();
#ifdef PERF
	perf_init();
#endif
	profile_load();
	for (;;) {
		video_show_title();
		spr.map_visible = 0;
		text_clear();
		text_put(9, 19, "Press START", BGPAL_UI);
		do
			frame();
		while (!(keys_down & (KEY_START | KEY_A)));
		text_clear();
		const char *items[4] = { "Campaign", "Continue", "How to Play", "Speed" };
		u8 en[4] = { 1, profile.has_suspend, 1, 1 };
		int c = text_menu(8, 11, 14, items, en, 4, profile.has_suspend ? 1 : 0);
		text_clear();
		if (c == 0) {
			int m = mission_select();
			text_clear();
			if (m >= 0)
				battle_run(m);
		} else if (c == 1) {
			battle_run(-1);
		} else if (c == 2) {
			how_to_play();
		} else if (c == 3) {
			static const char *const sp[2] = { "Fast", "Normal" };
			int s = text_menu(10, 11, 10, sp, NULL, 2, profile.anim_speed);
			if (s >= 0) {
				profile.anim_speed = s;
				profile_save();
			}
		}
	}
}
