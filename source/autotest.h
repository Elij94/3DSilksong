#pragma once
#include "hero.h"

// scripted input and a state log, for tests in an emulator (see autotest.c); off unless the script file exists
bool autotest_init(void);
bool autotest_on(void);
void autotest_note(const char *msg);   // a line in the log
// replaces the input with the script's; returns 1 for a warp (scene, x, y filled), 2 to quit, 3 for a screenshot
// (its name in warp_scene)
int autotest_step(float dt, Input *in, char *warp_scene, float *wx, float *wy);
void autotest_log(const char *scene, float x, float y, float vx, float vy, bool ground, int state, const char *clip,
                  float cx, float cy);
void autotest_end(void);
