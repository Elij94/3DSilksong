// The game's hit and death effects, per kind of enemy (fx/profiles.txt, tools/build_fx.py): the particles a needle
// hit or a kill throws (fx/<prefab>.spt) and the sounds it plays.
#include <stdlib.h>
#include <stdio.h>
#include "fx.h"
#include "particles.h"
#include "audio.h"

#define MAXP 96
#define MAXE 4

typedef struct {
    char kind;
    char name[48];
    bool flash;
    char fx[MAXE][40];
    int nfx;
    char snd[MAXE][33];
    int nsnd;
} FxProf;

static FxProf *s_p;
static int s_n = -1;

static void split(char *s, char out[][40], int cap, int width, int *n) {
    *n = 0;
    while (*s && *n < cap) {
        char *c = strchr(s, ',');
        if (c) *c = 0;
        if (*s) { snprintf(out[*n], width, "%s", s); (*n)++; }
        if (!c) break;
        s = c + 1;
    }
}

static void load(void) {
    s_n = 0;
    s_p = calloc(MAXP, sizeof(FxProf));
    FILE *f = fopen(data_path("fx/profiles.txt"), "r");
    if (!f || !s_p) { if (f) fclose(f); return; }
    char line[512];
    while (s_n < MAXP && fgets(line, sizeof line, f)) {
        char *col[6] = {0};
        int k = 0;
        char *p = line;
        col[k++] = p;
        while (*p && k < 6) { if (*p == '\t') { *p = 0; col[k++] = p + 1; } p++; }
        for (char *e = line; *e; e++) if (*e == '\n' || *e == '\r') *e = 0;
        if (k < 5) continue;
        FxProf *pr = &s_p[s_n++];
        pr->kind = col[0][0];
        snprintf(pr->name, sizeof pr->name, "%s", col[1]);
        pr->flash = col[2][0] == '1';
        split(col[3], pr->fx, MAXE, 40, &pr->nfx);
        char tmp[MAXE][40];
        int ns;
        split(col[4], tmp, MAXE, 40, &ns);
        pr->nsnd = ns;
        for (int i = 0; i < ns; i++) snprintf(pr->snd[i], 33, "%s", tmp[i]);
    }
    fclose(f);
}

int fx_profile(const char *name) {
    if (s_n < 0) load();
    if (!name || !name[0]) return -1;
    for (int i = 0; i < s_n; i++) if (!strcmp(s_p[i].name, name)) return i;
    return -1;
}

bool fx_flash(int prof) { return prof < 0 || prof >= s_n || s_p[prof].flash; }

static void play(int prof, float x, float y, float z, float angle, bool all_sounds) {
    if (prof < 0 || prof >= s_n) return;
    const FxProf *p = &s_p[prof];
    for (int i = 0; i < p->nfx; i++) particles_fx(p->fx[i], x, y, z, angle);
    float pan = clampf((x - g_view.cx) / 14, -0.8f, 0.8f);
    if (all_sounds) for (int i = 0; i < p->nsnd; i++) sfx_play(p->snd[i], 1, pan);
    else if (p->nsnd) sfx_play(p->snd[rand() % p->nsnd], 1, pan);
}

void fx_hit(int prof, float x, float y, float z, float angle) { play(prof, x, y, z, angle, false); }
void fx_death(int prof, float x, float y, float z, float angle, bool sounds) {
    if (prof < 0 || prof >= s_n) return;
    const FxProf *p = &s_p[prof];
    for (int i = 0; i < p->nfx; i++) particles_fx(p->fx[i], x, y, z, angle);
    if (sounds) for (int i = 0; i < p->nsnd; i++) sfx_play(p->snd[i], 1, clampf((x - g_view.cx) / 14, -0.8f, 0.8f));
}
