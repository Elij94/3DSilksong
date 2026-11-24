// Music and ambience: music/<track>.pcm (16-bit mono 22050 Hz, tools/build_music_pcm.py) streamed from the RomFS
// to two DSP channels, refilled every frame. A new track fades the old one out first. Without the DSP firmware
// (SD:/3ds/dspfirm.cdc, dumped with DSP1) there is simply no sound.
#include <stdio.h>
#include <stdlib.h>
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

static void set_vol(int c, float v) {
    float mix[12] = {0};
    mix[0] = mix[1] = v;
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

void audio_exit(void) {
    if (!s_on) return;
    for (int c = 0; c < 2; c++) {
        stop(c);
        linearFree(s_st[c].data);
    }
    ndspExit();
    s_on = false;
}

bool audio_on(void) { return s_on; }
const char *audio_track(int c) { return s_on ? s_st[c].track : ""; }
