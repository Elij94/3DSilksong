// A generic behaviour for the enemies while their PlayMaker FSMs don't run: chosen from the animations each one has.
//   walkers (Walk / Run / Crawl): patrol, turn at walls and ledges, run at Hornet when she is near
//   flyers (Fly): hover around their spot, chase Hornet when she is near, never through walls
//   the rest: stay put (their idle animation)
// At 0 hp: "Death Air" while falling, "Death Land" on the ground, and the corpse stays, as in the game.
#include <math.h>
#include "objects.h"
#include "room.h"
#include "currency.h"

#define WALK_SPEED   1.8f
#define CHASE_SPEED  3.6f
#define FLY_SPEED    3.0f
#define AGGRO_X      9.0f
#define AGGRO_Y      3.5f
#define FLY_AGGRO    10.0f
#define GRAVITY      60.0f   // Physics2D: -60
#include <stdlib.h>
#include "fx.h"
#include "audio.h"
#include "fsm.h"

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
        if (o->node >= 0 && fsm_node_fsms(o->node) > 0) { o->ai = AI_FSM; continue; }   // its own FSMs drive it
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

// Walker, the game's walking component: along at its speed, turning at walls and at the edge of its floor, with
// pauses if it has them ("Walk", "Turn", "Idle")
static void walker(Objects *os, int i, const Room *r, float dt) {
    Obj *o = &os->o[i];
    float vx, vy;
    fsm_node_velocity(o->node, &vx, &vy, false);
    bool right = o->scx < 0;                   // the art faces left
    if (o->w_t > 0) {                          // pausing
        o->w_t -= dt;
        vx = 0;
        fsm_node_velocity(o->node, &vx, &vy, true);
        if (o->w_t <= 0) {
            o->w_t = -(o->w_wait[0] + (o->w_wait[1] - o->w_wait[0]) * (rand() / (float)RAND_MAX));
            int c = sprite_clip(o->anim.set, "Walk");
            if (c >= 0) anim_play(&o->anim, c, true);
        }
        return;
    }
    if (o->w_pauses) {
        o->w_t += dt;                          // counts up from minus the walking time
        if (o->w_t >= 0) {
            o->w_t = o->w_pause[0] + (o->w_pause[1] - o->w_pause[0]) * (rand() / (float)RAND_MAX) + 0.01f;
            int c = sprite_clip(o->anim.set, "Idle");
            if (c >= 0) anim_play(&o->anim, c, true);
            return;
        }
    }
    float x0 = o->rx, x1 = o->rx + o->rw, f = right ? 1.0f : -1.0f;
    bool wall = room_box_solid(r, right ? x1 : x0 - 0.3f, o->ry + 0.2f, right ? x1 + 0.3f : x0, o->ry + o->rh - 0.1f);
    float ex = (right ? x1 : x0) + f * (0.5f + o->w_edge);
    bool hole = !o->w_ignore_holes && !room_box_solid(r, ex - 0.05f, o->ry - 0.6f, ex + 0.05f, o->ry - 0.02f);
    if (wall || hole) {
        o->scx = -o->scx;
        right = !right;
        int c = sprite_clip(o->anim.set, "Turn");
        if (c >= 0) anim_play(&o->anim, c, true);
    } else if (o->anim.done || o->anim.clip < 0) {
        int c = sprite_clip(o->anim.set, "Walk");
        if (c >= 0) anim_play(&o->anim, c, true);
    }
    vx = right ? o->w_speed[1] : o->w_speed[0];
    fsm_node_velocity(o->node, &vx, &vy, true);
}

// Crawler / WalkerV2: along the floor the way it faces, turning at walls and at edges
// a ray against the room's solids: true if it meets one within len
static bool ray_hit(const Room *r, float x, float y, float dx, float dy, float len) {
    for (float d = 0; d <= len + 1e-4f; d += 0.05f) {
        float px = x + dx * d, py = y + dy * d;
        if (room_box_solid(r, px - 0.01f, py - 0.01f, px + 0.01f, py + 0.01f)) return true;
    }
    return false;
}

// Crawler, the game's: along its surface (the floor, or the roof when upside down) at its speed; on ground under its
// back edge, it turns at a wall in front or where the ground ahead ends, its turn clip played standing, then flipped
static void crawler(Objects *os, int i, const Room *r, float dt) {
    Obj *o = &os->o[i];
    float vx, vy;
    fsm_node_velocity(o->node, &vx, &vy, false);
    float up = o->scy < 0 ? -1.0f : 1.0f;                                                   // its "up" in the world
    int fwd = (o->scx >= 0 ? 1 : -1) * (o->crawl_sfr ? 1 : -1);                             // +1: going right
    float x0 = o->rx, x1 = o->rx + o->rw, yb = up > 0 ? o->ry : o->ry + o->rh;
    float front = fwd > 0 ? x1 : x0, back = fwd > 0 ? x0 : x1;
    const char *crawl = o->crawl_anim ? o->crawl_anim : "Walk";
    if (o->turn_t > 0) {
        o->turn_t -= dt;
        vx = 0;
        if (o->turn_t <= 0) {
            o->scx = -o->scx;
            anim_play_name(&o->anim, crawl, true);
        }
        fsm_node_velocity(o->node, &vx, &vy, true);
        return;
    }
    bool turn = false;
    if (ray_hit(r, back, yb + 0.6f * up, 0, -up, 1.1f)) {
        float len = fmaxf(0.2f, fabsf(o->crawl_speed * dt)) + 0.1f;
        if (ray_hit(r, front - 0.1f * fwd, yb + 0.06f * up, fwd, 0, len)) turn = true;
        else if (!ray_hit(r, front + 0.2f * fwd, yb + 0.6f * up, 0, -up, 1.1f)) turn = true;
    }
    if (turn) {
        int c = o->turn_anim ? sprite_clip(o->anim.set, o->turn_anim) : -1;
        if (c >= 0) {
            anim_play(&o->anim, c, true);
            o->turn_t = o->anim.set->clips[c].n / fmaxf(1, o->anim.set->clips[c].fps);
            vx = 0;
            fsm_node_velocity(o->node, &vx, &vy, true);
            return;
        }
        o->scx = -o->scx;
        fwd = -fwd;
    } else if (o->anim.clip < 0 || strcmp(o->anim.set->clips[o->anim.clip].name, crawl))
        anim_play_name(&o->anim, crawl, false);
    vx = fwd * o->crawl_speed;
    fsm_node_velocity(o->node, &vx, &vy, true);
}

// as HealthManager.Die and EnemyDeathEffects: the freeze, the death sounds and effects, then the corpse flung off
void enemy_kill(Objects *os, int i, float from_x, int dir) {
    Obj *o = &os->o[i];
    float cx = o->rx + o->rw * 0.5f, cy = o->ry + o->rh * 0.5f;
    if (objects_freeze) objects_freeze(0.04f, 0.024f, 0.04f);          // FreezeMomentTypes.EnemyDeath
    fx_shake(0.12f);                                                    // its deathCameraShake
    float pan = clampf((cx - g_view.cx) / 14, -0.8f, 0.8f);
    for (int k = 0; k < o->ndsnd; k++) sfx_play(o->dsnd[k], 1, pan);
    fx_death(o->deathp, cx, cy - 0.5f, -0.5f, dir * 90.0f, o->ndsnd == 0);
    fsm_node_send(o->node, "DEATH");
    fsm_node_stop(o->node);
    if (o->persist[0]) pd_set_bool(o->persist, true);
    for (int k = 0; k < 5; k++)              // HealthManager.SpawnCurrency: its rosaries and shell shards
        if (o->geo[k]) currency_fling(k, o->geo[k], cx, cy, o->mega_fling);
    o->walking = false;
    o->recoil_t = 0;
    // the corpse: the corpse prefab's animations ("Death Air" in the air, "Death Land" on the ground)
    if (o->corpse_set && o->corpse_set != o->anim.set) {
        int a = sprite_clip(o->corpse_set, "Death Air"), l = sprite_clip(o->corpse_set, "Death Land");
        if (a >= 0 || l >= 0) {
            o->anim.set = o->corpse_set;
            o->c_dair = a;
            o->c_dland = l;
        }
    }
    if ((o->eflags & EF_NO_CORPSE) || (o->c_dair < 0 && o->c_dland < 0)) {
        o->alive = false;
        return;
    }
    o->dead = true;
    // flung away from the hit: 60 / 120 degrees sideways (10 / 170 on a low arc), straight down, or up 75-105
    // degrees a third faster; facing where the hit came from
    float sp = o->fling > 0 ? o->fling : 8, ang;
    switch (dir & 3) {
    case 0: ang = (o->eflags & EF_LOW_ARC) ? 10 : 60; break;
    case 2: ang = (o->eflags & EF_LOW_ARC) ? 170 : 120; break;
    case 3: ang = 270; break;
    default: ang = 75 + 30 * (rand() / (float)RAND_MAX); sp *= 1.3f; break;
    }
    o->vx = cosf(ang * (float)M_PI / 180) * sp;
    o->vy = sinf(ang * (float)M_PI / 180) * sp;
    o->kb = 10;            // keeps moving sideways until it lands
    o->grounded = false;
    if (!(dir & 1)) face(o, cx < from_x ? 1 : -1);
    anim_play(&o->anim, o->c_dair >= 0 ? o->c_dair : o->c_dland, true);
}

void enemies_update(Objects *os, const Room *r, float hx, float hy, float dt) {
    for (int i = 0; i < os->n; i++) {
        Obj *o = &os->o[i];
        if (o->ai == AI_NONE || !o->alive) continue;
        if (o->ai == AI_FSM && !o->dead && o->recoil_t <= 0 && !o->walking) continue;
        o->t += dt;
        if (o->turn_t > 0) o->turn_t -= dt;
        float cx = o->rx + o->rw * 0.5f, cy = o->ry + o->rh * 0.5f;

        // the recoil of a hit (Recoil: its speed, falling off over its duration, along the hit)
        if (o->recoil_t > 0 && !o->dead) {
            float k = o->recoil_t / o->recoil_dur;
            o->recoil_t -= dt;
            float v = o->recoil_v * k;
            if (o->rdx != 0 && !try_x(os, i, r, o->rdx * v * dt)) o->recoil_t = 0;
            if (o->rdy != 0 && !try_y(os, i, r, o->rdy * v * dt)) o->recoil_t = 0;
            if (o->recoil_t <= 0) fsm_node_send(o->node, "RECOIL END");
            continue;      // the recoil takes over the movement
        }
        if (o->ai == AI_FSM && !o->dead) {
            if (o->walking) { if (o->crawler) crawler(os, i, r, dt); else walker(os, i, r, dt); }
            continue;
        }
        if (o->dead) {     // the corpse's flight, then it lies there
            if (!o->grounded && o->vx != 0 && !try_x(os, i, r, o->vx * dt)) o->vx = 0;
            fall(os, i, r, dt);
            if (o->grounded) {
                o->vx = 0;
                if (o->c_dland >= 0 && o->anim.clip != o->c_dland) anim_play(&o->anim, o->c_dland, true);
            }
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
