// A generic behaviour for the enemies while their PlayMaker FSMs don't run: chosen from the animations each one has.
//   walkers (Walk / Run / Crawl): patrol, turn at walls and ledges, run at Hornet when she is near
//   flyers (Fly): hover around their spot, chase Hornet when she is near, never through walls
//   the rest: stay put (their idle animation)
// At 0 hp: "Death Air" while falling, "Death Land" on the ground, and the corpse stays, as in the game.
#include <math.h>
#include "objects.h"
#include "room.h"

#define WALK_SPEED   1.8f
#define CHASE_SPEED  3.6f
#define FLY_SPEED    3.0f
#define AGGRO_X      9.0f
#define AGGRO_Y      3.5f
#define FLY_AGGRO    10.0f
#define GRAVITY      40.0f

static int clip_of(const SpriteSet *s, const char *const *names) {
    for (int i = 0; names[i]; i++) {
        int c = sprite_clip(s, names[i]);
        if (c >= 0) return c;
    }
    return -1;
}

// an enemy among the parents: this object is a part of it, moved with it
static bool enemy_part(const Objects *os, int i) {
    for (int p = os->o[i].parent, k = 0; p >= 0 && p < os->n && k < 32; p = os->o[p].parent, k++)
        if (os->o[p].flags & OF_ENEMY) return true;
    return false;
}

static bool floor_under(const Room *r, const Obj *o, float depth) {
    return room_box_solid(r, o->rx + 0.1f, o->ry - depth, o->rx + o->rw - 0.1f, o->ry - 0.01f);
}

void enemies_init(Objects *os, const Room *r) {
    static const char *const MOVE[] = {"Walk", "Run", "Crawl", "Walk Alert", NULL};
    static const char *const FLY[] = {"Fly", "Fly1", "Fly2", "Flapping", NULL};
    static const char *const TURN[] = {"Turn", NULL};
    static const char *const DAIR[] = {"Death Air", "Death", "Die", NULL};
    static const char *const DLAND[] = {"Death Land", NULL};
    for (int i = 0; i < os->n; i++) {
        Obj *o = &os->o[i];
        o->ai = AI_NONE;
        if (!(o->flags & OF_ENEMY) || !o->alive || !o->anim.set || o->rw <= 0 || enemy_part(os, i)) continue;
        const SpriteSet *s = o->anim.set;
        o->c_move = clip_of(s, MOVE);
        o->c_fly = clip_of(s, FLY);
        o->c_turn = clip_of(s, TURN);
        o->c_dair = clip_of(s, DAIR);
        o->c_dland = clip_of(s, DLAND);
        o->c_idle = o->anim.clip;
        o->home_x = o->x;
        o->home_y = o->y;
        bool flies = strstr(o->clip, "Fly") || strstr(o->clip, "Rise") || (o->c_fly >= 0 && o->c_move < 0);
        if (flies && o->c_fly >= 0) o->ai = AI_FLY;
        else if (o->c_move >= 0 && floor_under(r, o, 0.6f)) o->ai = AI_WALK;   // not those on walls or ceilings
        else o->ai = AI_STILL;
    }
}

static void face(Obj *o, int dir) {
    o->dir = dir;
    o->scx = fabsf(o->scx) * (dir > 0 ? -1.0f : 1.0f);   // the art faces left
}

// moves along x unless a wall is in the way; true if it moved
static bool try_x(Objects *os, int i, const Room *r, float dx) {
    Obj *o = &os->o[i];
    float x0 = o->rx + dx, x1 = o->rx + o->rw + dx;
    if (room_box_solid(r, x0, o->ry + 0.15f, x1, o->ry + o->rh - 0.05f)) return false;
    obj_move(os, i, dx, 0);
    return true;
}

static bool try_y(Objects *os, int i, const Room *r, float dy) {
    Obj *o = &os->o[i];
    if (room_box_solid(r, o->rx + 0.05f, o->ry + dy, o->rx + o->rw - 0.05f, o->ry + o->rh + dy)) return false;
    obj_move(os, i, 0, dy);
    return true;
}

// gravity for walkers and corpses: fall, then rest on the floor
static void fall(Objects *os, int i, const Room *r, float dt) {
    Obj *o = &os->o[i];
    if (floor_under(r, o, 0.05f) && o->vy <= 0) {
        o->vy = 0;
        o->grounded = true;
        return;
    }
    o->grounded = false;
    o->vy = fmaxf(o->vy - GRAVITY * dt, -20.0f);
    float dy = o->vy * dt;
    while (fabsf(dy) > 1e-4f) {
        float st = clampf(dy, -0.2f, 0.2f);
        dy -= st;
        if (!try_y(os, i, r, st)) {
            if (st < 0) o->grounded = true;
            o->vy = 0;
            break;
        }
    }
    if (o->ry < r->gy - 10) o->alive = false;   // out of the room
}

void enemy_kill(Objects *os, int i, float from_x) {
    Obj *o = &os->o[i];
    if (o->ai == AI_NONE || (o->c_dair < 0 && o->c_dland < 0)) {
        o->alive = false;
        return;
    }
    o->dead = true;
    o->vy = 7.0f;
    o->vx = (o->rx + o->rw * 0.5f < from_x ? -1 : 1) * 4.0f;
    o->kb = 0.3f;
    anim_play(&o->anim, o->c_dair >= 0 ? o->c_dair : o->c_dland, true);
}

void enemies_update(Objects *os, const Room *r, float hx, float hy, float dt) {
    for (int i = 0; i < os->n; i++) {
        Obj *o = &os->o[i];
        if (o->ai == AI_NONE || !o->alive) continue;
        o->t += dt;
        if (o->turn_t > 0) o->turn_t -= dt;
        float cx = o->rx + o->rw * 0.5f, cy = o->ry + o->rh * 0.5f;

        // knocked back by a hit (and the corpse's flight)
        if (o->kb > 0) {
            o->kb -= dt;
            if (!try_x(os, i, r, o->vx * dt)) o->vx = 0;
            if (o->ai == AI_FLY && !o->dead) continue;
        }
        if (o->dead) {
            fall(os, i, r, dt);
            if (o->grounded && o->c_dland >= 0 && o->anim.clip != o->c_dland) anim_play(&o->anim, o->c_dland, true);
            continue;
        }

        if (o->ai == AI_WALK) {
            fall(os, i, r, dt);
            if (!o->grounded) continue;
            bool aggro = fabsf(hx - cx) < AGGRO_X && fabsf(hy - cy) < AGGRO_Y;
            int want = aggro ? (hx > cx ? 1 : -1) : o->dir;
            if (want != o->dir && o->turn_t <= 0) {
                face(o, want);
                o->turn_t = 0.35f;
            }
            float front = o->dir > 0 ? o->rx + o->rw : o->rx;
            bool ledge = !room_solid(r, front + o->dir * 0.2f, o->ry - 0.3f) &&
                         !room_solid(r, front + o->dir * 0.2f, o->ry - 0.7f);
            float speed = aggro ? CHASE_SPEED : WALK_SPEED;
            bool moved = !ledge && o->turn_t <= 0 && try_x(os, i, r, o->dir * speed * dt);
            if (!moved && o->turn_t <= 0) {
                if (!aggro) { face(o, -o->dir); o->turn_t = 0.35f; }
                anim_play(&o->anim, o->c_idle >= 0 ? o->c_idle : o->c_move, false);
            } else if (moved) {
                anim_play(&o->anim, o->c_move, false);
            }
        } else if (o->ai == AI_FLY) {
            float tx, ty, sp;
            float dxh = hx - cx, dyh = hy + 0.6f - cy;
            if (dxh * dxh + dyh * dyh < FLY_AGGRO * FLY_AGGRO) {
                tx = hx; ty = hy + 0.6f; sp = FLY_SPEED;
            } else {   // a slow figure of eight around its spot
                tx = o->home_x + sinf(o->t * 0.7f) * 1.5f + (cx - o->x);
                ty = o->home_y + sinf(o->t * 1.4f) * 0.6f + (cy - o->y);
                sp = FLY_SPEED * 0.5f;
            }
            float ddx = tx - cx, ddy = ty - cy, d = sqrtf(ddx * ddx + ddy * ddy);
            if (d > 0.3f) {
                o->vx += (ddx / d * sp - o->vx) * fminf(1, dt * 3);
                o->vy += (ddy / d * sp - o->vy) * fminf(1, dt * 3);
            } else {
                o->vx *= 0.9f;
                o->vy *= 0.9f;
            }
            if (!try_x(os, i, r, o->vx * dt)) o->vx = 0;
            if (!try_y(os, i, r, o->vy * dt)) o->vy = 0;
            if (fabsf(o->vx) > 0.5f && (o->vx > 0 ? 1 : -1) != o->dir && o->turn_t <= 0) {
                face(o, o->vx > 0 ? 1 : -1);
                o->turn_t = 0.4f;
            }
            anim_play(&o->anim, o->c_fly, false);
        }
    }
}
