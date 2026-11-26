#pragma once
// The inventory's Inv pane on the bottom screen (SELECT), from the game's (text/inventory.txt, ui/inventory.sss:
// tools/build_inventory.py): the needle at its level, the heart pieces and spool pieces, the silk spool and the
// skills Hornet has, the rosaries and shell shards, the collectables she holds (PlayerData "item:<name>" counts, or
// their states' tests), and the selection's name and description. The game waits while it is open.
#include "common.h"

void inv_open(void);
void inv_close(void);
bool inv_active(void);
void inv_update(u32 down);
void inv_draw(void);            // into the bottom screen (320 x 240)
void inv_draw_top(void);        // over the top screen: the selection's name and description
