#include "menu.h"
#include "font.h"
#include "sprite.h"
#include "dialogue.h"
#include "audio.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SETTINGS_PATH "sdmc:" SS_DATADIR "settings.txt"

enum { SCR_NONE, SCR_TITLE, SCR_PROFILES, SCR_OPTIONS, SCR_PAUSE, SCR_CONFIRM };

static int s_scr, s_back;          // the screen shown, and the one Retour goes back to
static int s_sel;
static int s_music = 10, s_sfx = 10, s_debug;
static float s_t;                  // time on this screen (fades)
static int s_del_slot;

typedef struct {
    bool used;
    char zone[64];
    int hp, maxhp, geo, shards;
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

const char *menu_zone_name(const char *scene) {
    static char out[64], last[48];
    if (!strcmp(last, scene)) return out;
    snprintf(last, sizeof last, "%s", scene);
    out[0] = 0;
    FILE *f = fopen(data_path("text/zones.txt"), "r");
    if (!f) return out;
    char line[96];
    size_t n = strlen(scene);
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, scene, n) || line[n] != '\t') continue;
        char *z = line + n + 1;
        z[strcspn(z, "\r\n")] = 0;
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
    case SCR_PAUSE: return s_debug ? 4 : 3;
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
    if (in->down & KEY_DOWN) { s_sel = (s_sel + 1) % n; click(); }
    if (in->down & KEY_UP) { s_sel = (s_sel + n - 1) % n; click(); }
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
            else { s_scr = SCR_NONE; return MA_ROOMS; }
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
    if (i == s_sel) {   // the selection's little marks either side
        float w = font_width(title_font(), t, 15), ax = g_view.sx / g_view.sy;
        C2D_DrawRectSolid(cx - w * 0.5f - 12 * ax, y + 7, 0.95f, 5 * ax, 2, C_ON);
        C2D_DrawRectSolid(cx + w * 0.5f + 7 * ax, y + 7, 0.95f, 5 * ax, 2, C_ON);
    }
}

static void shade(u8 a) { C2D_DrawRectSolid(0, 0, 0.9f, g_view.sw, g_view.sh, C2D_Color32(0, 0, 0, a)); }

void menu_draw(void) {
    float cx = g_view.sw * 0.5f;
    char t[96];
    switch (s_scr) {
    case SCR_TITLE:
        item("MAIN_START", 0, 150);
        item("MAIN_OPTIONS", 1, 172);
        item("MAIN_QUIT", 2, 194);
        break;
    case SCR_PROFILES:
        shade(150);
        g_draw_depth = 0.96f;   // the icons over the shading
        for (int i = 0; i < 4; i++) {
            SlotInfo *s = &s_slots[i];
            float y = 28 + i * 44;
            u32 c = i == s_sel ? C_ON : C_OFF;
            if (i == s_sel)
                C2D_DrawRectSolid(cx - 160 * g_view.sx / g_view.sy, y - 4, 0.92f, 320 * g_view.sx / g_view.sy, 38,
                                  C2D_Color32(255, 255, 255, 28));
            char num[4];
            snprintf(num, sizeof num, "%d", i + 1);
            font_draw(title_font(), num, cx - 140 * g_view.sx / g_view.sy, y + 6, 16, c, FONT_LEFT);
            if (!s->used) {
                text_line("MainMenu", "PROFILE_NEW_GAME", t, sizeof t);
                font_draw(title_font(), t, cx, y + 8, 14, c, FONT_CENTER);
            } else {
                float ax = g_view.sx / g_view.sy, x0 = cx - 110 * ax;
                font_draw(title_font(), s->zone, x0, y + 1, 13, c, FONT_LEFT);
                // the masks, then the rosaries and the shell shards with their HUD icons
                static SpriteSet *hud, *icons;
                if (!hud) hud = sprites_get("sprites/hud_anim.sss");
                if (!icons) icons = sprites_get("sprites/hud_spool.sss");
                int full = hud ? sprite_clip(hud, "Health Idle") : -1, empty = hud ? sprite_clip(hud, "Health Empty") : -1;
                for (int k = 0; k < s->maxhp && k < 10; k++) {
                    int cl = k < s->hp ? full : empty;
                    if (cl >= 0 && hud->clips[cl].n)
                        sprite_draw_screen(hud, rds16((const u8 *)&hud->clips[cl].frames[0]), x0 + 6 * ax + k * 11 * ax, y + 25,
                                           9, NULL);
                }
                char num[16];
                int gc = icons ? sprite_clip(icons, "HUD_coin_v020004") : -1, sc = icons ? sprite_clip(icons, "shell_shard_icon0004") : -1;
                float gx = x0 + 70 * ax;
                if (gc >= 0) sprite_draw_screen(icons, rds16((const u8 *)&icons->clips[gc].frames[0]), gx, y + 25, 12, NULL);
                snprintf(num, sizeof num, "%d", s->geo);
                font_draw(title_font(), num, gx + 8 * ax, y + 19, 11, c, FONT_LEFT);
                gx += 50 * ax;
                if (sc >= 0) sprite_draw_screen(icons, rds16((const u8 *)&icons->clips[sc].frames[0]), gx, y + 25, 12, NULL);
                snprintf(num, sizeof num, "%d", s->shards);
                font_draw(title_font(), num, gx + 8 * ax, y + 19, 11, c, FONT_LEFT);
            }
        }
        g_draw_depth = 0.5f;
        item("NAV_BACK", 4, 208);
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
        if (s_debug) {
            float y = 162;
            font_draw(title_font(), strcmp(text_lang(), "fr") ? "Rooms" : "Salles", cx, y, 15, s_sel == 3 ? C_ON : C_OFF,
                      FONT_CENTER);
        }
        break;
    }
}
