#pragma once
// The game's hit and death effects per kind of enemy (fx/profiles.txt, tools/build_fx.py).
#include "common.h"

// camera shakes asked for (a kill, an FSM's DoCameraShake): the strongest since the last take
void fx_shake(float amp);
float fx_take_shake(void);
int fx_profile(const char *name);    // -1 if unknown
bool fx_flash(int prof);             // does a hit flash the enemy white
// a needle hit: the profile's particles turned to the hit's direction (degrees), one of its sounds
void fx_hit(int prof, float x, float y, float z, float angle);
// a kill: the death profile's particles, and its sounds unless the enemy has its own
void fx_death(int prof, float x, float y, float z, float angle, bool sounds);
