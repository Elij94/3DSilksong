#pragma once
// A room: rooms/<Scene>.ssr (collisions, gates, triggers, scenery layers) and its tiles in rooms/<Scene>.ssl,
// streamed into a texture cache as the camera shows them.
#include <stdio.h>
#include "common.h"

typedef struct {
    char name[17], target[33], entry[17];
    float x, y, w, h;
    float edx, edy;
    u8 enter_left, enter_right;
} Gate;

enum { TRIG_RESPAWN = 1, TRIG_HAZARD = 2, TRIG_CAMLOCK = 3 };

typedef struct {
    u8 kind;
    float x, y, w, h;
    float extra[4];
    char name[33];
} Trigger;

typedef struct {
    u16 tx, ty;
    u32 off, size;
    s16 slot;              // texture cache slot, -1 if not loaded
} RTile;

typedef struct {
    char name[9];
    float z, par, ox, oy, ppu;
    u16 tile, ntx, nty, ntiles;
    u8 fmt, blend;
    float tsize;           // a tile's side in units
    u8 down;               // tiles shrunk by this factor when loaded (1, 2, 4): far layers need fewer pixels
    RTile *tiles;
    s16 *lut;              // ntx * nty -> tile index, -1 = empty
} Layer;

typedef struct {
    char scene[48];
    int cpu;               // cells per unit
    float gx, gy;
    int gw, gh, rowbytes;
    u8 *grid;
    float spawn_x, spawn_y;
    int ngates, ntrig, nlayers;
    Gate *gates;
    Trigger *trig;
    Layer *layers;
    float bx0, by0, bx1, by1;   // the room's extent, for the camera
    FILE *ssl;
} Room;

bool room_load(Room *r, const char *scene);
void room_free(Room *r);
bool room_solid(const Room *r, float x, float y);
bool room_box_solid(const Room *r, float x0, float y0, float x1, float y1);

// draws layer i; loads the tiles it needs (a few per frame unless 'all')
void room_draw_layer(Room *r, int i, bool all);
// loads a few of the tiles just outside the view, ahead of the camera
void room_prefetch(Room *r);
// loads every tile the view shows now (after a room change)
void room_preload_view(Room *r);
void room_frame_begin(void);
bool room_evict_tile(void);
void room_cache_init(void);
int room_cache_used(void);
