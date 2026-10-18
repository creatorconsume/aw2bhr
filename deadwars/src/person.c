// Dead Wars: The Last Boat - person helpers shared by the run and the missions
#include "lb.h"

static const u8 skill_steps[5] = { 10, 25, 45, 70, 100 };
static const s8 bite_days[4] = { 2, 0, -1, -1 }; // turn time after a bite vs base, by difficulty (16)

int person_move(const Person *p)
{
	int mp = 2 + (p->speed + 1) / 2 - armor_info[p->armor].move_pen - (p->wounds ? 1 : 0);
	return MAX(mp, 1);
}

const char *person_name(const Person *p)
{
	return p->cast < CAST_COUNT ? cast_info[p->cast].name : p->name;
}

// Skills grow from use: each use adds XP; crossing a step raises the skill (max 5).
void skill_use(Person *p, int skill, int xp)
{
	int v = p->skill_xp[skill] + xp;
	p->skill_xp[skill] = v > 255 ? 255 : v;
	while (p->skill[skill] < 5 && p->skill_xp[skill] >= skill_steps[p->skill[skill]])
		p->skill[skill]++;
}

// Infected now, by a bite in a mission (tac_rules.c) or a storylet's `infect` alike: turns in
// 3 + grit/2 days, then the difficulty's bite days (LAST_BOAT.md 5.6, 16), at least 1.
void person_bitten(Person *p)
{
	int t = 3 + (p->grit >> 1) + bite_days[run.difficulty & 3];
	p->flags |= PF_INFECTED;
	p->turn_in = t < 1 ? 1 : t > 255 ? 255 : t;
}

int seats_total(void)
{
	int n = 0;
	for (int i = 0; i < 4; i++)
		if (run.vehicle[i])
			n += vehicle_info[run.vehicle[i]].seats;
	return n;
}

void chronicle(int event, int actor, int arg)
{
	LogRec *r = &run.log[(run.log_head + run.log_count) % LOG_MAX];
	if (run.log_count < LOG_MAX)
		run.log_count++;
	else
		run.log_head = (run.log_head + 1) % LOG_MAX;
	r->day = run.day;
	r->event = event;
	r->actor = actor;
	r->arg = arg;
}
