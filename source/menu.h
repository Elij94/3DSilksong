#pragma once
// The game's menus, with its own texts (MainMenu sheet) and font: the title (Commencer le jeu, Options, Quitter le
// jeu), the profile screen (4 saves), the options (music and sound volumes, language, debug info) and the pause menu.
// Settings in SD:/3ds/3dsilksong/settings.txt.
#include "hero.h"

typedef enum { MA_NONE, MA_RESUME, MA_LOAD, MA_NEW, MA_TO_TITLE, MA_QUIT, MA_ROOMS, MA_TEST } MenuAction;
// MA_TEST: a test menu choice for the game to apply (in *slot): 0 health and silk full, 1 one more mask
enum { TEST_REFILL, TEST_MASK };

void menu_init(void);                 // reads and applies the settings
void menu_open_title(void);
void menu_open_pause(void);
// one frame of the open menu; MA_LOAD / MA_NEW give the slot (1..4) in *slot
MenuAction menu_update(const Input *in, int *slot);
void menu_draw(void);                 // over the top screen
bool menu_debug(void);                // the debug info option
// a save slot's file (1..4)
const char *menu_save_path(int slot);
// the area a room belongs to, in the current language ("" if unknown)
const char *menu_zone_name(const char *scene);
