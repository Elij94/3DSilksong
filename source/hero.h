#pragma once
// Hornet: movement against the room's collision grid, needle attacks, dash, damage. Animations from
// sprites/knight.sss (the game's "Knight" tk2d library is Hornet's gameplay set).
#include "room.h"
#include "sprite.h"

// the collider, relative to the hero's origin (the sprites' origin): Hornet's feet are 1.53 units below it
#define HERO_HW   0.28f
#define HERO_FOOT (-1.53f)
#define HERO_HEAD 0.30f

enum { HS_NORMAL, HS_DASH, HS_HURT, HS_RESPAWN, HS_DEAD, HS_SIT, HS_BIND, HS_MANTLE, HS_SPIKE, HS_WALL, HS_HARPOON,
       HS_SOAR, HS_NEEDOLIN, HS_SWIM };
#define SILK_MAX  9       // the spool, and what a bind costs
#define BIND_HEAL 3
enum { ATK_NONE, ATK_SIDE, ATK_UP, ATK_DOWN, ATK_HARPOON };

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
    bool jump_cut_ok;       // releasing B still halves the rise (not after a bounce or a ledge jump)
    bool did_hang;          // the slow-gravity moment at the top of this jump is spent
    float grav;             // gravity scale: 1, or less just after the apex while B is held
    float coyote, jump_buf;
    float invuln;
    float land_t;
    int look;               // -1 down, 0, +1 up (idle and holding)
    float look_t;
    int hp, maxhp;
    int silk;
    bool sprint;            // running fast: R held on after a dash on the ground
    bool fast_air;          // a jump or an air dash that keeps sprint speed while going the same way
    float skid_t;           // slowing down from a sprint
    float recoil_t, recoil_v;   // pushed back by a needle hit
    float spike_rec;        // after a down spike: no attack yet
    int phase;              // HS_MANTLE / HS_SPIKE steps
    float swim_top, swim_x0, swim_x1, swim_flow;   // HS_SWIM: the water's top, its sides, its flow
    bool swim_spa;
    int swim_loop;          // the swimming sound's voice, -1
    // entering a room (HeroController.EnterScene): the buttons ignored, Left/Right held for her, up to enter_t, then
    // (enter_land) until she lands; nograv_t: rising at nograv_v without gravity
    float enter_t, nograv_t, nograv_v;
    int enter_dir;
    bool enter_land;
    bool queued_jump;
    // the abilities found later in the game, all unlocked here
    bool djump_used;        // Faydown Cloak: the double jump, once per jump
    float djump_t;          // its timing, -1 when not double jumping
    bool floating;          // Drifter's Cloak: floating down while B is held
    int wall_dir;           // Cling Grip: the wall's side
    float wall_t;
    float wj_t;             // time since a wall jump, -1 when none
    int wj_dir;
    float harpoon_d;        // Clawline: distance left to travel
    float claw_cd;
    float air_sprint_t;     // the glide after an air dash, -1 when none
    float turn_t;           // a sprint turning round
    bool bind_done;
    bool ctrl_off;          // a script holds her (RelinquishControl): no input
    bool anim_ext;          // a script plays her animations (StopAnimationControl)
    bool performing;        // playing the Needolin (the scripts' CheckHeroPerformanceRegion hear it)
    int needolin_loop;      // its sound's voice, -1 when none
    float sit_x0, sit_x1;   // the bench: from where she stood to the seat
    Anim body, fx, bsilk;  // bsilk: the bind silk round her
    float fx_dx, fx_dy, fx_kx, fx_ky;   // the effect's place by her (facing left) and its scale (the attack objects')
    Anim burst;             // the down spike's Downspike Burst, behind her
    bool burst_on;
    int burst_face;
    bool fx_on, silk_on;
    float silk_sx;          // the bind silk's mirroring (RandomlyFlipFloat)
    float flash_t;          // a white flash on her (flashFocusHeal, flashBenchRest)
    Anim heal;              // "Heal Anim": the Bind Flash burst when the bind heals
    bool heal_on;
    int bind_loop;          // the bind's looping sound (its voice), -1 when none
    int fx_kind;
} Hero;

typedef struct {
    u32 down, held, up;
} Input;

bool hero_init(Hero *h);
void hero_place(Hero *h, float x, float y, int face);
// the game's scene entry: dir held for t seconds (then until landing if land), rising at up_v for up_t first
void hero_enter(Hero *h, int dir, float t, bool land, float up_v, float up_t);
void hero_door_exit(Hero *h);   // out of a door: Exit Door To Idle, then control
// one frame of play; returns the attack's hit box through *box (x0,y0,x1,y1) when it can hit this frame
bool hero_update(Hero *h, Room *r, const Input *in, float dt, float box[4]);
void hero_pogo(Hero *h);          // a down attack connected: the bounce
// set by the game: is there an enemy in this box? the nearest one's x (along 'face') in *ex
extern bool (*hero_probe)(float x0, float y0, float x1, float y1, int face, float *ex);
void hero_recoil(Hero *h);        // a side attack connected: a short push back
void hero_hurt(Hero *h, int dmg, float from_x);
void hero_respawn(Hero *h, float x, float y);
// sits on a bench at x (any input gets her up)
void hero_sit(Hero *h, float x);
void hero_draw(Hero *h);
bool hero_box_free(const Room *r, float x, float y);
// drops onto the floor below (up to 'range' units), or pushes out of the ground; for room entries
void hero_settle(Hero *h, const Room *r, float range);
