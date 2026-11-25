// The rooms' ambient particles (rooms/<Scene>.spt, textures in particles/textures.sss): dust, swarms, drips, steam,
// light motes... Only the emitters that loop and play on their own are run, and only near the view; the one-off
// effects (splashes, impacts, breaks) wait for the game's scripts. Unity's curves are reduced to start / end values.
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "particles.h"
#include "sprite.h"

#define MAX_PARTS 480
#define PER_EMITTER 48
#define PAR_D 38.1f

typedef struct {
    u16 flags;
    u8 blend;
    s8 shape;
    float x, y, z, rot;
    float duration, rate;
    u16 maxp, burst;
    float burst_t;
    float life[2], speed[2], size[2], srot[2];
    float gravity, radius, angle, box[2];
    float c0[4], c1[4], ce[4];
    float end_size;
    u8 tx, ty;
    int clip;              // in the texture set, -1: none
    float par;
    float acc, cyc;        // spawn accumulator, time in the cycle
    bool burst_done;
    int live;
    bool fx_done;          // an effect: its one cycle is over
} Emitter;

// one-off effects (a hit, a death: fx/<name>.spt, tools/build_fx.py): templates read once, then copies placed
// where they happen; their particles use emitter numbers FX_BASE + slot
#define FX_BASE 20000
#define FX_SLOTS 48
#define FX_TEMPLATES 48
typedef struct { char name[40]; Emitter *em; int n; } FxTemplate;
static FxTemplate s_fxt[FX_TEMPLATES];
static int s_nfxt;
static Emitter s_fx[FX_SLOTS];
static bool s_fx_on[FX_SLOTS];

typedef struct {
    float x, y, vx, vy, age, life, size;
    float c[4];
    s16 em;
    u8 frame;
} Part;

static Emitter *s_em;
static int s_nem;
static Part s_p[MAX_PARTS];
static int s_np;
static SpriteSet *s_tex;

static float frand(void) { return (float)rand() / (float)RAND_MAX; }
static Emitter *em_at(int i) { return i >= FX_BASE ? &s_fx[i - FX_BASE] : &s_em[i]; }
static bool parse(const u8 *p, Emitter *e, bool ambient);
static float range(const float r[2]) { return r[0] + (r[1] - r[0]) * frand(); }

static bool one_off(const char *name) {
    static const char *const W[] = {"burst", "splash", "Splash", "impact", "Impact", "bounce", "break", "Break",
                                    "hit", "Hit", "Spatter", "Bits", "death", "Death", "shatter", "slam", NULL};
    for (int i = 0; W[i]; i++)
        if (strstr(name, W[i])) return true;
    return false;
}

void particles_free(void) {
    free(s_em);
    s_em = NULL;
    s_nem = s_np = 0;
    memset(s_fx_on, 0, sizeof s_fx_on);
}

void particles_load(const char *scene) {
    particles_free();
    if (!s_tex) {
        s_tex = sprites_get("particles/textures.sss");
        if (s_tex) s_tex->pinned = true;
    }
    size_t size;
    u8 *d = file_load(data_path("rooms/%s.spt", scene), &size);
    if (!d) return;
    if (size < 8 || memcmp(d, "SSPT", 4) || !s_tex) { free(d); return; }
    int n = rd16(d + 6);
    if (8 + (size_t)n * 208 > size) n = (size - 8) / 208;
    s_em = calloc(n + 1, sizeof(Emitter));
    for (int i = 0; i < n; i++)
        if (parse(d + 8 + i * 208, &s_em[s_nem], true)) s_nem++;
    free(d);
}

// one emitter of a .spt; ambient: only the ones that loop and play on their own
static bool parse(const u8 *p, Emitter *e, bool ambient) {
    {
        memset(e, 0, sizeof *e);
        e->flags = rd16(p);
        char tex[33], name[33];
        rdstr(tex, p + 144, 32);
        rdstr(name, p + 176, 32);
        // looping, playing on awake, active at load, with a texture, not a one-off effect
        if (ambient && ((e->flags & 5) != 5 || (e->flags & 16) || one_off(name))) return false;
        if (!tex[0]) return false;
        e->blend = p[2];
        e->shape = (s8)p[3];
        e->x = rdf(p + 4); e->y = rdf(p + 8); e->z = rdf(p + 12); e->rot = rdf(p + 16);
        e->duration = rdf(p + 20); e->rate = rdf(p + 24);
        e->maxp = rd16(p + 28); e->burst = rd16(p + 30); e->burst_t = rdf(p + 32);
        for (int k = 0; k < 2; k++) {
            e->life[k] = rdf(p + 36 + 4 * k); e->speed[k] = rdf(p + 44 + 4 * k);
            e->size[k] = rdf(p + 52 + 4 * k); e->srot[k] = rdf(p + 60 + 4 * k);
        }
        e->gravity = rdf(p + 68); e->radius = rdf(p + 72); e->angle = rdf(p + 76);
        e->box[0] = rdf(p + 80); e->box[1] = rdf(p + 84);
        for (int k = 0; k < 4; k++) { e->c0[k] = rdf(p + 88 + 4 * k); e->c1[k] = rdf(p + 104 + 4 * k); e->ce[k] = rdf(p + 120 + 4 * k); }
        e->end_size = rdf(p + 136);
        e->tx = p[140] ? p[140] : 1;
        e->ty = p[141] ? p[141] : 1;
        e->clip = sprite_clip(s_tex, tex);
        if (e->clip < 0 || (e->rate <= 0 && e->burst == 0) || e->life[1] <= 0 || e->size[1] <= 0) return false;
        if (e->duration <= 0) e->duration = ambient ? 5 : 0.5f;
        e->par = PAR_D / (PAR_D + e->z);
        if (e->par <= 0.05f || e->par > 20) e->par = 1;
        return true;
    }
}

static FxTemplate *fx_template(const char *name) {
    for (int i = 0; i < s_nfxt; i++) if (!strcmp(s_fxt[i].name, name)) return &s_fxt[i];
    if (s_nfxt >= FX_TEMPLATES || !s_tex) return NULL;
    FxTemplate *t = &s_fxt[s_nfxt++];
    snprintf(t->name, sizeof t->name, "%s", name);
    size_t size;
    u8 *d = file_load(data_path("fx/%s.spt", name), &size);
    if (!d || size < 8 || memcmp(d, "SSPT", 4)) { free(d); return t; }
    int n = rd16(d + 6);
    if (8 + (size_t)n * 208 > size) n = (size - 8) / 208;
    t->em = calloc(n + 1, sizeof(Emitter));
    for (int i = 0; i < n; i++)
        if (parse(d + 8 + i * 208, &t->em[t->n], false)) t->n++;
    free(d);
    return t;
}

void particles_fx(const char *name, float x, float y, float z, float angle) {
    if (!s_tex) {
        s_tex = sprites_get("particles/textures.sss");
        if (s_tex) s_tex->pinned = true;
    }
    FxTemplate *t = fx_template(name);
    if (!t) return;
    float ca = cosf(angle * (float)M_PI / 180), sa = sinf(angle * (float)M_PI / 180);
    for (int i = 0; i < t->n; i++) {
        int k = 0;
        while (k < FX_SLOTS && s_fx_on[k]) k++;
        if (k == FX_SLOTS) return;
        Emitter *e = &s_fx[k];
        *e = t->em[i];
        float ox = e->x, oy = e->y;              // the prefab's own offsets, turned with the effect
        e->x = x + ox * ca - oy * sa;
        e->y = y + ox * sa + oy * ca;
        e->rot += angle;
        e->z += z;
        e->par = PAR_D / (PAR_D + e->z);
        if (e->par <= 0.05f || e->par > 20) e->par = 1;
        e->live = 0;
        e->cyc = e->acc = 0;
        e->burst_done = e->fx_done = false;
        s_fx_on[k] = true;
    }
}

static bool parse(const u8 *p, Emitter *e, bool ambient);

static void spawn(int ei) {
    if (s_np >= MAX_PARTS) return;
    Emitter *e = em_at(ei);
    if (e->live >= PER_EMITTER || (e->maxp && e->live >= e->maxp)) return;
    Part *q = &s_p[s_np++];
    e->live++;
    float px = 0, py = 0, dirx, diry;
    float base = (e->rot + 90.0f) * (float)M_PI / 180.0f;   // the emitter's up, turned by its rotation
    switch (e->shape) {
    case 5: case 17:   // box, rectangle
        px = (frand() - 0.5f) * e->box[0];
        py = (frand() - 0.5f) * e->box[1];
        break;
    case 12:           // edge
        px = (frand() * 2 - 1) * e->radius;
        break;
    case -1:
        break;
    default: {         // sphere, circle, cone: a point in the disc
        float a = frand() * 2 * (float)M_PI, r = e->radius * sqrtf(frand());
        px = cosf(a) * r;
        py = sinf(a) * r;
    }
    }
    if (e->shape == 4 || e->shape == 12 || e->shape == 5) {   // cone, edge, box: around the emitter's direction
        float a = base + (frand() * 2 - 1) * e->angle * (float)M_PI / 180.0f;
        dirx = cosf(a); diry = sinf(a);
    } else {
        float a = frand() * 2 * (float)M_PI;
        dirx = cosf(a); diry = sinf(a);
    }
    float sp = range(e->speed);
    q->x = e->x + px;
    q->y = e->y + py;
    q->vx = dirx * sp;
    q->vy = diry * sp;
    q->age = 0;
    q->life = range(e->life);
    q->size = range(e->size);
    float t = frand();
    for (int k = 0; k < 4; k++) q->c[k] = e->c0[k] + (e->c1[k] - e->c0[k]) * t;
    q->em = ei;
    q->frame = (u8)(rand() % (e->tx * e->ty));
}

static bool near_view(const Emitter *e, float margin) {
    const View *v = &g_view;
    float hw = v->hw / e->par + margin, hh = v->hh / e->par + margin;
    return fabsf(e->x - v->cx) < hw && fabsf(e->y - v->cy) < hh;
}

void particles_update(float dt) {
    // age and move
    for (int i = 0; i < s_np;) {
        Part *q = &s_p[i];
        q->age += dt;
        if (q->age >= q->life) {
            em_at(q->em)->live--;
            s_p[i] = s_p[--s_np];
            continue;
        }
        Emitter *e = em_at(q->em);
        q->vy -= e->gravity * 9.81f * dt;
        q->x += q->vx * dt;
        q->y += q->vy * dt;
        i++;
    }
    // emit, near the view only
    for (int ei = 0; ei < s_nem; ei++) {
        Emitter *e = &s_em[ei];
        if (!near_view(e, 4)) continue;
        e->cyc += dt;
        if (e->cyc >= e->duration) { e->cyc -= e->duration; e->burst_done = false; }
        if (e->burst && !e->burst_done && e->cyc >= e->burst_t) {
            e->burst_done = true;
            for (int k = 0; k < e->burst && k < 16; k++) spawn(ei);
        }
        e->acc += e->rate * dt;
        if (e->acc > 8) e->acc = 8;
        while (e->acc >= 1) { e->acc -= 1; spawn(ei); }
    }
    // effects: one cycle, then gone once their particles are
    for (int k = 0; k < FX_SLOTS; k++) {
        if (!s_fx_on[k]) continue;
        Emitter *e = &s_fx[k];
        int ei = FX_BASE + k;
        if (!e->fx_done) {
            if (e->burst && !e->burst_done && e->cyc >= e->burst_t) {
                e->burst_done = true;
                for (int j = 0; j < e->burst && j < 24; j++) spawn(ei);
            }
            e->acc += e->rate * dt;
            if (e->acc > 8) e->acc = 8;
            while (e->acc >= 1) { e->acc -= 1; spawn(ei); }
            e->cyc += dt;
            if (e->cyc >= e->duration) e->fx_done = true;
        }
        if (e->fx_done && e->live <= 0) s_fx_on[k] = false;
    }
}

void particles_draw(float zmin, float zmax) {
    if (!s_np || !s_tex) return;
    C2D_ImageTint tint;
    C2D_SetTintMode(C2D_TintMult);   // texture x colour
    for (int i = 0; i < s_np; i++) {
        Part *q = &s_p[i];
        Emitter *e = em_at(q->em);
        if (e->z <= zmin || e->z > zmax) continue;
        const SClip *c = &s_tex->clips[e->clip];
        if (!c->n) continue;
        int f = rds16((const u8 *)&c->frames[0]);
        if (f < 0 || f >= s_tex->nframes) continue;
        const SFrame *fr = &s_tex->frames[f];
        float t = q->age / q->life;
        float sz = q->size * (1 + (e->end_size - 1) * t) * 0.5f;
        float col[4];
        for (int k = 0; k < 4; k++) col[k] = q->c[k] * (1 + (e->ce[k] - 1) * t);
        // fade in and out a little, as most of these do
        float fade = fminf(1, fminf(t * 6, (1 - t) * 3));
        u32 rgba = C2D_Color32f(clampf(col[0], 0, 1), clampf(col[1], 0, 1), clampf(col[2], 0, 1),
                                clampf(col[3] * fade, 0, 1));
        C2D_PlainImageTint(&tint, rgba, 1.0f);
        // one frame of the texture sheet
        int fx = q->frame % e->tx, fy = q->frame / e->tx;
        float du = (fr->u1 - fr->u0) / e->tx, dv = (fr->v1 - fr->v0) / e->ty;
        float u0 = fr->u0 + fx * du, v1 = fr->v1 - fy * dv;
        gfx_blend(e->blend);
        sprite_draw_uv(s_tex, fr->page, u0, v1 - dv, u0 + du, v1, q->x - sz, q->y - sz, q->x + sz, q->y + sz, e->par, &tint);
    }
    C2D_SetTintMode(C2D_TintSolid);
    gfx_blend(BLEND_NORMAL);
}

int particles_count(void) { return s_np; }
int particles_emitters(void) { return s_nem; }

void particles_debug(char *out, int n) {
    if (!s_np) { snprintf(out, n, "-"); return; }
    Part *q = &s_p[0];
    Emitter *e = em_at(q->em);
    const SClip *c = s_tex ? &s_tex->clips[e->clip] : NULL;
    int f = c && c->n ? rds16((const u8 *)&c->frames[0]) : -1;
    snprintf(out, n, "p0=%.1f,%.1f sz=%.2f a=%.2f clip=%s f=%d page=%d", q->x, q->y, q->size, q->c[3],
             c ? c->name : "?", f, f >= 0 ? s_tex->frames[f].page : -1);
}
