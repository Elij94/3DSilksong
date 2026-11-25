#pragma once
// The game's PlayMaker state machines, compiled by tools/build_fsm.py into rooms/<Scene>.ssf: the scene's hierarchy
// (nodes: every game object, linked to the room's objects where there is one) and the FSMs with the actions the
// engine runs (source/fsm_ops.h). They drive gates, levers, breakable walls, platforms, scene events...
#include "objects.h"
#include "hero.h"

// loads the room's FSMs (none if the file is missing); links the objects to their nodes
void fsm_load(const char *scene, Objects *os);
void fsm_free(void);
// one frame: the FSMs of active nodes, hero triggers, timers
void fsm_update(float dt, Hero *h, Objects *os);
// a needle hit on this box: the FSMs listening to damage on what it touches; true when something took it
bool fsm_strike(float x0, float y0, float x1, float y1);
// the walls FSMs open and close (breakable walls, battle gates...): is something solid in this box?
bool fsm_solid(float x0, float y0, float x1, float y1);
// Up near something whose FSM waits for INTERACT (an NPC, a lever, a door...): sends it; true when one took it
bool fsm_interact(float x0, float y0, float x1, float y1);
bool fsm_can_interact(float x0, float y0, float x1, float y1);   // for the prompt
// is the node active, with all its parents
bool fsm_node_active(int node);
// the player's progress flags (PlayerData), kept with the save
bool pd_bool(const char *name);
void pd_set_bool(const char *name, bool v);
int pd_int(const char *name);
void pd_set_int(const char *name, int v);
void pd_save(FILE *f);              // "pd name value" lines
void pd_load_line(const char *line);
void pd_clear(void);                // a new game
int fsm_count(void);                // for the status screen
