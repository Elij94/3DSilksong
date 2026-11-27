#pragma once
#include "common.h"

// items lying in the rooms (pickups.c)
void pickups_load(const char *scene);
// Hornet's box: what she touches is picked up
void pickups_update(float dt, float x0, float y0, float x1, float y1);
void pickups_draw(void);
// the name and icon of what she just picked up (top screen, sw x sh)
void pickups_draw_popup(float sw, float sh);
