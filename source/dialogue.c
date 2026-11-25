#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <citro2d.h>
#include "dialogue.h"
#include "common.h"

#define MAXTEXT 2048
#define MAXPAGES 16

static const char *s_lang = "en";
static bool s_lang_set;
static char s_text[MAXTEXT];
static char *s_page[MAXPAGES];
static int s_npages, s_cur;
static bool s_open;
static float s_t;                   // time on this page: the text appears letter by letter
static void (*s_done)(void *);
static void *s_ctx;
static C2D_TextBuf s_buf;

void dlg_init(void) {
    u8 lang = CFG_LANGUAGE_EN;
    if (R_SUCCEEDED(cfguInit())) {
        CFGU_GetSystemLanguage(&lang);
        cfguExit();
    }
    if (!s_lang_set) s_lang = lang == CFG_LANGUAGE_FR ? "fr" : "en";
    if (!s_buf) s_buf = C2D_TextBufNew(4096);
}

void text_set_lang(const char *lang) { s_lang = strcmp(lang, "en") ? "fr" : "en"; s_lang_set = true; }
const char *text_lang(void) { return s_lang; }
static bool find_line(const char *sheet, const char *key, char *out, size_t cap);
bool text_line(const char *sheet, const char *key, char *out, size_t cap) {
    if (find_line(sheet, key, out, cap)) return true;
    snprintf(out, cap, "%s", key);
    return false;
}

// the line KEY<TAB>text of text/<lang>/<sheet>.txt, unescaped (\n, \f, \\)
static bool find_line(const char *sheet, const char *key, char *out, size_t cap) {
    FILE *f = fopen(data_path("text/%s/%s.txt", s_lang, sheet), "r");
    if (!f && strcmp(s_lang, "en")) f = fopen(data_path("text/en/%s.txt", sheet), "r");
    if (!f) return false;
    static char line[MAXTEXT];
    size_t kl = strlen(key);
    bool ok = false;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, key, kl) || line[kl] != '\t') continue;
        const char *s = line + kl + 1;
        size_t n = 0;
        for (; *s && *s != '\n' && *s != '\r' && n + 1 < cap; s++) {
            if (*s == '\\' && s[1]) {
                s++;
                out[n++] = *s == 'n' ? '\n' : *s == 'f' ? '\f' : *s;
            } else out[n++] = *s;
        }
        out[n] = 0;
        ok = true;
        break;
    }
    fclose(f);
    return ok;
}

bool dlg_open(const char *sheet, const char *key, void (*done)(void *), void *ctx) {
    if (!find_line(sheet, key, s_text, sizeof s_text)) {
        snprintf(s_text, sizeof s_text, "[%s / %s]", sheet, key);
    }
    s_npages = 0;
    char *p = s_text;
    while (s_npages < MAXPAGES) {
        s_page[s_npages++] = p;
        char *e = strchr(p, '\f');
        if (!e) break;
        *e = 0;
        p = e + 1;
    }
    s_cur = 0;
    s_t = 0;
    s_open = true;
    s_done = done;
    s_ctx = ctx;
    return true;
}

bool dlg_active(void) { return s_open; }

void dlg_close(void) { s_open = false; s_done = NULL; }

void dlg_update(u32 down) {
    if (!s_open) return;
    s_t += 1.0f / 60;
    if (!(down & (KEY_A | KEY_B | KEY_Y))) return;
    size_t len = strlen(s_page[s_cur]);
    if (s_t * 60 < len) { s_t = 1000; return; }       // first press: the whole page at once
    if (++s_cur < s_npages) { s_t = 0; return; }
    s_open = false;
    void (*cb)(void *) = s_done;
    s_done = NULL;
    if (cb) cb(s_ctx);
}

void dlg_draw(void) {
    if (!s_open || !s_buf) return;
    float ax = g_view.sx / g_view.sy;   // 2 in the 800 wide mode
    float x0 = 24 * ax, y0 = 150, w = 352 * ax, h = 80;
    C2D_DrawRectSolid(x0, y0, 0.96f, w, h, C2D_Color32(0, 0, 0, 200));
    C2D_DrawRectSolid(x0, y0, 0.96f, w, 1, C2D_Color32(220, 210, 190, 255));
    C2D_DrawRectSolid(x0, y0 + h - 1, 0.96f, w, 1, C2D_Color32(220, 210, 190, 255));
    // the part of the page shown so far (whole UTF-8 characters)
    static char shown[MAXTEXT];
    const char *pg = s_page[s_cur];
    size_t n = (size_t)(s_t * 60);
    size_t len = strlen(pg);
    if (n > len) n = len;
    while (n < len && (pg[n] & 0xC0) == 0x80) n++;
    memcpy(shown, pg, n);
    shown[n] = 0;
    C2D_TextBufClear(s_buf);
    C2D_Text t;
    C2D_TextParse(&t, s_buf, shown);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, x0 + 10 * ax, y0 + 8, 0.97f, 0.5f * ax, 0.5f,
                 C2D_Color32(240, 235, 225, 255), w - 20 * ax);
    if (n == len) {   // more to read: a small mark
        C2D_DrawTriangle(x0 + w - 16 * ax, y0 + h - 12, C2D_Color32(220, 210, 190, 255), x0 + w - 8 * ax, y0 + h - 12,
                         C2D_Color32(220, 210, 190, 255), x0 + w - 12 * ax, y0 + h - 6, C2D_Color32(220, 210, 190, 255),
                         0.97f);
    }
}
