#pragma once
// The game's map on the bottom screen (ui/map.sss, text/map.txt: tools/build_map.py), as GameMap shows it: an area
// appears once its map is bought (PlayerData Has<Area>Map), with every room's rough outline; the rooms Hornet visited
// are drawn in full once mapped, which a bench rest does when she has the quill (hasQuill). Visited and mapped rooms
// are saved with the slot ("sv <scene>", "sm <scene>").
#include "common.h"

void map_init(void);
void map_clear(void);                        // a new game, or before a slot is read
bool map_load_line(const char *line);        // a save line of the map's; false if it isn't one
void map_save(FILE *f);
void map_visit(const char *scene);           // a room entered, or a part of one (MapSceneRegion)
void map_set_mapped(const char *scene);      // SetMappedOnStart
// a bench rest: the visited rooms of the areas whose map she has become mapped (with the quill); true if any did
bool map_bench_update(void);
bool map_has_any(void);
void map_give_all(void);                     // testing: every area's map and the quill                      // PlayerData.HasAnyMap: an area's map bought
// draws the current room's area into the bottom screen (320 x 240), under its name; false if there is nothing to show
bool map_draw(const char *scene);
