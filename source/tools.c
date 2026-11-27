// Hornet's tools and crests (text/tools.txt, tools/build_tools.py), as the game keeps them:
// - a tool is hers once found or bought (PlayerData tool:<name>);
// - her crest (PlayerData crest, Hunter by default) has slots: red (attack), blue (defence), yellow (exploration),
//   skill; a tool goes in a free slot of its colour when she gets it (equip:<crest>:<slot> = tool index + 1);
// - red tools are thrown with R (up, neutral or down: the slot's binding), each use taken from its stock
//   (tooluse:<name>), refilled at benches; their projectiles are packs (rooms/pf_*.*) that hurt enemies.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tools.h"
#include "fsm.h"
#include "audio.h"
#include "sprite.h"

#define MAX_TOOLS 96
#define MAX_CRESTS 16
#define MAX_SLOTS 12

typedef struct {
    char name[40], prefab[40], icon[8], hud[8], sheet[12], key[40], dsheet[12], dkey[40];
    int type, storage;
    float vx, vy, cooldown;
} Tool;

typedef struct { int type; float x, y; int binding, locked; } Slot;

typedef struct {
    char name[24], icon[8], sheet[12], key[40];
    int nslots;
    Slot slot[MAX_SLOTS];
} Crest;

static Tool s_tool[MAX_TOOLS];
static int s_ntools;
static Crest s_crest[MAX_CRESTS];
static int s_ncrests;
static float s_cool;
static SpriteSet *s_icons;

static void unbar(char *s) { for (; *s; s++) if (*s == '|') *s = ' '; }

void tools_init(void) {
    if (s_ntools) return;
    size_t size = 0;
    char *txt = file_load(data_path("text/tools.txt"), &size);
    if (!txt) return;
    char *save = NULL;
    for (char *l = strtok_r(txt, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        if (l[0] == 'T' && s_ntools < MAX_TOOLS) {
            Tool *t = &s_tool[s_ntools];
            char ev[40];
            if (sscanf(l, "T %39s %d %11s %39s %11s %39s %7s %7s %d %39s %f %f %f %39s", t->name, &t->type, t->sheet,
                       t->key, t->dsheet, t->dkey, t->icon, t->hud, &t->storage, t->prefab, &t->vx, &t->vy,
                       &t->cooldown, ev) >= 13) {
                unbar(t->name);
                s_ntools++;
            }
        } else if (l[0] == 'C' && s_ncrests < MAX_CRESTS) {
            Crest *c = &s_crest[s_ncrests];
            char ds[12], dk[40];
            if (sscanf(l, "C %23s %11s %39s %11s %39s %7s %d", c->name, c->sheet, c->key, ds, dk, c->icon, &c->nslots) == 7) {
                if (c->nslots > MAX_SLOTS) c->nslots = MAX_SLOTS;
                s_ncrests++;
            }
        } else if (l[0] == 'S') {
            char cn[24];
            int i, type, b, lk;
            float x, y;
            if (sscanf(l, "S %23s %d %d %f %f %d %d", cn, &i, &type, &x, &y, &b, &lk) == 7)
                for (int k = 0; k < s_ncrests; k++)
                    if (!strcmp(s_crest[k].name, cn) && i < MAX_SLOTS) s_crest[k].slot[i] = (Slot){type, x, y, b, lk};
        }
    }
    free(txt);
}

static int crest_now(void) {
    int c = pd_int("crest");                 // its index + 1; 0: the Hunter's, as a new game starts
    if (c > 0 && c <= s_ncrests) return c - 1;
    for (int k = 0; k < s_ncrests; k++) if (!strcmp(s_crest[k].name, "Hunter")) return k;
    return 0;
}

static bool owned(int t) {
    char k[64];
    snprintf(k, sizeof k, "tool:%s", s_tool[t].name);
    return pd_bool(k);
}

static bool slot_unlocked(int c, int i) {
    if (!s_crest[c].slot[i].locked) return true;
    char k[64];
    snprintf(k, sizeof k, "slotopen:%s:%d", s_crest[c].name, i);
    return pd_bool(k);
}

static int equipped(int c, int i) {
    char k[64];
    snprintf(k, sizeof k, "equip:%s:%d", s_crest[c].name, i);
    return pd_int(k) - 1;
}

static void equip(int c, int i, int t) {
    char k[64];
    snprintf(k, sizeof k, "equip:%s:%d", s_crest[c].name, i);
    pd_set_int(k, t + 1);
}

static int uses(int t) {
    char k[64];
    snprintf(k, sizeof k, "tooluse:%s", s_tool[t].name);
    return pd_int(k);
}

static void set_uses(int t, int n) {
    char k[64];
    snprintf(k, sizeof k, "tooluse:%s", s_tool[t].name);
    pd_set_int(k, n);
}

// every tool she owns and hasn't placed goes into a free slot of its colour (as the game does when she gets one)
void tools_sync(void) {
    tools_init();
    if (!s_ncrests) return;
    int c = crest_now();
    for (int t = 0; t < s_ntools; t++) {
        if (!owned(t)) continue;
        bool placed = false;
        for (int i = 0; i < s_crest[c].nslots && !placed; i++) if (equipped(c, i) == t) placed = true;
        if (placed) continue;
        char k[64];
        snprintf(k, sizeof k, "toolseen:%s", s_tool[t].name);
        if (pd_bool(k)) continue;   // she took it off herself: it stays off
        pd_set_bool(k, true);
        if (s_tool[t].type == 0 && !uses(t)) set_uses(t, s_tool[t].storage);
        for (int i = 0; i < s_crest[c].nslots; i++)
            if (s_crest[c].slot[i].type == s_tool[t].type && slot_unlocked(c, i) && equipped(c, i) < 0) {
                equip(c, i, t);
                break;
            }
    }
}

void tools_give_all(void) {   // the test menu
    tools_init();
    char k[64];
    for (int t = 0; t < s_ntools; t++) { snprintf(k, sizeof k, "tool:%s", s_tool[t].name); pd_set_bool(k, true); }
    tools_sync();
    tools_refill();
}

void tools_refill(void) {
    tools_init();
    for (int t = 0; t < s_ntools; t++)
        if (s_tool[t].type == 0 && owned(t)) set_uses(t, s_tool[t].storage);
}

// R: the red tool bound to the direction held (up, down, else neutral), thrown from Hornet
bool tools_use(float x, float y, int face, int dir) {
    tools_init();
    if (s_cool > 0 || !s_ncrests) return false;
    int c = crest_now(), want = dir > 0 ? 1 : dir < 0 ? 2 : 0, pick = -1;
    for (int pass = 0; pass < 2 && pick < 0; pass++)   // the binding asked, else any red tool
        for (int i = 0; i < s_crest[c].nslots && pick < 0; i++) {
            const Slot *s = &s_crest[c].slot[i];
            int t = equipped(c, i);
            if (s->type != 0 || t < 0 || t >= s_ntools || !slot_unlocked(c, i)) continue;
            if (pass == 0 && s->binding != want) continue;
            if (uses(t) <= 0 || !s_tool[t].prefab[0] || !strcmp(s_tool[t].prefab, "-")) continue;
            pick = t;
        }
    if (pick < 0) return false;
    const Tool *t = &s_tool[pick];
    float vx = t->vx * face, vy = t->vy;
    if (fsm_spawn(t->prefab, x + 0.6f * face, y + 0.9f, vx, vy, face) < 0) return false;
    set_uses(pick, uses(pick) - 1);
    s_cool = t->cooldown > 0 ? t->cooldown : 0.4f;
    sfx_play_name("hornet_throw_tool", 0.9f, 0);
    return true;
}

void tools_update(float dt) { if (s_cool > 0) s_cool -= dt; }

// the HUD: the red tools in her slots, their icon and how many uses are left
int tools_hud(ToolHud *out, int max) {
    tools_init();
    if (!s_ncrests) return 0;
    if (!s_icons) { s_icons = sprites_get("sprites/tools.sss"); if (s_icons) s_icons->pinned = true; }
    int c = crest_now(), n = 0;
    for (int i = 0; i < s_crest[c].nslots && n < max; i++) {
        int t = equipped(c, i);
        if (s_crest[c].slot[i].type != 0 || t < 0 || t >= s_ntools) continue;
        out[n].set = s_icons;
        out[n].clip = strcmp(s_tool[t].hud, "-") ? s_tool[t].hud : s_tool[t].icon;
        out[n].uses = uses(t);
        out[n].binding = s_crest[c].slot[i].binding;
        n++;
    }
    return n;
}

// ---------------------------------------------------------------- the Tools pane (bottom screen, the inventory's tab)

static int s_ui_sel;
bool g_tools_at_bench;   // set by the game: tools can be changed only while she sits at a bench

static const u32 SLOT_COL[4] = {0xFF3C3CD8, 0xFFD89A3C, 0xFF3CC8D8, 0xFFE8E8E8};   // ABGR: red, blue, yellow, white

static void draw_icon(const char *clip, float x, float y, float size) {
    if (!s_icons) { s_icons = sprites_get("sprites/tools.sss"); if (s_icons) s_icons->pinned = true; }
    if (!s_icons || !clip || !strcmp(clip, "-")) return;
    int c = sprite_clip(s_icons, clip);
    if (c < 0 || !s_icons->clips[c].n) return;
    sprite_draw_screen(s_icons, rds16((const u8 *)&s_icons->clips[c].frames[0]), x, y, size, NULL);
}

void tools_ui_update(u32 down) {
    tools_init();
    if (!s_ncrests) return;
    int c = crest_now(), n = s_crest[c].nslots;
    if (!n) return;
    int dx = (down & KEY_RIGHT) ? 1 : (down & KEY_LEFT) ? -1 : 0, dy = (down & KEY_DOWN) ? -1 : (down & KEY_UP) ? 1 : 0;
    if (dx || dy) {   // the nearest slot that way
        const Slot *cs = &s_crest[c].slot[s_ui_sel];
        int best = -1;
        float bd = 1e9f;
        for (int i = 0; i < n; i++) {
            if (i == s_ui_sel) continue;
            float ex = s_crest[c].slot[i].x - cs->x, ey = s_crest[c].slot[i].y - cs->y;
            float along = dx ? ex * dx : ey * dy, across = fabsf(dx ? ey : ex);
            if (along <= 0.2f) continue;
            float d = along + 2 * across;
            if (d < bd) { bd = d; best = i; }
        }
        if (best >= 0) { s_ui_sel = best; sfx_play_name("ui_change_selection", 0.6f, 0); }
    }
    if ((down & KEY_A) && g_tools_at_bench && slot_unlocked(c, s_ui_sel)) {
        // the next tool of the slot's colour she owns and hasn't put elsewhere, then none
        int type = s_crest[c].slot[s_ui_sel].type, cur = equipped(c, s_ui_sel), next = -1;
        for (int t = cur + 1; t < s_ntools && next < 0; t++) {
            if (s_tool[t].type != type || !owned(t)) continue;
            bool used = false;
            for (int i = 0; i < n; i++) if (i != s_ui_sel && equipped(c, i) == t) used = true;
            if (!used) next = t;
        }
        equip(c, s_ui_sel, next);
        sfx_play_name("ui_button_confirm", 0.7f, 0);
    }
}

void tools_ui_draw(void) {
    tools_init();
    if (!s_ncrests) return;
    int c = crest_now();
    const Crest *cr = &s_crest[c];
    if (s_ui_sel >= cr->nslots) s_ui_sel = 0;
    draw_icon(cr->icon, 160, 112, 44);   // the crest, under its slots
    for (int i = 0; i < cr->nslots; i++) {
        const Slot *s = &cr->slot[i];
        float x = 160 + s->x * 30, y = 112 - s->y * 30;
        bool open = slot_unlocked(c, i);
        u32 col = open ? SLOT_COL[s->type & 3] : 0xFF505050;
        C2D_DrawCircleSolid(x, y, 0.6f, i == s_ui_sel ? 17 : 15, i == s_ui_sel ? 0xFFFFFFFF : 0xFF000000);
        C2D_DrawCircleSolid(x, y, 0.61f, 13, col);
        C2D_DrawCircleSolid(x, y, 0.62f, 11, 0xFF101010);
        int t = equipped(c, i);
        if (t >= 0 && t < s_ntools) draw_icon(s_tool[t].icon, x, y, 22);
    }
}

// the selection's name and description (sheet, key) for the top screen
void tools_ui_text(const char **sh, const char **nm, const char **dsh, const char **ds) {
    static const char none[] = "";
    *sh = *nm = *dsh = *ds = none;
    tools_init();
    if (!s_ncrests) return;
    int c = crest_now(), t = s_ui_sel < s_crest[c].nslots ? equipped(c, s_ui_sel) : -1;
    if (t >= 0 && t < s_ntools) { *sh = s_tool[t].sheet; *nm = s_tool[t].key; *dsh = s_tool[t].dsheet; *ds = s_tool[t].dkey; }
    else { *sh = s_crest[c].sheet; *nm = s_crest[c].key; }
}
