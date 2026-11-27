// The items lying in the rooms (the game's CollectableItemPickup), from text/pickups.txt (tools/build_pickups.py):
// drawn where they lie, picked up when Hornet touches one: the item counted in the save (item:<name>, a tool's
// tool:<name>, a PlayerDataCollectable's PlayerData field, the pickup's own PlayerData bool), never seen again
// (pickup:<room>:<index>), its icon and name shown a moment.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pickups.h"
#include "sprite.h"
#include "fsm.h"
#include "font.h"
#include "audio.h"
#include "dialogue.h"

#define MAX_PICKUPS 16

typedef struct {
    int index;
    float x, y;
    char kind[28], item[48], pd[48], clip[8], icon[8], sheet[24], key[48], link[48];
    bool taken;
} Pickup;

static Pickup s_p[MAX_PICKUPS];
static int s_n;
static char s_room[48];
static SpriteSet *s_set;
static float s_t;
static char s_pop_name[96], s_pop_icon[8];
static float s_pop_t = -1;

static void unbar(char *s) { for (; *s; s++) if (*s == '|') *s = ' '; }

static void flag_name(char *out, size_t n, int index) { snprintf(out, n, "pickup:%s:%d", s_room, index); }

void pickups_load(const char *scene) {
    s_n = 0;
    snprintf(s_room, sizeof s_room, "%s", scene);
    size_t size = 0;
    char *txt = file_load(data_path("text/pickups.txt"), &size);
    if (!txt) return;
    char *save = NULL;
    for (char *line = strtok_r(txt, "\n", &save); line && s_n < MAX_PICKUPS; line = strtok_r(NULL, "\n", &save)) {
        Pickup p;
        memset(&p, 0, sizeof p);
        char room[48];
        if (sscanf(line, "%47s %d %f %f %27s %47s %47s %7s %7s %23s %47s %47s", room, &p.index, &p.x, &p.y, p.kind,
                   p.item, p.pd, p.clip, p.icon, p.sheet, p.key, p.link) < 8 || strcmp(room, scene))
            continue;
        unbar(p.item); unbar(p.pd); unbar(p.sheet); unbar(p.key); unbar(p.link);
        char k[96];
        flag_name(k, sizeof k, p.index);
        if (pd_bool(k) || (strcmp(p.pd, "-") && pd_bool(p.pd))) continue;   // already picked up
        s_p[s_n++] = p;
    }
    free(txt);
    if (s_n && !s_set) {
        s_set = sprites_get("sprites/pickups.sss");
        if (s_set) s_set->pinned = true;
    }
}

static void give(const Pickup *p) {
    char k[96];
    if (!strncmp(p->kind, "ToolItem", 8)) {
        snprintf(k, sizeof k, "tool:%s", p->item);
        pd_set_bool(k, true);
    } else if (!strcmp(p->kind, "PlayerDataCollectable") && strcmp(p->link, "-")) {
        if (p->link[0] == 'b') pd_set_bool(p->link + 2, true);
        else pd_set_int(p->link + 2, pd_int(p->link + 2) + 1);
    } else {
        snprintf(k, sizeof k, "item:%s", p->item);
        pd_set_int(k, pd_int(k) + 1);
    }
    if (strcmp(p->pd, "-")) pd_set_bool(p->pd, true);
    flag_name(k, sizeof k, p->index);
    pd_set_bool(k, true);
    // the popup: its name (the text sheets), else the asset's name
    if (strcmp(p->key, "-") && text_line(p->sheet, p->key, s_pop_name, sizeof s_pop_name)) {}
    else snprintf(s_pop_name, sizeof s_pop_name, "%s", p->item);
    snprintf(s_pop_icon, sizeof s_pop_icon, "%s", strcmp(p->icon, "-") ? p->icon : p->clip);
    s_pop_t = 0;
    sfx_play_name("shiny_item_pickup", 0.9f, 0);
}

void pickups_update(float dt, float x0, float y0, float x1, float y1) {
    s_t += dt;
    if (s_pop_t >= 0 && (s_pop_t += dt) > 3.0f) s_pop_t = -1;
    for (int i = 0; i < s_n; i++) {
        Pickup *p = &s_p[i];
        if (p->taken) continue;
        if (p->x + 0.6f > x0 && p->x - 0.6f < x1 && p->y + 0.8f > y0 && p->y - 0.6f < y1) {
            p->taken = true;
            give(p);
        }
    }
}

static int clip_frame(const char *clip) {
    if (!s_set || !strcmp(clip, "-")) return -1;
    int c = sprite_clip(s_set, clip);
    if (c < 0 || !s_set->clips[c].n) return -1;
    return rds16((const u8 *)&s_set->clips[c].frames[0]);
}

void pickups_draw(void) {
    for (int i = 0; i < s_n; i++) {
        Pickup *p = &s_p[i];
        if (p->taken) continue;
        int fr = clip_frame(strcmp(p->clip, "-") ? p->clip : p->icon);
        if (fr < 0) fr = clip_frame("p1");   // the game's shiny glint when it has no picture of its own
        if (fr < 0) continue;
        float bob = sinf(s_t * 2.5f + i) * 0.06f;
        sprite_draw(s_set, fr, p->x, p->y + bob, 1, 1, 1, NULL);
    }
}

void pickups_draw_popup(float sw, float sh) {
    if (s_pop_t < 0) return;
    float a = s_pop_t < 0.3f ? s_pop_t / 0.3f : s_pop_t > 2.4f ? (3.0f - s_pop_t) / 0.6f : 1;
    if (a <= 0) return;
    Font *f = font_get("trajanpro_bold_sdf");
    if (!f) f = font_get("trajan_bold_tmpro");
    u32 c = C2D_Color32(236, 230, 218, (u8)(fminf(a, 1) * 255));
    C2D_DrawRectSolid(sw * 0.25f, sh * 0.74f, 0.9f, sw * 0.5f, sh * 0.14f, C2D_Color32(0, 0, 0, (u8)(fminf(a, 1) * 150)));
    int fr = clip_frame(s_pop_icon);
    if (fr >= 0) sprite_draw_screen(s_set, fr, sw * 0.3f, sh * 0.81f, 22, NULL);
    if (f) font_draw(f, s_pop_name, sw * 0.36f, sh * 0.775f, 12, c, FONT_LEFT);
}
