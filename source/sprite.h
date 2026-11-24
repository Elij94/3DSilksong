#pragma once
// tk2d sprite sets (sprites/<name>.sss, ui/<atlas>.sss, particles/textures.sss) and their animations.
// The file stays in memory compressed; a texture page is inflated into the GPU only when a frame on it is drawn,
// and the least recently drawn pages are dropped when the linear memory runs out.
#include "common.h"

typedef struct {
    u16 page;
    float u0, v0, u1, v1;
    float x0, y0, x1, y1;   // the quad in units, relative to the sprite's origin
} SFrame;

typedef struct {
    char name[33];
    float fps;
    u16 loop_start;
    u8 wrap;                // tk2d: 0 loop, 1 loop section, 2 once, 3 ping-pong, 4/5 random, 6 single
    u16 n;
    const s16 *frames;      // into the file (little-endian s16, read with rds16)
} SClip;

typedef struct {
    u16 w, h;
    u8 fmt;
    u32 csize;
    const u8 *cdata;
    C3D_Tex tex;
    bool loaded;
    u32 last_used;
} SPage;

typedef struct SpriteSet {
    char path[96];
    u8 *blob;
    int npages, nframes, nclips;
    SPage *pages;
    SFrame *frames;
    SClip *clips;
    int refs;               // rooms / objects holding it; 0 = can be freed
    bool pinned;            // never freed (the hero)
    struct SpriteSet *next;
} SpriteSet;

// path relative to romfs:/ ("sprites/knight.sss"). Cached: the same set comes back, with refs + 1.
SpriteSet *sprites_get(const char *rel);
void sprites_put(SpriteSet *s);
// frees the sets nobody holds (between rooms)
void sprites_collect(void);
// drops the least recently drawn page that was not drawn this frame; false if there is none
bool sprites_evict_one(void);

int sprite_clip(const SpriteSet *s, const char *name);   // -1 if missing
// inflates the pages a clip's frames sit on now (instead of at its first drawing)
void sprite_preload_clip(SpriteSet *s, const char *name);
// draws frame f with its origin at world (x, y), scaled (negative = flipped)
void sprite_draw(SpriteSet *s, int f, float x, float y, float scx, float scy, float par, const C2D_ImageTint *tint);

// a region (u, v from the bottom) of one page on a world rectangle
void sprite_draw_uv(SpriteSet *s, int page, float u0, float v0, float u1, float v1, float x0, float y0, float x1,
                    float y1, float par, const C2D_ImageTint *tint);

typedef struct {
    SpriteSet *set;
    int clip;
    float t;
    bool done;
} Anim;

void anim_play(Anim *a, int clip, bool restart);
bool anim_play_name(Anim *a, const char *name, bool restart);
void anim_update(Anim *a, float dt);
int anim_frame(const Anim *a);   // frame index in the set, -1 if none
