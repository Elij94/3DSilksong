#pragma once
// The room's live objects (rooms/<Scene>.sso): drawn with their default animation; enemies and breakables can be
// hit, and enemies / damaging objects hurt Hornet on contact. Their behaviour (the PlayMaker FSMs) is not run yet.
#include "sprite.h"

enum {
    OF_ENEMY = 1, OF_BREAKABLE = 2, OF_INTERACT = 4, OF_ANIMATED = 8, OF_FSM = 16, OF_DAMAGE = 32, OF_INACTIVE = 64
};

typedef struct {
    u16 flags;
    s16 parent;
    float x, y, z, scx, scy;
    float rx, ry, rw, rh;
    int hp, damage;
    char name[33], sprite_set[33], clip[33];
    Anim anim;
    float par;
    bool alive;
    float flash;           // hit flash, seconds left
} Obj;

typedef struct {
    int n;
    Obj *o;
    s16 *order;            // drawing order: far to near
} Objects;

void objects_load(Objects *os, const char *scene);
void objects_free(Objects *os);
void objects_update(Objects *os, float dt);
// draws the objects with z in (zmin, zmax]
void objects_draw(Objects *os, float zmin, float zmax);
bool obj_alive(const Objects *os, int i);
// strikes the objects in the box; returns how many were hit (enemies take dmg)
int objects_strike(Objects *os, float x0, float y0, float x1, float y1, int dmg);
// damage dealt by the first live damaging object overlapping the box, 0 if none; its centre in *cx
int objects_touch_damage(const Objects *os, float x0, float y0, float x1, float y1, float *cx);
