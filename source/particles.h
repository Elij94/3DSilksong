#pragma once
#include "common.h"

// the room's ambient particles (particles.c)
void particles_load(const char *scene);
void particles_free(void);
void particles_update(float dt);
// draws the particles of the emitters with z in (zmin, zmax]
void particles_draw(float zmin, float zmax);
// a one-off effect, fx/<name>.spt, at (x, y), z added to its depth, turned by angle (degrees)
void particles_fx(const char *name, float x, float y, float z, float angle);
int particles_count(void);
int particles_emitters(void);
void particles_debug(char *out, int n);
