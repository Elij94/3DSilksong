// Music and ambience: music/<track>.pcm (16-bit mono 22050 Hz, tools/build_music_pcm.py) streamed from the RomFS
// to two DSP channels, refilled every frame. A new track fades the old one out first. Without the DSP firmware
// (SD:/3ds/dspfirm.cdc, dumped with DSP1) there is simply no sound.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"

#define RATE    22050
#define NBUF    8
#define BUFSAMP 4096
#define FADE    0.6f      // seconds

typedef struct {
    FILE *f;
    char track[40], next[40];
    bool pending;
    float vol, target, maxvol;
    ndspWaveBuf wb[NBUF];
    s16 *data;
} Stream;

static bool s_on;
static Stream s_st[2];

bool audio_init(void) {
    if (R_FAILED(ndspInit())) return false;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for (int c = 0; c < 2; c++) {
        Stream *s = &s_st[c];
        memset(s, 0, sizeof *s);
        s->data = linearAlloc(NBUF * BUFSAMP * sizeof(s16));
        if (!s->data) { ndspExit(); return false; }
        s->maxvol = c == 0 ? 0.8f : 0.6f;
        ndspChnReset(c);
        ndspChnSetInterp(c, NDSP_INTERP_LINEAR);
        ndspChnSetRate(c, RATE);
        ndspChnSetFormat(c, NDSP_FORMAT_MONO_PCM16);
        for (int i = 0; i < NBUF; i++) {
            s->wb[i].data_vaddr = s->data + i * BUFSAMP;
            s->wb[i].status = NDSP_WBUF_DONE;
        }
    }
    s_on = true;
    return true;
}

float g_music_vol = 1, g_sfx_vol = 1;   // the options' volumes

static void set_vol(int c, float v) {
    float mix[12] = {0};
    mix[0] = mix[1] = v * g_music_vol;
    ndspChnSetMix(c, mix);
}

static void stop(int c) {
    Stream *s = &s_st[c];
    ndspChnWaveBufClear(c);
    if (s->f) fclose(s->f);
    s->f = NULL;
    s->track[0] = 0;
    for (int i = 0; i < NBUF; i++) s->wb[i].status = NDSP_WBUF_DONE;
}

static void start(int c, const char *track) {
    Stream *s = &s_st[c];
    stop(c);
    if (!track[0]) return;
    s->f = fopen(data_path("music/%s.pcm", track), "rb");
    if (!s->f) return;
    snprintf(s->track, sizeof s->track, "%s", track);
    s->vol = 0;
    s->target = 1;
    set_vol(c, 0);
}

void audio_play(int c, const char *track) {
    if (!s_on || c < 0 || c > 1) return;
    Stream *s = &s_st[c];
    if (!strcmp(track, s->track) && !s->pending) { s->target = 1; return; }
    if (s->pending && !strcmp(track, s->next)) return;
    if (!s->f) { start(c, track); return; }
    snprintf(s->next, sizeof s->next, "%s", track);   // fade the old one out first
    s->pending = true;
    s->target = 0;
}

static void fill(int c) {
    Stream *s = &s_st[c];
    if (!s->f) return;
    for (int i = 0; i < NBUF; i++) {
        ndspWaveBuf *w = &s->wb[i];
        if (w->status != NDSP_WBUF_DONE && w->status != NDSP_WBUF_FREE) continue;
        s16 *dst = s->data + i * BUFSAMP;
        size_t got = fread(dst, sizeof(s16), BUFSAMP, s->f);
        if (got < BUFSAMP) {   // the end: loop
            fseek(s->f, 0, SEEK_SET);
            got += fread(dst + got, sizeof(s16), BUFSAMP - got, s->f);
            if (!got) return;
        }
        w->nsamples = got;
        DSP_FlushDataCache(dst, got * sizeof(s16));
        ndspChnWaveBufAdd(c, w);
    }
}

void audio_update(float dt) {
    if (!s_on) return;
    for (int c = 0; c < 2; c++) {
        Stream *s = &s_st[c];
        s->vol = approachf(s->vol, s->target, dt / FADE);
        if (s->pending && s->vol <= 0) {
            s->pending = false;
            start(c, s->next);
        }
        set_vol(c, s->vol * s->maxvol);
        fill(c);
    }
}

static void sfx_free(void);

void audio_exit(void) {
    if (!s_on) return;
    for (int c = 0; c < 2; c++) {
        stop(c);
        linearFree(s_st[c].data);
    }
    sfx_free();
    ndspExit();
    s_on = false;
}

// ------------------------------------------------------------------ sound effects
// sfx/<name>.pcm (the same PCM as the music, tools/build_music_pcm.py romfs sfx), found by the guid the FSMs give
// through sfx/index.txt; played whole on channels 2..9, kept in a small cache of linear memory.

#define SFX_CH0   2
#define SFX_VOICES 8
#define SFX_CACHE 48
#define SFX_MAXSAMP (RATE * 4)
#define SFX_BUDGET (3u << 20)

typedef struct { char guid[33]; char name[64]; } SfxIdx;
typedef struct { char name[64]; s16 *data; u32 n; u32 last; } SfxBuf;
static SfxIdx *s_idx;
static int s_nidx;
static SfxBuf s_cache[SFX_CACHE];
static size_t s_cache_used;
static u32 s_clock;
static ndspWaveBuf s_vwb[SFX_VOICES];
static s16 *s_vdata[SFX_VOICES];

static int cmp_idx(const void *a, const void *b) { return strcmp(((const SfxIdx *)a)->guid, ((const SfxIdx *)b)->guid); }

static void sfx_index(void) {
    if (s_idx) return;
    size_t size;
    char *d = file_load(data_path("sfx/index.txt"), &size);
    if (!d) { s_idx = calloc(1, sizeof(SfxIdx)); return; }
    int lines = 1;
    for (size_t i = 0; i < size; i++) lines += d[i] == '\n';
    s_idx = calloc(lines + 1, sizeof(SfxIdx));
    char *p = d, *end = d + size;
    while (p < end) {
        char *nl = memchr(p, '\n', end - p);
        if (!nl) nl = end;
        char *tab = memchr(p, '\t', nl - p);
        if (tab && tab - p == 32) {
            SfxIdx *e = &s_idx[s_nidx++];
            memcpy(e->guid, p, 32);
            size_t n = nl - tab - 1;
            if (n && tab[n] == '\r') n--;
            if (n > 4 && !memcmp(tab + 1 + n - 4, ".ogg", 4)) n -= 4;
            if (n > 63) n = 63;
            memcpy(e->name, tab + 1, n);
        }
        p = nl + 1;
    }
    free(d);
    qsort(s_idx, s_nidx, sizeof(SfxIdx), cmp_idx);
}

static bool voice_uses(const s16 *data) {
    for (int v = 0; v < SFX_VOICES; v++)
        if (s_vdata[v] == data && s_vwb[v].status != NDSP_WBUF_DONE && s_vwb[v].status != NDSP_WBUF_FREE) return true;
    return false;
}

static SfxBuf *sfx_get(const char *name) {
    s_clock++;
    for (int i = 0; i < SFX_CACHE; i++)
        if (s_cache[i].data && !strcmp(s_cache[i].name, name)) { s_cache[i].last = s_clock; return &s_cache[i]; }
    FILE *f = fopen(data_path("sfx/%s.pcm", name), "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    u32 n = (u32)(bytes / 2);
    if (n > SFX_MAXSAMP) n = SFX_MAXSAMP;
    if (!n) { fclose(f); return NULL; }
    // room: the least recently played sounds that no voice is playing
    int slot = -1;
    for (;;) {
        int lru = -1;
        for (int i = 0; i < SFX_CACHE; i++) {
            if (!s_cache[i].data) { if (slot < 0) slot = i; continue; }
            if (!voice_uses(s_cache[i].data) && (lru < 0 || s_cache[i].last < s_cache[lru].last)) lru = i;
        }
        if (slot >= 0 && s_cache_used + n * 2 <= SFX_BUDGET) break;
        if (lru < 0) { fclose(f); return NULL; }
        s_cache_used -= s_cache[lru].n * 2;
        linearFree(s_cache[lru].data);
        s_cache[lru].data = NULL;
        if (slot < 0) slot = lru;
    }
    SfxBuf *b = &s_cache[slot];
    b->data = linearAlloc(n * 2);
    if (!b->data) { fclose(f); return NULL; }
    n = fread(b->data, 2, n, f);
    fclose(f);
    DSP_FlushDataCache(b->data, n * 2);
    snprintf(b->name, sizeof b->name, "%s", name);
    b->n = n;
    b->last = s_clock;
    s_cache_used += n * 2;
    return b;
}

static int sfx_start(const char *name, float vol, float pan, bool loop);

void sfx_play(const char *guid, float vol, float pan) {
    if (!s_on || !guid || strlen(guid) != 32 || vol <= 0.01f) return;
    sfx_index();
    SfxIdx key;
    memcpy(key.guid, guid, 33);
    SfxIdx *e = bsearch(&key, s_idx, s_nidx, sizeof(SfxIdx), cmp_idx);
    if (e) sfx_start(e->name, vol, pan, false);
}

void sfx_play_name(const char *name, float vol, float pan) {
    if (s_on && name && name[0] && vol > 0.01f) sfx_start(name, vol, pan, false);
}

int sfx_loop_name(const char *name, float vol) {
    return s_on && name && name[0] && vol > 0.01f ? sfx_start(name, vol, 0, true) : -1;
}

void sfx_stop(int v) {
    if (s_on && v >= 0 && v < SFX_VOICES) {
        ndspChnWaveBufClear(SFX_CH0 + v);
        s_vwb[v].status = NDSP_WBUF_DONE;
    }
}

static int sfx_start(const char *name, float vol, float pan, bool loop) {
    static int nosfx = -1;   // testing: SD:/3ds/3dsilksong/nosfx turns the sound effects off
    if (nosfx < 0) { FILE *t = fopen("sdmc:/3ds/3dsilksong/nosfx", "r"); nosfx = t != NULL; if (t) fclose(t); }
    if (nosfx) return -1;
    SfxBuf *b = sfx_get(name);
    if (!b) return -1;
    int v = -1;
    for (int i = 0; i < SFX_VOICES; i++)
        if (s_vwb[i].status == NDSP_WBUF_DONE || s_vwb[i].status == NDSP_WBUF_FREE) { v = i; break; }
    if (v < 0) return -1;
    int c = SFX_CH0 + v;
    ndspChnWaveBufClear(c);
    ndspChnReset(c);
    ndspChnSetInterp(c, NDSP_INTERP_LINEAR);
    ndspChnSetRate(c, RATE);
    ndspChnSetFormat(c, NDSP_FORMAT_MONO_PCM16);
    float mix[12] = {0};
    pan = clampf(pan, -1, 1);
    vol *= g_sfx_vol;
    mix[0] = vol * (pan > 0 ? 1 - pan : 1);
    mix[1] = vol * (pan < 0 ? 1 + pan : 1);
    ndspChnSetMix(c, mix);
    memset(&s_vwb[v], 0, sizeof s_vwb[v]);
    s_vwb[v].data_vaddr = b->data;
    s_vwb[v].nsamples = b->n;
    s_vwb[v].looping = loop;
    s_vdata[v] = b->data;
    ndspChnWaveBufAdd(c, &s_vwb[v]);
    return v;
}

static void sfx_free(void) {
    for (int v = 0; v < SFX_VOICES; v++) ndspChnWaveBufClear(SFX_CH0 + v);
    for (int i = 0; i < SFX_CACHE; i++) if (s_cache[i].data) { linearFree(s_cache[i].data); s_cache[i].data = NULL; }
}

bool audio_on(void) { return s_on; }
const char *audio_track(int c) { return s_on ? s_st[c].track : ""; }
