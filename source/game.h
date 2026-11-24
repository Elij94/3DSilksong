#pragma once
#include "hero.h"

// loads the save (SD:/3ds/3dsilksong/save.txt) or starts in the first room; false if the data is unusable
bool game_start(void);
void game_update(const Input *in, float dt);
void game_draw(void);
// the bottom screen's text (console)
void game_status(float fps, float ms);
void game_save(void);
// frees the room (and closes its files): before romfsUnmount, or the C library's exit crashes on them
void game_end(void);
