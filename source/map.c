#include "map.h"
#include "sprite.h"
#include "fsm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    u8 zone, state;            // MapZone; 0 hidden, 1 rough, 2 full
    char scene[40];
    float x, y, sx, sy;
    int f_init, f_full;        // frames in ui/map.sss, -1
    char also[96];             // mappedIfAllMapped: these scenes, comma-separated
    bool vis, map;             // visited, mapped (cached)
} Piece;

typedef struct { u8 zone; char name[32], pd[40]; } Zone;

static Piece *s_p;
static int s_np;
static Zone s_z[48];
static int s_nz;
static SpriteSet *s_set;          // the shown area's pieces (ui/map_<zone>.sss)
static int s_set_zone = -1;
static bool s_loaded, s_dirty = true;

// the scenes visited and mapped (PlayerData scenesVisited, scenesMapped)
typedef struct { char (*name)[40]; int n, cap; } Names;
static Names s_visited, s_mapped;

static bool names_has(const Names *s, const char *n) {
    for (int i = 0; i < s->n; i++) if (!strcmp(s->name[i], n)) return true;
    return false;
}

static void names_add(Names *s, const char *n) {
    if (!n[0] || names_has(s, n)) return;
    if (s->n == s->cap) {
        int cap = s->cap ? s->cap * 2 : 128;
        void *p = realloc(s->name, sizeof *s->name * cap);
        if (!p) return;
        s->name = p;
        s->cap = cap;
    }
    snprintf(s->name[s->n++], sizeof *s->name, "%s", n);
    s_dirty = true;
}

static int frame_of(const char *clip) {
    int c = s_set ? sprite_clip(s_set, clip) : -1;
    return c >= 0 && s_set->clips[c].n ? rds16((const u8 *)&s_set->clips[c].frames[0]) : -1;
}

void map_init(void) {
    if (s_loaded) return;
    s_loaded = true;
    FILE *f = fopen(data_path("text/map.txt"), "r");
    if (!f) return;
    int cap = 1024;
    s_p = calloc(cap, sizeof(Piece));
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == 'Z' && s_nz < 48) {
            Zone *z = &s_z[s_nz];
            int zi;
            if (sscanf(line, "Z %d %31s %39s", &zi, z->name, z->pd) == 3) { z->zone = (u8)zi; s_nz++; }
        } else if (line[0] == 'P' && s_np < cap) {
            Piece *p = &s_p[s_np];
            int zi, st;
            if (sscanf(line, "P %d %39s %d %f %f %f %f %95s", &zi, p->scene, &st, &p->x, &p->y, &p->sx, &p->sy, p->also) != 8)
                continue;
            if (!strcmp(p->also, "-")) p->also[0] = 0;
            p->zone = (u8)zi; p->state = (u8)st;
            p->f_init = p->f_full = -1;   // read with the area's set
            s_np++;
        }
    }
    fclose(f);
}

void map_clear(void) { s_visited.n = s_mapped.n = 0; s_dirty = true; }

bool map_load_line(const char *line) {
    char n[48];
    if (sscanf(line, "sv %47s", n) == 1) { names_add(&s_visited, n); return true; }
    if (sscanf(line, "sm %47s", n) == 1) { names_add(&s_mapped, n); return true; }
    return false;
}

void map_save(FILE *f) {
    for (int i = 0; i < s_visited.n; i++) fprintf(f, "sv %s\n", s_visited.name[i]);
    for (int i = 0; i < s_mapped.n; i++) fprintf(f, "sm %s\n", s_mapped.name[i]);
}

void map_visit(const char *scene) { names_add(&s_visited, scene); }

static int s_test = -1;   // testing: SD:/3ds/3dsilksong/allmaps gives every map and the quill
static bool test_all(void) {
    if (s_test < 0) { struct stat st; s_test = stat("sdmc:/3ds/3dsilksong/allmaps", &st) == 0; }
    return s_test;
}
static bool has_quill(void) { return test_all() || pd_bool("hasQuill"); }

void map_set_mapped(const char *scene) { if (has_quill()) names_add(&s_mapped, scene); }

static bool zone_unlocked(int zone) {
    for (int i = 0; i < s_nz; i++)
        if (s_z[i].zone == zone && (test_all() || !strcmp(s_z[i].pd, "-") || pd_bool(s_z[i].pd))) return true;
    return false;
}

static bool any_map(void) {
    for (int i = 0; i < s_nz; i++) if (strcmp(s_z[i].pd, "-") && pd_bool(s_z[i].pd)) return true;
    return test_all();
}

bool map_bench_update(void) {   // GameMap.UpdateGameMap (CanUpdateMap: the quill and a map)
    if (!has_quill() || !any_map()) return false;
    bool any = false;
    for (int i = 0; i < s_visited.n; i++) {
        const char *sc = s_visited.name[i];
        if (names_has(&s_mapped, sc)) continue;
        for (int k = 0; k < s_np; k++)
            if (!strcmp(s_p[k].scene, sc) && zone_unlocked(s_p[k].zone)) {
                names_add(&s_mapped, sc);
                any = true;
                break;
            }
    }
    return any;
}

static void refresh(void) {
    if (!s_dirty) return;
    s_dirty = false;
    for (int k = 0; k < s_np; k++) {
        Piece *p = &s_p[k];
        p->map = names_has(&s_mapped, p->scene);
        if (!p->map && p->also[0]) {   // mapped once all of these are
            bool all = true;
            char tmp[96];
            snprintf(tmp, sizeof tmp, "%s", p->also);
            for (char *t = strtok(tmp, ","); t; t = strtok(NULL, ",")) if (!names_has(&s_mapped, t)) all = false;
            p->map = all;
        }
        p->vis = p->map || names_has(&s_visited, p->scene);
    }
}

// the frame a piece shows (GameMapScene.SetMapped / SetNotMapped), -1 for none; *grey: a rough one without a full one
static int piece_frame(const Piece *p, bool *grey) {
    *grey = false;
    if ((p->state == 2 || p->map) && has_quill())
        return p->state == 1 && p->f_full >= 0 ? p->f_full : p->f_init;
    if (p->state == 0) return -1;
    if (p->state == 1 && p->f_full < 0) *grey = true;
    return p->f_init;
}

// the area's sprite set, the one before it let go (only one in memory)
static bool use_zone(int zone) {
    if (zone == s_set_zone) return s_set != NULL;
    if (s_set) sprites_put(s_set);
    s_set_zone = zone;
    char path[32];
    snprintf(path, sizeof path, "ui/map_%d.sss", zone);
    s_set = sprites_get(path);
    for (int k = 0; k < s_np; k++) {
        Piece *p = &s_p[k];
        p->f_init = p->f_full = -1;
        if (p->zone != zone || !s_set) continue;
        char c[16];
        snprintf(c, sizeof c, "%di", k);
        p->f_init = frame_of(c);
        snprintf(c, sizeof c, "%df", k);
        p->f_full = frame_of(c);
    }
    return s_set != NULL;
}

bool map_draw(const char *scene) {
    map_init();
    if (!s_np) return false;
    refresh();
    // the room's area: a piece of the room's own
    int zone = -1;
    for (int k = 0; k < s_np && zone < 0; k++) if (!strcmp(s_p[k].scene, scene)) zone = s_p[k].zone;
    if (zone < 0 || !zone_unlocked(zone) || !use_zone(zone)) return false;
    // its bounds, to fit the area on the screen (under its name)
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int k = 0; k < s_np; k++) {
        const Piece *p = &s_p[k];
        bool grey;
        int fr = p->zone == zone ? piece_frame(p, &grey) : -1;
        if (fr < 0) continue;
        const SFrame *q = &s_set->frames[fr];
        float a = p->x + q->x0 * p->sx, b = p->x + q->x1 * p->sx, c = p->y + q->y0 * p->sy, d = p->y + q->y1 * p->sy;
        if (a > b) { float t = a; a = b; b = t; }
        if (a < x0) x0 = a;
        if (b > x1) x1 = b;
        if (c < y0) y0 = c;
        if (d > y1) y1 = d;
    }
    if (x1 <= x0) return false;
    const float L = 8, T = 40, W = 304, H = 192;
    float k = W / (x1 - x0) < H / (y1 - y0) ? W / (x1 - x0) : H / (y1 - y0);
    if (k > 140) k = 140;
    float ox = L + (W - (x1 - x0) * k) * 0.5f, oy = T + (H - (y1 - y0) * k) * 0.5f;
    C2D_ImageTint grey;
    C2D_PlainImageTint(&grey, C2D_Color32(128, 128, 128, 255), 0.5f);
    for (int i = 0; i < s_np; i++) {
        const Piece *p = &s_p[i];
        bool g;
        int fr = p->zone == zone ? piece_frame(p, &g) : -1;
        if (fr < 0) continue;
        float px = ox + (p->x - x0) * k, py = oy + (y1 - p->y) * k;
        sprite_draw_screen_xy(s_set, fr, px, py, k * p->sx, k * p->sy, g ? &grey : NULL);
    }
    return true;
}
