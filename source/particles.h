#pragma once
#include "common.h"

// the room's ambient particles (particles.c)
void particles_load(const char *scene);
void particles_free(void);
void particles_update(float dt);
// draws the particles of the emitters with z in (zmin, zmax]
void particles_draw(float zmin, float zmax);
int particles_count(void);
int particles_emitters(void);
void particles_debug(char *out, int n);
