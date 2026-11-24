#pragma once
// Hornet: movement against the room's collision grid, needle attacks, dash, damage. Animations from
// sprites/knight.sss (the game's "Knight" tk2d library is Hornet's gameplay set).
#include "room.h"
#include "sprite.h"

// the collider, relative to the hero's origin (the sprites' origin): Hornet's feet are 1.53 units below it
#define HERO_HW   0.28f
#define HERO_FOOT (-1.53f)
#define HERO_HEAD 0.30f

enum { HS_NORMAL, HS_DASH, HS_HURT, HS_RESPAWN, HS_DEAD, HS_SIT, HS_BIND };
#define SILK_MAX  9       // the spool, and what a bind costs
#define BIND_HEAL 3
enum { ATK_NONE, ATK_SIDE, ATK_UP, ATK_DOWN };

typedef struct {
    float x, y, vx, vy;
    int face;               // +1 right, -1 left
    bool ground, was_ground;
    int state;
    float st;               // time in the state
    int attack;             // ATK_*, while an attack plays
    float atk_t;
    bool atk_alt, atk_hit;
    float atk_cd, dash_cd;
    bool air_dash;          // the air dash is still available
    bool jumping;           // rising from a jump at full speed (released B cuts it)
    float jump_t;
    float coyote, jump_buf;
    float invuln;
    float land_t;
    int look;               // -1 down, 0, +1 up (idle and holding)
    float look_t;
    int hp, maxhp;
    int silk;
    bool sprint;            // running fast: R held on after a dash on the ground
    bool bind_done;
    Anim body, fx;
    bool fx_on;
    int fx_kind;
} Hero;

typedef struct {
    u32 down, held, up;
} Input;

bool hero_init(Hero *h);
void hero_place(Hero *h, float x, float y, int face);
// one frame of play; returns the attack's hit box through *box (x0,y0,x1,y1) when it can hit this frame
bool hero_update(Hero *h, Room *r, const Input *in, float dt, float box[4]);
void hero_pogo(Hero *h);          // a down attack connected
void hero_hurt(Hero *h, int dmg, float from_x);
void hero_respawn(Hero *h, float x, float y);
// sits on a bench at x (any input gets her up)
void hero_sit(Hero *h, float x);
void hero_draw(Hero *h);
bool hero_box_free(const Room *r, float x, float y);
// drops onto the floor below (up to 'range' units), or pushes out of the ground; for room entries
void hero_settle(Hero *h, const Room *r, float range);
