#pragma once
// What a kill flings out (HealthManager.SpawnCurrency): rosaries and shell shards that fall, bounce, lie on the
// ground, and go to Hornet when she touches them (CurrencyObjectBase). Totals in PlayerData "geo" and "ShellShards".
#include "common.h"
#include "room.h"

enum { CUR_SMALL, CUR_MED, CUR_LARGE, CUR_SMOOTH, CUR_SHARD, CUR_KINDS };

void currency_init(void);
void currency_clear(void);                          // a new room
// n pieces of a kind from (x, y): HealthManager's fling (80-100 degrees at 15-30, 65-115 at 30-45 for a mega fling)
void currency_fling(int kind, int n, float x, float y, bool mega);
// moves them; those touching Hornet's box (after a short delay) are picked up
void currency_update(const Room *r, float dt, float hx0, float hy0, float hx1, float hy1);
void currency_draw(void);
// the last pickup: what was got and how long ago (for the HUD counter)
float currency_shown(int *geo, int *shards);
int currency_count(void);                           // pieces out (the test log)
void currency_debug(char *out, int n);
