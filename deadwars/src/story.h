// Dead Wars: The Last Boat - storylet engine (RimWorld-style). Content: data/story/*.tsv,
// compiled and integrity-checked by tools/storyc.py into build/gen/story_data.{c,h}.
#ifndef STORY_H
#define STORY_H
#include "lb.h"

// trigger points
enum { SP_CAMP, SP_ROAD, SP_STOP, SP_MISSION_START, SP_MISSION_END, SP_DEATH, SP_BITE, SP_COUNT };

void story_init_run(void);               // run_new(): story vars to their defaults
// Pick and play at most one storylet for this phase: bind roles, dialogue with portraits,
// choices, effects, Chronicle record. `subject`: roster index the event is about (death,
// bite) or -1. Returns true if a storylet played.
bool story_fire(int phase, int subject);
const char *story_log_text(const LogRec *r); // journal line of a LOG_STORY record (<= 25 chars), or NULL

#endif
