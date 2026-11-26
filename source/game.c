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
#include "particles.h"
#include "audio.h"
#include "fsm.h"
#include "dialogue.h"
#include "fx.h"
#include "currency.h"
#include "menu.h"
#include "font.h"

#define FIRST_ROOM "Tut_01"
#define OLD_SAVE   "sdmc:" SS_DATADIR "save.txt"   // before the 4 slots: becomes slot 1
#define TITLE_ROOM "Menu_Title"
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

// title, play, pause
enum { GS_TITLE, GS_PLAY, GS_PAUSE };
static int s_mode = GS_TITLE;
static int s_slot = 1;            // the save slot played (1..4)
static char s_status[1024];       // the debug info (bottom screen, when the option is on)
static int s_pause_sel;
static bool s_quit;
// the last bench sat on: where Hornet comes back after dying
static char s_bench_scene[48];
static float s_bench_x, s_bench_y;
static bool s_bench_prompt;
static float s_save_in;          // a save waiting (seconds)
static C2D_TextBuf s_tbuf;

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
    fsm_free();
    objects_free(&s_objs);
    currency_clear();
    particles_free();
    room_free(&s_room);
    free(s_order);
    s_order = NULL;
    s_loaded = false;
    sprites_collect();
}

// music/rooms.txt (tools/music_map.py): Scene<TAB>music<TAB>ambience
static char *s_music_map;

static void room_music(const char *scene) {
    if (!s_music_map) {
        size_t size;
        char *d = file_load(data_path("music/rooms.txt"), &size);
        if (!d) return;
        s_music_map = realloc(d, size + 1);
        s_music_map[size] = 0;
    }
    size_t n = strlen(scene);
    for (const char *p = s_music_map; p && *p;) {
        const char *e = strchr(p, '\n');
        if (!strncmp(p, scene, n) && p[n] == '\t') {
            // the two fields after the scene, either may be empty
            char f[2][40] = {"", ""};
            const char *q = p + n + 1;
            for (int k = 0; k < 2; k++) {
                int len = 0;
                while (q[len] && q[len] != '\t' && q[len] != '\n' && q[len] != '\r') len++;
                if (len > 39) len = 39;
                memcpy(f[k], q, len);
                f[k][len] = 0;
                q += len;
                if (*q == '\t') q++;
                else break;
            }
            if (f[0][0]) audio_play(0, f[0]);   // a room without its own music keeps the one playing
            if (f[1][0]) audio_play(1, f[1]);
            return;
        }
        p = e ? e + 1 : NULL;
    }
}

static bool room_exists(const char *scene) {
    struct stat st;
    return stat(data_path("rooms/%s.ssr", scene), &st) == 0;
}

static void camera_snap(void);

static bool enter(const char *scene) {
    unload();
    g_fsm_fade = 0;   // a script's fade ends with its room
    if (!room_load(&s_room, scene)) return false;
    objects_load(&s_objs, scene);
    fsm_load(scene, &s_objs);
    room_dyn_solid = fsm_solid;
    enemies_init(&s_objs, &s_room);
    particles_load(scene);
    room_music(scene);
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
        hero_place(h, s_room.spawn_x, s_room.spawn_y, 0);   // markers are where Hornet's origin goes
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
    objects_preload(&s_objs);
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
    // The camera lock areas around Hornet bound the camera's centre; several overlap, the smallest one (the most
    // specific) wins. -1 leaves that side free.
    const Trigger *best = NULL;
    float barea = 1e30f;
    for (int i = 0; i < r->ntrig; i++) {
        const Trigger *t = &r->trig[i];
        if (t->kind != TRIG_CAMLOCK || t->w * t->h >= barea) continue;
        if (!rects_overlap(s_hero.x, s_hero.y, 0.01f, 0.01f, t->x, t->y, t->w, t->h)) continue;
        best = t;
        barea = t->w * t->h;
    }
    if (best) {
        const float *e = best->extra;
        if (e[0] != -1) *cx = fmaxf(*cx, e[0]);
        if (e[1] != -1) *cx = fminf(*cx, e[1]);
        if (e[2] != -1) *cy = fmaxf(*cy, e[2]);
        if (e[3] != -1) *cy = fminf(*cy, e[3]);
    }
    float w = r->bx1 - r->bx0, hgt = r->by1 - r->by0;
    *cx = w > 2 * v->hw ? clampf(*cx, r->bx0 + v->hw, r->bx1 - v->hw) : (r->bx0 + r->bx1) * 0.5f;
    *cy = hgt > 2 * v->hh ? clampf(*cy, r->by0 + v->hh, r->by1 - v->hh) : (r->by0 + r->by1) * 0.5f;
    // whatever the room says, Hornet stays in the view (the locks were made for the PC's wider view)
    float mx = v->hw - 3.0f, my = v->hh - 2.5f;
    *cx = clampf(*cx, s_hero.x - mx, s_hero.x + mx);
    *cy = clampf(*cy, s_hero.y - 0.6f - my, s_hero.y - 0.6f + my);
}

static void camera_snap(void) {
    float tx, ty;
    camera_target(&tx, &ty);
    camera_clamp(&tx, &ty);
    g_view.cx = tx;
    g_view.cy = ty;
}

// a camera shake: an offset that dies away
static float s_shake, s_shake_x, s_shake_y;

static void camera_update(float dt) {
    float a = fx_take_shake();
    if (a > s_shake) s_shake = a;
    s_shake = fmaxf(0, s_shake - dt * 0.6f);
    s_shake_x = s_shake > 0 ? ((rand() / (float)RAND_MAX) * 2 - 1) * s_shake : 0;
    s_shake_y = s_shake > 0 ? ((rand() / (float)RAND_MAX) * 2 - 1) * s_shake : 0;
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
    if (s_mode == GS_TITLE || !strcmp(s_room.scene, TITLE_ROOM)) return;
    FILE *f = fopen(menu_save_path(s_slot), "w");
    if (!f) return;
    fprintf(f, "%s %.3f %.3f %d\n", s_room.scene, s_safe_x, s_safe_y, s_hero.hp);
    fprintf(f, "bench %s %.3f %.3f\n", s_bench_scene[0] ? s_bench_scene : "-", s_bench_x, s_bench_y);
    pd_save(f);
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
        if (n < 5 || n > 36 || strcmp(e->d_name + n - 4, ".ssr") || !strncmp(e->d_name, "Menu_", 5)) continue;
        if (s_nrooms == cap) s_rooms = realloc(s_rooms, sizeof *s_rooms * (cap *= 2));
        memcpy(s_rooms[s_nrooms], e->d_name, n - 4);
        s_rooms[s_nrooms++][n - 4] = 0;
    }
    closedir(d);
    qsort(s_rooms, s_nrooms, sizeof *s_rooms, cmp_names);
}

// a slot's save: where Hornet was, her masks, the last bench, the PlayerData
static bool load_slot(int slot) {
    char scene[48] = FIRST_ROOM;
    float x = 0, y = 0;
    int hp = 0;
    bool saved = false;
    s_bench_scene[0] = 0;
    pd_clear();
    FILE *f = fopen(menu_save_path(slot), "r");
    if (f) {
        saved = fscanf(f, "%47s %f %f %d", scene, &x, &y, &hp) >= 3;
        char kw[8];
        if (fscanf(f, "%7s %47s %f %f", kw, s_bench_scene, &s_bench_x, &s_bench_y) != 4 || strcmp(kw, "bench") ||
            !room_exists(s_bench_scene))
            s_bench_scene[0] = 0;
        char line[128];
        while (fgets(line, sizeof line, f)) pd_load_line(line);
        fclose(f);
    }
    if (!saved || !room_exists(scene)) { strcpy(scene, FIRST_ROOM); saved = false; }
    if (!room_exists(scene) && s_nrooms) snprintf(scene, sizeof scene, "%s", s_rooms[0]), saved = false;
    hero_init(&s_hero);
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

// the title screen: the game's Menu_Title scene behind the menu, its camera where the game puts it
static void to_title(void) {
    s_mode = GS_TITLE;
    s_menu = false;
    menu_open_title();
    if (room_exists(TITLE_ROOM) && enter(TITLE_ROOM)) {
        g_view.cx = 18.6f;
        g_view.cy = 9.2f;
        room_preload_view(&s_room);
    }
    s_fade = 1;
}

bool game_start(void) {
    if (!hero_init(&s_hero)) message("sprites/knight.sss missing");
    dlg_init();
    menu_init();
    list_rooms();
    // the save from before the slots: slot 1
    struct stat st;
    if (stat(OLD_SAVE, &st) == 0 && stat(menu_save_path(1), &st) != 0) rename(OLD_SAVE, menu_save_path(1));
    s_tbuf = C2D_TextBufNew(512);
    struct stat at;
    if (autotest_on() && stat("sdmc:" SS_DATADIR "autotitle", &at) != 0) {   // testing: autotitle keeps the title
        s_slot = 1;
        if (!load_slot(1)) return false;
        s_mode = GS_PLAY;
        return true;
    }
    to_title();
    if (!s_loaded && !load_slot(1)) return false;
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
    if (bd > 400 && hero_box_free(&s_room, s_safe_x, s_safe_y)) { bx = s_safe_x; by = s_safe_y; }
    hero_respawn(h, bx, by + 0.1f);
    hero_settle(h, &s_room, 6);
    camera_snap();
}

static bool probe_enemy(float x0, float y0, float x1, float y1, int face, float *ex) {
    (void)face;
    return objects_enemy_in(&s_objs, x0, y0, x1, y1, s_hero.x, ex);
}

static void play(const Input *in_raw, float dt) {
    Hero *h = &s_hero;
    // a dialogue open: it takes the buttons, Hornet stands
    Input none = {0}, *in = (Input *)in_raw;
    if (dlg_active()) { dlg_update(in_raw->down); in = &none; }
    else if ((in_raw->down & KEY_UP) && h->ground && h->state == HS_NORMAL &&
             fsm_interact(h->x - HERO_HW, h->y + HERO_FOOT, h->x + HERO_HW, h->y + HERO_HEAD)) in = &none;
    float box[4];
    hero_probe = probe_enemy;
    g_where = "hero_update";
    bool hitting = hero_update(h, &s_room, in, dt, box);
    g_where = "objects/enemies";
    objects_update(&s_objs, dt);
    g_where = "fsm_update";
    fsm_input(in_raw->down, in_raw->held, in_raw->up);
    fsm_update(dt, h, &s_objs, &s_room);
    g_where = "play";
    enemies_update(&s_objs, &s_room, h->x, h->y - 0.6f, dt);
    particles_update(dt);
    if (hitting && !h->atk_hit && fsm_strike(box[0], box[1], box[2], box[3])) h->atk_hit = true;
    if (hitting && !h->atk_hit) {
        int dir = h->attack == ATK_UP ? 1 : h->attack == ATK_DOWN ? 3 : (h->face > 0 ? 0 : 2);
        if (objects_strike(&s_objs, box[0], box[1], box[2], box[3], NEEDLE_DMG, h->x, dir)) {
            h->atk_hit = true;
            if (h->silk < SILK_MAX) h->silk++;   // hits spin silk
            if (h->attack == ATK_DOWN || h->attack == ATK_HARPOON) hero_pogo(h);
            else if (h->attack == ATK_SIDE) hero_recoil(h);
        }
    }
    float x0 = h->x - HERO_HW, y0 = h->y + HERO_FOOT, x1 = h->x + HERO_HW, y1 = h->y + HERO_HEAD;
    if (s_save_in > 0 && (s_save_in -= dt) <= 0) game_save();
    if (h->state != HS_DEAD) currency_update(&s_room, dt, x0, y0, x1, y1);

    if (h->state == HS_DEAD) {
        s_dead_t += dt;
        if (s_dead_t > 2.0f) {   // back to the room's start, healed
            s_dead_t = 0;
            int max = h->maxhp;
            char sc[48];
            snprintf(sc, sizeof sc, "%s", s_room.scene);
            s_fade = 1;
            if (s_bench_scene[0]) {   // back at the last bench
                go(s_bench_scene, NULL);
                hero_place(h, s_bench_x, s_bench_y, 0);
                hero_settle(h, &s_room, 3);
                camera_snap();
            } else go(sc, NULL);
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

    // benches: Up to sit, heal and make it the place to come back to
    s_bench_prompt = false;
    if (h->state == HS_NORMAL && h->ground) {
        for (int i = 0; i < s_objs.n; i++) {
            const Obj *o = &s_objs.o[i];
            if (strncmp(o->name, "RestBench", 9) || !o->alive || o->rw <= 0) continue;
            if (!rects_overlap(x0 - 0.6f, y0 - 0.5f, x1 - x0 + 1.2f, y1 - y0 + 1.0f, o->rx, o->ry, o->rw, o->rh + 2.0f))
                continue;
            s_bench_prompt = true;
            if (in->down & KEY_UP) {
                hero_sit(h, o->rx + o->rw * 0.5f);
                h->silk = SILK_MAX;   // a rest fills the spool
                pd_clear_semi();      // and the enemies come back
                h->hp = h->maxhp;
                snprintf(s_bench_scene, sizeof s_bench_scene, "%s", s_room.scene);
                s_bench_x = h->sit_x1;
                s_bench_y = h->y;
                s_safe_x = h->sit_x1;
                s_safe_y = h->y;
                s_save_in = 0.8f;     // saved once she is seated: writing the card now would stall her sitting down
                message("Rested. Hornet will come back here.");
            }
            break;
        }
        // something to talk to or use
        if (!s_bench_prompt && !dlg_active()) s_bench_prompt = fsm_can_interact(x0, y0, x1, y1);
    }

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

static void rooms_update(const Input *in) {
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
    if (x == 0 && y == 0) { s_fade = 0; return; }   // 0 0: the room's own entry point
    hero_place(&s_hero, x, y, 1);
    hero_settle(&s_hero, &s_room, 3);
    camera_snap();
    s_fade = 0;
}

static void log_state(void) {
    const Hero *h = &s_hero;
    char clip[256];
    int dead = 0, live = 0;
    for (int i = 0; i < s_objs.n; i++)
        if (s_objs.o[i].ai != AI_NONE) { if (s_objs.o[i].dead || !s_objs.o[i].alive) dead++; else live++; }
    snprintf(clip, sizeof clip, "%s hp=%d enemies=%d/%d parts=%d/%d cur=%d geo=%d sh=%d sp=%d",
             h->body.set && h->body.clip >= 0 ? h->body.set->clips[h->body.clip].name : "-", h->hp, live, dead,
             particles_count(), particles_emitters(), currency_count(), pd_int("geo"), pd_int("ShellShards"), fsm_spawned());
    { char cd[64]; currency_debug(cd, sizeof cd); strncat(clip, cd, sizeof clip - strlen(clip) - 1); }
    { char sd[96]; fsm_spawn_debug(sd, sizeof sd); strncat(clip, sd, sizeof clip - strlen(clip) - 1); }
    // the nearest enemy: where, its hp, its clip
    int ne = -1;
    float bd = 1e9f;
    for (int i = 0; i < s_objs.n; i++) {
        const Obj *o = &s_objs.o[i];
        if (!(o->flags & OF_ENEMY) || o->ai == AI_NONE || !o->alive) continue;
        float d = fabsf(o->x - h->x) + fabsf(o->y - h->y);
        if (d < bd) { bd = d; ne = i; }
    }
    if (ne >= 0) {
        const Obj *o = &s_objs.o[ne];
        char e[96];
        snprintf(e, sizeof e, " E[%s %.1f,%.1f hp=%d %s%s]", o->name, o->x, o->y, o->hp,
                 o->anim.set && o->anim.clip >= 0 ? o->anim.set->clips[o->anim.clip].name : "-", o->dead ? " dead" : "");
        strncat(clip, e, sizeof clip - strlen(clip) - 1);
    }
    char pd[96];
    particles_debug(pd, sizeof pd);
    strncat(clip, " ", sizeof clip - strlen(clip) - 1);
    strncat(clip, pd, sizeof clip - strlen(clip) - 1);
    autotest_log(s_room.scene, h->x, h->y, h->vx, h->vy, h->ground, h->state, clip, g_view.cx, g_view.cy);
}

bool game_quit(void) { return s_quit; }

static void new_game(void) {
    s_bench_scene[0] = 0;
    pd_clear();
    s_fade = 1;
    hero_init(&s_hero);
    go(FIRST_ROOM, NULL);
    s_hero.hp = s_hero.maxhp;
    s_hero.state = HS_NORMAL;
}

// the game's FreezeMoment: time slows to a stop (down), stays stopped (hold), comes back (up)
static float s_frz_down, s_frz_hold, s_frz_up, s_frz_t = -1;
static void freeze(float down, float hold, float up) {
    s_frz_down = down; s_frz_hold = hold; s_frz_up = up; s_frz_t = 0;
}
static float time_scale(float dt) {
    if (s_frz_t < 0) return 1;
    float t = s_frz_t;
    s_frz_t += dt;
    if (t < s_frz_down) return 1 - t / s_frz_down;
    t -= s_frz_down;
    if (t < s_frz_hold) return 0;
    t -= s_frz_hold;
    if (t < s_frz_up) return t / s_frz_up;
    s_frz_t = -1;
    return 1;
}

void game_update(const Input *in, float dt) {
    log_state();
    objects_freeze = freeze;
    if (s_mode == GS_TITLE || s_mode == GS_PAUSE) {
        int slot = 1;
        MenuAction m = menu_update(in, &slot);
        if (s_mode == GS_TITLE) {   // the title scene lives on behind the menu
            if (s_fade > 0) s_fade = s_fade - dt * 2 < 0 ? 0 : s_fade - dt * 2;
            objects_update(&s_objs, dt);
            particles_update(dt);
        }
        if (m == MA_QUIT) s_quit = true;
        else if (m == MA_LOAD) { s_slot = slot; load_slot(slot); s_mode = GS_PLAY; s_fade = 1; }
        else if (m == MA_NEW) { s_slot = slot; new_game(); s_mode = GS_PLAY; game_save(); }
        else if (m == MA_RESUME) s_mode = GS_PLAY;
        else if (m == MA_ROOMS) { s_mode = GS_PLAY; s_menu = true; }
        else if (m == MA_TO_TITLE) { game_save(); to_title(); }
        return;
    }
    if ((in->down & KEY_START) && !s_menu) { s_mode = GS_PAUSE; menu_open_pause(); return; }
    if (s_msg_t > 0) s_msg_t -= dt;
    if ((in->down & KEY_SELECT) && (menu_debug() || s_menu)) s_menu = !s_menu;
    if (s_menu) { rooms_update(in); return; }
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
    float gdt = dt * time_scale(dt);
    int hp0 = s_hero.hp;
    play(in, gdt);
    // Hornet hurt: the game's damage freeze (her DAMAGE_FREEZE_WAIT / _UP: 0.1 s stopped, 0.1 s back)
    if (s_hero.hp < hp0 && s_hero.hp > 0) freeze(0, 0.1f, 0.1f);
    camera_update(gdt);
    g_where = "room_prefetch";
    room_prefetch(&s_room);
    g_where = "update";
}

// ------------------------------------------------------------------ drawing

// The game's HUD (_GameCameras / HudCamera / Hud Canvas), placed as the game places it: its camera shows 8.71 units
// above and below the centre (13.8 pixels per unit on the 240 lines here), the HUD anchored at the top left. The bind
// orb's frame, the masks (scale 0.71, 0.94 apart), the silk spool under them (SilkSpool: the rod between its two caps,
// a chunk of silk every 0.18), and the rosary and shell shard counters when they change.
static SpriteSet *s_hud, *s_spool;
static float s_hud_t;
static int s_hud_hp = -1;
static float s_break_t[16];
static float s_refill_t[16];                 // a mask given back: its Health Refill
static int s_hud_silk = -1;
static float s_chunk_t[SILK_MAX];            // a chunk coming (> 0, Up) or going (< 0, Down) on the spool

static float hud_dur(const char *clip) {
    int c = s_hud ? sprite_clip(s_hud, clip) : -1;
    return c >= 0 && s_hud->clips[c].fps > 0 ? s_hud->clips[c].n / s_hud->clips[c].fps : 0;
}

#define HUD_K   (240.0f / (2 * 8.710664f))
#define HUD_X(x) (((x) + 8.710664f * 16 / 9) * HUD_K * g_view.sx / g_view.sy)
#define HUD_Y(y) ((8.710664f - (y)) * HUD_K)

static int hud_frame(const char *clip, float t, bool loop) {
    int c = sprite_clip(s_hud, clip);
    if (c < 0) return -1;
    const SClip *cl = &s_hud->clips[c];
    if (!cl->n) return -1;
    int i = (int)(t * (cl->fps > 0 ? cl->fps : 12));
    i = loop ? i % cl->n : (i < cl->n ? i : cl->n - 1);
    return rds16((const u8 *)&cl->frames[i]);
}

static int spool_frame(const char *name) {
    int c = s_spool ? sprite_clip(s_spool, name) : -1;
    return c >= 0 && s_spool->clips[c].n ? rds16((const u8 *)&s_spool->clips[c].frames[0]) : -1;
}

// a left-aligned line in the system font
static void hud_text(const char *str, float x, float y, float size, u32 color) {
    if (!s_tbuf) return;
    C2D_Text t;
    C2D_TextBufClear(s_tbuf);
    C2D_TextParse(&t, s_tbuf, str);
    C2D_TextOptimize(&t);
    float w, h;
    float ax = g_view.sx / g_view.sy;
    C2D_TextGetDimensions(&t, size * ax, size, &w, &h);
    C2D_DrawText(&t, C2D_WithColor, x, y - h * 0.5f, 0.95f, size * ax, size, color);
}

static void draw_hud(void) {
    if (!s_hud) {
        s_hud = sprites_get("sprites/hud_anim.sss");
        if (s_hud) s_hud->pinned = true;
        s_spool = sprites_get("sprites/hud_spool.sss");
        if (s_spool) s_spool->pinned = true;
    }
    s_hud_t += 1.0f / 60;
    if (s_hud_hp >= 0 && s_hero.hp < s_hud_hp)
        for (int i = s_hero.hp; i < s_hud_hp && i < 16; i++) s_break_t[i] = 0.001f;
    if (s_hud_hp >= 0 && s_hero.hp > s_hud_hp)
        for (int i = s_hud_hp; i < s_hero.hp && i < 16; i++) s_refill_t[i] = 0.001f;
    s_hud_hp = s_hero.hp;
    if (s_hud_silk >= 0 && s_hero.silk != s_hud_silk)
        for (int i = 0; i < SILK_MAX; i++) {
            if (i >= s_hud_silk && i < s_hero.silk) s_chunk_t[i] = 0.001f;        // spun: Up
            else if (i >= s_hero.silk && i < s_hud_silk) s_chunk_t[i] = -0.001f;  // taken: Down
        }
    s_hud_silk = s_hero.silk;
    if (!s_hud) {
        for (int i = 0; i < s_hero.maxhp; i++) {
            float x = HUD_X(-11.84f + 0.94f * i), y = HUD_Y(6.79f);
            C2D_DrawCircleSolid(x, y, 0.5f, 5.5f, C2D_Color32(20, 16, 18, 200));
            if (i < s_hero.hp) C2D_DrawCircleSolid(x, y, 0.5f, 4.5f, C2D_Color32(240, 236, 228, 255));
        }
        return;
    }
    gfx_blend(BLEND_NORMAL);
    const float K = HUD_K;
    // the bind orb's frame (Hunter crest)
    int fr = hud_frame("HUD Frame Idle", 0, false);
    if (fr < 0) fr = hud_frame("HUD Frame Wanderer Idle", 0, false);
    sprite_draw_screen(s_hud, fr, HUD_X(-12.58f), HUD_Y(6.62f), K, NULL);
    // the masks
    for (int i = 0; i < s_hero.maxhp && i < 16; i++) {
        float x = HUD_X(-11.84f + 0.94f * i), y = HUD_Y(6.79f), S = K * 0.714f;
        if (i < s_hero.hp && s_refill_t[i] > 0 && s_refill_t[i] < hud_dur("Health Refill")) {
            s_refill_t[i] += 1.0f / 60;
            sprite_draw_screen(s_hud, hud_frame("Health Refill", s_refill_t[i], false), x, y, S, NULL);
        } else if (i < s_hero.hp) {
            s_break_t[i] = 0;
            s_refill_t[i] = 0;
            sprite_draw_screen(s_hud, hud_frame("Health Idle", s_hud_t + i * 0.3f, true), x, y, S, NULL);
        } else if (s_break_t[i] > 0 && s_break_t[i] < 0.5f) {
            s_break_t[i] += 1.0f / 60;
            sprite_draw_screen(s_hud, hud_frame("Health Break", s_break_t[i], false), x, y, S, NULL);
        } else {
            sprite_draw_screen(s_hud, hud_frame("Health Empty", 0, false), x, y, S, NULL);
        }
    }
    // the spool: the rod from the left cap to the right one (9 chunks long), the silk wound on it
    const float tx = -13.75f, ty = 5.256f, len = 0.085f + 0.18f * (SILK_MAX - 1) + 0.15f;
    int rod = spool_frame("Hornet_Spool_0002_rod");
    if (rod >= 0) {
        float rw = s_spool->frames[rod].x1 - s_spool->frames[rod].x0;
        sprite_draw_screen_xy(s_spool, rod, HUD_X(tx), HUD_Y(ty - 0.004f), K * len / (rw > 0 ? rw : 1), K, NULL);
    }
    int cap = hud_frame("Cap Idle", 0, false);
    if (cap < 0) cap = spool_frame("Hornet_Spool_0000_cap");
    SpriteSet *cs = hud_frame("Cap Idle", 0, false) >= 0 ? s_hud : s_spool;
    sprite_draw_screen_xy(cs, cap, HUD_X(tx), HUD_Y(ty), -K, K, NULL);
    sprite_draw_screen_xy(cs, cap, HUD_X(tx + len), HUD_Y(ty), K, K, NULL);
    bool glow = s_hero.silk >= SILK_MAX;   // a bind is ready: the silk glows
    for (int i = 0; i < SILK_MAX; i++) {
        char name[24];
        float t = s_chunk_t[i];
        if (t != 0) {   // coming or going: SilkChunk Up / Down, then idle or gone
            snprintf(name, sizeof name, t > 0 ? "SilkChunk Up%d" : "SilkChunk Down%d", 1 + i % 3);
            float at = fabsf(t);
            if (at < hud_dur(name)) {
                s_chunk_t[i] = t > 0 ? t + 1.0f / 60 : t - 1.0f / 60;
                sprite_draw_screen(s_hud, hud_frame(name, at, false), HUD_X(tx + 0.085f + 0.18f * i), HUD_Y(ty), K, NULL);
                continue;
            }
            s_chunk_t[i] = 0;
        }
        if (i >= s_hero.silk) continue;
        snprintf(name, sizeof name, glow ? "SilkChunk Glow %d" : "SilkChunk Idle %d", 1 + i % 3);
        sprite_draw_screen(s_hud, hud_frame(name, s_hud_t + i * 0.17f, true), HUD_X(tx + 0.085f + 0.18f * i), HUD_Y(ty), K,
                           NULL);
    }
    // the counters, a few seconds after a pickup: rosaries, then shell shards one unit lower
    int geo, shards;
    float shown = currency_shown(&geo, &shards);
    if (shown < 3.5f) {
        u8 a = shown < 3 ? 255 : (u8)(255 * (3.5f - shown) / 0.5f);
        C2D_ImageTint tint;
        C2D_AlphaImageTint(&tint, a / 255.0f);
        char txt[16];
        sprite_draw_screen(s_spool, spool_frame("HUD_coin_v020004"), HUD_X(-13.77f), HUD_Y(1.967f), K, &tint);
        snprintf(txt, sizeof txt, "%d", geo);
        hud_text(txt, HUD_X(-13.2f), HUD_Y(1.89f), 0.36f, C2D_Color32(255, 255, 255, a));
        sprite_draw_screen(s_spool, spool_frame("shell_shard_icon0004"), HUD_X(-13.77f), HUD_Y(0.963f), K, &tint);
        snprintf(txt, sizeof txt, "%d", shards);
        hud_text(txt, HUD_X(-13.2f), HUD_Y(0.89f), 0.36f, C2D_Color32(255, 255, 255, a));
    }
}

// a centred line of text in the system font (title, pause)
static void draw_text(const char *str, float cx, float y, float size, u32 color) {
    if (!s_tbuf) return;
    C2D_Text t;
    C2D_TextBufClear(s_tbuf);
    C2D_TextParse(&t, s_tbuf, str);
    C2D_TextOptimize(&t);
    float ax = g_view.sx / g_view.sy, w, h;
    C2D_TextGetDimensions(&t, size * ax, size, &w, &h);
    C2D_DrawText(&t, C2D_WithColor, cx - w * 0.5f, y, 0.95f, size * ax, size, color);
}

static void draw_band(float zmin, float zmax) {
    objects_draw(&s_objs, zmin, zmax);
    particles_draw(zmin, zmax);
}

static void draw_all(void);

// the scene drawn from the shaken camera
void game_draw(void) {
    g_view.cx += s_shake_x;
    g_view.cy += s_shake_y;
    draw_all();
    g_view.cx -= s_shake_x;
    g_view.cy -= s_shake_y;
}

static void draw_all(void) {
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
            draw_band(-0.001f, zcur);
            zcur = -0.001f;
            currency_draw();
            if (s_mode != GS_TITLE) hero_draw(&s_hero);
            hero_done = true;
        }
        float lower = rank == 4 ? 0.0f : L->z;
        if (lower < zcur) {
            draw_band(lower, zcur);
            zcur = lower;
        }
        room_draw_layer(&s_room, li, false);
    }
    if (!hero_done) {
        draw_band(-0.001f, zcur);
        zcur = -0.001f;
        currency_draw();
        if (s_mode != GS_TITLE) hero_draw(&s_hero);
    }
    draw_band(-1e30f, zcur);
    gfx_blend(BLEND_NORMAL);
    if (s_mode != GS_TITLE) draw_hud();
    dlg_draw();
    if (s_bench_prompt && s_mode == GS_PLAY) {   // a small arrow over Hornet: Up to sit
        float px = g_view.sw * 0.5f + (s_hero.x - g_view.cx) * g_view.sx;
        float py = g_view.sh * 0.5f - (s_hero.y + 1.9f - g_view.cy) * g_view.sy;
        float w = 5 * g_view.sx / g_view.sy;
        u32 c = C2D_Color32(240, 236, 228, 230);
        C2D_DrawTriangle(px - w, py, c, px + w, py, c, px, py - 7, c, 0.6f);
    }
    float fade = s_fade > g_fsm_fade ? s_fade : g_fsm_fade;   // the room's fade in, or a script's ScreenFader
    if (fade > 0)
        C2D_DrawRectSolid(0, 0, 0.9f, g_view.sw, g_view.sh, C2D_Color32(0, 0, 0, (u8)(clampf(fade, 0, 1) * 255)));
    if (s_mode == GS_TITLE || s_mode == GS_PAUSE) menu_draw();
}

void game_status(float fps, float ms) {
    char *o = s_status;
    size_t cap = sizeof s_status;
    int n = snprintf(o, cap, "3DSilksong %s   %.1f fps  %.1f ms\n", SS_VERSION, fps, ms);
    if (s_menu) {
        n += snprintf(o + n, cap - n, "ROOMS (%d)  A: go  B: close  L/R: page\n", s_nrooms);
        int top = s_menu_sel - 7;
        if (top < 0) top = 0;
        for (int i = 0; i < 15 && n < (int)cap - 64; i++) {
            int k = top + i;
            if (k < s_nrooms) n += snprintf(o + n, cap - n, "%s %s\n", k == s_menu_sel ? ">" : " ", s_rooms[k]);
        }
        return;
    }
    n += snprintf(o + n, cap - n, "Room %s\nPos %.2f %.2f %s  st %d\nCam %.2f %.2f\n", s_room.scene, s_hero.x, s_hero.y,
                  s_hero.ground ? "ground" : "air", s_hero.state, g_view.cx, g_view.cy);
    n += snprintf(o + n, cap - n, "Masks %d/%d  objects %d  silk %d/%d\n", s_hero.hp, s_hero.maxhp, s_objs.n, s_hero.silk,
                  SILK_MAX);
    n += snprintf(o + n, cap - n, "Tiles %d  linear %lu KB free\n", room_cache_used(),
                  (unsigned long)(linearSpaceFree() / 1024));
    n += snprintf(o + n, cap - n, "Music %s\n%s\n", audio_on() ? (audio_track(0)[0] ? audio_track(0) : "-") : "off",
                  s_msg_t > 0 ? s_msg : "");
}

// the bottom screen (320x240): the area's name on a dark screen (the map and the inventory will be here), or the
// debug info when the option is on
void game_draw_bottom(void) {
    View save = g_view;
    g_view.sw = 320; g_view.sh = 240; g_view.sx = g_view.sy;   // square pixels down there
    C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, C2D_Color32(8, 7, 10, 255));
    if (menu_debug() || s_menu) {
        if (s_tbuf) {
            C2D_Text t;
            C2D_TextBufClear(s_tbuf);
            C2D_TextParse(&t, s_tbuf, s_status);
            C2D_TextOptimize(&t);
            C2D_DrawText(&t, C2D_WithColor, 6, 4, 0.6f, 0.42f, 0.42f, C2D_Color32(230, 226, 220, 255));
        }
    } else if (s_mode != GS_TITLE && s_loaded) {
        const char *z = menu_zone_name(s_room.scene);
        Font *f = font_get("trajanpro_bold_sdf");
        if (z[0]) font_draw(f, z, 160, 18, 16, C2D_Color32(220, 214, 200, 255), FONT_CENTER);
    }
    g_view = save;
}
