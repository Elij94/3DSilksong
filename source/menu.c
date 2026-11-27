#include "menu.h"
#include "font.h"
#include "sprite.h"
#include "dialogue.h"
#include "audio.h"
#include "version.h"
#include "fsm.h"
#include "map.h"
#include "tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SETTINGS_PATH "sdmc:" SS_DATADIR "settings.txt"

enum { SCR_NONE, SCR_TITLE, SCR_PROFILES, SCR_OPTIONS, SCR_PAUSE, SCR_CONFIRM, SCR_TESTS };

// the test menu (pause): abilities switched on and off (their PlayerData), rosaries, shards, maps, health
static const struct { const char *pd, *fr, *en; } TEST_ABIL[] = {
    {"hasDash", "Sprint", "Sprint"}, {"hasWalljump", "Accroche aux murs", "Cling grip"},
    {"hasHarpoonDash", "Griffe (Clawline)", "Clawline"}, {"hasDoubleJump", "Double saut", "Double jump"},
    {"hasBrolly", "Planeur", "Drifter's cloak"}, {"hasSuperJump", "Envol de soie", "Silk Soar"},
    {"hasNeedolin", "Needolin", "Needolin"}, {"hasChargeSlash", "Frappe charg\xc3\xa9" "e", "Charge slash"},
};
#define N_ABIL (int)(sizeof TEST_ABIL / sizeof *TEST_ABIL)
enum { T_GEO = N_ABIL, T_SHARDS, T_MAPS, T_TOOLS, T_REFILL, T_MASK, T_BACK, T_COUNT };

static int s_scr, s_back;          // the screen shown, and the one Retour goes back to
static int s_sel;
static int s_music = 10, s_sfx = 10, s_debug;
static float s_t;                  // time on this screen (fades)
static int s_del_slot;

typedef struct {
    bool used;
    char zone[64], zkey[40];   // the area's name, its MapZone key
    int hp, maxhp, geo, shards, nail, secs;
} SlotInfo;
static SlotInfo s_slots[4];

// ---------------------------------------------------------------- settings

static void apply(void) {
    g_music_vol = s_music / 10.0f;
    g_sfx_vol = s_sfx / 10.0f;
}

static void save_settings(void) {
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:" SS_DATADIR, 0777);
    FILE *f = fopen(SETTINGS_PATH, "w");
    if (!f) return;
    fprintf(f, "music %d\nsfx %d\nlang %s\ndebug %d\n", s_music, s_sfx, text_lang(), s_debug);
    fclose(f);
}

void menu_init(void) {
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) text_set_lang("fr");   // French unless chosen otherwise in the options
    if (f) {
        char k[16], v[16];
        while (fscanf(f, "%15s %15s", k, v) == 2) {
            if (!strcmp(k, "music")) s_music = atoi(v);
            else if (!strcmp(k, "sfx")) s_sfx = atoi(v);
            else if (!strcmp(k, "lang")) text_set_lang(v);
            else if (!strcmp(k, "debug")) s_debug = atoi(v);
        }
        fclose(f);
    }
    if (s_music < 0 || s_music > 10) s_music = 10;
    if (s_sfx < 0 || s_sfx > 10) s_sfx = 10;
    apply();
}

bool menu_debug(void) { return s_debug; }

const char *menu_save_path(int slot) {
    static char p[64];
    snprintf(p, sizeof p, "sdmc:" SS_DATADIR "save%d.txt", slot);
    return p;
}

// ---------------------------------------------------------------- the areas (text/zones.txt, Map Zones sheet)

static char s_zkey[40];   // the MapZone key of the last menu_zone_name scene

const char *menu_zone_name(const char *scene) {
    static char out[64], last[48];
    if (!strcmp(last, scene)) return out;
    snprintf(last, sizeof last, "%s", scene);
    out[0] = 0;
    s_zkey[0] = 0;
    FILE *f = fopen(data_path("text/zones.txt"), "r");
    if (!f) return out;
    char line[96];
    size_t n = strlen(scene);
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, scene, n) || line[n] != '\t') continue;
        char *z = line + n + 1;
        z[strcspn(z, "\r\n")] = 0;
        snprintf(s_zkey, sizeof s_zkey, "%s", z);
        text_line("Map Zones", z, out, sizeof out);
        break;
    }
    fclose(f);
    return out;
}

static void read_slots(void) {
    for (int i = 0; i < 4; i++) {
        SlotInfo *s = &s_slots[i];
        memset(s, 0, sizeof *s);
        FILE *f = fopen(menu_save_path(i + 1), "r");
        if (!f) continue;
        char scene[48];
        float x, y;
        if (fscanf(f, "%47s %f %f %d", scene, &x, &y, &s->hp) >= 3) {
            s->used = true;
            snprintf(s->zone, sizeof s->zone, "%s", menu_zone_name(scene));
            snprintf(s->zkey, sizeof s->zkey, "%s", s_zkey);
            if (!s->zone[0]) snprintf(s->zone, sizeof s->zone, "%s", scene);
        }
        char line[128];
        s->maxhp = 5;
        while (fgets(line, sizeof line, f)) {
            int kind, v, n = 0;
            if (sscanf(line, "pd %d %d %n", &kind, &v, &n) >= 2 && n > 0) {
                if (!strncmp(line + n, "geo", 3) && (line[n + 3] == '\n' || line[n + 3] == '\r' || !line[n + 3])) s->geo = v;
                if (!strncmp(line + n, "ShellShards", 11)) s->shards = v;
                if (!strncmp(line + n, "maxHealth", 9) && v > 0) s->maxhp = v;
                if (!strncmp(line + n, "nailUpgrades", 12)) s->nail = v;
                if (!strncmp(line + n, "playTime", 8)) s->secs = v;
            }
        }
        fclose(f);
    }
}

// ---------------------------------------------------------------- screens

static void open(int scr, int sel) { s_scr = scr; s_sel = sel; s_t = 0; }

void menu_open_title(void) { read_slots(); open(SCR_TITLE, 0); }
void menu_open_pause(void) { open(SCR_PAUSE, 0); }

static int n_items(void) {
    switch (s_scr) {
    case SCR_TITLE: return 3;
    case SCR_PROFILES: return 5;
    case SCR_OPTIONS: return 5;
    case SCR_PAUSE: return s_debug ? 5 : 4;
    case SCR_TESTS: return T_COUNT;
    case SCR_CONFIRM: return 2;
    }
    return 0;
}

static void click(void) { sfx_play_name("ui_change_selection", 0.7f, 0); }
static void ok_snd(void) { sfx_play_name("ui_button_confirm", 0.8f, 0); }

MenuAction menu_update(const Input *in, int *slot) {
    s_t += 1.0f / 60;
    int n = n_items();
    if (!n) return MA_NONE;
    if (s_scr == SCR_PROFILES) {   // the 4 cards side by side, Retour under them
        static int card;
        if (s_sel < 4) card = s_sel;
        if ((in->down & KEY_RIGHT) && s_sel < 4) { s_sel = (s_sel + 1) % 4; click(); }
        if ((in->down & KEY_LEFT) && s_sel < 4) { s_sel = (s_sel + 3) % 4; click(); }
        if ((in->down & KEY_DOWN) && s_sel < 4) { s_sel = 4; click(); }
        if ((in->down & KEY_UP) && s_sel == 4) { s_sel = card; click(); }
    } else {
        if (in->down & KEY_DOWN) { s_sel = (s_sel + 1) % n; click(); }
        if (in->down & KEY_UP) { s_sel = (s_sel + n - 1) % n; click(); }
    }
    bool a = in->down & KEY_A, b = in->down & KEY_B;
    switch (s_scr) {
    case SCR_TITLE:
        if (a) {
            ok_snd();
            if (s_sel == 0) { read_slots(); open(SCR_PROFILES, 0); }
            else if (s_sel == 1) { s_back = SCR_TITLE; open(SCR_OPTIONS, 0); }
            else return MA_QUIT;
        }
        break;
    case SCR_PROFILES:
        if (b || (a && s_sel == 4)) { open(SCR_TITLE, 0); break; }
        if (a) {
            ok_snd();
            *slot = s_sel + 1;
            s_scr = SCR_NONE;
            return s_slots[s_sel].used ? MA_LOAD : MA_NEW;
        }
        if ((in->down & KEY_X) && s_sel < 4 && s_slots[s_sel].used) { s_del_slot = s_sel; open(SCR_CONFIRM, 1); }
        break;
    case SCR_CONFIRM:
        if (b || (a && s_sel == 1)) { open(SCR_PROFILES, s_del_slot); break; }
        if (a) {
            remove(menu_save_path(s_del_slot + 1));
            read_slots();
            open(SCR_PROFILES, s_del_slot);
        }
        break;
    case SCR_OPTIONS: {
        int d = (in->down & KEY_RIGHT) ? 1 : (in->down & KEY_LEFT) ? -1 : 0;
        if (s_sel == 0 && d) { s_music = s_music + d < 0 ? 0 : s_music + d > 10 ? 10 : s_music + d; apply(); click(); }
        if (s_sel == 1 && d) { s_sfx = s_sfx + d < 0 ? 0 : s_sfx + d > 10 ? 10 : s_sfx + d; apply(); click(); }
        if (s_sel == 2 && (d || a)) { text_set_lang(strcmp(text_lang(), "fr") ? "fr" : "en"); click(); }
        if (s_sel == 3 && (d || a)) { s_debug = !s_debug; click(); }
        if (b || (a && s_sel == 4)) { save_settings(); open(s_back, s_back == SCR_PAUSE ? 1 : 1); }
        break;
    }
    case SCR_PAUSE:
        if (b || (in->down & KEY_START)) { s_scr = SCR_NONE; return MA_RESUME; }
        if (a) {
            ok_snd();
            if (s_sel == 0) { s_scr = SCR_NONE; return MA_RESUME; }
            if (s_sel == 1) { s_back = SCR_PAUSE; open(SCR_OPTIONS, 0); }
            else if (s_sel == 2) { read_slots(); open(SCR_TITLE, 0); return MA_TO_TITLE; }
            else if (s_sel == 3) open(SCR_TESTS, 0);
            else { s_scr = SCR_NONE; return MA_ROOMS; }
        }
        break;
    case SCR_TESTS:
        if (b || (a && s_sel == T_BACK)) { open(SCR_PAUSE, 3); break; }
        if (a) {
            ok_snd();
            if (s_sel < N_ABIL) pd_set_bool(TEST_ABIL[s_sel].pd, !pd_bool(TEST_ABIL[s_sel].pd));
            else if (s_sel == T_GEO) pd_set_int("geo", pd_int("geo") + 1000);
            else if (s_sel == T_SHARDS) pd_set_int("ShellShards", pd_int("ShellShards") + 100);
            else if (s_sel == T_MAPS) map_give_all();
            else if (s_sel == T_TOOLS) tools_give_all();
            else if (s_sel == T_REFILL) { *slot = TEST_REFILL; return MA_TEST; }
            else if (s_sel == T_MASK) { *slot = TEST_MASK; return MA_TEST; }
        }
        break;
    }
    return MA_NONE;
}

// ---------------------------------------------------------------- drawing

static const u32 C_ON = 0xFFFFFFFF, C_OFF = 0xFF9A9A9A;   // ABGR: white, grey

static Font *title_font(void) {
    Font *f = font_get("trajanpro_bold_sdf");
    return f ? f : font_get("trajan_bold_tmpro");
}

static Font *body_font(void) {
    Font *f = font_get("perpetua_tmpro");
    return f ? f : font_get("perpetua_sdf");
}

static void item(const char *key, int i, float y) {
    char t[96];
    text_line("MainMenu", key, t, sizeof t);
    float cx = g_view.sw * 0.5f;
    font_draw(title_font(), t, cx, y, 15, i == s_sel ? C_ON : C_OFF, FONT_CENTER);
    if (i == s_sel) {   // the selection's fleurs either side (Menu_Title's Pointer L / R: dialogue_anim Pointer Up)
        static SpriteSet *ptr;
        if (!ptr) ptr = sprites_get("sprites/dialogue_anim.sss");
        int c = ptr ? sprite_clip(ptr, "Pointer Up") : -1;
        float w = font_width(title_font(), t, 15), ax = g_view.sx / g_view.sy;
        if (c >= 0 && ptr->clips[c].n) {
            int n = ptr->clips[c].n, f = (int)(s_t * 12);
            int fr = rds16((const u8 *)&ptr->clips[c].frames[f < n ? f : n - 1]);
            const SFrame *q = &ptr->frames[fr];
            float fh = q->y1 - q->y0, k = fh > 0 ? 13 / fh : 1;
            float yc = y + 8 + (q->y0 + q->y1) * 0.5f * k, dx = (q->x0 + q->x1) * 0.5f * k * ax;
            sprite_draw_screen_xy(ptr, fr, cx - w * 0.5f - 12 * ax - dx, yc, k, k, NULL);
            sprite_draw_screen_xy(ptr, fr, cx + w * 0.5f + 12 * ax + dx, yc, -k, k, NULL);
        } else {
            C2D_DrawRectSolid(cx - w * 0.5f - 12 * ax, y + 7, 0.95f, 5 * ax, 2, C_ON);
            C2D_DrawRectSolid(cx + w * 0.5f + 7 * ax, y + 7, 0.95f, 5 * ax, 2, C_ON);
        }
    }
}

static void shade(u8 a) { C2D_DrawRectSolid(0, 0, 0.9f, g_view.sw, g_view.sh, C2D_Color32(0, 0, 0, a)); }


// ---------------------------------------------------------------- the profile screen (Silksong's save slots)

static SpriteSet *s_art, *s_hud, *s_icons, *s_inv;

// a clip's first frame drawn to fit w x h screen pixels (keeping its shape), centred on (x, y); false if missing
static bool draw_fit(SpriteSet *set, const char *clip, float x, float y, float w, float h, const C2D_ImageTint *tint) {
    int c = set ? sprite_clip(set, clip) : -1;
    if (c < 0 || !set->clips[c].n) return false;
    int fr = rds16((const u8 *)&set->clips[c].frames[0]);
    const SFrame *q = &set->frames[fr];
    float ax = g_view.sx / g_view.sy, fw = (q->x1 - q->x0) * ax, fh = q->y1 - q->y0;
    if (fw <= 0 || fh <= 0) return false;
    float k = w / fw < h / fh ? w / fw : h / fh;
    sprite_draw_screen(set, fr, x - (q->x0 + q->x1) * 0.5f * k * ax, y + (q->y0 + q->y1) * 0.5f * k, k, tint);
    return true;
}

// the slot's picture: SaveSlotBackgrounds (text/save_art.txt), by its area's MapZone; the default one else
static const char *art_of(const char *zkey) {
    static char out[64];
    snprintf(out, sizeof out, "Area_Art__0011_Mosscave");
    FILE *f = fopen(data_path("text/save_art.txt"), "r");
    if (!f) return out;
    char line[160], z[48], a[64];
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "%47s %63s", z, a) == 2 && !strcmp(z, zkey)) { snprintf(out, sizeof out, "%s", a); break; }
    fclose(f);
    return out;
}

static void draw_profiles(void) {
    char t[96], num[24];
    float W = g_view.sw, H = g_view.sh, ax = g_view.sx / g_view.sy;
    if (!s_art) s_art = sprites_get("ui/area_art.sss");
    if (!s_hud) s_hud = sprites_get("sprites/hud_anim.sss");
    if (!s_icons) s_icons = sprites_get("sprites/hud_spool.sss");
    if (!s_inv) s_inv = sprites_get("ui/inv_pane.sss");
    shade(225);   // the game's profile screen: the embers on black, the title scene's logo gone
    g_draw_depth = 0.96f;
    text_line("MainMenu", "SCREEN_SAVE_PROFILES", t, sizeof t);
    font_draw(title_font(), t, W * 0.5f, H * 0.045f, 15, C_ON, FONT_CENTER);
    C2D_DrawRectSolid(W * 0.5f - 70 * ax, H * 0.155f, 0.95f, 140 * ax, 1, C2D_Color32(230, 226, 220, 200));
    // the cards, as the game lays them out on a 16:9 screen (their centres at 17.7, 38.8, 60.5 and 82.3 %)
    static const float CX[4] = {0.177f, 0.388f, 0.605f, 0.823f};
    float cw = W * 0.165f, top = H * 0.215f, bot = H * 0.755f, ch = bot - top;
    for (int i = 0; i < 4; i++) {
        const SlotInfo *sl = &s_slots[i];
        float x = W * CX[i], x0 = x - cw * 0.5f, x1 = x + cw * 0.5f;
        bool on = i == s_sel;
        u32 c = on ? C_ON : C_OFF;
        if (sl->used) {
            draw_fit(s_art, art_of(sl->zkey), x, (top + bot) * 0.5f, cw, ch, NULL);
            // the HUD's frame and masks, the needle, the rosaries and the shell shards, over the picture
            draw_fit(s_hud, "HUD Frame Idle", x0 + cw * 0.22f, top + ch * 0.12f, cw * 0.28f, ch * 0.16f, NULL);
            for (int k = 0; k < sl->maxhp && k < 10; k++)
                draw_fit(s_hud, "Health Idle", x0 + cw * (0.44f + k * 0.1f), top + ch * 0.12f, cw * 0.09f, ch * 0.08f, NULL);
            snprintf(num, sizeof num, "nail%d", (sl->nail < 0 ? 0 : sl->nail > 4 ? 4 : sl->nail) + 1);
            draw_fit(s_inv, num, x0 + cw * 0.27f, top + ch * 0.27f, cw * 0.34f, ch * 0.06f, NULL);
            draw_fit(s_icons, "HUD_coin_v020004", x0 + cw * 0.2f, top + ch * 0.37f, cw * 0.11f, ch * 0.07f, NULL);
            snprintf(num, sizeof num, "%d", sl->geo);
            font_draw(title_font(), num, x0 + cw * 0.3f, top + ch * 0.34f, 9, C_ON, FONT_LEFT);
            draw_fit(s_icons, "shell_shard_icon0004", x0 + cw * 0.2f, top + ch * 0.46f, cw * 0.11f, ch * 0.07f, NULL);
            snprintf(num, sizeof num, "%d", sl->shards);
            font_draw(title_font(), num, x0 + cw * 0.3f, top + ch * 0.43f, 9, C_ON, FONT_LEFT);
            // the play time, the area's name
            int m = sl->secs / 60;
            if (m >= 60) snprintf(num, sizeof num, "%dH %dM", m / 60, m % 60);
            else snprintf(num, sizeof num, "%dM", m);
            font_draw(title_font(), num, x1 - cw * 0.06f, top + ch * 0.78f, 8, C_ON, FONT_RIGHT);
            font_draw_wrapped(title_font(), sl->zone, x1 - cw * 0.06f, top + ch * 0.84f, 8, C_ON, cw * 0.62f, FONT_RIGHT);
        } else {
            text_line("MainMenu", "PROFILE_NEW_GAME", t, sizeof t);
            float sz = 11;   // one line, as the game's
            while (sz > 6 && font_width(title_font(), t, sz) > cw * 0.92f) sz -= 0.5f;
            font_draw(title_font(), t, x, top + ch * 0.45f, sz, c, FONT_CENTER);
            // the empty card's frame lines (the fleurs' long strokes)
            u32 lc = C2D_Color32(230, 226, 220, 210);
            C2D_DrawRectSolid(x0 + cw * 0.3f, top, 0.95f, cw * 0.75f, 1, lc);
            C2D_DrawRectSolid(x0 - cw * 0.05f, bot, 0.95f, cw * 0.75f, 1, lc);
            C2D_DrawRectSolid(x0, top + ch * 0.25f, 0.95f, 1 * ax, ch * 0.6f, lc);
            C2D_DrawRectSolid(x1, top + ch * 0.25f, 0.95f, 1 * ax, ch * 0.5f, lc);
        }
        // the frame's fleurs (top left, bottom right), the slot's number
        // at the picture's scale (6.4 units its height): the top fleur's 4.99 down the left side, the bottom one's corner
        float u = ch / 6.4f;
        draw_fit(s_art, "h_profile_fleur_top0008", x0 + 1.4f * u * ax, top + 2.35f * u, 3.02f * u * ax, 4.99f * u, NULL);
        draw_fit(s_art, "h_profile_fleur_bottom0008", x1 - 1.15f * u * ax, bot - 0.95f * u, 2.53f * u * ax, 2.09f * u, NULL);
        snprintf(num, sizeof num, "%d.", i + 1);
        font_draw(title_font(), num, x0 + cw * 0.1f, bot - ch * 0.17f, 15, c, FONT_LEFT);
        if (on) {   // the selection's arrows either side
            draw_fit(s_art, "h_profile_fleur_arrow_selector0", x0 - 9 * ax, (top + bot) * 0.5f, 7 * ax, 22, NULL);
            C2D_ImageTint flip;
            (void)flip;
            int ca = sprite_clip(s_art, "h_profile_fleur_arrow_selector0");
            if (ca >= 0 && s_art->clips[ca].n) {
                int fr = rds16((const u8 *)&s_art->clips[ca].frames[0]);
                const SFrame *q = &s_art->frames[fr];
                float fh = q->y1 - q->y0, k = fh > 0 ? 22 / fh : 1;
                sprite_draw_screen_xy(s_art, fr, x1 + 9 * ax + (q->x0 + q->x1) * 0.5f * k * ax, (top + bot) * 0.5f +
                                      (q->y0 + q->y1) * 0.5f * k, -k, k, NULL);
            }
            if (sl->used) {   // X: clear it (the game's bin under the card)
                const char *x_lbl = strcmp(text_lang(), "fr") ? "X: clear" : "X : effacer";
                font_draw(body_font(), x_lbl, x, bot + H * 0.03f, 9, C_OFF, FONT_CENTER);
            }
        }
    }
    g_draw_depth = 0.5f;
    text_line("MainMenu", "NAV_BACK", t, sizeof t);
    font_draw(title_font(), t, W * 0.5f, H * 0.87f, 13, s_sel == 4 ? C_ON : C_OFF, FONT_CENTER);
}

void menu_draw(void) {
    float cx = g_view.sw * 0.5f;
    char t[96];
    switch (s_scr) {
    case SCR_TITLE:
        font_draw(body_font(), SS_VERSION, 8 * g_view.sx / g_view.sy, g_view.sh - 16, 9, C_OFF, FONT_LEFT);
        item("MAIN_START", 0, 137);
        item("MAIN_OPTIONS", 1, 153);
        item("MAIN_QUIT", 2, 169);
        break;
    case SCR_PROFILES:
        draw_profiles();
        break;
    case SCR_CONFIRM:
        shade(190);
        text_line("MainMenu", "PROFILE_CLEAR_PROMPT", t, sizeof t);
        font_draw(title_font(), t, cx, 80, 15, C_ON, FONT_CENTER);
        item("NAV_YES", 0, 140);
        item("NAV_NO", 1, 164);
        break;
    case SCR_OPTIONS: {
        shade(170);
        text_line("MainMenu", "MAIN_OPTIONS", t, sizeof t);
        font_draw(title_font(), t, cx, 22, 20, C_ON, FONT_CENTER);
        const char *keys[] = {"AUDIO_MUSIC_VOL", "AUDIO_SOUND_VOL", "GAME_LANGUAGE"};
        float ax = g_view.sx / g_view.sy;
        for (int i = 0; i < 4; i++) {
            float y = 70 + i * 28;
            u32 c = i == s_sel ? C_ON : C_OFF;
            if (i < 3) text_line("MainMenu", keys[i], t, sizeof t);
            else snprintf(t, sizeof t, "%s", strcmp(text_lang(), "fr") ? "Debug info" : "Infos de d\xc3\xa9" "bogage");
            font_draw(title_font(), t, cx - 20 * ax, y, 13, c, FONT_RIGHT);
            if (i < 2) {
                int v = i == 0 ? s_music : s_sfx;
                for (int k = 0; k < 10; k++)
                    C2D_DrawRectSolid(cx + (4 + k * 9) * ax, y + 2, 0.92f, 6 * ax, 10, k < v ? c : C2D_Color32(80, 80, 80, 255));
            } else if (i == 2) {
                text_line("MainMenu", "LANG_CURRENT", t, sizeof t);
                font_draw(title_font(), t, cx + 4 * ax, y, 13, c, FONT_LEFT);
            } else {
                text_line("MainMenu", s_debug ? "NAV_YES" : "NAV_NO", t, sizeof t);
                font_draw(title_font(), t, cx + 4 * ax, y, 13, c, FONT_LEFT);
            }
        }
        item("NAV_BACK", 4, 200);
        break;
    }
    case SCR_PAUSE:
        shade(140);
        item("PAUSE_CONTINUE", 0, 90);
        item("PAUSE_OPTIONS", 1, 114);
        item("PAUSE_MAIN", 2, 138);
        font_draw(title_font(), "Tests", cx, 162, 15, s_sel == 3 ? C_ON : C_OFF, FONT_CENTER);
        if (s_debug)
            font_draw(title_font(), strcmp(text_lang(), "fr") ? "Rooms" : "Salles", cx, 186, 15, s_sel == 4 ? C_ON : C_OFF,
                      FONT_CENTER);
        break;
    case SCR_TESTS: {
        shade(190);
        bool fr = !strcmp(text_lang(), "fr");
        font_draw(title_font(), "Tests", cx, 6, 16, C_ON, FONT_CENTER);
        for (int i = 0; i < T_COUNT; i++) {
            const char *lab;
            char buf[64];
            if (i < N_ABIL) {
                snprintf(buf, sizeof buf, "%s : %s", fr ? TEST_ABIL[i].fr : TEST_ABIL[i].en,
                         pd_bool(TEST_ABIL[i].pd) ? (fr ? "oui" : "on") : (fr ? "non" : "off"));
                lab = buf;
            } else if (i == T_GEO) lab = fr ? "+1000 chapelets" : "+1000 rosaries";
            else if (i == T_SHARDS) lab = fr ? "+100 fragments de coquille" : "+100 shell shards";
            else if (i == T_MAPS) lab = fr ? "Toutes les cartes et la plume" : "Every map and the quill";
            else if (i == T_TOOLS) lab = fr ? "Tous les outils" : "Every tool";
            else if (i == T_REFILL) lab = fr ? "Vie et soie au maximum" : "Full health and silk";
            else if (i == T_MASK) lab = fr ? "Un masque de plus" : "One more mask";
            else lab = fr ? "Retour" : "Back";
            font_draw(title_font(), lab, cx, 28 + i * 14.5f, 11, i == s_sel ? C_ON : C_OFF, FONT_CENTER);
        }
        break;
    }
    }
}
