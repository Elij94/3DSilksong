#include <stdlib.h>
#include <stdio.h>
#include "objects.h"
#include "fsm.h"
#include "fx.h"
#include "audio.h"

void (*objects_freeze)(float down, float hold, float up);

#define PAR_D 38.1f   // parallax = d / (d + z), as for the scenery layers

static Objects *s_sort_os;
static int cmp_far_to_near(const void *a, const void *b) {
    float za = s_sort_os->o[*(const s16 *)a].z, zb = s_sort_os->o[*(const s16 *)b].z;
    if (za != zb) return za < zb ? 1 : -1;
    return *(const s16 *)a - *(const s16 *)b;
}

// the sprite pages of every object's current animation, inflated now (while the screen is black) rather than when
// the camera first reaches it
bool objects_enemy_in(const Objects *os, float x0, float y0, float x1, float y1, float from_x, float *ex) {
    float best = 1e9f;
    for (int i = 0; i < os->n; i++) {
        const Obj *o = &os->o[i];
        if (!(o->flags & OF_ENEMY) || o->dead || !obj_alive(os, i)) continue;
        if (!rects_overlap(x0, y0, x1 - x0, y1 - y0, o->rx, o->ry, o->rw, o->rh)) continue;
        float cx = o->rx + o->rw * 0.5f;
        if (fabsf(cx - from_x) < fabsf(best - from_x)) best = cx;
    }
    if (best > 1e8f) return false;
    *ex = best;
    return true;
}

void objects_preload(Objects *os) {
    for (int i = 0; i < os->n; i++) {
        Anim *a = &os->o[i].anim;
        if (a->set && a->clip >= 0 && a->clip < a->set->nclips) sprite_preload_clip(a->set, a->set->clips[a->clip].name);
    }
}

#define SPAWN_OBJS 256   // kept past the room's objects for what the scripts spawn (fsm.c)

// one object record of a .sso (244 bytes): at index i, moved by (dx, dy), parent indices from base
static void obj_parse(Obj *o, const u8 *p, int i, int base, float dx, float dy) {
    if (o->anim.set) sprites_put(o->anim.set);
    memset(o, 0, sizeof *o);
    o->flags = rd16(p);
    o->parent = rds16(p + 2);
    if (o->parent >= 0) o->parent += base;
    o->x = rdf(p + 4) + dx; o->y = rdf(p + 8) + dy; o->z = rdf(p + 12);
    o->scx = rdf(p + 16); o->scy = rdf(p + 20);
    o->rx = rdf(p + 24) + dx; o->ry = rdf(p + 28) + dy; o->rw = rdf(p + 32); o->rh = rdf(p + 36);
    o->hp = (int)rd32(p + 40); o->damage = (int)rd32(p + 44);
    rdstr(o->name, p + 52, 32);
    rdstr(o->sprite_set, p + 84, 32);
    rdstr(o->clip, p + 116, 32);
    o->alive = !(o->flags & OF_INACTIVE);
    o->node = -1;
    o->hitp = o->deathp = -1;
    o->flash_t = -1;
    o->recoil_v = 15; o->recoil_dur = 0.15f;
    o->par = PAR_D / (PAR_D + o->z);
    if (o->par <= 0.05f || o->par > 20) o->par = 1;
    o->anim.clip = -1;
    o->dir = o->scx < 0 ? 1 : -1;   // the art faces left
    if (o->sprite_set[0]) {   // also when switched off: a script may switch it on
        char rel[64];
        snprintf(rel, sizeof rel, "sprites/%s.sss", o->sprite_set);
        o->anim.set = sprites_get(rel);
        if (o->anim.set) {
            int c = sprite_clip(o->anim.set, o->clip);
            anim_play(&o->anim, c >= 0 ? c : 0, true);
            // don't let every copy of an animation move in step
            o->anim.t = (float)(i * 37 % 100) / 100.0f;
        }
    }
}

void objects_sorted(Objects *os) {
    for (int i = 0; i < os->n; i++) os->order[i] = i;
    s_sort_os = os;
    qsort(os->order, os->n, sizeof(s16), cmp_far_to_near);
}

void objects_load(Objects *os, const char *scene) {
    memset(os, 0, sizeof *os);
    size_t size;
    u8 *d = file_load(data_path("rooms/%s.sso", scene), &size);
    int n = 0;
    if (d && (size < 8 || memcmp(d, "SSOB", 4))) { free(d); d = NULL; }
    if (d) {
        n = rd16(d + 6);
        if (8 + (size_t)n * 244 > size) n = (size - 8) / 244;
    }
    os->cap = n + SPAWN_OBJS;
    os->o = calloc(os->cap + 1, sizeof(Obj));
    os->order = calloc(os->cap + 1, sizeof(s16));
    os->n = n;
    for (int i = 0; i < n; i++) obj_parse(&os->o[i], d + 8 + i * 244, i, 0, 0, 0);
    free(d);
    objects_sorted(os);
}

bool objects_room(const Objects *os, int n) { return n <= os->cap; }

void objects_put(Objects *os, const u8 *sso, size_t size, int at, float dx, float dy) {
    if (!sso || size < 8 || memcmp(sso, "SSOB", 4)) return;
    int n = rd16(sso + 6);
    if (8 + (size_t)n * 244 > size) n = (size - 8) / 244;
    if (at + n > os->cap) return;
    for (int i = 0; i < n; i++) obj_parse(&os->o[at + i], sso + 8 + i * 244, at + i, at, dx, dy);
    if (at + n > os->n) os->n = at + n;
}

void objects_free(Objects *os) {
    for (int i = 0; i < os->n; i++) sprites_put(os->o[i].anim.set);
    free(os->o);
    free(os->order);
    memset(os, 0, sizeof *os);
}

bool obj_alive(const Objects *os, int i) {
    if (i >= 0 && i < os->n && os->o[i].node >= 0 && !fsm_node_active(os->o[i].node)) return false;
    for (int k = 0; i >= 0 && i < os->n && k < 32; k++) {
        if (!os->o[i].alive) return false;
        i = os->o[i].parent;
    }
    return true;
}

void objects_update(Objects *os, float dt) {
    for (int i = 0; i < os->n; i++) {
        Obj *o = &os->o[i];
        if (!o->alive) continue;
        anim_update(&o->anim, dt);
        if (o->flash > 0) o->flash -= dt;
        if (o->flash_t >= 0) { o->flash_t += dt; if (o->flash_t > 0.12f) o->flash_t = -1; }
        if (o->hit_cd > 0) o->hit_cd -= dt;
    }
}

void objects_draw(Objects *os, float zmin, float zmax) {
    C2D_ImageTint white;
    C2D_PlainImageTint(&white, C2D_Color32(255, 255, 255, 255), 0.8f);
    gfx_blend(BLEND_NORMAL);
    for (int k = 0; k < os->n; k++) {
        int i = os->order[k];
        Obj *o = &os->o[i];
        if (o->z <= zmin || o->z > zmax || !o->anim.set || o->hidden || !obj_alive(os, i)) continue;
        // gfx_draw_world scales positions and sizes around the camera by the parallax
        // the game's hit flash: white at 85 % for 0.07 s, gone 0.05 s later
        const C2D_ImageTint *tint = NULL;
        if (o->flash_t >= 0) {
            float k = o->flash_t < 0.07f ? 0.85f : 0.85f * (1 - (o->flash_t - 0.07f) / 0.05f);
            C2D_PlainImageTint(&white, C2D_Color32(255, 255, 255, 255), clampf(k, 0, 1));
            tint = &white;
        } else if (o->flash > 0) tint = &white;
        sprite_draw(o->anim.set, anim_frame(&o->anim), o->x, o->y, o->scx, o->scy, o->par, tint);
    }
}

// enemies need their art (invisible ones are trigger helpers); breakables don't
static bool hittable(const Obj *o) {
    if (o->dead || o->rw <= 0 || o->rh <= 0 || o->nocoll) return false;
    if (o->flags & OF_ENEMY) return o->anim.set != NULL;
    return (o->flags & OF_BREAKABLE) != 0;
}

void obj_move(Objects *os, int i, float dx, float dy) {
    Obj *o = &os->o[i];
    o->x += dx; o->y += dy; o->rx += dx; o->ry += dy;
    for (int j = 0; j < os->n; j++)
        if (os->o[j].parent == i && j != i) obj_move(os, j, dx, dy);
}

int objects_strike(Objects *os, float x0, float y0, float x1, float y1, int dmg, float from_x, int dir) {
    static const char *const HIT_DIR[] = {"HIT RIGHT", "HIT UP", "HIT LEFT", "HIT DOWN"};
    int hits = 0;
    for (int i = 0; i < os->n; i++) {
        Obj *o = &os->o[i];
        if (!hittable(o) || o->hit_cd > 0 || !obj_alive(os, i)) continue;
        if (!rects_overlap(x0, y0, x1 - x0, y1 - y0, o->rx, o->ry, o->rw, o->rh)) continue;
        hits++;
        o->hit_cd = 0.12f;
        if (o->flags & OF_ENEMY) {
            // as HealthManager.TakeDamage: the damage, the events, the flash, the recoil (Recoil), the effects
            if (!o->invincible) o->hp -= dmg;
            float cx = o->rx + o->rw * 0.5f, cy = o->ry + o->rh * 0.5f;
            fsm_node_send(o->node, "HIT");
            fsm_node_send(o->node, "TOOK DAMAGE");
            if (fx_flash(o->hitp)) o->flash_t = 0;
            if (o->recoil_v > 0 && !(o->eflags & EF_FREEZE) && o->hp > 0) {
                static const float DX[] = {1, 0, -1, 0}, DY[] = {0, 1, 0, -1};
                o->rdx = DX[dir & 3];
                o->rdy = DY[dir & 3];
                o->recoil_t = o->recoil_dur;
                fsm_node_send(o->node, "RECOIL");
                if (!(dir & 1)) fsm_node_send(o->node, "RECOIL HORIZONTAL");
                fsm_node_send(o->node, HIT_DIR[dir & 3]);
            }
            fx_hit(o->hitp, cx, cy, -0.5f, dir * 90.0f);
            if (o->hp <= 0) enemy_kill(os, i, from_x, dir);
        } else {   // a breakable
            o->flash = 0.2f;
            o->alive = false;
        }
    }
    return hits;
}

int objects_touch_damage(const Objects *os, float x0, float y0, float x1, float y1, float *cx) {
    for (int i = 0; i < os->n; i++) {
        const Obj *o = &os->o[i];
        if (!(o->flags & OF_DAMAGE) || o->damage <= 0 || o->rw <= 0 || o->dead || o->nocoll || o->nodamage ||
            !obj_alive(os, i)) continue;
        if (!(o->flags & OF_ENEMY) && o->damage > 0 && o->rw * o->rh > 400) continue;   // giant triggers: not contact
        if (rects_overlap(x0, y0, x1 - x0, y1 - y0, o->rx, o->ry, o->rw, o->rh)) {
            if (cx) *cx = o->rx + o->rw * 0.5f;
            return o->damage;
        }
    }
    return 0;
}
