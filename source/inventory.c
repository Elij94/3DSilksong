#include "inventory.h"
#include "fsm.h"
#include "shop.h"
#include "font.h"
#include "sprite.h"
#include "audio.h"
#include "dialogue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char part[16]; int state; char sheet[16], name[48], dsheet[16], desc[48]; } Disp;
typedef struct { char clip[24], test[96], sheet[16], name[48], dsheet[16], desc[48]; } Skill;
typedef struct { char sheet[16], name[48], dsheet[16], desc[48], test[160]; } CState;
typedef struct { char name[48]; bool always, overrides; int first, n; } Coll;

static Disp s_d[24];
static int s_nd;
static Skill s_sk[12];
static int s_nsk;
static CState *s_cs;
static int s_ncs;
static Coll s_c[128];
static int s_nc;
static bool s_loaded, s_open;
static SpriteSet *s_set, *s_hud;

static void unspace(char *s) { for (; *s; s++) if (*s == 0x1f) *s = ' '; }
static void dash(char *s) { if (!strcmp(s, "-")) s[0] = 0; else unspace(s); }

static void load(void) {
    if (s_loaded) return;
    s_loaded = true;
    FILE *f = fopen(data_path("text/inventory.txt"), "r");
    if (!f) return;
    int cap = 256;
    s_cs = calloc(cap, sizeof *s_cs);
    char line[512];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == 'D' && s_nd < 24) {
            Disp *d = &s_d[s_nd];
            if (sscanf(line, "D %15s %d %15s %47s %15s %47s", d->part, &d->state, d->sheet, d->name, d->dsheet, d->desc) == 6) {
                dash(d->sheet); dash(d->name); dash(d->dsheet); dash(d->desc);
                s_nd++;
            }
        } else if (line[0] == 'K' && s_nsk < 12) {
            Skill *k = &s_sk[s_nsk];
            if (sscanf(line, "K %23s %95s %15s %47s %15s %47s", k->clip, k->test, k->sheet, k->name, k->dsheet, k->desc) == 6) {
                dash(k->clip); dash(k->test); dash(k->sheet); dash(k->name); dash(k->dsheet); dash(k->desc);
                s_nsk++;
            }
        } else if (line[0] == 'C' && s_nc < 128) {
            Coll *c = &s_c[s_nc];
            int i, al, ov;
            if (sscanf(line, "C %d %47s %d %d", &i, c->name, &al, &ov) == 4) {
                unspace(c->name);
                c->always = al; c->overrides = ov;
                c->first = s_ncs; c->n = 0;
                s_nc++;
            }
        } else if (line[0] == 'S' && s_nc > 0) {
            if (s_ncs == cap) {
                void *p = realloc(s_cs, sizeof *s_cs * cap * 2);
                if (!p) break;
                s_cs = p;
                cap *= 2;
            }
            CState *st = &s_cs[s_ncs];
            int i, k;
            if (sscanf(line, "S %d %d %15s %47s %15s %47s %159s", &i, &k, st->sheet, st->name, st->dsheet, st->desc, st->test) == 7) {
                dash(st->sheet); dash(st->name); dash(st->dsheet); dash(st->desc); dash(st->test);
                s_ncs++;
                s_c[s_nc - 1].n++;
            }
        }
    }
    fclose(f);
}

static const Disp *disp(const char *part, int state) {
    for (int i = 0; i < s_nd; i++) if (!strcmp(s_d[i].part, part) && s_d[i].state == state) return &s_d[i];
    return NULL;
}

// CollectableItem: how many she holds; its shown state (CollectableItemStates: the last whose test passes), -1
static int coll_count(const Coll *c) {
    char k[64];
    snprintf(k, sizeof k, "item:%s", c->name);
    return pd_int(k) + (c->always ? 1 : 0);
}
static int coll_state(const Coll *c) {
    int s = -1;
    for (int i = 0; i < c->n; i++) if (pd_conditions(s_cs[c->first + i].test)) s = i;
    return s;
}
static bool coll_shown(const Coll *c) {
    if (c->overrides) return coll_state(c) >= 0;
    return coll_count(c) > 0 && coll_state(c) >= 0;
}

// ---------------------------------------------------------------- what can be selected

enum { E_NAIL, E_HEART, E_SPOOLP, E_THREAD, E_SKILL, E_COLL };
typedef struct { int kind, i; float x, y; } Entry;
static Entry s_e[160];
static int s_ne, s_sel, s_scroll;   // s_scroll: the first row of collectables shown

#define GRID_X 136
#define GRID_Y 34
#define CELL 30
#define COLS 6
#define ROWS 6   // rows of collectables shown at a time (the description is on the top screen)

static void build(void) {
    s_ne = 0;
    s_e[s_ne++] = (Entry){E_NAIL, 0, 34, 62};
    s_e[s_ne++] = (Entry){E_THREAD, 0, 92, 62};
    s_e[s_ne++] = (Entry){E_HEART, 0, 22, 112};
    s_e[s_ne++] = (Entry){E_SPOOLP, 0, 58, 112};
    int ns = 0;
    for (int i = 0; i < s_nsk; i++)
        if (pd_conditions(s_sk[i].test)) { s_e[s_ne++] = (Entry){E_SKILL, i, 14 + (ns % 4) * 26, 142 + (ns / 4) * 24}; ns++; }
    int n = 0;
    for (int i = 0; i < s_nc && s_ne < 160; i++)
        if (coll_shown(&s_c[i])) {
            s_e[s_ne++] = (Entry){E_COLL, i, GRID_X + 12 + (n % COLS) * CELL, GRID_Y + 12 + (n / COLS) * CELL};
            n++;
        }
    if (s_sel >= s_ne) s_sel = 0;
}

void inv_open(void) {
    load();
    s_open = true;
    s_sel = 0;
    s_scroll = 0;
    build();
    if (!s_set) s_set = sprites_get("ui/inv_pane.sss");
    if (!s_hud) s_hud = sprites_get("sprites/hud_spool.sss");
    sfx_play_name("ui_button_confirm", 0.7f, 0);
}

void inv_close(void) {
    s_open = false;
    if (s_set) { sprites_put(s_set); s_set = NULL; }
    sfx_play_name("ui_change_selection", 0.6f, 0);
}

bool inv_active(void) { return s_open; }

void inv_update(u32 down) {
    if (!s_open) return;
    if (down & (KEY_SELECT | KEY_B | KEY_START)) { inv_close(); return; }
    int dx = (down & KEY_RIGHT) ? 1 : (down & KEY_LEFT) ? -1 : 0;
    int dy = (down & KEY_DOWN) ? 1 : (down & KEY_UP) ? -1 : 0;
    if (!dx && !dy) return;
    // the nearest entry that way (more weight on the cross axis)
    const Entry *c = &s_e[s_sel];
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < s_ne; i++) {
        if (i == s_sel) continue;
        float ex = s_e[i].x - c->x, ey = s_e[i].y - c->y;
        float along = dx ? ex * dx : ey * dy, across = dx ? ey : ex;
        if (along <= 2) continue;
        float d = along + 2.5f * (across < 0 ? -across : across);
        if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0) { s_sel = best; sfx_play_name("ui_change_selection", 0.6f, 0); }
}

// ---------------------------------------------------------------- drawing

static int clip_frame(const char *clip) {
    int c = s_set ? sprite_clip(s_set, clip) : -1;
    return c >= 0 && s_set->clips[c].n ? rds16((const u8 *)&s_set->clips[c].frames[0]) : -1;
}

static void pic(const char *clip, float x, float y, float size, const C2D_ImageTint *tint) {
    int fr = clip_frame(clip);
    if (fr < 0) return;
    const SFrame *q = &s_set->frames[fr];
    float w = q->x1 - q->x0, h = q->y1 - q->y0, m = w > h ? w : h;
    if (m > 0) sprite_draw_screen(s_set, fr, x, y, size / m, tint);
}

static Font *title_font(void) { Font *f = font_get("trajanpro_bold_sdf"); return f ? f : font_get("trajan_bold_tmpro"); }
static Font *body_font(void) { Font *f = font_get("perpetua_tmpro"); return f ? f : font_get("perpetua_sdf"); }

static void text_of(const char *sheet, const char *key, char *out, size_t cap) {
    out[0] = 0;
    if (!key[0] || !text_line(sheet, key, out, cap)) { out[0] = 0; return; }
    for (char *p = out; *p; p++) if (*p == '\f') *p = ' ';
}

// the selection's name and description (sheet, key pairs)
static void sel_text(const char **sh, const char **nm, const char **dsh, const char **ds) {
    *sh = *nm = *dsh = *ds = "";
    if (!s_ne) return;
    const Entry *e = &s_e[s_sel];
    int nail = pd_int("nailUpgrades");
    nail = nail < 0 ? 0 : nail > 4 ? 4 : nail;
    const Disp *d = NULL;
    if (e->kind == E_NAIL) d = disp("nail", nail);
    else if (e->kind == E_HEART) d = disp("heart", pd_int("heartPieces") % 4);
    else if (e->kind == E_SPOOLP) d = disp("spool_piece", pd_int("silkSpoolParts") % 2);
    else if (e->kind == E_THREAD) d = disp("thread", 0);
    if (d) { *sh = d->sheet; *nm = d->name; *dsh = d->dsheet; *ds = d->desc; }
    else if (e->kind == E_SKILL) { const Skill *k = &s_sk[e->i]; *sh = k->sheet; *nm = k->name; *dsh = k->dsheet; *ds = k->desc; }
    else if (e->kind == E_COLL) {
        const Coll *cl = &s_c[e->i];
        int st = coll_state(cl);
        const CState *c = &s_cs[cl->first + (st < 0 ? 0 : st)];
        *sh = c->sheet; *nm = c->name; *dsh = c->dsheet; *ds = c->desc;
    }
}

// the top screen while it is open: the game dimmed, the selection's name and description in large letters
void inv_draw_top(void) {
    if (!s_open) return;
    const char *sh, *nm, *dsh, *ds;
    sel_text(&sh, &nm, &dsh, &ds);
    float ax = g_view.sx / g_view.sy, cx = g_view.sw * 0.5f;
    C2D_DrawRectSolid(0, 0, 0.9f, g_view.sw, g_view.sh, C2D_Color32(0, 0, 0, 170));
    char t[512];
    text_of(sh, nm, t, sizeof t);
    font_draw(title_font(), t, cx, 34, 20, C2D_Color32(240, 236, 228, 255), FONT_CENTER);
    C2D_DrawRectSolid(cx - 120 * ax, 64, 0.95f, 240 * ax, 1, C2D_Color32(150, 146, 140, 255));
    text_of(dsh, ds, t, sizeof t);
    font_draw_wrapped(body_font(), t, cx - 170 * ax, 78, 16, C2D_Color32(214, 210, 204, 255), 340 * ax, FONT_LEFT);
}

void inv_draw(void) {
    if (!s_open) return;
    build();   // what she holds can't change while it is open, but the list is cheap
    u32 white = C2D_Color32(240, 236, 228, 255), grey = C2D_Color32(160, 156, 150, 255);
    char t[512], num[16];
    C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, C2D_Color32(6, 5, 8, 255));
    text_line("UI", "PANE_INVENTORY", t, sizeof t);
    font_draw(title_font(), t, 160, 6, 13, white, FONT_CENTER);
    C2D_DrawRectSolid(124, 30, 0.6f, 1, 196, C2D_Color32(120, 116, 110, 255));   // the divider
    // the needle (PlayerData nailUpgrades), the silk spool, the heart and spool pieces
    int nail = pd_int("nailUpgrades");
    if (nail < 0) nail = 0;
    if (nail > 4) nail = 4;
    snprintf(num, sizeof num, "nail%d", nail + 1);
    pic(num, 34, 62, 52, NULL);
    pic(pd_int("silkMax") > 9 ? "spool_wide" : "spool_slim", 92, 62, 44, NULL);
    int hp = pd_int("heartPieces") % 4;
    pic("heart_back", 22, 112, 30, NULL);
    for (int i = 1; i <= hp; i++) { snprintf(num, sizeof num, "heart%d", i); pic(num, 22, 112, 30, NULL); }
    int sp = pd_int("silkSpoolParts") % 2;
    pic("spool_piece", 58, 112, 30, NULL);
    if (sp) pic("spool_half", 58, 112, 30, NULL);
    // the rosaries and shell shards
    if (s_hud) {
        int gc = sprite_clip(s_hud, "HUD_coin_v020004"), sc = sprite_clip(s_hud, "shell_shard_icon0004");
        if (gc >= 0 && s_hud->clips[gc].n) sprite_draw_screen(s_hud, rds16((const u8 *)&s_hud->clips[gc].frames[0]), 92, 108, 11, NULL);
        if (sc >= 0 && s_hud->clips[sc].n) sprite_draw_screen(s_hud, rds16((const u8 *)&s_hud->clips[sc].frames[0]), 92, 124, 11, NULL);
    }
    snprintf(num, sizeof num, "%d", pd_int("geo"));
    font_draw(title_font(), num, 100, 102, 10, white, FONT_LEFT);
    snprintf(num, sizeof num, "%d", pd_int("ShellShards"));
    font_draw(title_font(), num, 100, 118, 10, white, FONT_LEFT);
    // the skills, the collectables (ROWS rows of them at a time)
    if (s_ne && s_e[s_sel].kind == E_COLL) {
        int row = (int)((s_e[s_sel].y - GRID_Y - 12) / CELL + 0.5f);
        if (row < s_scroll) s_scroll = row;
        if (row > s_scroll + ROWS - 1) s_scroll = row - ROWS + 1;
    }
    for (int i = 0; i < s_ne; i++) {
        Entry ed = s_e[i];
        const Entry *e = &ed;
        if (e->kind == E_COLL) {
            ed.y -= s_scroll * CELL;
            if (ed.y < GRID_Y || ed.y > GRID_Y + ROWS * CELL) continue;
        }
        if (e->kind == E_SKILL) pic(s_sk[e->i].clip, e->x, e->y, 22, NULL);
        else if (e->kind == E_COLL) {
            const Coll *c = &s_c[e->i];
            snprintf(num, sizeof num, "c%d_%d", e->i, coll_state(c) < 0 ? 0 : coll_state(c));
            pic(num, e->x, e->y, 24, NULL);
            int n = coll_count(c);
            if (n > 1) {
                snprintf(num, sizeof num, "%d", n);
                C2D_DrawRectSolid(e->x + 3, e->y + 4, 0.9f, 11, 10, C2D_Color32(6, 5, 8, 200));
                font_draw(title_font(), num, e->x + 13, e->y + 4, 10, white, FONT_RIGHT);
            }
        }
    }
    // the selection: a frame round it, its name and description
    if (s_ne) {
        Entry es = s_e[s_sel];
        if (es.kind == E_COLL) es.y -= s_scroll * CELL;
        const Entry *e = &es;
        float r = e->kind == E_NAIL || e->kind == E_THREAD ? 26 : e->kind == E_COLL ? 14 : 15;
        u32 c = C2D_Color32(240, 236, 228, 200);
        C2D_DrawRectSolid(e->x - r, e->y - r, 0.7f, 2 * r, 1, c);
        C2D_DrawRectSolid(e->x - r, e->y + r, 0.7f, 2 * r, 1, c);
        C2D_DrawRectSolid(e->x - r, e->y - r, 0.7f, 1, 2 * r, c);
        C2D_DrawRectSolid(e->x + r, e->y - r, 0.7f, 1, 2 * r + 1, c);
    }
}
