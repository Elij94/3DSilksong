#include "font.h"
#include "tex.h"
#include "inflate.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    u32 code;
    float u0, v0, u1, v1, xo, yo, w, h, adv;
} Glyph;

struct Font {
    char name[48];
    int n;
    float pt, line, asc, desc, pad;
    Glyph *g;
    C3D_Tex tex;
    bool ok;
    Font *next;
};

static Font *s_fonts;

static int cmp_glyph(const void *a, const void *b) {
    u32 x = ((const Glyph *)a)->code, y = ((const Glyph *)b)->code;
    return x < y ? -1 : x > y;
}

Font *font_get(const char *name) {
    for (Font *f = s_fonts; f; f = f->next)
        if (!strcmp(f->name, name)) return f->ok ? f : NULL;
    Font *f = calloc(1, sizeof *f);
    snprintf(f->name, sizeof f->name, "%s", name);
    f->next = s_fonts;
    s_fonts = f;
    size_t size;
    u8 *d = file_load(data_path("fonts/%s.ssf", name), &size);
    if (!d || size < 40 || memcmp(d, "SSFN", 4)) { free(d); return NULL; }
    f->n = rd16(d + 6);
    f->pt = rdf(d + 8); f->line = rdf(d + 12); f->asc = rdf(d + 16); f->desc = rdf(d + 20); f->pad = rdf(d + 24);
    int aw = rd16(d + 28), ah = rd16(d + 30), fmt = d[32];
    u32 csize = rd32(d + 36);
    const u8 *p = d + 40;
    if (p + (size_t)f->n * 40 + csize > d + size) { free(d); return NULL; }
    f->g = malloc(sizeof(Glyph) * (f->n + 1));
    for (int i = 0; i < f->n; i++, p += 40) {
        Glyph *g = &f->g[i];
        g->code = rd32(p);
        g->u0 = rdf(p + 4); g->v0 = rdf(p + 8); g->u1 = rdf(p + 12); g->v1 = rdf(p + 16);
        g->xo = rdf(p + 20); g->yo = rdf(p + 24); g->w = rdf(p + 28); g->h = rdf(p + 32); g->adv = rdf(p + 36);
    }
    qsort(f->g, f->n, sizeof(Glyph), cmp_glyph);
    if (!tex_alloc(&f->tex, aw, ah, fmt)) { free(d); return NULL; }
    size_t full = tex_bytes(aw, ah, fmt);
    if (zinflate(p, csize, f->tex.data, full) != (long)full) { C3D_TexDelete(&f->tex); free(d); return NULL; }
    C3D_TexFlush(&f->tex);
    C3D_TexSetFilter(&f->tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&f->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    free(d);
    f->ok = true;
    return f;
}

static const Glyph *glyph(const Font *f, u32 code) {
    Glyph key = {.code = code};
    const Glyph *g = bsearch(&key, f->g, f->n, sizeof(Glyph), cmp_glyph);
    if (!g && code >= 'a' && code <= 'z') g = bsearch(&(Glyph){.code = code - 32}, f->g, f->n, sizeof(Glyph), cmp_glyph);
    return g;
}

// the next code point of a UTF-8 string
static u32 utf8_next(const char **s) {
    const u8 *p = (const u8 *)*s;
    u32 c = *p++;
    if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = (c & 7) << 18 | (p[0] & 63) << 12 | (p[1] & 63) << 6 | (p[2] & 63); p += 3; }
    else if (c >= 0xE0 && p[0] && p[1]) { c = (c & 15) << 12 | (p[0] & 63) << 6 | (p[1] & 63); p += 2; }
    else if (c >= 0xC0 && p[0]) { c = (c & 31) << 6 | (p[0] & 63); p += 1; }
    *s = (const char *)p;
    return c;
}

// size: the em in pixels -> scale of the font's points; across, the screen's pixel aspect
static float k_of(const Font *f, float size) { return size / (f->pt > 0 ? f->pt : 64); }

float font_width(Font *f, const char *s, float size) {
    if (!f) return 0;
    float k = k_of(f, size) * g_view.sx / g_view.sy, w = 0, best = 0;
    while (*s) {
        u32 c = utf8_next(&s);
        if (c == '\n') { if (w > best) best = w; w = 0; continue; }
        const Glyph *g = glyph(f, c);
        w += (g ? g->adv : f->pt * 0.3f) * k;
    }
    return w > best ? w : best;
}

static void draw_line(Font *f, const char *s, const char *end, float x, float y, float size, u32 color) {
    float k = k_of(f, size), ax = g_view.sx / g_view.sy;
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, color, 1.0f);
    float base = y + f->asc * k;   // the baseline
    while (s < end && *s) {
        u32 c = utf8_next(&s);
        const Glyph *g = glyph(f, c);
        if (!g) { x += f->pt * 0.3f * k * ax; continue; }
        if (g->w > 0 && g->h > 0) {
            float l = x + g->xo * k * ax, t = base - g->yo * k;
            gfx_draw_screen(&f->tex, g->u0, g->v0, g->u1, g->v1, l, t, l + g->w * k * ax, t + g->h * k, &tint);
        }
        x += g->adv * k * ax;
    }
}

// the alpha test keeps the inside of the distance field's 0.5 contour
static float s_depth_was;
static void sdf_begin(void) {
    s_depth_was = g_draw_depth;
    g_draw_depth = 0.97f;   // over the menus' shading
    C2D_Flush();
    C3D_AlphaTest(true, GPU_GREATER, 0x7F);
}

static void sdf_end(void) {
    g_draw_depth = s_depth_was;
    C2D_Flush();
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
}

void font_draw(Font *f, const char *s, float x, float y, float size, u32 color, int align) {
    if (!f || !s) return;
    sdf_begin();
    float lh = f->line * k_of(f, size);
    while (*s) {
        const char *e = strchr(s, '\n');
        if (!e) e = s + strlen(s);
        char tmp[256];
        size_t n = (size_t)(e - s) < sizeof tmp - 1 ? (size_t)(e - s) : sizeof tmp - 1;
        memcpy(tmp, s, n);
        tmp[n] = 0;
        float w = font_width(f, tmp, size);
        float lx = align == FONT_CENTER ? x - w * 0.5f : align == FONT_RIGHT ? x - w : x;
        draw_line(f, tmp, tmp + n, lx, y, size, color);
        y += lh;
        s = *e ? e + 1 : e;
    }
    sdf_end();
}

float font_draw_wrapped(Font *f, const char *s, float x, float y, float size, u32 color, float max_w, int align) {
    if (!f || !s) return 0;
    float lh = f->line * k_of(f, size), y0 = y;
    char line[256];
    size_t ln = 0;
    const char *p = s;
    sdf_begin();
    while (1) {
        // the next word (with its leading spaces)
        const char *w0 = p;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && *p != '\n') p++;
        size_t wl = (size_t)(p - w0);
        char cand[256];
        size_t cn = ln + wl < sizeof cand - 1 ? ln + wl : sizeof cand - 1;
        memcpy(cand, line, ln);
        memcpy(cand + ln, w0, cn - ln);
        cand[cn] = 0;
        bool brk = ln > 0 && font_width(f, cand, size) > max_w;
        if (brk) {   // the line so far, then this word starts the next one
            line[ln] = 0;
            float w = font_width(f, line, size);
            float lx = align == FONT_CENTER ? x - w * 0.5f : align == FONT_RIGHT ? x - w : x;
            draw_line(f, line, line + ln, lx, y, size, color);
            y += lh;
            while (*w0 == ' ') w0++;
            ln = (size_t)(p - w0) < sizeof line - 1 ? (size_t)(p - w0) : sizeof line - 1;
            memcpy(line, w0, ln);
        } else {
            memcpy(line, cand, cn);
            ln = cn;
        }
        if (*p == '\n' || !*p) {
            line[ln] = 0;
            float w = font_width(f, line, size);
            float lx = align == FONT_CENTER ? x - w * 0.5f : align == FONT_RIGHT ? x - w : x;
            draw_line(f, line, line + ln, lx, y, size, color);
            y += lh;
            ln = 0;
            if (!*p) break;
            p++;
        }
    }
    sdf_end();
    return y - y0;
}
