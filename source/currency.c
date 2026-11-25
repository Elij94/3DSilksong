// Rosaries and shell shards (prefabs Geo Small / Med / Large / Large Smooth, Shell Shard 01): flung out of a kill,
// they fall with gravity scale 1.5, rosaries bounce (Rosary Bead: bounciness 0.5, almost no friction), shards don't
// (Default Friction), and Hornet picks them up on touch, 0.35 s after they appear (CurrencyObjectBase).
#include "currency.h"
#include "sprite.h"
#include "audio.h"
#include "fsm.h"
#include <math.h>
#include <stdlib.h>

#define MAX_PIECES 96
#define GRAV (60.0f * 1.5f)
#define PICKUP_DELAY 0.35f

typedef struct {
    bool on, ground;
    u8 kind;
    float x, y, vx, vy, t, gleam;
    int frame;              // a shard: its sprite, picked at random
    Anim anim;
} Piece;

static Piece s_p[MAX_PIECES];
static SpriteSet *s_set;
static float s_shown_t = 99;
static const int VALUE[CUR_KINDS] = {1, 5, 15, 25, 1};
static const char *SIZE[CUR_KINDS] = {"Small", "Med", "Large", "Large", ""};
// half sizes: Geo Small's circle (0.223 at scale 1.2), the others scaled from it; a shard's polygon about 0.2
static const float RAD[CUR_KINDS] = {0.27f, 0.32f, 0.38f, 0.38f, 0.2f};

static float frand(float a, float b) { return a + (b - a) * (rand() / (float)RAND_MAX); }

void currency_init(void) {
    if (!s_set) {
        s_set = sprites_get("sprites/rosary.sss");
        if (s_set) s_set->pinned = true;
    }
}

void currency_clear(void) {
    for (int i = 0; i < MAX_PIECES; i++) s_p[i].on = false;
}

static void play(Piece *p, const char *state, bool restart) {
    char name[24];
    if (p->kind == CUR_SHARD) return;
    snprintf(name, sizeof name, "%s %s", SIZE[p->kind], state);
    anim_play_name(&p->anim, name, restart);
}

void currency_fling(int kind, int n, float x, float y, bool mega) {
    currency_init();
    for (int k = 0; k < n; k++) {
        Piece *p = NULL;
        for (int i = 0; i < MAX_PIECES && !p; i++)
            if (!s_p[i].on) p = &s_p[i];
        if (!p) {   // full: the oldest goes
            p = &s_p[0];
            for (int i = 1; i < MAX_PIECES; i++) if (s_p[i].t > p->t) p = &s_p[i];
        }
        memset(p, 0, sizeof *p);
        p->on = true;
        p->kind = (u8)kind;
        p->x = x;
        p->y = y;
        float ang = (mega ? frand(65, 115) : frand(80, 100)) * (float)M_PI / 180;
        float sp = mega ? frand(30, 45) : frand(15, 30);
        p->vx = cosf(ang) * sp;
        p->vy = sinf(ang) * sp;
        p->anim.set = s_set;
        p->anim.clip = -1;
        p->gleam = frand(1.5f, 2);
        if (kind == CUR_SHARD) {
            int c = s_set ? sprite_clip(s_set, "Shard") : -1;
            p->frame = c >= 0 && s_set->clips[c].n ? rds16((const u8 *)&s_set->clips[c].frames[rand() % s_set->clips[c].n]) : -1;
        } else play(p, "Air", true);
    }
}

static bool solid(const Room *r, float x, float y, float h) {
    return room_box_solid(r, x - h, y - h, x + h, y + h);
}

static void collect(Piece *p, float hx) {
    char name[32];
    if (p->kind == CUR_SHARD) {
        pd_set_int("ShellShards", pd_int("ShellShards") + VALUE[p->kind]);
        snprintf(name, sizeof name, "shell_shard_pickup_%d", 1 + rand() % 4);
    } else {
        pd_set_int("geo", pd_int("geo") + VALUE[p->kind]);
        snprintf(name, sizeof name, "rosary_collect_%d", 1 + rand() % 5);
    }
    sfx_play_name(name, 0.8f, (p->x - hx) * 0.05f);
    s_shown_t = 0;
    p->on = false;
}

void currency_update(const Room *r, float dt, float hx0, float hy0, float hx1, float hy1) {
    s_shown_t += dt;
    float hx = (hx0 + hx1) * 0.5f;
    for (int i = 0; i < MAX_PIECES; i++) {
        Piece *p = &s_p[i];
        if (!p->on) continue;
        p->t += dt;
        float h = RAD[p->kind];
        // gravity, then x and y moved apart so walls and floors stop each on its own, in steps shorter than the
        // piece (the floors can be thin)
        if (p->t <= dt)   // flung from inside a floor: up out of it first
            for (int k = 0; k < 20 && solid(r, p->x, p->y, h); k++) p->y += 0.1f;
        if (!p->ground) p->vy -= GRAV * dt;
        else if (!solid(r, p->x, p->y - 0.05f, h)) { p->ground = false; play(p, "Air", true); }
        if (p->vy < -40) p->vy = -40;
        float mv = fmaxf(fabsf(p->vx), fabsf(p->vy)) * dt;
        int steps = (int)ceilf(mv / (h * 0.5f));
        if (steps < 1) steps = 1;
        if (steps > 8) steps = 8;
        float sdt = dt / steps;
        for (int st = 0; st < steps; st++) {
        float nx = p->x + p->vx * sdt;
        if (solid(r, nx, p->y, h)) {
            p->vx = p->kind == CUR_SHARD ? 0 : -p->vx * 0.5f;
        } else p->x = nx;
        float ny = p->y + p->vy * sdt;
        bool was = p->ground;
        if (p->ground) continue;
        if (solid(r, p->x, ny, h)) {
            if (p->vy < 0) {   // the floor: rosaries bounce while they come down fast enough, then lie still
                if (!was) {
                    char name[24];
                    if (p->kind != CUR_SHARD) {
                        snprintf(name, sizeof name, "rosary_bounce_%d", 1 + rand() % 3);
                        sfx_play_name(name, 0.5f, (p->x - hx) * 0.05f);
                    }
                }
                if (p->kind != CUR_SHARD && p->vy < -4) p->vy = -p->vy * 0.5f;
                else { p->vy = 0; p->ground = true; }
                if (p->ground && !was) play(p, "Land", true);
            } else p->vy = 0;
        } else p->y = ny;
        }
        if (p->ground) {
            p->vx *= expf(-(p->kind == CUR_SHARD ? 10.0f : 3.0f) * dt);
            if (fabsf(p->vx) < 0.05f) p->vx = 0;
            // lying still: idle, a gleam now and then (GeoControl)
            if (p->kind != CUR_SHARD && p->anim.done) {
                p->gleam -= dt;
                if (p->gleam <= 0) { play(p, "Gleam", true); p->gleam = frand(1.5f, 2); }
                else play(p, "Idle", false);
            }
        }
        anim_update(&p->anim, dt);
        if (p->t >= PICKUP_DELAY && p->x + h > hx0 && p->x - h < hx1 && p->y + h > hy0 && p->y - h < hy1)
            collect(p, hx);
        else if (p->y < r->gy - 20) p->on = false;   // fell out of the room
    }
}

void currency_draw(void) {
    if (!s_set) return;
    gfx_blend(BLEND_NORMAL);
    for (int i = 0; i < MAX_PIECES; i++) {
        Piece *p = &s_p[i];
        if (!p->on) continue;
        float sc = p->kind == CUR_SHARD ? 1.25f : 1.2f;
        sprite_draw(s_set, p->kind == CUR_SHARD ? p->frame : anim_frame(&p->anim), p->x, p->y, sc, sc, 1, NULL);
    }
}

float currency_shown(int *geo, int *shards) {
    *geo = pd_int("geo");
    *shards = pd_int("ShellShards");
    return s_shown_t;
}

int currency_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_PIECES; i++) n += s_p[i].on;
    return n;
}

void currency_debug(char *out, int n) {
    out[0] = 0;
    for (int i = 0; i < MAX_PIECES; i++)
        if (s_p[i].on) {
            snprintf(out, n, " c0=%.1f,%.1f k=%d f=%d g=%d", s_p[i].x, s_p[i].y, s_p[i].kind,
                     s_p[i].kind == CUR_SHARD ? s_p[i].frame : anim_frame(&s_p[i].anim), s_p[i].ground);
            return;
        }
}
