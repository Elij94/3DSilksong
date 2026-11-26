#include "shop.h"
#include "fsm.h"
#include "map.h"
#include "font.h"
#include "sprite.h"
#include "audio.h"
#include "dialogue.h"
#include "autotest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char sheet[24], name[48], dsheet[24], desc[48];
    int cost, currency, flags_type, frame, flags;
    char pd[40], saved[48];
    char appear[160], bools[160], ints[96], event[40];
} Item;

typedef struct { char scene[40], obj[48]; int stock; } Owner;
typedef struct { int first, n; } Stock;

static Item *s_it;
static int s_nit;
static Owner *s_own;
static int s_nown;
static Stock s_st[32];
static int s_nst;
static bool s_loaded;

enum { SH_CLOSED, SH_LIST, SH_CONFIRM, SH_THANKS };
static int s_mode, s_stock = -1, s_owner = -1;
static int s_avail[64], s_navail, s_sel, s_top, s_yes;   // the items for sale (indices into s_it), the selection
static float s_t, s_jitter;
static bool s_bought, s_bought_map;
static char s_after[40];      // eventAfterPurchased of what was bought: sent to the register once the menu is gone
static SpriteSet *s_icons, *s_hud;

static void unspace(char *s) { for (; *s; s++) if (*s == 0x1f) *s = ' '; }
static void dash(char *s) { if (!strcmp(s, "-")) s[0] = 0; else unspace(s); }

void shop_init(void) {
    if (s_loaded) return;
    s_loaded = true;
    FILE *f = fopen(data_path("text/shops.txt"), "r");
    if (!f) return;
    int cap_it = 128, cap_own = 64;
    s_it = calloc(cap_it, sizeof *s_it);
    s_own = calloc(cap_own, sizeof *s_own);
    char line[768];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == 'S' && s_nown < cap_own) {
            Owner *o = &s_own[s_nown];
            if (sscanf(line, "S %39s %47s %d", o->scene, o->obj, &o->stock) == 3) { unspace(o->obj); s_nown++; }
        } else if (line[0] == 'K' && s_nst < 32) {
            s_st[s_nst].first = s_nit;
            s_st[s_nst].n = 0;
            s_nst++;
        } else if (line[0] == 'I' && s_nst > 0) {
            if (s_nit == cap_it) {
                void *p = realloc(s_it, sizeof *s_it * cap_it * 2);
                if (!p) break;
                s_it = p;
                cap_it *= 2;
            }
            Item *it = &s_it[s_nit];
            memset(it, 0, sizeof *it);
            if (sscanf(line, "I %23s %47s %23s %47s %d %d %d %d %39s %47s %d %159s %159s %95s %39s", it->sheet, it->name,
                       it->dsheet, it->desc, &it->cost, &it->currency, &it->flags_type, &it->frame, it->pd, it->saved,
                       &it->flags, it->appear, it->bools, it->ints, it->event) != 15)
                continue;
            dash(it->sheet); dash(it->name); dash(it->dsheet); dash(it->desc); dash(it->pd); dash(it->saved);
            dash(it->appear); dash(it->bools); dash(it->ints); dash(it->event);
            s_nit++;
            s_st[s_nst - 1].n++;
        }
    }
    fclose(f);
}

int shop_of(const char *scene, const char *object) {
    shop_init();
    if (!scene || !object) return -1;
    for (int i = 0; i < s_nown; i++)
        if (!strcmp(s_own[i].scene, scene) && !strcmp(s_own[i].obj, object)) return s_own[i].stock;
    return -1;
}

// ---------------------------------------------------------------- the stock (ShopItem)

// "T:<tool>" bought once; "C:<collectable>" counted
static void saved_key(const Item *it, char *out, size_t cap) {
    snprintf(out, cap, "%s%s", it->saved[0] == 'T' ? "tool:" : "item:", it->saved + 2);
}

static bool purchased(const Item *it) {   // IsPurchased
    if (it->pd[0] && pd_bool(it->pd)) return true;
    if (it->saved[0] == 'T') { char k[64]; saved_key(it, k, sizeof k); return pd_bool(k); }
    return false;
}

static bool test_one(const char *t) {   // a PlayerDataTest test: "b:<field>:<0|1>", "i:<field>:<numType>:<value>"
    char kind, field[64];
    if (!strcmp(t, "T")) return true;
    if (sscanf(t, "%c:%63[^:]", &kind, field) != 2) return false;
    const char *rest = t + 2 + strlen(field);
    if (kind == 'b') return pd_bool(field) == (atoi(rest + 1) != 0);
    if (kind == 'i') {
        int nt = 0;
        float v = 0;
        if (sscanf(rest, ":%d:%f", &nt, &v) != 2) return false;
        float x = (float)pd_int(field);
        return nt == 0 ? x == v : nt == 1 ? x != v : nt == 2 ? x < v : x > v;
    }
    return false;
}

static bool conditions(const char *appear) {   // any group whose tests all pass (none: passes)
    if (!appear[0]) return true;
    char buf[160];
    snprintf(buf, sizeof buf, "%s", appear);
    for (char *g = buf; g;) {
        char *next = strchr(g, '|');
        if (next) *next++ = 0;
        bool all = true;
        for (char *t = g; t && all;) {
            char *nt = strchr(t, ',');
            if (nt) *nt++ = 0;
            all = test_one(t);
            t = nt;
        }
        if (all) return true;
        g = next;
    }
    return false;
}

static bool available(const Item *it) {   // IsAvailable (items needing what we can't give yet aren't shown)
    return !it->flags && !purchased(it) && conditions(it->appear);
}

static bool not_infinite(const Item *it) { return it->pd[0] || it->saved[0] == 'T'; }

bool shop_has_stock(int stock, bool only_unique) {
    shop_init();
    if (stock < 0 || stock >= s_nst) return false;
    for (int i = 0; i < s_st[stock].n; i++) {
        const Item *it = &s_it[s_st[stock].first + i];
        if (available(it) && (!only_unique || not_infinite(it))) return true;
    }
    return false;
}

static void build_list(void) {
    s_navail = 0;
    if (s_stock < 0) return;
    for (int i = 0; i < s_st[s_stock].n && s_navail < 64; i++)
        if (available(&s_it[s_st[s_stock].first + i])) s_avail[s_navail++] = s_st[s_stock].first + i;
    if (s_sel >= s_navail) s_sel = s_navail ? s_navail - 1 : 0;
}

static int wallet(int currency) { return pd_int(currency ? "ShellShards" : "geo"); }

static void set_purchased(const Item *it) {   // ShopItem.SetPurchased
    const char *cur = it->currency ? "ShellShards" : "geo";
    pd_set_int(cur, pd_int(cur) - it->cost);
    if (it->pd[0]) pd_set_bool(it->pd, true);
    if (it->saved[0]) {
        char k[64];
        saved_key(it, k, sizeof k);
        if (it->saved[0] == 'T') pd_set_bool(k, true);
        else pd_set_int(k, pd_int(k) + 1);
    }
    char buf[160];
    snprintf(buf, sizeof buf, "%s", it->bools);
    for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) pd_set_bool(t, true);
    snprintf(buf, sizeof buf, "%s", it->ints);
    for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {   // PlayerDataIntOperation
        char name[64];
        int op, n;
        if (sscanf(t, "%63[^:]:%d:%d", name, &op, &n) != 3) continue;
        int v = pd_int(name);
        v = op == 0 ? v + n : op == 1 ? v - n : op == 2 ? v * n : n;
        pd_set_int(name, v);
    }
    if (it->flags_type & 2) { map_bench_update(); s_bought_map = true; }   // a map: GameManager.UpdateGameMap
    if (it->event[0]) snprintf(s_after, sizeof s_after, "%s", it->event);
    s_bought = true;
}

// ---------------------------------------------------------------- the menu (shop_control)

void shop_select(int stock) { s_stock = stock; }
bool shop_active(void) { return s_mode != SH_CLOSED; }
bool shop_was_purchased(void) { return s_bought; }

static void close_menu(void) {   // Down / Special Close: then SHOP CLOSED, the event after purchase
    sfx_play_name("ui_shop_or_questboard_dissapear", 0.9f, 0);
    s_mode = SH_CLOSED;
    if (s_icons) { sprites_put(s_icons); s_icons = NULL; }
    if (!shop_has_stock(s_stock, false)) fsm_broadcast("SHOP NO STOCK");
    fsm_broadcast("SHOP CLOSED");
    if (s_after[0]) { fsm_broadcast(s_after); s_after[0] = 0; }
}

void shop_event(const char *ev, int owner) {
    if (autotest_on()) {
        char m[96];
        snprintf(m, sizeof m, "shop event %s, stock %d, mode %d", ev, s_stock, s_mode);
        autotest_note(m);
    }
    if (strcmp(ev, "SHOP UP") || s_mode != SH_CLOSED) return;
    shop_init();
    s_owner = owner;
    s_bought = s_bought_map = false;
    s_after[0] = 0;
    s_sel = s_top = 0;
    build_list();
    if (!s_navail) { fsm_broadcast("SHOP NO STOCK"); return; }   // Stock? -> No Stock
    sfx_play_name("ui_shop_or_questboard_appear", 0.9f, 0);
    s_mode = SH_LIST;
    s_t = 0;
    if (!s_icons) s_icons = sprites_get("ui/shop_items.sss");
    if (!s_hud) s_hud = sprites_get("sprites/hud_spool.sss");
}

#define ROWS 6

void shop_update(u32 down, u32 held, float dt) {
    (void)held;
    if (s_mode == SH_CLOSED) return;
    s_t += dt;
    if (s_jitter > 0) s_jitter -= dt;
    if (s_mode == SH_LIST) {
        if (s_t < 0.15f) return;   // the window fading in
        if ((down & KEY_DOWN) && s_sel + 1 < s_navail) { s_sel++; sfx_play_name("ui_change_selection_fast", 0.8f, 0); }
        if ((down & KEY_UP) && s_sel > 0) { s_sel--; sfx_play_name("ui_change_selection_fast", 0.8f, 0); }
        if (s_sel < s_top) s_top = s_sel;
        if (s_sel >= s_top + ROWS) s_top = s_sel - ROWS + 1;
        if (down & KEY_A) {
            const Item *it = &s_it[s_avail[s_sel]];
            if (wallet(it->currency) >= it->cost) {   // CanBuy
                sfx_play_name("ui_button_confirm", 0.8f, 0);
                s_mode = SH_CONFIRM;
                s_yes = 0;
                s_t = 0;
            } else {                                   // BuyFail: the selector shakes
                sfx_play_name("sword_hit_reject", 0.8f, 0);
                s_jitter = 0.25f;
            }
        } else if (down & (KEY_B | KEY_START)) close_menu();
    } else if (s_mode == SH_CONFIRM) {
        if (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) { s_yes ^= 1; sfx_play_name("ui_change_selection", 0.8f, 0); }
        if (down & KEY_A) {
            if (s_yes == 0) {   // Oui
                set_purchased(&s_it[s_avail[s_sel]]);
                sfx_play_name("shiny_item_pickup", 1.0f, 0);
                s_mode = SH_THANKS;
                s_t = 0;
            } else { s_mode = SH_LIST; s_t = 1; sfx_play_name("ui_button_confirm", 0.8f, 0); }
        } else if (down & KEY_B) { s_mode = SH_LIST; s_t = 1; sfx_play_name("ui_change_selection", 0.8f, 0); }
    } else if (s_mode == SH_THANKS) {
        if (s_t < 1.4f) return;   // the thank-you and its particles
        build_list();
        if (s_after[0] || !s_navail) close_menu();   // CLOSE SHOP WINDOW (INACTIVE)
        else { s_mode = SH_LIST; s_t = 1; }         // RESET SHOP WINDOW
    }
}

// ---------------------------------------------------------------- drawing

static Font *title_font(void) { Font *f = font_get("trajanpro_bold_sdf"); return f ? f : font_get("trajan_bold_tmpro"); }
static Font *body_font(void) { Font *f = font_get("perpetua_tmpro"); return f ? f : font_get("perpetua_sdf"); }

static float X(float x) { return g_view.sw * 0.5f + (x - 200) * g_view.sx / g_view.sy; }   // a 400-wide layout
static float AX(void) { return g_view.sx / g_view.sy; }

static void icon(int frame, float x, float y, float size) {
    if (!s_icons || frame < 0 || frame >= s_icons->nframes) return;
    const SFrame *q = &s_icons->frames[frame];
    float w = q->x1 - q->x0, h = q->y1 - q->y0, m = w > h ? w : h;
    if (m <= 0) return;
    sprite_draw_screen(s_icons, frame, X(x), y, size / m, NULL);
}

static void coin(int currency, float x, float y, float size) {
    if (!s_hud) return;
    int c = sprite_clip(s_hud, currency ? "shell_shard_icon0004" : "HUD_coin_v020004");
    if (c >= 0 && s_hud->clips[c].n) sprite_draw_screen(s_hud, rds16((const u8 *)&s_hud->clips[c].frames[0]), X(x), y, size, NULL);
}

static void text_of(const char *sheet, const char *key, char *out, size_t cap) {
    out[0] = 0;
    if (!key[0] || !text_line(sheet, key, out, cap)) { out[0] = 0; return; }
    for (char *p = out; *p; p++) {   // page breaks: one paragraph
        if (*p == '\f') *p = ' ';
        else if (p[0] == '\\' && p[1] == 'f') { p[0] = ' '; p[1] = ' '; }
    }
}

void shop_draw(void) {
    if (s_mode == SH_CLOSED) return;
    float a = s_t < 0.15f && s_mode == SH_LIST ? s_t / 0.15f : 1;
    float d = g_draw_depth;
    C2D_DrawRectSolid(0, 0, 0.9f, g_view.sw, g_view.sh, C2D_Color32(0, 0, 0, (u8)(175 * a)));
    g_draw_depth = 0.96f;
    u8 al = (u8)(255 * a);
    u32 white = C2D_Color32(255, 255, 255, al), grey = C2D_Color32(170, 170, 170, al);
    char t[512];
    // the wallets
    char num[16];
    snprintf(num, sizeof num, "%d", wallet(0));
    coin(0, 300, 20, 12);
    font_draw(title_font(), num, X(310), 14, 12, white, FONT_LEFT);
    bool shards = false;
    for (int i = 0; i < s_navail; i++) shards |= s_it[s_avail[i]].currency == 1;
    if (shards) {
        snprintf(num, sizeof num, "%d", wallet(1));
        coin(1, 350, 20, 12);
        font_draw(title_font(), num, X(360), 14, 12, white, FONT_LEFT);
    }
    // the list: an icon and its price per row, the selection framed (hidden behind the confirm window, as the game's)
    const float L = 24, T = 40, RH = 30;
    bool list = s_mode == SH_LIST;
    for (int r = 0; list && r < ROWS && s_top + r < s_navail; r++) {
        int k = s_top + r;
        const Item *it = &s_it[s_avail[k]];
        float y = T + r * RH;
        if (k == s_sel) {
            float jx = s_jitter > 0 ? (((int)(s_jitter * 60) & 1) ? 2.f : -2.f) : 0;
            C2D_DrawRectSolid(X(L - 4 + jx), y - 2, 0.93f, 160 * AX(), RH - 2, C2D_Color32(255, 255, 255, (u8)(40 * a)));
            C2D_DrawRectSolid(X(L - 4 + jx), y - 2, 0.94f, 2 * AX(), RH - 2, white);
        }
        icon(it->frame, L + 16, y + 13, 24);
        coin(it->currency, L + 110, y + 13, 10);
        snprintf(num, sizeof num, "%d", it->cost);
        font_draw(title_font(), num, X(L + 120), y + 7, 12, wallet(it->currency) >= it->cost ? white : grey, FONT_LEFT);
    }
    u32 arrow = C2D_Color32(230, 230, 230, al);
    if (list && s_top > 0) C2D_DrawTriangle(X(L + 70), T - 10, arrow, X(L + 80), T - 10, arrow, X(L + 75), T - 16, arrow, 0.95f);
    if (list && s_top + ROWS < s_navail) {
        float y = T + ROWS * RH;
        C2D_DrawTriangle(X(L + 70), y, arrow, X(L + 80), y, arrow, X(L + 75), y + 6, arrow, 0.95f);
    }
    // the selection's name and description
    if (list && s_navail) {
        const Item *it = &s_it[s_avail[s_sel]];
        text_of(it->sheet, it->name, t, sizeof t);
        font_draw_wrapped(title_font(), t, X(200), 44, 13, white, 180 * AX(), FONT_LEFT);
        text_of(it->dsheet, it->desc, t, sizeof t);
        font_draw_wrapped(body_font(), t, X(200), 72, 11, grey, 184 * AX(), FONT_LEFT);
    }
    if (s_mode == SH_CONFIRM || s_mode == SH_THANKS) {   // the confirm window: the item, its price, Oui / Non
        const Item *it = &s_it[s_avail[s_sel]];
        C2D_DrawRectSolid(X(90), 50, 0.97f, 220 * AX(), 140, C2D_Color32(12, 10, 14, 240));
        C2D_DrawRectSolid(X(90), 50, 0.975f, 220 * AX(), 1, white);
        C2D_DrawRectSolid(X(90), 189, 0.975f, 220 * AX(), 1, white);
        g_draw_depth = 0.98f;
        icon(it->frame, 200, 82, 40);
        text_of(it->sheet, it->name, t, sizeof t);
        font_draw(title_font(), t, X(200), 106, 12, white, FONT_CENTER);
        if (s_mode == SH_CONFIRM) {
            text_line("UI", "SHOP_PURCHASE_CONFIRM", t, sizeof t);
            font_draw(body_font(), t, X(200), 124, 11, grey, FONT_CENTER);
            coin(it->currency, 190, 146, 10);
            snprintf(num, sizeof num, "%d", it->cost);
            font_draw(title_font(), num, X(200), 140, 12, white, FONT_LEFT);
            const char *keys[2] = {"NAV_YES", "NAV_NO"};
            for (int i = 0; i < 2; i++) {
                text_line("MainMenu", keys[i], t, sizeof t);
                font_draw(title_font(), t, X(i ? 240 : 160), 164, 13, i == s_yes ? white : grey, FONT_CENTER);
            }
        } else {
            text_line("UI", "SHOP_PURCHASE_COMPLETE", t, sizeof t);
            font_draw(body_font(), t, X(200), 134, 11, white, FONT_CENTER);
        }
    }
    g_draw_depth = d;
}
