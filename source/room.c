#include <stdlib.h>
#include <math.h>
#include "room.h"
#include "inflate.h"
#include "tex.h"

// ---------------------------------------------------------------- the tile texture cache

#define MAX_SLOTS 640
typedef struct {
    C3D_Tex tex;
    bool init;
    u8 fmt;
    u16 size;
    u32 last_used;
    RTile *owner;
} Slot;

static Slot s_slots[MAX_SLOTS];
static size_t s_budget = 16 << 20;  // bytes of linear memory for tiles, from what is free at start
static size_t s_used;
static int s_loads;                 // tiles inflated this frame
static int s_load_limit = 3;
static u8 *s_scratch;
static size_t s_scratch_size;

void room_cache_init(void) {
    // ~45% of the linear memory for scenery, the rest for sprites
    s_budget = (size_t)(linearSpaceFree() * 0.45f);
}

int room_cache_used(void) {
    int n = 0;
    for (int i = 0; i < MAX_SLOTS; i++) n += s_slots[i].owner != NULL;
    return n;
}

void room_frame_begin(void) { s_loads = 0; }

static void slot_release(int i) {
    Slot *s = &s_slots[i];
    if (s->owner) s->owner->slot = -1;
    s->owner = NULL;
}

static void slot_delete(int i) {
    Slot *s = &s_slots[i];
    slot_release(i);
    if (!s->init) return;
    C3D_TexDelete(&s->tex);
    s->init = false;
    s_used -= tex_bytes(s->size, s->size, s->fmt);
}

static bool slot_create(int i, int size, int fmt) {
    Slot *s = &s_slots[i];
    if (!tex_alloc(&s->tex, size, size, fmt)) return false;
    s->init = true; s->fmt = fmt; s->size = size; s->owner = NULL;
    s_used += tex_bytes(size, size, fmt);
    C3D_TexSetFilter(&s->tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&s->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    return true;
}

static int slot_lru(void) {
    int k = -1;
    for (int i = 0; i < MAX_SLOTS; i++) {
        Slot *s = &s_slots[i];
        if (s->init && s->last_used != g_view.frame && (k < 0 || s->last_used < s_slots[k].last_used)) k = i;
    }
    return k;
}

bool room_evict_tile(void) {
    int best = slot_lru();
    if (best < 0) return false;
    slot_delete(best);
    return true;
}

// a slot for a tile of this size and format: a free one, a new one under the budget, else the least recently drawn
static int slot_get(int size, int fmt, bool prefetch) {
    int freei = -1, empty = -1, lru = -1;
    for (int i = 0; i < MAX_SLOTS; i++) {
        Slot *s = &s_slots[i];
        if (!s->init) { if (empty < 0) empty = i; continue; }
        if (!s->owner && s->fmt == fmt && s->size == size) { if (freei < 0) freei = i; }
        else if (s->last_used != g_view.frame && (!prefetch || g_view.frame - s->last_used > 90) &&
                 (lru < 0 || s->last_used < s_slots[lru].last_used)) lru = i;
    }
    if (freei >= 0) return freei;
    size_t need = tex_bytes(size, size, fmt);
    if (empty >= 0 && s_used + need <= s_budget && slot_create(empty, size, fmt)) return empty;
    if (lru < 0) return -1;
    if (s_slots[lru].fmt == fmt && s_slots[lru].size == size) {
        slot_release(lru);
        return lru;
    }
    // another size: free least recently drawn slots until this one fits the budget
    slot_delete(lru);
    while (s_used + need > s_budget) {
        int k = slot_lru();
        if (k < 0) break;
        slot_delete(k);
    }
    return slot_create(lru, size, fmt) ? lru : -1;
}

static bool tile_load(Room *r, Layer *L, RTile *t, bool prefetch) {
    if (t->slot >= 0) return true;
    if (!r->ssl || t->size == 0) return false;
    int size = L->tile / L->down;
    int si = slot_get(size, L->fmt, prefetch);
    if (si < 0) return false;
    if (t->size > s_scratch_size) {
        free(s_scratch);
        s_scratch_size = t->size + 4096;
        s_scratch = malloc(s_scratch_size);
        if (!s_scratch) { s_scratch_size = 0; return false; }
    }
    Slot *s = &s_slots[si];
    if (fseek(r->ssl, t->off, SEEK_SET) || fread(s_scratch, 1, t->size, r->ssl) != t->size ||
        !tex_inflate_scaled(s_scratch, t->size, L->tile, L->tile, L->fmt, L->down, s->tex.data)) {
        t->size = 0;   // broken: don't try again
        return false;
    }
    C3D_TexFlush(&s->tex);
    s->owner = t;
    s->last_used = g_view.frame;
    t->slot = si;
    s_loads++;
    return true;
}

// ---------------------------------------------------------------- loading

void room_free(Room *r) {
    for (int i = 0; i < MAX_SLOTS; i++)
        if (s_slots[i].owner) slot_release(i);
    for (int i = 0; i < r->nlayers; i++) { free(r->layers[i].tiles); free(r->layers[i].lut); }
    free(r->layers);
    free(r->gates);
    free(r->trig);
    free(r->grid);
    if (r->ssl) fclose(r->ssl);
    memset(r, 0, sizeof *r);
}

bool room_load(Room *r, const char *scene) {
    memset(r, 0, sizeof *r);
    size_t size;
    u8 *d = file_load(data_path("rooms/%s.ssr", scene), &size);
    if (!d) return false;
    if (size < 36 || memcmp(d, "SSRM", 4)) { free(d); return false; }
    snprintf(r->scene, sizeof r->scene, "%s", scene);
    r->cpu = rd16(d + 6);
    r->gx = rdf(d + 8);
    r->gy = rdf(d + 12);
    r->gw = rd16(d + 16);
    r->gh = rd16(d + 18);
    r->spawn_x = rdf(d + 20);
    r->spawn_y = rdf(d + 24);
    r->ngates = rd16(d + 28);
    r->ntrig = rd16(d + 30);
    r->nlayers = rd16(d + 32);
    if (!r->cpu) r->cpu = 4;
    const u8 *p = d + 36, *end = d + size;
    r->rowbytes = (r->gw + 7) / 8;
    size_t gsz = (size_t)r->rowbytes * r->gh;
    if (p + gsz > end) goto bad;
    r->grid = malloc(gsz ? gsz : 1);
    memcpy(r->grid, p, gsz);
    p += gsz;

    r->gates = calloc(r->ngates + 1, sizeof(Gate));
    for (int i = 0; i < r->ngates; i++, p += 92) {
        if (p + 92 > end) goto bad;
        Gate *g = &r->gates[i];
        rdstr(g->name, p, 16);
        rdstr(g->target, p + 16, 32);
        rdstr(g->entry, p + 48, 16);
        g->x = rdf(p + 64); g->y = rdf(p + 68); g->w = rdf(p + 72); g->h = rdf(p + 76);
        g->edx = rdf(p + 80); g->edy = rdf(p + 84);
        g->enter_left = p[88]; g->enter_right = p[89];
    }
    r->trig = calloc(r->ntrig + 1, sizeof(Trigger));
    for (int i = 0; i < r->ntrig; i++, p += 68) {
        if (p + 68 > end) goto bad;
        Trigger *t = &r->trig[i];
        t->kind = p[0];
        t->x = rdf(p + 4); t->y = rdf(p + 8); t->w = rdf(p + 12); t->h = rdf(p + 16);
        for (int k = 0; k < 4; k++) t->extra[k] = rdf(p + 20 + 4 * k);
        rdstr(t->name, p + 36, 32);
    }
    r->layers = calloc(r->nlayers + 1, sizeof(Layer));
    for (int i = 0; i < r->nlayers; i++) {
        if (p + 40 > end) goto bad;
        Layer *L = &r->layers[i];
        rdstr(L->name, p, 8);
        L->z = rdf(p + 8); L->par = rdf(p + 12); L->ox = rdf(p + 16); L->oy = rdf(p + 20); L->ppu = rdf(p + 24);
        L->tile = rd16(p + 28); L->ntx = rd16(p + 30); L->nty = rd16(p + 32); L->ntiles = rd16(p + 34);
        L->fmt = p[36]; L->blend = p[37];
        if (L->par <= 0) L->par = 1;
        if (L->ppu <= 0) L->ppu = 64;
        L->tsize = L->tile / L->ppu;
        // the screen shows g_view.sy * par pixels per unit of this layer: keep about that much of its ppu
        L->down = 1;
        while (L->down < 4 && L->tile / (L->down * 2) >= 32 && L->ppu / (L->down * 2) >= 0.75f * g_view.sy * L->par)
            L->down *= 2;
        p += 40;
        if (p + 12 * L->ntiles > end) goto bad;
        L->tiles = calloc(L->ntiles + 1, sizeof(RTile));
        L->lut = malloc(sizeof(s16) * (L->ntx * L->nty + 1));
        for (int k = 0; k < L->ntx * L->nty; k++) L->lut[k] = -1;
        for (int k = 0; k < L->ntiles; k++, p += 12) {
            RTile *t = &L->tiles[k];
            t->tx = rd16(p); t->ty = rd16(p + 2); t->off = rd32(p + 4); t->size = rd32(p + 8); t->slot = -1;
            if (t->tx < L->ntx && t->ty < L->nty) L->lut[t->ty * L->ntx + t->tx] = k;
        }
    }
    free(d);
    r->ssl = fopen(data_path("rooms/%s.ssl", scene), "rb");

    // the room's extent: the gates sit on its edges; else the collision grid (which has a view's margin)
    float gx1 = r->gx + (float)r->gw / r->cpu, gy1 = r->gy + (float)r->gh / r->cpu;
    r->bx0 = r->gx + 16 < 0 ? r->gx + 16 : 0; r->by0 = r->gy + 9 < 0 ? r->gy + 9 : 0;
    r->bx1 = gx1 - 16; r->by1 = gy1 - 9;
    float rx = -1e9f, ty = -1e9f;
    for (int i = 0; i < r->ngates; i++) {
        Gate *g = &r->gates[i];
        if (!strncmp(g->name, "right", 5) && g->x > rx) rx = g->x;
        if (!strncmp(g->name, "top", 3) && g->y > ty) ty = g->y;
        if (!strncmp(g->name, "left", 4) && g->x + g->w < r->bx0) r->bx0 = g->x + g->w;
        if (!strncmp(g->name, "bot", 3) && g->y + g->h < r->by0) r->by0 = g->y + g->h;
    }
    if (rx > -1e8f) r->bx1 = rx + 1;
    if (ty > -1e8f) r->by1 = ty;
    if (r->bx1 <= r->bx0) r->bx1 = r->bx0 + 1;
    if (r->by1 <= r->by0) r->by1 = r->by0 + 1;
    return true;
bad:
    free(d);
    room_free(r);
    return false;
}

bool room_solid(const Room *r, float x, float y) {
    int cx = (int)floorf((x - r->gx) * r->cpu), cy = (int)floorf((y - r->gy) * r->cpu);
    if (cx < 0 || cy < 0 || cx >= r->gw || cy >= r->gh) return false;
    return r->grid[cy * r->rowbytes + (cx >> 3)] >> (cx & 7) & 1;
}

bool room_box_solid(const Room *r, float x0, float y0, float x1, float y1) {
    int cx0 = (int)floorf((x0 - r->gx) * r->cpu), cx1 = (int)floorf((x1 - r->gx) * r->cpu);
    int cy0 = (int)floorf((y0 - r->gy) * r->cpu), cy1 = (int)floorf((y1 - r->gy) * r->cpu);
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 >= r->gw) cx1 = r->gw - 1;
    if (cy1 >= r->gh) cy1 = r->gh - 1;
    for (int y = cy0; y <= cy1; y++) {
        const u8 *row = r->grid + y * r->rowbytes;
        for (int x = cx0; x <= cx1; x++)
            if (row[x >> 3] >> (x & 7) & 1) return true;
    }
    return false;
}

// ---------------------------------------------------------------- drawing

// tile range of layer L that the view shows, grown by 'margin' tiles
static void layer_range(const Layer *L, int margin, int *tx0, int *tx1, int *ty0, int *ty1) {
    const View *v = &g_view;
    float hw = v->hw / L->par, hh = v->hh / L->par;
    *tx0 = (int)floorf((v->cx - hw - L->ox) / L->tsize) - margin;
    *tx1 = (int)floorf((v->cx + hw - L->ox) / L->tsize) + margin;
    *ty0 = (int)floorf((L->oy - (v->cy + hh)) / L->tsize) - margin;
    *ty1 = (int)floorf((L->oy - (v->cy - hh)) / L->tsize) + margin;
    if (*tx0 < 0) *tx0 = 0;
    if (*ty0 < 0) *ty0 = 0;
    if (*tx1 >= L->ntx) *tx1 = L->ntx - 1;
    if (*ty1 >= L->nty) *ty1 = L->nty - 1;
}

void room_draw_layer(Room *r, int li, bool all) {
    Layer *L = &r->layers[li];
    int tx0, tx1, ty0, ty1;
    layer_range(L, 0, &tx0, &tx1, &ty0, &ty1);
    gfx_blend(L->blend);
    // a hair of overlap so the linear filtering leaves no seams between tiles
    float ov = 0.5f / (g_view.sy * L->par);
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++) {
            int k = L->lut[ty * L->ntx + tx];
            if (k < 0) continue;
            RTile *t = &L->tiles[k];
            if (t->slot < 0 && (all || s_loads < s_load_limit)) tile_load(r, L, t, false);
            if (t->slot < 0) continue;
            Slot *s = &s_slots[t->slot];
            s->last_used = g_view.frame;
            float x0 = L->ox + tx * L->tsize, y1 = L->oy - ty * L->tsize;
            gfx_draw_world(&s->tex, 0, 0, 1, 1, x0 - ov, y1 - L->tsize - ov, x0 + L->tsize + ov, y1 + ov, L->par, NULL);
        }
}

void room_prefetch(Room *r) {
    for (int li = 0; li < r->nlayers && s_loads < s_load_limit; li++) {
        Layer *L = &r->layers[li];
        int tx0, tx1, ty0, ty1;
        layer_range(L, 1, &tx0, &tx1, &ty0, &ty1);
        for (int ty = ty0; ty <= ty1 && s_loads < s_load_limit; ty++)
            for (int tx = tx0; tx <= tx1 && s_loads < s_load_limit; tx++) {
                int k = L->lut[ty * L->ntx + tx];
                if (k >= 0 && L->tiles[k].slot < 0) {
                    // only into free slots or ones not drawn recently, so prefetching never fights the view
                    tile_load(r, L, &L->tiles[k], true);
                }
            }
    }
}

void room_preload_view(Room *r) {
    for (int li = 0; li < r->nlayers; li++) {
        Layer *L = &r->layers[li];
        int tx0, tx1, ty0, ty1;
        layer_range(L, 0, &tx0, &tx1, &ty0, &ty1);
        for (int ty = ty0; ty <= ty1; ty++)
            for (int tx = tx0; tx <= tx1; tx++) {
                int k = L->lut[ty * L->ntx + tx];
                if (k >= 0) tile_load(r, L, &L->tiles[k], false);
            }
    }
}
