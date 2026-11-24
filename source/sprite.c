#include <stdio.h>
#include <stdlib.h>
#include "sprite.h"
#include "inflate.h"
#include "tex.h"

static SpriteSet *s_sets;

static void set_free(SpriteSet *s) {
    for (int i = 0; i < s->npages; i++)
        if (s->pages[i].loaded) C3D_TexDelete(&s->pages[i].tex);
    free(s->pages);
    free(s->frames);
    free(s->clips);
    free(s->blob);
    free(s);
}

static SpriteSet *set_load(const char *rel) {
    size_t size;
    u8 *d = file_load(data_path("%s", rel), &size);
    if (!d) return NULL;
    if (size < 14 || memcmp(d, "SSSP", 4)) { free(d); return NULL; }
    SpriteSet *s = calloc(1, sizeof *s);
    s->blob = d;
    snprintf(s->path, sizeof s->path, "%s", rel);
    s->npages = rd16(d + 6);
    s->nframes = rd16(d + 8);
    s->nclips = rd16(d + 10);
    s->pages = calloc(s->npages ? s->npages : 1, sizeof(SPage));
    s->frames = calloc(s->nframes ? s->nframes : 1, sizeof(SFrame));
    s->clips = calloc(s->nclips ? s->nclips : 1, sizeof(SClip));
    const u8 *p = d + 14, *end = d + size;
    for (int i = 0; i < s->npages; i++, p += 16) {
        s->pages[i].w = rd16(p);
        s->pages[i].h = rd16(p + 2);
        s->pages[i].fmt = p[4];
        s->pages[i].csize = rd32(p + 12);
    }
    for (int i = 0; i < s->nframes; i++, p += 36) {
        SFrame *f = &s->frames[i];
        f->page = rd16(p);
        f->u0 = rdf(p + 4); f->v0 = rdf(p + 8); f->u1 = rdf(p + 12); f->v1 = rdf(p + 16);
        f->x0 = rdf(p + 20); f->y0 = rdf(p + 24); f->x1 = rdf(p + 28); f->y1 = rdf(p + 32);
    }
    for (int i = 0; i < s->nclips; i++) {
        SClip *c = &s->clips[i];
        if (p + 42 > end) break;
        rdstr(c->name, p, 32);
        c->fps = rdf(p + 32);
        c->loop_start = rd16(p + 36);
        c->wrap = p[38];
        c->n = rd16(p + 40);
        c->frames = (const s16 *)(p + 42);
        p += 42 + 2 * c->n;
    }
    for (int i = 0; i < s->npages; i++) {
        s->pages[i].cdata = p;
        p += s->pages[i].csize;
    }
    if (p > end) { set_free(s); return NULL; }
    return s;
}

SpriteSet *sprites_get(const char *rel) {
    for (SpriteSet *s = s_sets; s; s = s->next)
        if (!strcmp(s->path, rel)) { s->refs++; return s; }
    SpriteSet *s = set_load(rel);
    if (!s) return NULL;
    s->refs = 1;
    s->next = s_sets;
    s_sets = s;
    return s;
}

void sprites_put(SpriteSet *s) { if (s && s->refs > 0) s->refs--; }

void sprites_collect(void) {
    SpriteSet **pp = &s_sets;
    while (*pp) {
        SpriteSet *s = *pp;
        if (s->refs <= 0 && !s->pinned) { *pp = s->next; set_free(s); }
        else pp = &s->next;
    }
}

bool sprites_evict_one(void) {
    SPage *best = NULL;
    // the pinned sets (Hornet) last: inflating one of their 1024x1024 pages again is a visible hitch
    for (int pass = 0; pass < 2 && !best; pass++)
        for (SpriteSet *s = s_sets; s; s = s->next) {
            if (s->pinned != (pass == 1)) continue;
            for (int i = 0; i < s->npages; i++) {
                SPage *pg = &s->pages[i];
                if (pg->loaded && pg->last_used != g_view.frame && (!best || pg->last_used < best->last_used)) best = pg;
            }
        }
    if (!best) return false;
    C3D_TexDelete(&best->tex);
    best->loaded = false;
    return true;
}

static bool page_ready(SpriteSet *s, int i) {
    SPage *pg = &s->pages[i];
    pg->last_used = g_view.frame;
    if (pg->loaded) return true;
    if (pg->csize == 0xFFFFFFFF) return false;   // failed before
    // the big pages (Hornet's are 1024x1024) at half size: on screen a sprite gets far fewer pixels than that
    int f = (pg->w >= 512 && pg->h >= 512) ? 2 : 1;
    if (!tex_alloc(&pg->tex, pg->w / f, pg->h / f, pg->fmt)) return false;
    if (!tex_inflate_scaled(pg->cdata, pg->csize, pg->w, pg->h, pg->fmt, f, pg->tex.data)) {
        C3D_TexDelete(&pg->tex);
        pg->csize = 0xFFFFFFFF;
        return false;
    }
    C3D_TexFlush(&pg->tex);
    C3D_TexSetFilter(&pg->tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&pg->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    pg->loaded = true;
    return true;
}

void sprite_preload_clip(SpriteSet *s, const char *name) {
    int c = sprite_clip(s, name);
    if (c < 0) return;
    for (int i = 0; i < s->clips[c].n; i++) {
        int f = rds16((const u8 *)&s->clips[c].frames[i]);
        if (f >= 0 && f < s->nframes && s->frames[f].page < s->npages) page_ready(s, s->frames[f].page);
    }
}

int sprite_clip(const SpriteSet *s, const char *name) {
    if (!s) return -1;
    for (int i = 0; i < s->nclips; i++)
        if (!strcmp(s->clips[i].name, name)) return i;
    return -1;
}

void sprite_draw(SpriteSet *s, int f, float x, float y, float scx, float scy, float par, const C2D_ImageTint *tint) {
    if (!s || f < 0 || f >= s->nframes) return;
    const SFrame *fr = &s->frames[f];
    if (fr->page >= s->npages || !page_ready(s, fr->page)) return;
    float x0 = x + fr->x0 * scx, x1 = x + fr->x1 * scx;
    float y0 = y + fr->y0 * scy, y1 = y + fr->y1 * scy;
    float u0 = fr->u0, u1 = fr->u1, v0 = fr->v0, v1 = fr->v1;
    if (x1 < x0) { float t = x0; x0 = x1; x1 = t; t = u0; u0 = u1; u1 = t; }
    if (y1 < y0) { float t = y0; y0 = y1; y1 = t; t = v0; v0 = v1; v1 = t; }
    gfx_draw_world(&s->pages[fr->page].tex, u0, v0, u1, v1, x0, y0, x1, y1, par, tint);
}

void anim_play(Anim *a, int clip, bool restart) {
    if (clip < 0) return;
    if (a->clip == clip && !restart) return;
    a->clip = clip;
    a->t = 0;
    a->done = false;
}

bool anim_play_name(Anim *a, const char *name, bool restart) {
    int c = sprite_clip(a->set, name);
    if (c < 0) return false;
    anim_play(a, c, restart);
    return true;
}

void anim_update(Anim *a, float dt) {
    if (!a->set || a->clip < 0 || a->clip >= a->set->nclips) return;
    a->t += dt;
    const SClip *c = &a->set->clips[a->clip];
    if ((c->wrap == 2 || c->wrap == 6) && a->t * c->fps >= c->n) a->done = true;
}

int anim_frame(const Anim *a) {
    if (!a->set || a->clip < 0 || a->clip >= a->set->nclips) return -1;
    const SClip *c = &a->set->clips[a->clip];
    if (!c->n) return -1;
    int i = (int)(a->t * c->fps), n = c->n;
    switch (c->wrap) {
    case 1: {   // loop section
        int ls = c->loop_start < n ? c->loop_start : 0;
        if (i >= n) i = ls + (i - ls) % (n - ls);
        break;
    }
    case 2: if (i >= n) i = n - 1; break;
    case 3: {   // ping-pong
        int per = n > 1 ? 2 * n - 2 : 1;
        i %= per;
        if (i >= n) i = per - i;
        break;
    }
    case 6: i = 0; break;
    default: i %= n; break;
    }
    return rds16((const u8 *)&c->frames[i]);
}
