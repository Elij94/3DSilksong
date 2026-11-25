#include <stdlib.h>
#include <stdio.h>
#include "objects.h"

#define PAR_D 38.1f   // parallax = d / (d + z), as for the scenery layers

static Objects *s_sort_os;
static int cmp_far_to_near(const void *a, const void *b) {
    float za = s_sort_os->o[*(const s16 *)a].z, zb = s_sort_os->o[*(const s16 *)b].z;
    if (za != zb) return za < zb ? 1 : -1;
    return *(const s16 *)a - *(const s16 *)b;
}

// the sprite pages of every object's current animation, inflated now (while the screen is black) rather than when
// the camera first reaches it
void objects_preload(Objects *os) {
    for (int i = 0; i < os->n; i++) {
        Anim *a = &os->o[i].anim;
        if (a->set && a->clip >= 0 && a->clip < a->set->nclips) sprite_preload_clip(a->set, a->set->clips[a->clip].name);
    }
}

void objects_load(Objects *os, const char *scene) {
    memset(os, 0, sizeof *os);
    size_t size;
    u8 *d = file_load(data_path("rooms/%s.sso", scene), &size);
    if (!d) return;
    if (size < 8 || memcmp(d, "SSOB", 4)) { free(d); return; }
    int n = rd16(d + 6);
    if (8 + (size_t)n * 244 > size) n = (size - 8) / 244;
    os->o = calloc(n + 1, sizeof(Obj));
    os->order = calloc(n + 1, sizeof(s16));
    os->n = n;
    for (int i = 0; i < n; i++) {
        const u8 *p = d + 8 + i * 244;
        Obj *o = &os->o[i];
        o->flags = rd16(p);
        o->parent = rds16(p + 2);
        o->x = rdf(p + 4); o->y = rdf(p + 8); o->z = rdf(p + 12);
        o->scx = rdf(p + 16); o->scy = rdf(p + 20);
        o->rx = rdf(p + 24); o->ry = rdf(p + 28); o->rw = rdf(p + 32); o->rh = rdf(p + 36);
        o->hp = (int)rd32(p + 40); o->damage = (int)rd32(p + 44);
        rdstr(o->name, p + 52, 32);
        rdstr(o->sprite_set, p + 84, 32);
        rdstr(o->clip, p + 116, 32);
        o->alive = !(o->flags & OF_INACTIVE);
        o->par = PAR_D / (PAR_D + o->z);
        if (o->par <= 0.05f || o->par > 20) o->par = 1;
        o->anim.clip = -1;
        o->dir = o->scx < 0 ? 1 : -1;   // the art faces left
        if (o->alive && o->sprite_set[0]) {
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
        os->order[i] = i;
    }
    free(d);
    s_sort_os = os;
    qsort(os->order, n, sizeof(s16), cmp_far_to_near);
}

void objects_free(Objects *os) {
    for (int i = 0; i < os->n; i++) sprites_put(os->o[i].anim.set);
    free(os->o);
    free(os->order);
    memset(os, 0, sizeof *os);
}

bool obj_alive(const Objects *os, int i) {
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
    }
}

void objects_draw(Objects *os, float zmin, float zmax) {
    C2D_ImageTint white;
    C2D_PlainImageTint(&white, C2D_Color32(255, 255, 255, 255), 0.8f);
    gfx_blend(BLEND_NORMAL);
    for (int k = 0; k < os->n; k++) {
        int i = os->order[k];
        Obj *o = &os->o[i];
        if (o->z <= zmin || o->z > zmax || !o->anim.set || !obj_alive(os, i)) continue;
        // gfx_draw_world scales positions and sizes around the camera by the parallax
        sprite_draw(o->anim.set, anim_frame(&o->anim), o->x, o->y, o->scx, o->scy, o->par, o->flash > 0 ? &white : NULL);
    }
}

// enemies need their art (invisible ones are trigger helpers); breakables don't
static bool hittable(const Obj *o) {
    if (o->dead || o->rw <= 0 || o->rh <= 0) return false;
    if (o->flags & OF_ENEMY) return o->anim.set != NULL;
    return (o->flags & OF_BREAKABLE) != 0;
}

void obj_move(Objects *os, int i, float dx, float dy) {
    Obj *o = &os->o[i];
    o->x += dx; o->y += dy; o->rx += dx; o->ry += dy;
    for (int j = 0; j < os->n; j++)
        if (os->o[j].parent == i && j != i) obj_move(os, j, dx, dy);
}

int objects_strike(Objects *os, float x0, float y0, float x1, float y1, int dmg, float from_x) {
    int hits = 0;
    for (int i = 0; i < os->n; i++) {
        Obj *o = &os->o[i];
        if (!hittable(o) || o->flash > 0.12f || !obj_alive(os, i)) continue;
        if (!rects_overlap(x0, y0, x1 - x0, y1 - y0, o->rx, o->ry, o->rw, o->rh)) continue;
        hits++;
        o->flash = 0.2f;
        if (o->flags & OF_ENEMY) {
            o->hp -= dmg;
            o->kb = 0.15f;
            o->vx = (o->rx + o->rw * 0.5f < from_x ? -1 : 1) * 6.0f;
            if (o->hp <= 0) enemy_kill(os, i, from_x);
        } else o->alive = false;   // a breakable
    }
    return hits;
}

int objects_touch_damage(const Objects *os, float x0, float y0, float x1, float y1, float *cx) {
    for (int i = 0; i < os->n; i++) {
        const Obj *o = &os->o[i];
        if (!(o->flags & OF_DAMAGE) || o->damage <= 0 || o->rw <= 0 || o->dead || !obj_alive(os, i)) continue;
        if (!(o->flags & OF_ENEMY) && o->damage > 0 && o->rw * o->rh > 400) continue;   // giant triggers: not contact
        if (rects_overlap(x0, y0, x1 - x0, y1 - y0, o->rx, o->ry, o->rw, o->rh)) {
            if (cx) *cx = o->rx + o->rw * 0.5f;
            return o->damage;
        }
    }
    return 0;
}
