#pragma once
// The room's live objects (rooms/<Scene>.sso): drawn with their default animation; enemies and breakables can be
// hit, and enemies / damaging objects hurt Hornet on contact. Their behaviour (the PlayMaker FSMs) is not run yet.
#include "sprite.h"

struct Room_;

enum { EF_LOW_ARC = 1, EF_FACES_RIGHT = 2, EF_NO_CORPSE = 4, EF_FREEZE = 8, EF_RECOIL = 16 };
enum {
    OF_ENEMY = 1, OF_BREAKABLE = 2, OF_INTERACT = 4, OF_ANIMATED = 8, OF_FSM = 16, OF_DAMAGE = 32, OF_INACTIVE = 64
};

typedef struct {
    u16 flags;
    s16 parent;
    float x, y, z, scx, scy;
    float rx, ry, rw, rh;
    int hp, damage;
    char name[33], sprite_set[33], clip[33];
    Anim anim;
    float par;
    bool alive;
    float flash;           // hit flash, seconds left
    s16 node;              // its node in the room's FSM hierarchy (fsm.c), -1 if none
    bool hidden;           // renderer off (an FSM)
    float rot;             // turned by a script (SetRotation, Rotate, FaceAngle), degrees counter-clockwise
    bool nocoll;           // collider off (an FSM)
    bool nodamage;         // doesn't hurt Hornet (an FSM)
    // a hit and a kill, as the game does them (the .ssf's enemy table: fsm.c; effects: fx.c)
    s16 hitp, deathp;      // effect profiles, -1
    SpriteSet *corpse_set; // the corpse's animations when they are not in the enemy's own set
    float fling;           // the corpse's speed
    u8 eflags;             // EF_*
    float recoil_v, recoil_dur, recoil_t, rdx, rdy;
    float flash_t;         // time since a hit flash started (< 0: none)
    float hit_cd;          // one hit per swing
    u8 ndsnd;
    const char *dsnd[4];   // death sounds (guids)
    bool invincible;       // SetInvincible: hits land (effects) but do no damage
    // Walker: the game's walking component, started and stopped by the FSMs
    bool has_walker, walking, w_pauses, w_ignore_holes;
    float w_speed[2], w_pause[2], w_wait[2], w_edge, w_t;
    // Crawler / WalkerV2: along the floor in the way it faces at one speed, turning at walls and edges
    bool crawler, crawl_sfr;
    float crawl_speed;
    const char *crawl_anim, *turn_anim, *idle_anim;
    // PersistentBoolItem: a kill remembered (semi: until a bench rest), its PlayerData key
    char persist[48];
    // HealthManager's drops: small, medium, large, large smooth rosaries, shell shards; a wider, faster fling
    u16 geo[5];
    bool mega_fling;
    // behaviour (enemy.c)
    u8 ai;                 // AI_*
    bool dead;             // a corpse: still drawn, no longer hits nor gets hit
    bool grounded;
    s8 dir;                // facing: +1 right, -1 left
    float vx, vy, kb, turn_t, t;
    float home_x, home_y;
    s16 c_move, c_fly, c_turn, c_dair, c_dland, c_idle;
} Obj;

typedef struct {
    int n, cap;            // cap: room kept for spawned prefabs (fsm.c)
    Obj *o;
    s16 *order;            // drawing order: far to near
} Objects;

// spawned prefabs (fsm.c): is there room for n objects, a pack's objects put from index at, the drawing order again
bool objects_room(const Objects *os, int n);
void objects_put(Objects *os, const u8 *sso, size_t size, int at, float dx, float dy);
void objects_sorted(Objects *os);

void objects_load(Objects *os, const char *scene);
void objects_free(Objects *os);
void objects_update(Objects *os, float dt);
// moves object i and the objects attached under it
void obj_move(Objects *os, int i, float dx, float dy);
// draws the objects with z in (zmin, zmax]
void objects_draw(Objects *os, float zmin, float zmax);
void objects_preload(Objects *os);
// the live enemy nearest from_x whose hit box overlaps the box: its centre x in *ex
bool objects_enemy_in(const Objects *os, float x0, float y0, float x1, float y1, float from_x, float *ex);   // inflates the pages their animations show
bool obj_alive(const Objects *os, int i);
// strikes the objects in the box; returns how many were hit (enemies take dmg, pushed away from from_x)
// dir: the hit's direction (0 right, 1 up, 2 left, 3 down)
int objects_strike(Objects *os, float x0, float y0, float x1, float y1, int dmg, float from_x, int dir);
// damage dealt by the first live damaging object overlapping the box, 0 if none; its centre in *cx
int objects_touch_damage(const Objects *os, float x0, float y0, float x1, float y1, float *cx);

enum { AI_NONE, AI_STILL, AI_WALK, AI_FLY, AI_FSM };   // AI_FSM: its own FSMs drive it
// enemy.c: picks a behaviour for each enemy from its animations, and runs them
void enemies_init(Objects *os, const struct Room_ *r);
void enemies_update(Objects *os, const struct Room_ *r, float hx, float hy, float dt);
// an enemy at 0 hp: its death animation and corpse, or gone
void enemy_kill(Objects *os, int i, float from_x, int dir);
// set by the game: a short slow-down (the game's FreezeMoment: ramp down, hold, ramp up, in seconds)
extern void (*objects_freeze)(float down, float hold, float up);
