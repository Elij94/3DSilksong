// Scripted input for testing in an emulator: SD:/3ds/3dsilksong/autotest.txt, one command per line, times in seconds
// of game time from the start:
//   <t0> <t1> KEY [KEY...]   hold these keys from t0 to t1 (A B X Y L R ZR UP DOWN LEFT RIGHT SELECT START)
//   <t> warp <Scene> <x> <y> put Hornet there
//   <t> quit                 save the log and leave
//   <t> shot <name>          the top screen as it is shown (800 or 400 wide) into SD:/3ds/3dsilksong/<name>.ppm
// Meanwhile SD:/3ds/3dsilksong/log.txt gets Hornet's state 20 times a second. Without the file nothing changes.
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "autotest.h"
#include "version.h"

#define MAXCMD 128
typedef struct {
    float t0, t1;
    u32 keys;
    int kind;   // 0 keys, 1 warp, 2 quit
    char scene[48];
    float x, y;
    bool done;
} Cmd;

static Cmd s_cmd[MAXCMD];
static int s_n;
static FILE *s_log;
static float s_t, s_next_log;
static bool s_on;

static u32 key_of(const char *k) {
    static const struct { const char *n; u32 k; } T[] = {
        {"A", KEY_A}, {"B", KEY_B}, {"X", KEY_X}, {"Y", KEY_Y}, {"L", KEY_L}, {"R", KEY_R}, {"ZR", KEY_ZR}, {"ZL", KEY_ZL},
        {"UP", KEY_UP}, {"DOWN", KEY_DOWN}, {"LEFT", KEY_LEFT}, {"RIGHT", KEY_RIGHT}, {"SELECT", KEY_SELECT},
        {"START", KEY_START}};
    for (unsigned i = 0; i < sizeof T / sizeof *T; i++)
        if (!strcasecmp(k, T[i].n)) return T[i].k;
    return 0;
}

bool autotest_init(void) {
    FILE *f = fopen("sdmc:" SS_DATADIR "autotest.txt", "r");
    if (!f) return false;
    char line[160];
    while (s_n < MAXCMD && fgets(line, sizeof line, f)) {
        Cmd *c = &s_cmd[s_n];
        memset(c, 0, sizeof *c);
        char w[6][48];
        int n = sscanf(line, "%47s %47s %47s %47s %47s %47s", w[0], w[1], w[2], w[3], w[4], w[5]);
        if (n < 2 || w[0][0] == '#') continue;
        c->t0 = atof(w[0]);
        if (!strcasecmp(w[1], "quit")) c->kind = 2;
        else if (!strcasecmp(w[1], "shot") && n >= 3) { c->kind = 3; snprintf(c->scene, sizeof c->scene, "%s", w[2]); }
        else if (!strcasecmp(w[1], "warp") && n >= 5) {
            c->kind = 1;
            snprintf(c->scene, sizeof c->scene, "%s", w[2]);
            c->x = atof(w[3]);
            c->y = atof(w[4]);
        } else {
            c->t1 = atof(w[1]);
            for (int i = 2; i < n; i++) c->keys |= key_of(w[i]);
        }
        s_n++;
    }
    fclose(f);
    s_log = fopen("sdmc:" SS_DATADIR "log.txt", "w");
    s_on = true;
    return true;
}

bool autotest_on(void) { return s_on; }

void autotest_note(const char *msg) {
    if (s_log) { fprintf(s_log, "%.2f note %s\n", s_t, msg); fflush(s_log); }
}

int autotest_step(float dt, Input *in, char *warp_scene, float *wx, float *wy) {
    if (!s_on) return 0;
    u32 held = 0;
    int r = 0;
    s_t += dt;
    for (int i = 0; i < s_n; i++) {
        Cmd *c = &s_cmd[i];
        if (c->kind == 0) {
            if (s_t >= c->t0 && s_t < c->t1) held |= c->keys;
        } else if (!c->done && s_t >= c->t0) {
            c->done = true;
            if (c->kind == 2) r = 2;
            else if (c->kind == 3 && !r) { strcpy(warp_scene, c->scene); r = 3; }
            else if (c->kind == 1 && !r) {
                strcpy(warp_scene, c->scene);
                *wx = c->x;
                *wy = c->y;
                r = 1;
            }
        }
    }
    static u32 last;
    in->held = held;   // the script alone drives the game
    in->down = held & ~last;
    in->up = last & ~held;
    last = held;
    return r;
}

void autotest_log(const char *scene, float x, float y, float vx, float vy, bool ground, int state, const char *clip,
                  float cx, float cy) {
    if (!s_log || s_t < s_next_log) return;
    s_next_log = s_t + 0.05f;
    fprintf(s_log, "%.2f %s x=%.2f y=%.2f vx=%.2f vy=%.2f %s st=%d clip=%s cam=%.2f,%.2f\n", s_t, scene, x, y, vx, vy,
            ground ? "G" : "A", state, clip, cx, cy);
    fflush(s_log);
}

void autotest_end(void) {
    if (s_log) fclose(s_log);
    s_log = NULL;
}
