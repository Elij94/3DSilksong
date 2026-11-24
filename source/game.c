// The game loop: the current room, its objects and Hornet; camera, gates between rooms, hazards, death, the HUD,
// the room menu (SELECT) and the save.
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include "game.h"
#include "objects.h"
#include "version.h"
#include "autotest.h"

#define FIRST_ROOM "Tut_01"
#define SAVE_PATH  "sdmc:" SS_DATADIR "save.txt"
#define NEEDLE_DMG 5

static Room s_room;
static Objects s_objs;
static Hero s_hero;
static bool s_loaded;

static s16 *s_order;          // layers, in drawing order
static int s_norder;

// the gate being taken: fade out, load, fade in
static struct {
    bool active;
    float t;
    char target[33], entry[17];
} s_trans;
static float s_fade = 1;      // 1 = black
static float s_gate_lock;     // no gate right after an entry
static float s_dead_t;
static float s_safe_x, s_safe_y;
static char s_msg[64];
static float s_msg_t;

// the room menu
static bool s_menu;
static char (*s_rooms)[33];
static int s_nrooms, s_menu_sel;

// ------------------------------------------------------------------ layers' order

static int layer_rank(const Layer *L) {
    if (!strncmp(L->name, "bg", 2)) return 4 - (L->name[2] - '0');   // bg4 = 0 ... bg1 = 3
    if (!strncmp(L->name, "main", 4)) return 4;
    if (!strncmp(L->name, "front", 5)) return 5;
    return L->z > 0 ? 3 : 5;
}

static int cmp_layers(const void *a, const void *b) {
    int ia = *(const s16 *)a, ib = *(const s16 *)b;
    int ra = layer_rank(&s_room.layers[ia]), rb = layer_rank(&s_room.layers[ib]);
    return ra != rb ? ra - rb : ia - ib;
}

// ------------------------------------------------------------------ rooms

static void message(const char *m) { snprintf(s_msg, sizeof s_msg, "%s", m); s_msg_t = 3; }

static void unload(void) {
    if (!s_loaded) return;
    objects_free(&s_objs);
    room_free(&s_room);
    free(s_order);
    s_order = NULL;
    s_loaded = false;
    sprites_collect();
}

static bool room_exists(const char *scene) {
    struct stat st;
    return stat(data_path("rooms/%s.ssr", scene), &st) == 0;
}

static void camera_snap(void);

static bool enter(const char *scene) {
    unload();
    if (!room_load(&s_room, scene)) return false;
    objects_load(&s_objs, scene);
    enemies_init(&s_objs, &s_room);
    s_norder = s_room.nlayers;
    s_order = malloc(sizeof(s16) * (s_norder + 1));
    for (int i = 0; i < s_norder; i++) s_order[i] = i;
    qsort(s_order, s_norder, sizeof(s16), cmp_layers);
    s_loaded = true;
    return true;
}

// places Hornet at the entry gate 'entry' of the new room (the spawn point if there is none)
static void place_at_entry(const char *entry) {
    Hero *h = &s_hero;
    const Gate *g = NULL;
    for (int i = 0; i < s_room.ngates; i++)
        if (!strcmp(s_room.gates[i].name, entry)) g = &s_room.gates[i];
    if (!g) {
        hero_place(h, s_room.spawn_x, s_room.spawn_y - HERO_FOOT, 0);
        hero_settle(h, &s_room, 6);
        return;
    }
    float cx = g->x + g->w * 0.5f;
    if (!strncmp(g->name, "left", 4)) {
        hero_place(h, g->x + g->w + 1.0f, g->y - HERO_FOOT + 0.1f, 1);
        hero_settle(h, &s_room, g->h + 4);
        h->vx = 0;
    } else if (!strncmp(g->name, "right", 5)) {
        hero_place(h, g->x - 1.0f, g->y - HERO_FOOT + 0.1f, -1);
        hero_settle(h, &s_room, g->h + 4);
    } else if (!strncmp(g->name, "top", 3)) {
        hero_place(h, cx, g->y - 2.2f, 0);   // falls in from above
    } else if (!strncmp(g->name, "bot", 3)) {
        hero_place(h, cx, g->y + g->h - HERO_FOOT + 0.3f, 0);
        h->vy = 22.0f;                        // jumps up into the room
    } else {                                  // doors
        hero_place(h, cx, g->y - HERO_FOOT + 0.1f, 0);
        hero_settle(h, &s_room, 4);
    }
    // whatever the gate, never inside the ground
    for (int i = 0; i < 40 && !hero_box_free(&s_room, h->x, h->y); i++) h->y += 0.25f;
}

static void go(const char *scene, const char *entry) {
    char sc[48], en[17];
    snprintf(sc, sizeof sc, "%s", scene);
    snprintf(en, sizeof en, "%s", entry ? entry : "");
    if (!enter(sc)) {
        message("Room missing, back to the first one");
        if (!enter(FIRST_ROOM)) return;
        en[0] = 0;
    }
    place_at_entry(en);
    s_safe_x = s_hero.x;
    s_safe_y = s_hero.y;
    s_gate_lock = 0.5f;
    camera_snap();
    // every tile the first frame shows, at once (the screen is black meanwhile)
    room_preload_view(&s_room);
    game_save();
}

// ------------------------------------------------------------------ camera

static void camera_target(float *tx, float *ty) {
    Hero *h = &s_hero;
    // Hornet in the middle of the screen: her origin is at her chest, the middle of her sprite ~0.3 below; a little
    // room ahead of her when she runs
    *tx = h->x + h->face * (fabsf(h->vx) > 1 ? 1.5f : 0.6f);
    *ty = h->y - 0.3f;
    if (h->look && h->look_t > 0.4f) *ty += h->look * 4.5f;
}

static void camera_clamp(float *cx, float *cy) {
    View *v = &g_view;
    const Room *r = &s_room;
    // a camera lock area around Hornet bounds the camera's centre
    for (int i = 0; i < r->ntrig; i++) {
        const Trigger *t = &r->trig[i];
        if (t->kind != TRIG_CAMLOCK) continue;
        if (!rects_overlap(s_hero.x, s_hero.y, 0.01f, 0.01f, t->x, t->y, t->w, t->h)) continue;
        float x0 = t->extra[0], x1 = t->extra[1], y0 = t->extra[2], y1 = t->extra[3];
        if (x1 >= x0 && !(x0 == -1 && x1 == -1)) *cx = clampf(*cx, x0, x1);
        if (y1 >= y0 && !(y0 == -1 && y1 == -1)) *cy = clampf(*cy, y0, y1);
        break;
    }
    float w = r->bx1 - r->bx0, hgt = r->by1 - r->by0;
    *cx = w > 2 * v->hw ? clampf(*cx, r->bx0 + v->hw, r->bx1 - v->hw) : (r->bx0 + r->bx1) * 0.5f;
    *cy = hgt > 2 * v->hh ? clampf(*cy, r->by0 + v->hh, r->by1 - v->hh) : (r->by0 + r->by1) * 0.5f;
}

static void camera_snap(void) {
    float tx, ty;
    camera_target(&tx, &ty);
    camera_clamp(&tx, &ty);
    g_view.cx = tx;
    g_view.cy = ty;
}

static void camera_update(float dt) {
    float tx, ty;
    camera_target(&tx, &ty);
    camera_clamp(&tx, &ty);
    // horizontally a quick follow; vertically quicker when falling, so she never drops out of the view
    float kx = 1 - expf(-dt * 9.0f);
    float ky = 1 - expf(-dt * (s_hero.vy < -10 ? 14.0f : 8.0f));
    g_view.cx += (tx - g_view.cx) * kx;
    g_view.cy += (ty - g_view.cy) * ky;
}

// ------------------------------------------------------------------ save

void game_end(void) { unload(); }

void game_save(void) {
    if (!s_loaded) return;
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:" SS_DATADIR, 0777);
    FILE *f = fopen(SAVE_PATH, "w");
    if (!f) return;
    fprintf(f, "%s %.3f %.3f %d\n", s_room.scene, s_safe_x, s_safe_y, s_hero.hp);
    fclose(f);
}

static int cmp_names(const void *a, const void *b) { return strcmp(a, b); }

static void list_rooms(void) {
    DIR *d = opendir("romfs:/rooms");
    if (!d) return;
    int cap = 64;
    s_rooms = malloc(sizeof *s_rooms * cap);
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n < 5 || n > 36 || strcmp(e->d_name + n - 4, ".ssr")) continue;
        if (s_nrooms == cap) s_rooms = realloc(s_rooms, sizeof *s_rooms * (cap *= 2));
        memcpy(s_rooms[s_nrooms], e->d_name, n - 4);
        s_rooms[s_nrooms++][n - 4] = 0;
    }
    closedir(d);
    qsort(s_rooms, s_nrooms, sizeof *s_rooms, cmp_names);
}

bool game_start(void) {
    if (!hero_init(&s_hero)) message("sprites/knight.sss missing");
    list_rooms();
    char scene[48] = FIRST_ROOM;
    float x = 0, y = 0;
    int hp = 0;
    bool saved = false;
    FILE *f = fopen(SAVE_PATH, "r");
    if (f) {
        saved = fscanf(f, "%47s %f %f %d", scene, &x, &y, &hp) >= 3;
        fclose(f);
    }
    if (!saved || !room_exists(scene)) { strcpy(scene, FIRST_ROOM); saved = false; }
    if (!room_exists(scene) && s_nrooms) snprintf(scene, sizeof scene, "%s", s_rooms[0]), saved = false;
    go(scene, NULL);
    if (!s_loaded) return false;
    if (saved) {
        hero_place(&s_hero, x, y, 1);
        hero_settle(&s_hero, &s_room, 3);
        if (hp > 0 && hp <= s_hero.maxhp) s_hero.hp = hp;
        camera_snap();
    }
    for (int i = 0; i < s_nrooms; i++)
        if (!strcmp(s_rooms[i], s_room.scene)) s_menu_sel = i;
    return true;
}

// ------------------------------------------------------------------ play

static void hazard_respawn(void) {
    Hero *h = &s_hero;
    // the respawn marker nearest to the last safe ground
    float bx = s_room.spawn_x, by = s_room.spawn_y, bd = 1e30f;
    for (int i = 0; i < s_room.ntrig; i++) {
        const Trigger *t = &s_room.trig[i];
        if (t->kind != TRIG_RESPAWN) continue;
        float d = (t->x - s_safe_x) * (t->x - s_safe_x) + (t->y - s_safe_y) * (t->y - s_safe_y);
        if (d < bd) { bd = d; bx = t->x; by = t->y; }
    }
    if (bd > 400 && hero_box_free(&s_room, s_safe_x, s_safe_y)) { bx = s_safe_x; by = s_safe_y + HERO_FOOT; }
    hero_respawn(h, bx, by - HERO_FOOT + 0.1f);
    hero_settle(h, &s_room, 6);
    camera_snap();
}

static void play(const Input *in, float dt) {
    Hero *h = &s_hero;
    float box[4];
    bool hitting = hero_update(h, &s_room, in, dt, box);
    objects_update(&s_objs, dt);
    enemies_update(&s_objs, &s_room, h->x, h->y - 0.6f, dt);
    if (hitting && !h->atk_hit) {
        if (objects_strike(&s_objs, box[0], box[1], box[2], box[3], NEEDLE_DMG, h->x)) {
            h->atk_hit = true;
            if (h->attack == ATK_DOWN) hero_pogo(h);
            else if (h->attack == ATK_SIDE) h->vx -= h->face * 3.0f;
        }
    }
    float x0 = h->x - HERO_HW, y0 = h->y + HERO_FOOT, x1 = h->x + HERO_HW, y1 = h->y + HERO_HEAD;

    if (h->state == HS_DEAD) {
        s_dead_t += dt;
        if (s_dead_t > 2.0f) {   // back to the room's start, healed
            s_dead_t = 0;
            int max = h->maxhp;
            char sc[48];
            snprintf(sc, sizeof sc, "%s", s_room.scene);
            s_fade = 1;
            go(sc, NULL);
            h->hp = max;
            h->state = HS_NORMAL;
        }
        return;
    }
    if (h->ground && h->state == HS_NORMAL) { s_safe_x = h->x; s_safe_y = h->y; }

    // hazards (spikes, acid...) and falling out of the room
    bool hazard = h->y < s_room.gy - 4;
    for (int i = 0; i < s_room.ntrig && !hazard; i++) {
        const Trigger *t = &s_room.trig[i];
        if (t->kind == TRIG_HAZARD && rects_overlap(x0, y0, x1 - x0, y1 - y0, t->x, t->y, t->w, t->h)) hazard = true;
    }
    if (hazard && h->state != HS_RESPAWN) {
        h->invuln = 0;
        hero_hurt(h, 1, h->x);
        if (h->state != HS_DEAD) hazard_respawn();
        return;
    }
    float ex;
    int dmg = objects_touch_damage(&s_objs, x0, y0, x1, y1, &ex);
    if (dmg) hero_hurt(h, dmg > 1 ? 1 : dmg, ex);   // contact damage: one mask, as in the game

    // gates
    if (s_gate_lock > 0) {
        s_gate_lock -= dt;
        return;
    }
    for (int i = 0; i < s_room.ngates; i++) {
        const Gate *g = &s_room.gates[i];
        if (!rects_overlap(x0, y0, x1 - x0, y1 - y0, g->x, g->y, g->w, g->h)) continue;
        bool door = !strncmp(g->name, "door", 4);
        if (door && !(in->down & KEY_UP)) continue;
        if (!g->target[0] || !room_exists(g->target)) {
            if (s_msg_t <= 0) message("This room was not converted");
            continue;
        }
        s_trans.active = true;
        s_trans.t = 0;
        snprintf(s_trans.target, sizeof s_trans.target, "%s", g->target);
        snprintf(s_trans.entry, sizeof s_trans.entry, "%s", g->entry);
        break;
    }
}

static void menu_update(const Input *in) {
    if (!s_nrooms) { s_menu = false; return; }
    int step = 0;
    if (in->down & KEY_DOWN) step = 1;
    if (in->down & KEY_UP) step = -1;
    if (in->down & KEY_R) step = 15;
    if (in->down & KEY_L) step = -15;
    s_menu_sel = (s_menu_sel + step % s_nrooms + s_nrooms) % s_nrooms;
    if (in->down & KEY_A) {
        s_menu = false;
        s_fade = 1;
        go(s_rooms[s_menu_sel], NULL);
        s_hero.hp = s_hero.maxhp;
    }
    if (in->down & KEY_B) s_menu = false;
}

void game_warp(const char *scene, float x, float y) {
    if (strcmp(scene, s_room.scene)) go(scene, NULL);
    hero_place(&s_hero, x, y, 1);
    hero_settle(&s_hero, &s_room, 3);
    camera_snap();
    s_fade = 0;
}

static void log_state(void) {
    const Hero *h = &s_hero;
    char clip[128];
    int dead = 0, live = 0;
    for (int i = 0; i < s_objs.n; i++)
        if (s_objs.o[i].ai != AI_NONE) { if (s_objs.o[i].dead || !s_objs.o[i].alive) dead++; else live++; }
    snprintf(clip, sizeof clip, "%s hp=%d enemies=%d/%d",
             h->body.set && h->body.clip >= 0 ? h->body.set->clips[h->body.clip].name : "-", h->hp, live, dead);
    autotest_log(s_room.scene, h->x, h->y, h->vx, h->vy, h->ground, h->state, clip, g_view.cx, g_view.cy);
}

void game_update(const Input *in, float dt) {
    log_state();
    if (s_msg_t > 0) s_msg_t -= dt;
    if (in->down & KEY_SELECT) s_menu = !s_menu;
    if (s_menu) { menu_update(in); return; }
    if (s_trans.active) {
        s_trans.t += dt;
        s_fade = fminf(1, s_trans.t / 0.2f);
        if (s_trans.t >= 0.2f) {
            s_trans.active = false;
            go(s_trans.target, s_trans.entry);
        }
        return;
    }
    if (s_fade > 0) s_fade = fmaxf(0, s_fade - dt / 0.25f);
    play(in, dt);
    camera_update(dt);
    room_prefetch(&s_room);
}

// ------------------------------------------------------------------ drawing

static void draw_hud(void) {
    // masks, top left (the game's own HUD art is a later step)
    for (int i = 0; i < s_hero.maxhp; i++) {
        float x = 22 + i * 18, y = 20;
        C2D_DrawCircleSolid(x, y, 0.5f, 7.5f, C2D_Color32(20, 16, 18, 200));
        if (i < s_hero.hp) C2D_DrawCircleSolid(x, y, 0.5f, 6, C2D_Color32(240, 236, 228, 255));
    }
}

void game_draw(void) {
    if (!s_loaded) return;
    room_frame_begin();
    float zcur = 1e30f;
    bool hero_done = false;
    for (int k = 0; k < s_norder; k++) {
        int li = s_order[k];
        const Layer *L = &s_room.layers[li];
        int rank = layer_rank(L);
        if (rank >= 5 && !hero_done) {
            // in front of everything at the gameplay plane: the objects there, then Hornet
            objects_draw(&s_objs, -0.001f, zcur);
            zcur = -0.001f;
            hero_draw(&s_hero);
            hero_done = true;
        }
        float lower = rank == 4 ? 0.0f : L->z;
        if (lower < zcur) {
            objects_draw(&s_objs, lower, zcur);
            zcur = lower;
        }
        room_draw_layer(&s_room, li, false);
    }
    if (!hero_done) {
        objects_draw(&s_objs, -0.001f, zcur);
        zcur = -0.001f;
        hero_draw(&s_hero);
    }
    objects_draw(&s_objs, -1e30f, zcur);
    gfx_blend(BLEND_NORMAL);
    draw_hud();
    if (s_fade > 0)
        C2D_DrawRectSolid(0, 0, 0.9f, g_view.sw, g_view.sh, C2D_Color32(0, 0, 0, (u8)(s_fade * 255)));
}

void game_status(float fps, float ms) {
    printf("\x1b[1;1H3DSilksong %-8s %5.1f fps %4.1f ms\x1b[K\n", SS_VERSION, fps, ms);
    if (s_menu) {
        printf("\x1b[2;1H\x1b[K\n ROOMS  (%d)  A: go  B: close  L/R: page\x1b[K\n\x1b[K\n", s_nrooms);
        int top = s_menu_sel - 10;
        if (top < 0) top = 0;
        for (int i = 0; i < 24; i++) {
            int k = top + i;
            if (k < s_nrooms) printf("%s %-36s\x1b[K\n", k == s_menu_sel ? ">" : " ", s_rooms[k]);
            else printf("\x1b[K\n");
        }
        return;
    }
    printf("\x1b[2;1H\x1b[K\nRoom  %-32s\x1b[K\n", s_room.scene);
    printf("Pos   %7.2f %7.2f  %s\x1b[K\n", s_hero.x, s_hero.y, s_hero.ground ? "ground" : "air   ");
    printf("Cam   %7.2f %7.2f  st %d vy %6.2f\x1b[K\n", g_view.cx, g_view.cy, s_hero.state, s_hero.vy);
    printf("Masks %d/%d   objects %d\x1b[K\n", s_hero.hp, s_hero.maxhp, s_objs.n);
    printf("Tiles %d cached   linear %lu KB free\x1b[K\n", room_cache_used(), (unsigned long)(linearSpaceFree() / 1024));
    printf("\x1b[K\n%s\x1b[K\n\x1b[K\n", s_msg_t > 0 ? s_msg : "");
    printf("Controls\x1b[K\n");
    printf(" Pad / Circle Pad  move, look up/down\x1b[K\n");
    printf(" B      jump (hold: higher)\x1b[K\n");
    printf(" Y      needle (up+Y, down+Y in air)\x1b[K\n");
    printf(" R/ZR   dash\x1b[K\n");
    printf(" Up     enter a door\x1b[K\n");
    printf(" SELECT room menu\x1b[K\n");
    printf(" START  save and quit\x1b[K\n");
    for (int i = 0; i < 11; i++) printf("\x1b[K\n");
}
