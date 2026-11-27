#pragma once
#include "common.h"

struct SpriteSet;

// Hornet's tools and crests (tools.c)
void tools_init(void);
// the tools she owns and hasn't placed go into a free slot of their colour
void tools_sync(void);
// at a bench: the red tools' stock full again
void tools_refill(void);
// R with dir +1 up, -1 down, 0 neutral: the red tool bound there thrown from (x, y); false if none could be
bool tools_use(float x, float y, int face, int dir);
void tools_update(float dt);

typedef struct { struct SpriteSet *set; const char *clip; int uses, binding; } ToolHud;
int tools_hud(ToolHud *out, int max);

// the Tools pane on the bottom screen (the inventory's second tab)
extern bool g_tools_at_bench;
void tools_ui_update(u32 down);
void tools_ui_draw(void);
void tools_ui_text(const char **sh, const char **nm, const char **dsh, const char **ds);
