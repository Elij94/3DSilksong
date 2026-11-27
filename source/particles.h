#pragma once
#include "common.h"

// the room's ambient particles (particles.c)
void particles_load(const char *scene);
// an added scene's emitters (rooms/<scene>.spt) after the room's: the first file index they take, -1 if none
int particles_append(const char *scene);
void particles_free(void);
void particles_update(float dt);
// draws the particles of the emitters with z in (zmin, zmax]
void particles_draw(float zmin, float zmax);
// a one-off effect, fx/<name>.spt, at (x, y), z added to its depth, turned by angle (degrees)
void particles_fx(const char *name, float x, float y, float z, float angle);
// emitters the scripts drive: the room's emitter (file index) under a node; started (count 0) or count particles
// out now; stopped. The game says whether a node is active and where it is.
void particles_bind(int file_i, int node);
void particles_play(int node, int count);
void particles_stop(int node);
extern bool (*particles_node_on)(int node);
extern void (*particles_node_pos)(int node, float *x, float *y);
int particles_count(void);
int particles_emitters(void);
void particles_debug(char *out, int n);
