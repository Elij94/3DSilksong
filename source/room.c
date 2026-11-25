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
    u16 w, h;
    u32 last_used;
    RTile *owner;
} Slot;

static Slot s_slots[MAX_SLOTS];
bool (*room_dyn_solid)(float x0, float y0, float x1, float y1);
static size_t s_budget = 16 << 20;  // bytes of linear memory for tiles, from what is free at start
static size_t s_used;
static int s_loads;                 // tiles inflated this frame
static int s_load_limit = 3;
static u8 *s_scratch;
static size_t s_scratch_size;

static void loader_start(void);

void room_cache_init(void) {
    // ~45% of the linear memory for scenery, the rest for sprites
    s_budget = (size_t)(linearSpaceFree() * 0.45f);
    loader_start();
}

int room_cache_used(void) {
    int n = 0;
    for (int i = 0; i < MAX_SLOTS; i++) n += s_slots[i].owner != NULL;
    return n;
}

static void loader_collect(void);

void room_frame_begin(void) {
    s_loads = 0;
    g_where = "loader_collect";
    loader_collect();
    g_where = "draw";
}

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
    s_used -= tex_bytes(s->w, s->h, s->fmt);
}

static bool slot_create(int i, int w, int h, int fmt) {
    Slot *s = &s_slots[i];
    if (!tex_alloc(&s->tex, w, h, fmt)) return false;
    s->init = true; s->fmt = fmt; s->w = w; s->h = h; s->owner = NULL;
    s_used += tex_bytes(w, h, fmt);
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
static int slot_get(int w, int h, int fmt, bool prefetch) {
    int freei = -1, empty = -1, lru = -1;
    for (int i = 0; i < MAX_SLOTS; i++) {
        Slot *s = &s_slots[i];
        if (!s->init) { if (empty < 0) empty = i; continue; }
        if (!s->owner && s->fmt == fmt && s->w == w && s->h == h) { if (freei < 0) freei = i; }
        else if (s->last_used != g_view.frame && (!prefetch || g_view.frame - s->last_used > 90) &&
                 (lru < 0 || s->last_used < s_slots[lru].last_used)) lru = i;
    }
    if (freei >= 0) return freei;
    size_t need = tex_bytes(w, h, fmt);
    if (empty >= 0 && s_used + need <= s_budget && slot_create(empty, w, h, fmt)) return empty;
    if (lru < 0) return -1;
    if (s_slots[lru].fmt == fmt && s_slots[lru].w == w && s_slots[lru].h == h) {
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
    return slot_create(lru, w, h, fmt) ? lru : -1;
}

static bool tile_load(Room *r, Layer *L, RTile *t, bool prefetch);

// ---------------------------------------------------------------- the loader thread
// Tiles the camera reaches are read and inflated on the New 3DS's spare core (or, on one core, while the game waits
// for the screen), so walking through a room never stalls a frame on zlib. The game thread only copies a finished
// tile into its texture, a memcpy.

#define NJOBS 24
enum { JOB_FREE, JOB_QUEUED, JOB_WORKING, JOB_DONE, JOB_FAILED };
typedef struct {
    RTile *t;
    u32 off, size;
    u16 tile;
    u8 fmt, dx, dy, blend;
    u32 gen;
    u8 *buf;
    size_t cap;
    u8 state;              // atomic: JOB_*
} Job;

static Job s_jobs[NJOBS];
static Thread s_thread;
static LightEvent s_wake;
static LightLock s_flock;      // the room's .ssl file: one reader at a time
static FILE *s_file;
static volatile u32 s_gen;     // which room the jobs are for
static volatile bool s_quit;
static u8 *s_wbuf;             // the loader's read buffer
static size_t s_wbuf_size;

static bool read_tile(FILE *f, u32 off, u32 size, u8 **buf, size_t *bsize) {
    if (size > *bsize) {
        free(*buf);
        *bsize = size + 4096;
        *buf = malloc(*bsize);
        if (!*buf) { *bsize = 0; return false; }
    }
    LightLock_Lock(&s_flock);
    bool ok = f && !fseek(f, off, SEEK_SET) && fread(*buf, 1, size, f) == size;
    LightLock_Unlock(&s_flock);
    return ok;
}

static void loader_main(void *arg) {
    (void)arg;
    while (!s_quit) {
        LightEvent_Wait(&s_wake);
        for (bool any = true; any && !s_quit;) {
            any = false;
            for (int i = 0; i < NJOBS; i++) {
                Job *j = &s_jobs[i];
                // claim it (the game thread may take it back at the same time, when a room goes away)
                u8 q = JOB_QUEUED;
                if (!__atomic_compare_exchange_n(&j->state, &q, JOB_WORKING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                    continue;
                any = true;
                bool ok = j->gen == s_gen && read_tile(s_file, j->off, j->size, &s_wbuf, &s_wbuf_size) &&
                          tex_inflate_scaled2(s_wbuf, j->size, j->tile, j->tile, j->fmt, j->dx, j->dy, j->buf);
                if (ok && j->blend == BLEND_MULTIPLY) tex_premultiply(j->buf, j->tile / j->dx, j->tile / j->dy, j->fmt);
                __atomic_store_n(&j->state, ok ? JOB_DONE : JOB_FAILED, __ATOMIC_RELEASE);
            }
        }
    }
}

static void loader_start(void) {
    LightEvent_Init(&s_wake, RESET_ONESHOT);
    LightLock_Init(&s_flock);
    FILE *t = fopen("sdmc:/3ds/3dsilksong/noloader", "r");   // testing: tiles on the game thread
    if (t) { fclose(t); return; }
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    if (prio < 0x3F) prio++;
    // the New 3DS's fourth core (2) is the game's to use; else the game's own core, below the game thread
    s_thread = threadCreate(loader_main, NULL, 16 * 1024, prio, 2, false);
    if (!s_thread) s_thread = threadCreate(loader_main, NULL, 16 * 1024, prio, -2, false);
}

void room_cache_exit(void) {
    if (!s_thread) return;
    s_quit = true;
    LightEvent_Signal(&s_wake);
    threadJoin(s_thread, U64_MAX);
    threadFree(s_thread);
    s_thread = NULL;
}

// waits for the job being worked on, then forgets them all (the room is going away)
static void loader_drain(void) {
    s_gen++;
    for (int i = 0; i < NJOBS; i++) {
        Job *j = &s_jobs[i];
        for (;;) {
            u8 q = JOB_QUEUED;
            if (__atomic_compare_exchange_n(&j->state, &q, JOB_FREE, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) break;
            if (q == JOB_WORKING) { svcSleepThread(200000); continue; }
            break;   // free, done or failed: the loader is not on it
        }
        if (j->t) j->t->pending = 0;
        j->t = NULL;
        __atomic_store_n(&j->state, JOB_FREE, __ATOMIC_RELEASE);
    }
}

// asks the loader for a tile; false when every job is taken
static bool tile_request(Room *r, Layer *L, RTile *t) {
    if (t->slot >= 0 || t->pending || t->size == 0 || !r->ssl) return true;
    if (!s_thread) return tile_load(r, L, t, false);
    for (int i = 0; i < NJOBS; i++) {
        Job *j = &s_jobs[i];
        if (j->state != JOB_FREE) continue;
        size_t need = tex_bytes(L->tile / L->dx, L->tile / L->dy, L->fmt);
        if (need > j->cap) {
            free(j->buf);
            j->buf = malloc(need);
            j->cap = j->buf ? need : 0;
            if (!j->buf) return false;
        }
        j->t = t; j->off = t->off; j->size = t->size;
        j->tile = L->tile; j->fmt = L->fmt; j->dx = L->dx; j->dy = L->dy; j->blend = L->blend;
        j->gen = s_gen;
        t->pending = 1;
        __atomic_store_n(&j->state, JOB_QUEUED, __ATOMIC_RELEASE);   // after the fields above
        LightEvent_Signal(&s_wake);
        return true;
    }
    return false;
}

// the tiles the loader finished: into textures
static void loader_collect(void) {
    for (int i = 0; i < NJOBS; i++) {
        Job *j = &s_jobs[i];
        u8 st = __atomic_load_n(&j->state, __ATOMIC_ACQUIRE);   // the tile's bytes are there before the state
        if (st != JOB_DONE && st != JOB_FAILED) continue;
        RTile *t = j->t;
        if (j->gen == s_gen && t) {
            t->pending = 0;
            if (st == JOB_FAILED) t->size = 0;
            else if (t->slot < 0) {
                int w = j->tile / j->dx, h = j->tile / j->dy;
                int si = slot_get(w, h, j->fmt, false);
                if (si >= 0) {
                    Slot *s = &s_slots[si];
                    memcpy(s->tex.data, j->buf, tex_bytes(w, h, j->fmt));
                    C3D_TexFlush(&s->tex);
                    s->owner = t;
                    s->last_used = g_view.frame;
                    t->slot = si;
                }
            }
        }
        __atomic_store_n(&j->state, JOB_FREE, __ATOMIC_RELEASE);
    }
}

static bool tile_load(Room *r, Layer *L, RTile *t, bool prefetch) {
    if (t->slot >= 0) return true;
    if (!r->ssl || t->size == 0) return false;
    int w = L->tile / L->dx, h = L->tile / L->dy;
    int si = slot_get(w, h, L->fmt, prefetch);
    if (si < 0) return false;
    Slot *s = &s_slots[si];
    if (!read_tile(r->ssl, t->off, t->size, &s_scratch, &s_scratch_size) ||
        !tex_inflate_scaled2(s_scratch, t->size, L->tile, L->tile, L->fmt, L->dx, L->dy, s->tex.data)) {
        t->size = 0;   // broken: don't try again
        return false;
    }
    if (L->blend == BLEND_MULTIPLY) tex_premultiply(s->tex.data, w, h, L->fmt);
    C3D_TexFlush(&s->tex);
    s->owner = t;
    s->last_used = g_view.frame;
    t->slot = si;
    s_loads++;
    return true;
}

// ---------------------------------------------------------------- loading

void room_free(Room *r) {
    loader_drain();
    LightLock_Lock(&s_flock);
    s_file = NULL;
    LightLock_Unlock(&s_flock);
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
        // the screen shows g_view.sx * par pixels per unit of this layer across and g_view.sy * par down (twice as
        // many across in the 800 wide mode): keep about that much of its ppu each way
        L->dx = L->dy = 1;
        while (L->dx < 4 && L->tile / (L->dx * 2) >= 32 && L->ppu / (L->dx * 2) >= 0.75f * g_view.sx * L->par)
            L->dx *= 2;
        while (L->dy < 4 && L->tile / (L->dy * 2) >= 32 && L->ppu / (L->dy * 2) >= 0.75f * g_view.sy * L->par)
            L->dy *= 2;
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
    LightLock_Lock(&s_flock);
    s_file = r->ssl;          // the loader thread reads tiles from it
    LightLock_Unlock(&s_flock);

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
    if (room_dyn_solid && room_dyn_solid(x0, y0, x1, y1)) return true;
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
    // Tile edges in whole screen pixels, each computed once from its index: neighbours share the exact same edge,
    // so there is no gap and no overlap (an overlap shows as a bright line on additive layers, a dark one on
    // multiply layers).
    const View *v = &g_view;
    float k = L->par, T = L->tsize;
#define EX(tx) floorf(v->sw * 0.5f + (L->ox + (tx) * T - v->cx) * k * v->sx + 0.5f)
#define EY(ty) floorf(v->sh * 0.5f - (L->oy - (ty) * T - v->cy) * k * v->sy + 0.5f)
    for (int ty = ty0; ty <= ty1; ty++) {
        float top = EY(ty), bot = EY(ty + 1);
        for (int tx = tx0; tx <= tx1; tx++) {
            int ki = L->lut[ty * L->ntx + tx];
            if (ki < 0) continue;
            RTile *t = &L->tiles[ki];
            if (t->slot < 0) { if (all) tile_load(r, L, t, false); else tile_request(r, L, t); }
            if (t->slot < 0) continue;
            Slot *s = &s_slots[t->slot];
            s->last_used = g_view.frame;
            gfx_draw_screen(&s->tex, 0, 0, 1, 1, EX(tx), top, EX(tx + 1), bot, NULL);
        }
    }
#undef EX
#undef EY
}

void room_prefetch(Room *r) {
    // what the view shows first (it may be missing a tile), then a ring of 2 tiles around it, nearest layers first
    for (int ring = 0; ring <= 2; ring += 2)
        for (int li = r->nlayers - 1; li >= 0; li--) {
            Layer *L = &r->layers[li];
            int tx0, tx1, ty0, ty1;
            layer_range(L, ring, &tx0, &tx1, &ty0, &ty1);
            for (int ty = ty0; ty <= ty1; ty++)
                for (int tx = tx0; tx <= tx1; tx++) {
                    int k = L->lut[ty * L->ntx + tx];
                    if (k >= 0 && L->tiles[k].slot < 0 && !tile_request(r, L, &L->tiles[k])) return;
                }
        }
}

void room_preload_view(Room *r) {
    for (int li = 0; li < r->nlayers; li++) {
        Layer *L = &r->layers[li];
        int tx0, tx1, ty0, ty1;
        layer_range(L, 1, &tx0, &tx1, &ty0, &ty1);   // and a tile around it: the first steps load nothing
        for (int ty = ty0; ty <= ty1; ty++)
            for (int tx = tx0; tx <= tx1; tx++) {
                int k = L->lut[ty * L->ntx + tx];
                if (k >= 0) tile_load(r, L, &L->tiles[k], true);
            }
    }
}
