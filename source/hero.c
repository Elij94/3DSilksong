#include <stdlib.h>
#include <math.h>
#include "hero.h"
#include "audio.h"

static void snd(const char *name) { sfx_play_name(name, 0.75f, 0); }

// tuning, in units and seconds (1 unit = 64 px of the original art). The values are Hornet's own (her HeroController
// and the Hunter crest's config); the game counts some of them in 20 ms physics steps.
#define RUN_SPEED    8.25f
#define GRAVITY      60.0f
#define HANG_GRAV    0.1f    // gravity scale just after the apex of a jump...
#define HANG_ACCEL   5.0f    // ...back to 1 at this rate while B is held, at once when it isn't
#define JUMP_V       18.6f   // held for up to 9 steps, at least 2; letting go of B halves the rise
#define JUMP_HOLD    0.18f
#define JUMP_MIN     0.04f
#define MAX_FALL     30.0f
#define DASH_V       28.0f
#define DASH_TIME    0.10f
#define AIR_DASH_TIME 0.08f
#define DASH_CD      0.425f
#define SPRINT_V     17.5f   // R held on after a dash: the dash speed eases down to this
#define AIR_SPRINT_V 12.0f
#define SPRINT_ACC   66.0f   // 1.1 per frame
#define SKID         0.85f   // sprint let go: speed kept per frame
#define ATK_TIME     0.35f
#define ATK_CD       0.41f
#define RECOIL_V     3.75f
#define RECOIL_TIME  0.16f
#define SPIKE_ANTIC  0.10f   // down spike: a stop in the air, then a dive forward and down
#define SPIKE_TIME   0.10f
#define SPIKE_V      26.0f
#define SPIKE_REC    0.25f
#define BOUNCE_TIME  0.10f   // a down spike that hits: a jump's last 5 steps, that B can't cut
#define COYOTE       0.08f
#define JUMP_BUF     0.12f
#define INVULN       1.0f
#define STEP_UP      0.55f
#define BIND_TIME    0.55f
#define SPRINT_TURN  0.30f   // "Sprint Turn"
#define AIR_SPRINT_T 0.235f  // the glide after an air dash, longer while R is held
// Cling Grip
#define WALL_CLING_DECEL 80.0f
#define WALL_STICK   0.15f
#define WALL_SLIDE_ACC 24.0f
#define WALL_SLIDE_MAX 12.0f
#define WJ_KICK      25.0f   // away from the wall, down to the run speed over WJ_TIME
#define WJ_TIME      0.30f
#define WJ_LOCK      0.10f
// Faydown Cloak, Drifter's Cloak
#define DJUMP_FALL   0.08f
#define DJUMP_RISE   0.08f
#define DJUMP_V      20.46f  // 1.1 jumps
#define DJUMP_MAXFALL 10.0f
#define FLOAT_V      (-3.0f)
// Clawline, Silk Soar
#define HARPOON_RANGE 11.0f
#define HARPOON_V    45.0f
#define HARPOON_ANTIC 0.15f
#define HARPOON_CD   0.16f
#define SOAR_CHARGE  0.30f
#define SOAR_THROW   0.28f
#define SOAR_ANTIC   0.28f
#define SOAR_V       34.0f
#define SOAR_CANCEL  0.20f
#define SOAR_MAX     2.5f

static SpriteSet *s_set;
bool (*hero_probe)(float x0, float y0, float x1, float y1, int face, float *ex);

bool hero_init(Hero *h) {
    memset(h, 0, sizeof *h);
    if (!s_set) {
        s_set = sprites_get("sprites/knight.sss");
        if (!s_set) s_set = sprites_get("sprites/hero.sss");
        if (s_set) {
            s_set->pinned = true;
            static const char *warm[] = {"Idle", "Run", "Airborne", "Fall", "Land Q", "Slash", "SlashAlt", "SlashEffect",
                                         "SlashEffectAlt", "UpSlash", "UpSlashEffect", "DownSpike", "DownSlashEffect",
                                         "Dash", "Air Dash", "DownSpike Antic", "Downspike Recovery",
                                         "Mantle Cling", "Mantle Vault", "Mantle Land", "Sprint", "Sprint Turn",
                                         "Sprint Air", "Sprint Air Loop", "Wall Cling", "Wall Slide", "Walljump",
                                         "Double Jump", "Double Jump Wings 2", "Umbrella Float", "Harpoon Dash"};
            for (unsigned i = 0; i < sizeof warm / sizeof *warm; i++) sprite_preload_clip(s_set, warm[i]);
        }
    }
    h->body.set = h->fx.set = s_set;
    h->body.clip = h->fx.clip = -1;
    h->maxhp = h->hp = 5;
    h->face = 1;
    anim_play_name(&h->body, "Idle", true);
    return s_set != NULL;
}

void hero_place(Hero *h, float x, float y, int face) {
    h->x = x; h->y = y;
    h->vx = h->vy = 0;
    if (face) h->face = face;
    h->state = HS_NORMAL;
    h->attack = ATK_NONE;
    h->fx_on = false;
    h->jumping = false;
    h->sprint = h->fast_air = false;
    h->grav = 1;
    h->spike_rec = h->recoil_t = h->skid_t = h->turn_t = 0;
    h->djump_used = h->floating = false;
    h->djump_t = h->wj_t = h->air_sprint_t = -1;
}

bool hero_box_free(const Room *r, float x, float y) {
    return !room_box_solid(r, x - HERO_HW, y + HERO_FOOT, x + HERO_HW, y + HERO_HEAD);
}

static void move_x(Hero *h, const Room *r, float dx) {
    while (fabsf(dx) > 1e-5f) {
        float st = clampf(dx, -0.2f, 0.2f);
        dx -= st;
        float nx = h->x + st;
        if (hero_box_free(r, nx, h->y)) { h->x = nx; continue; }
        if (h->ground || h->was_ground) {   // small steps and slopes in the grid
            bool ok = false;
            for (float up = 0.25f; up <= STEP_UP + 1e-3f; up += 0.25f)
                if (hero_box_free(r, nx, h->y + up)) { h->x = nx; h->y += up; ok = true; break; }
            if (ok) continue;
        }
        float cpu = (float)r->cpu;
        if (st > 0) {
            int c = (int)floorf((nx + HERO_HW - r->gx) * cpu);
            float x = r->gx + c / cpu - HERO_HW - 0.002f;
            if (x >= h->x - 0.25f && hero_box_free(r, x, h->y)) h->x = x;
        } else {
            int c = (int)floorf((nx - HERO_HW - r->gx) * cpu);
            float x = r->gx + (c + 1) / cpu + HERO_HW + 0.002f;
            if (x <= h->x + 0.25f && hero_box_free(r, x, h->y)) h->x = x;
        }
        if (h->state == HS_DASH) h->st = DASH_TIME;   // ends the dash
        h->sprint = false;
        h->vx = 0;
        break;
    }
}

// returns true when it hit something (ground when going down)
static bool move_y(Hero *h, const Room *r, float dy) {
    while (fabsf(dy) > 1e-5f) {
        float st = clampf(dy, -0.2f, 0.2f);
        dy -= st;
        float ny = h->y + st;
        if (hero_box_free(r, h->x, ny)) { h->y = ny; continue; }
        float cpu = (float)r->cpu;
        if (st < 0) {
            int c = (int)floorf((ny + HERO_FOOT - r->gy) * cpu);
            float y = r->gy + (c + 1) / cpu - HERO_FOOT + 0.002f;
            if (y <= h->y + 0.25f && hero_box_free(r, h->x, y)) h->y = y;
        } else {
            int c = (int)floorf((ny + HERO_HEAD - r->gy) * cpu);
            float y = r->gy + c / cpu - HERO_HEAD - 0.002f;
            if (y >= h->y - 0.25f && hero_box_free(r, h->x, y)) h->y = y;
        }
        return true;
    }
    return false;
}

void hero_settle(Hero *h, const Room *r, float range) {
    // out of the ground: the nearest free spot, down first (a low ceiling), then up
    if (!hero_box_free(r, h->x, h->y)) {
        float y = h->y;
        for (int i = 1; i <= 40; i++) {
            if (hero_box_free(r, h->x, y - i * 0.125f)) { h->y = y - i * 0.125f; break; }
            if (hero_box_free(r, h->x, y + i * 0.25f)) { h->y = y + i * 0.25f; break; }
        }
    }
    float y0 = h->y;
    if (move_y(h, r, -range)) { h->ground = true; return; }
    h->y = y0;   // nothing below: stay (falling in)
}

static void body(Hero *h, const char *clip, bool restart) { anim_play_name(&h->body, clip, restart); }

static bool clip_is(const Hero *h, const char *name) {
    return h->body.set && h->body.clip >= 0 && !strcmp(h->body.set->clips[h->body.clip].name, name);
}

static void start_attack(Hero *h, int kind) {
    h->attack = kind;
    h->atk_t = 0;
    h->atk_hit = false;
    h->atk_cd = ATK_CD;
    h->fx_on = true;
    h->fx_kind = kind;
    h->atk_alt = !h->atk_alt;
    const char *b = "Slash", *f = "SlashEffect";
    if (kind == ATK_SIDE && h->atk_alt) { b = "SlashAlt"; f = "SlashEffectAlt"; }
    if (kind == ATK_UP) { b = "UpSlash"; f = "UpSlashEffect"; }
    body(h, b, true);
    if (!anim_play_name(&h->fx, f, true)) h->fx_on = false;
    static const char *SW[] = {"hornet_sword_1", "hornet_sword_2", "hornet_sword_3", "hornet_sword_5"};
    snd(SW[rand() % 4]);
}

static void start_jump(Hero *h, bool full) {
    h->vy = JUMP_V;
    h->jumping = true;
    h->jump_t = 0;
    h->jump_cut_ok = !full;
    h->did_hang = false;
    h->grav = 1;
    h->ground = false;
    h->coyote = h->jump_buf = 0;
    h->fast_air = h->sprint;
    h->sprint = false;
    h->djump_t = -1;
    h->floating = false;
    snd(full ? "hornet_small_jump" : "hornet_jump_ss");
}

void hero_pogo(Hero *h) {
    // the bounce of a down spike that hit: back in control, rising for the end of a jump
    h->state = HS_NORMAL;
    h->attack = ATK_NONE;
    h->fx_on = false;
    h->spike_rec = 0;
    start_jump(h, true);
    h->jump_t = JUMP_HOLD - BOUNCE_TIME;
    h->vx = 0;
    h->air_dash = true;
    h->djump_used = false;
    h->atk_alt = !h->atk_alt;
    body(h, h->atk_alt ? "DownSpikeBounce 2" : "DownSpikeBounce 1", true);
}

void hero_recoil(Hero *h) {
    h->recoil_t = RECOIL_TIME;
    h->recoil_v = -h->face * RECOIL_V;
}

void hero_hurt(Hero *h, int dmg, float from_x) {
    if (h->invuln > 0 || h->state == HS_DEAD || h->state == HS_RESPAWN) return;
    h->hp -= dmg > 0 ? dmg : 1;
    h->invuln = INVULN;
    h->attack = ATK_NONE;
    h->fx_on = false;
    if (h->hp <= 0) {
        h->hp = 0;
        h->state = HS_DEAD;
        h->st = 0;
        h->vx = 0;
        body(h, "Death", true);
        return;
    }
    h->state = HS_HURT;
    h->st = 0;
    int away = h->x < from_x ? -1 : 1;
    h->vx = away * 9.0f;
    h->vy = 9.0f;
    h->face = -away;
    body(h, "Recoil", true);
}

void hero_respawn(Hero *h, float x, float y) {
    hero_place(h, x, y, 0);
    h->state = HS_RESPAWN;
    h->st = 0;
    h->invuln = INVULN;
    body(h, "Hazard Respawn", true);
}

void hero_sit(Hero *h, float x) {
    h->x = x;
    h->vx = h->vy = 0;
    h->state = HS_SIT;
    h->st = 0;
    h->attack = ATK_NONE;
    h->fx_on = false;
    body(h, "Sit", true);
}

static bool solid_at(const Room *r, float x0, float y0, float x1, float y1) {
    return room_box_solid(r, fminf(x0, x1), fminf(y0, y1), fmaxf(x0, x1), fmaxf(y0, y1));
}

// the first floor straight below (x, y) within len: its top
static bool ray_down(const Room *r, float x, float y, float len, float *top) {
    float cpu = (float)r->cpu, st = 0.5f / cpu;
    for (float d = 0; d <= len; d += st) {
        if (solid_at(r, x - 0.01f, y - d - 0.01f, x + 0.01f, y - d)) {
            int c = (int)floorf((y - d - 0.005f - r->gy) * cpu);
            *top = r->gy + (c + 1) / cpu;
            return true;
        }
    }
    return false;
}

// in the air against a wall, holding towards it, with its top within reach: the ledge's top
static bool find_ledge(const Hero *h, const Room *r, float *ly) {
    float f = (float)h->face, y = h->y + 0.67f, t1, t2, g;
    if (solid_at(r, h->x, y, h->x + f * 0.75f, y)) return false;           // the wall goes on above: no ledge
    if (!ray_down(r, h->x + f * 0.77f, y, 2.26f, &t1) || !ray_down(r, h->x + f * 0.37f, y, 2.26f, &t2)) return false;
    if (fabsf(t1 - t2) > 0.1f) return false;
    if (solid_at(r, h->x + f * 0.37f, t1 + 0.1f, h->x + f * 0.77f, t1 + 2.26f)) return false;  // no room on top
    if (ray_down(r, h->x, h->y + HERO_FOOT + 0.2f, 2.0f, &g) && t1 - g < 1.5f) return false;  // a floor just below
    *ly = t1;
    return true;
}

static bool touching_wall(const Hero *h, const Room *r, int side) {
    return !hero_box_free(r, h->x + side * 0.06f, h->y);
}

static void to_normal(Hero *h) {
    h->state = HS_NORMAL;
    h->st = 0;
}

static void start_wall(Hero *h, int side) {
    h->state = HS_WALL;
    h->st = 0;
    h->wall_dir = side;
    h->face = side;
    h->jumping = h->sprint = h->fast_air = h->floating = false;
    h->djump_t = -1;
    h->djump_used = false;
    h->air_dash = true;
    h->wj_t = -1;
    if (h->vy > 0) h->vy = 0;
    body(h, "Wall Cling", true);
    snd("hornet_wall_land");
}

static void start_dash(Hero *h) {
    h->state = HS_DASH;
    h->phase = h->ground ? 0 : 1;
    if (!h->ground) h->air_dash = false;
    h->st = 0;
    h->dash_cd = DASH_CD;
    h->attack = ATK_NONE;
    h->fx_on = false;
    h->vy = 0;
    h->jumping = h->floating = false;
    h->djump_t = -1;
    h->skid_t = h->turn_t = 0;
    h->wj_t = -1;
    body(h, h->ground ? "Dash" : "Air Dash", true);
    snd("hornet_hunter_new_dash");
}

static void approach(float *v, float target, float step) {
    *v = *v > target + step ? *v - step : *v < target - step ? *v + step : target;
}

bool hero_update(Hero *h, Room *r, const Input *in, float dt, float box[4]) {
    bool hit_frame = false;
    int dir = (in->held & KEY_RIGHT ? 1 : 0) - (in->held & KEY_LEFT ? 1 : 0);
    bool jump_down = in->down & KEY_B, jump_held = in->held & KEY_B;
    bool dash_down = in->down & (KEY_R | KEY_ZR), dash_held = in->held & (KEY_R | KEY_ZR);
    bool atk_down = in->down & KEY_Y;
    bool claw_down = in->down & (KEY_L | KEY_ZL), claw_held = in->held & (KEY_L | KEY_ZL);
    bool only_down = (in->held & KEY_DOWN) && !(in->held & (KEY_LEFT | KEY_RIGHT | KEY_UP));
    float frames = dt * 60.0f;

    h->st += dt;
    if (h->invuln > 0) h->invuln -= dt;
    if (h->atk_cd > 0) h->atk_cd -= dt;
    if (h->dash_cd > 0) h->dash_cd -= dt;
    if (h->claw_cd > 0) h->claw_cd -= dt;
    if (h->land_t > 0) h->land_t -= dt;
    if (h->spike_rec > 0) h->spike_rec -= dt;
    if (h->recoil_t > 0) h->recoil_t -= dt;
    h->was_ground = h->ground;

    switch (h->state) {
    case HS_DEAD:
        h->vy = fmaxf(h->vy - GRAVITY * dt, -MAX_FALL);
        if (move_y(h, r, h->vy * dt) && h->vy < 0) h->vy = 0;
        anim_update(&h->body, dt);
        return false;
    case HS_SIT:
        anim_update(&h->body, dt);
        if (h->st > 0.4f && (dir || jump_down || atk_down || dash_down)) {
            to_normal(h);
            body(h, "Get Off", true);
            break;
        }
        return false;
    case HS_RESPAWN:
        anim_update(&h->body, dt);
        if (h->body.done || h->st > 1.2f) { to_normal(h); body(h, "Idle", true); }
        return false;
    case HS_HURT:
        if (h->st > 0.25f) to_normal(h);
        dir = 0;
        jump_down = dash_down = atk_down = claw_down = false;
        break;
    case HS_DASH:
        if (h->st >= (h->phase ? AIR_DASH_TIME : DASH_TIME)) {
            to_normal(h);
            if (h->ground && dash_held) { h->sprint = true; h->vx = h->face * DASH_V; }
            else if (!h->ground) {
                // the air dash glides on: "Sprint Air", on at sprint speed while R is held
                h->air_sprint_t = 0;
                h->vx = h->face * DASH_V;
                body(h, "Sprint Air", true);
            }
            else h->skid_t = 0.2f;
        }
        break;
    case HS_BIND:
        // silk binds into health: a short charge (held in the air), then the burst; without a full spool it fails
        anim_update(&h->body, dt);
        h->vx = 0;
        if (!h->ground) h->vy = fmaxf(h->vy - GRAVITY * 0.1f * dt, -2.0f);
        else h->vy = 0;
        if (h->phase == 1) {                   // not enough silk
            if (h->st >= 0.3f) to_normal(h);
        } else {
            if (!h->bind_done && h->st >= BIND_TIME) {
                h->bind_done = true;
                h->silk -= SILK_MAX;
                h->hp = h->hp + BIND_HEAL > h->maxhp ? h->maxhp : h->hp + BIND_HEAL;
                body(h, h->ground ? "BindBurst Ground" : "BindBurst Air", true);
                snd("hornet_ability_bind");
            }
            if (h->st >= BIND_TIME + 0.2f) to_normal(h);
        }
        if (h->phase == 1 && h->st >= 0.18f && !clip_is(h, "BindCancel Ground") && !clip_is(h, "BindCancel Air"))
            body(h, h->ground ? "BindCancel Ground" : "BindCancel Air", true);
        move_y(h, r, h->vy * dt);
        h->ground = !hero_box_free(r, h->x, h->y - 0.03f);
        return false;
    case HS_MANTLE:
        // hanging on a ledge, then vaulting onto it; B jumps out of it (a full jump) once the vault has played
        anim_update(&h->body, dt);
        if (jump_down) h->queued_jump = true;
        if (h->phase == 0) {
            if (h->body.done && h->st >= 0.2f) {
                h->phase = 1;
                h->st = 0;
                for (float up = 1.6f; up > 0.4f; up -= 0.1f)
                    if (hero_box_free(r, h->x, h->y + up)) { h->y += up; break; }
                h->vx = h->face * 5.0f;
                body(h, "Mantle Vault", true);
                snd("hornet_edge_grab_up");
            }
            return false;
        }
        if (h->queued_jump && h->phase >= 1 && (h->body.done || h->phase == 2)) {
            to_normal(h);
            start_jump(h, true);
            h->queued_jump = false;
            body(h, "Mantle Cancel To Jump", true);
            jump_down = false;
            break;
        }
        if (atk_down && h->atk_cd <= 0) { to_normal(h); break; }
        move_x(h, r, h->vx * dt);
        if (h->phase == 1 && (h->body.done || h->st > 0.35f)) {
            h->phase = 2;
            h->st = 0;
            h->vy = 0;
            body(h, "Mantle Land", true);
        }
        if (h->phase == 2) {
            h->vy -= GRAVITY * dt;
            if (move_y(h, r, h->vy * dt)) h->vy = 0;
            if (h->st >= 0.12f) {
                to_normal(h);
                h->ground = !hero_box_free(r, h->x, h->y - 0.03f);
                h->land_t = 0.25f;
                body(h, dir ? "Mantle Land To Run" : "Mantle Land To Idle", true);
            }
        }
        return false;
    case HS_SPIKE: {
        // the down spike: a stop in the air, a dive forward and down (the hit), then the fall goes on
        if (h->phase == 0) {
            float k = powf(0.8f, frames);
            h->vx *= k;
            h->vy *= k;
            if (h->st >= SPIKE_ANTIC) {
                h->phase = 1;
                h->st = 0;
                h->attack = ATK_DOWN;
                h->atk_t = 0;
                h->atk_hit = false;
                body(h, "DownSpike", true);
                snd("hornet_sword_3");
            }
        } else {
            h->vx = h->face * SPIKE_V;
            h->vy = -SPIKE_V;
            hit_frame = true;
            float f = (float)h->face;
            float a = h->x + f * 0.1f, b = h->x + f * 2.0f;
            box[0] = fminf(a, b); box[2] = fmaxf(a, b); box[1] = h->y - 3.0f; box[3] = h->y - 0.6f;
        }
        move_x(h, r, h->vx * dt);
        bool landed = move_y(h, r, h->vy * dt) && h->vy < 0;
        if (landed || (h->phase == 1 && h->st >= SPIKE_TIME)) {
            to_normal(h);
            h->attack = ATK_NONE;
            h->spike_rec = SPIKE_REC;
            h->vy = landed ? 0 : -10.0f;
            h->ground = landed;
            if (landed) h->land_t = SPIKE_REC;
            body(h, landed ? "Downspike Recovery Land" : "Downspike Recovery", true);
        }
        anim_update(&h->body, dt);
        return hit_frame;
    }
    case HS_WALL: {
        // Cling Grip: against the wall, held towards it she clings; let go and she slides down. B jumps off it.
        anim_update(&h->body, dt);
        int side = h->wall_dir;
        if (jump_down) {
            to_normal(h);
            h->face = -side;
            start_jump(h, false);
            h->wj_t = 0;
            h->wj_dir = -side;
            h->vx = -side * WJ_KICK;
            body(h, "Walljump", true);
            snd("hornet_new_kickoff");
            jump_down = false;
            break;
        }
        if (dash_down && h->dash_cd <= 0) { h->face = -side; start_dash(h); break; }
        if (claw_down || atk_down) { to_normal(h); h->face = -side; break; }
        bool holding = dir == side;
        if (dir == -side && h->st > 0.06f) { to_normal(h); h->face = -side; break; }
        if (holding) {
            if (h->vy < -10) h->vy = -10;
            approach(&h->vy, 0, WALL_CLING_DECEL * dt);
            if (!clip_is(h, "Wall Cling")) body(h, "Wall Cling", true);
            h->wall_t = 0;
        } else {
            h->wall_t += dt;
            if (h->wall_t < WALL_STICK) h->vy = 0;
            else h->vy = fmaxf(h->vy - WALL_SLIDE_ACC * dt, -WALL_SLIDE_MAX);
            if (!clip_is(h, "Wall Slide")) body(h, "Wall Slide", true);
        }
        h->vx = 0;
        bool hit = move_y(h, r, h->vy * dt);
        h->ground = !hero_box_free(r, h->x, h->y - 0.03f);
        if ((hit && h->vy <= 0) || h->ground || !touching_wall(h, r, side)) { to_normal(h); h->vy = 0; }
        return false;
    }
    case HS_HARPOON: {
        // Clawline: the needle thrown ahead, then Hornet pulled along its thread to the wall or the enemy it caught
        anim_update(&h->body, dt);
        if (h->phase == 0) {
            h->vx = h->vy = 0;
            if (h->st >= HARPOON_ANTIC) {
                if (h->harpoon_d <= 0) {           // nothing in reach: the needle comes back
                    to_normal(h);
                    body(h, "Harpoon Needle Return", true);
                    break;
                }
                h->phase = 1;
                h->st = 0;
                h->attack = ATK_HARPOON;
                h->atk_t = 0;
                h->atk_hit = false;
                body(h, "Harpoon Dash", true);
            }
            return false;
        }
        float step = HARPOON_V * dt;
        if (step > h->harpoon_d) step = h->harpoon_d;
        float x0 = h->x;
        move_x(h, r, h->face * step);
        h->harpoon_d -= fabsf(h->x - x0);
        hit_frame = true;
        float a = h->x, b = h->x + h->face * 1.2f;
        box[0] = fminf(a, b); box[2] = fmaxf(a, b); box[1] = h->y - 1.4f; box[3] = h->y + 0.4f;
        bool blocked = fabsf(h->x - x0) < step * 0.5f;
        if (h->harpoon_d <= 0.01f || blocked || h->st > 0.6f) {
            h->attack = ATK_NONE;
            snd("hornet_needle_hit_wall");
            if (touching_wall(h, r, h->face) && !h->ground && !hero_box_free(r, h->x + h->face * 0.06f, h->y)) {
                start_wall(h, h->face);
                body(h, "Harpoon Catch", true);
            } else {
                to_normal(h);
                h->vx = h->face * RUN_SPEED;
                h->vy = 0;
                body(h, "Harpoon Catch", true);
            }
        }
        return hit_frame;
    }
    case HS_SOAR: {
        // Silk Soar: down + L held on the ground charges, letting go throws the needle up, and Hornet flies after it
        anim_update(&h->body, dt);
        h->vx = 0;
        if (h->phase == 0) {                        // charging
            h->vy = 0;
            if (!claw_held) {
                if (h->st < SOAR_CHARGE) { to_normal(h); body(h, "Super Jump Antic Cancel", true); return false; }
                h->phase = 1; h->st = 0;
                body(h, "Super Jump Throw", true);
                snd("hornet_superjump_pt_3_charge_ready");
            }
            return false;
        }
        if (h->phase == 1) {                        // the throw, the jump antic
            h->vy = 0;
            if (h->st >= SOAR_THROW && !clip_is(h, "Super Jump Jump Antic")) body(h, "Super Jump Jump Antic", true);
            if (h->st >= SOAR_THROW + SOAR_ANTIC) {
                h->phase = 2; h->st = 0;
                body(h, "Super Jump Loop", true);
                snd("hornet_superjump_pt_7_hornet_jump_big_2d");
            }
            return false;
        }
        // up, until the ceiling, a cancel (B, Y or L) or the end of the thread
        h->vy = SOAR_V;
        bool roof = move_y(h, r, h->vy * dt);
        if (roof) {
            to_normal(h);
            h->vy = 0;
            body(h, "Super Jump Hit Roof Q", true);
            return false;
        }
        if ((h->st > SOAR_CANCEL && (jump_down || atk_down || claw_down)) || h->st > SOAR_MAX) {
            to_normal(h);
            h->vy *= 0.35f;
            body(h, "Super Jump Loop Cancel", true);
        }
        return false;
    }
    }

    // ---- input
    if (h->state == HS_NORMAL) {
        bool recovering = h->spike_rec > 0;
        bool walljumping = h->wj_t >= 0 && h->wj_t < WJ_TIME;
        if (walljumping) {
            h->wj_t += dt;
            // locked away from the wall for a moment, then pushing back towards it ends the kick
            if (h->wj_t > WJ_LOCK && dir == -h->wj_dir) h->wj_t = WJ_TIME;
        } else h->wj_t = -1;
        walljumping = h->wj_t >= 0 && h->wj_t < WJ_TIME;
        if (dir && h->attack == ATK_NONE && !walljumping && h->turn_t <= 0) {
            if (dir != h->face) {
                if (h->sprint && h->ground) h->turn_t = SPRINT_TURN;   // a sprint turns with a skid
                else { h->sprint = false; h->fast_air = false; h->face = dir; }
            } else h->face = dir;
        }
        if (h->sprint && (!dash_held || !h->ground)) {
            if (h->ground) h->skid_t = 0.25f;
            h->sprint = false;
            h->turn_t = 0;
        }
        bool air_sprint = !h->ground && h->air_sprint_t >= 0 && (h->air_sprint_t < AIR_SPRINT_T || dash_held) &&
                          dir != -h->face;
        if (h->air_sprint_t >= 0) {
            h->air_sprint_t += dt;
            if (!air_sprint || h->ground) h->air_sprint_t = -1;
        }
        if (h->turn_t > 0) {
            h->turn_t -= dt;
            h->vx *= powf(SKID, frames);
            if (h->turn_t <= 0) { h->face = -h->face; h->vx = 0; }
        } else if (h->sprint) {
            approach(&h->vx, h->face * SPRINT_V, SPRINT_ACC * dt);
        } else if (walljumping) {
            float k = h->wj_t / WJ_TIME;
            h->vx = h->wj_dir * (WJ_KICK + (RUN_SPEED - WJ_KICK) * k);
        } else if (air_sprint) {
            approach(&h->vx, h->face * AIR_SPRINT_V, SPRINT_ACC * dt);
            float clamp = -10.0f - 20.0f * fminf(1, h->air_sprint_t);
            if (h->vy < clamp) h->vy = clamp;
        } else if (h->skid_t > 0 && h->ground) {
            h->skid_t -= dt;
            h->vx *= powf(SKID, frames);
            if (dir || fabsf(h->vx) < RUN_SPEED * 0.5f) h->skid_t = 0;
            if (h->skid_t <= 0) h->vx = dir * RUN_SPEED;
        } else if (h->fast_air && !h->ground && dir == h->face) {
            approach(&h->vx, h->face * AIR_SPRINT_V, SPRINT_ACC * dt);
        } else {
            h->fast_air = false;
            h->vx = (recovering && h->ground) ? 0 : dir * RUN_SPEED;
        }
        if (h->recoil_t > 0) h->vx = h->recoil_v;
        if (in->down & KEY_A) {
            if (h->attack == ATK_NONE) {
                h->state = HS_BIND;
                h->st = 0;
                h->bind_done = false;
                h->phase = h->silk >= SILK_MAX ? 0 : 1;
                h->sprint = h->jumping = h->floating = false;
                h->djump_t = -1;
                body(h, h->ground ? "BindCharge Ground" : "BindCharge Air", true);
                if (h->phase == 0) snd("hornet_bind_ready");
                return false;
            }
        }
        if (jump_down) h->jump_buf = JUMP_BUF;
        if (h->jump_buf > 0 && (h->ground || h->coyote > 0)) {
            start_jump(h, false);
            if (h->attack == ATK_NONE) body(h, h->fast_air ? "Sprint Air" : "Airborne", true);
        } else if (jump_down && !h->ground && h->attack == ATK_NONE) {
            if (!h->djump_used) {
                // Faydown Cloak: the double jump
                h->djump_used = true;
                h->djump_t = 0;
                h->jumping = h->floating = false;
                h->jump_cut_ok = false;
                h->did_hang = false;
                h->grav = 1;
                h->jump_buf = 0;
                h->air_sprint_t = -1;
                body(h, "Double Jump", true);
                snd("hornet_sprint_jump_first_spin");
                h->fx_on = anim_play_name(&h->fx, "Double Jump Wings 2", true);
            } else if (!h->floating && h->djump_t < 0) {
                // Drifter's Cloak: float down while B is held
                h->floating = true;
                h->jumping = false;
                h->jump_buf = 0;
                h->air_sprint_t = -1;
                body(h, "Umbrella Inflate", true);
                snd("hornet_umbrella_open");
            }
        }
        if (dash_down && h->dash_cd <= 0 && (h->ground || h->air_dash)) {
            if (dir) h->face = dir;
            start_dash(h);
        } else if (claw_down && h->claw_cd <= 0 && h->attack == ATK_NONE) {
            if (only_down && h->ground) {
                h->state = HS_SOAR;
                h->phase = 0;
                h->st = 0;
                h->sprint = false;
                body(h, "Super Jump Antic", true);
                return false;
            }
            // Clawline: the needle flies up to HARPOON_RANGE ahead; it catches on a wall or an enemy
            if (dir) h->face = dir;
            float f = (float)h->face, d = 0, reach = -1;
            for (d = 0.3f; d <= HARPOON_RANGE; d += 0.25f)
                if (solid_at(r, h->x + f * (d - 0.25f), h->y - 0.5f, h->x + f * d, h->y - 0.4f)) { reach = d - 0.35f; break; }
            float ex;
            float lim = reach > 0 ? reach : HARPOON_RANGE;
            if (hero_probe && hero_probe(fminf(h->x, h->x + f * lim), h->y - 1.5f, fmaxf(h->x, h->x + f * lim), h->y + 0.5f,
                                         h->face, &ex))
                reach = fabsf(ex - h->x);
            h->state = HS_HARPOON;
            h->phase = 0;
            h->st = 0;
            h->harpoon_d = reach;
            h->claw_cd = HARPOON_CD;
            h->sprint = h->jumping = h->floating = h->fast_air = false;
            h->djump_t = -1;
            h->air_sprint_t = -1;
            h->vx = h->vy = 0;
            body(h, "Harpoon Antic", true);
            return false;
        } else if (atk_down && h->atk_cd <= 0 && !recovering) {
            if ((in->held & KEY_DOWN) && !h->ground) {
                h->state = HS_SPIKE;
                h->phase = 0;
                h->st = 0;
                h->sprint = h->fast_air = h->jumping = h->floating = false;
                h->djump_t = -1;
                h->air_sprint_t = -1;
                h->vy = clampf(h->vy, -12.0f, 12.0f);
                h->atk_cd = ATK_CD;
                h->attack = ATK_NONE;
                h->fx_on = false;
                body(h, "DownSpike Antic", true);
                anim_update(&h->body, dt);
                return false;
            }
            start_attack(h, (in->held & KEY_UP) ? ATK_UP : ATK_SIDE);
        }
    }
    if (h->jump_buf > 0) h->jump_buf -= dt;

    // ---- the jump: full speed while B is held (at least JUMP_MIN, at most JUMP_HOLD); letting go halves the rise
    if (h->jumping) {
        h->jump_t += dt;
        if (h->jump_t >= JUMP_HOLD) h->jumping = false;
    }
    if (h->jump_cut_ok && !jump_held && h->vy > 0 && (h->jump_t >= JUMP_MIN || !h->jumping)) {
        h->vy *= 0.5f;
        h->jumping = false;
        h->jump_cut_ok = false;
    }
    if (h->floating && (!jump_held || h->ground)) {
        h->floating = false;
        if (!h->ground) body(h, "Umbrella Deflate", true);
    }

    // ---- motion
    if (h->state == HS_DASH) {
        h->vx = h->face * DASH_V;
        h->vy = 0;
    } else {
        if (h->djump_t >= 0) {
            // the double jump: a beat that only stops the fall, then the rise
            h->djump_t += dt;
            if (h->djump_t < DJUMP_FALL) { if (h->vy < -DJUMP_MAXFALL) h->vy = -DJUMP_MAXFALL; h->vy -= GRAVITY * dt; }
            else if (h->djump_t < DJUMP_FALL + DJUMP_RISE) h->vy = DJUMP_V;
            else { h->djump_t = -1; h->vy -= GRAVITY * dt; }
        } else if (h->floating) {
            h->vy = FLOAT_V;
        } else if (h->jumping) h->vy = JUMP_V;
        else {
            // a moment of floating at the top of the jump, as long as B is held
            if (!h->did_hang && !h->ground && h->vy < 0) { h->did_hang = true; h->grav = HANG_GRAV; }
            if (h->grav < 1) h->grav = jump_held ? fminf(1, h->grav + HANG_ACCEL * dt) : 1;
            h->vy -= GRAVITY * h->grav * dt;
        }
        if (h->vy < -MAX_FALL) h->vy = -MAX_FALL;
    }
    move_x(h, r, h->vx * dt);
    bool hit = move_y(h, r, h->vy * dt);
    if (hit) {
        if (h->vy < 0) {
            if (!h->was_ground && h->vy < -8 && h->attack == ATK_NONE && h->state == HS_NORMAL) {
                h->land_t = 0.25f;
                bool to_sprint = dash_held && dir == h->face && (h->air_sprint_t >= 0 || h->fast_air);
                if (to_sprint) { h->sprint = true; body(h, "Land to Sprint", true); }
                else body(h, h->spike_rec > 0 ? "Downspike Recovery Land" : "Land Q", true);
                snd(h->vy < -25 ? "hornet_land_hard_new" : "hornet_ground_land");
            }
            h->ground = true;
        }
        if (h->vy > 0) h->djump_t = -1;
        h->vy = 0;
        h->jumping = false;
    } else {
        h->ground = !hero_box_free(r, h->x, h->y - 0.03f);
        // walking down slopes and steps: stick to the floor
        if (!h->ground && h->was_ground && h->vy <= 0 && h->state != HS_DASH && !h->sprint) {
            float y0 = h->y;
            if (move_y(h, r, -STEP_UP - 0.05f)) h->ground = true;
            else h->y = y0;
        }
    }
    if (h->ground) {
        h->coyote = COYOTE;
        h->air_dash = true;
        h->did_hang = false;
        h->grav = 1;
        h->fast_air = false;
        h->jump_cut_ok = false;
        h->djump_used = false;
        h->djump_t = -1;
        h->floating = false;
        h->air_sprint_t = -1;
        h->wj_t = -1;
        if (h->vy < 0) h->vy = 0;
    } else if (h->coyote > 0) h->coyote -= dt;

    // ---- against a wall in the air: a ledge within reach to hang on, else cling to the wall
    if (h->state == HS_NORMAL && !h->ground && h->attack == ATK_NONE && dir && dir == h->face &&
        touching_wall(h, r, h->face)) {
        float ly;
        if (h->vy < 5 && find_ledge(h, r, &ly) && hero_box_free(r, h->x, ly + 0.1f)) {
            h->state = HS_MANTLE;
            h->phase = 0;
            h->st = 0;
            h->y = ly + 0.1f;
            h->vx = h->vy = 0;
            h->jumping = h->sprint = h->fast_air = h->floating = false;
            h->djump_t = -1;
            h->djump_used = false;
            h->air_sprint_t = -1;
            h->queued_jump = false;
            h->air_dash = true;
            body(h, "Mantle Cling", true);
            snd("hornet_edge_grab_v2_1");
            anim_update(&h->body, dt);
            return false;
        }
        if (h->vy <= 0 && !(h->wj_t >= 0 && h->wj_t < WJ_LOCK)) {
            start_wall(h, h->face);
            anim_update(&h->body, dt);
            return false;
        }
    }

    // ---- attack timing and hit box
    if (h->attack != ATK_NONE) {
        h->atk_t += dt;
        float t = h->atk_t;
        if (t > 0.03f && t < 0.18f) {
            hit_frame = true;
            float f = (float)h->face;
            if (h->attack == ATK_SIDE) {
                float a = h->x + f * 0.2f, b = h->x + f * 2.8f;
                box[0] = fminf(a, b); box[2] = fmaxf(a, b); box[1] = h->y - 1.3f; box[3] = h->y + 0.9f;
            } else {
                box[0] = h->x - 1.2f; box[2] = h->x + 1.2f; box[1] = h->y; box[3] = h->y + 3.0f;
            }
        }
        if (t >= ATK_TIME) h->attack = ATK_NONE;
    }

    // ---- look up / down when idle
    int look = 0;
    if (h->ground && !dir && h->state == HS_NORMAL && h->attack == ATK_NONE)
        look = (in->held & KEY_UP) ? 1 : (in->held & KEY_DOWN) ? -1 : 0;
    if (look != h->look) { h->look = look; h->look_t = 0; }
    else h->look_t += dt;

    // ---- animation
    if (h->state == HS_NORMAL && h->attack == ATK_NONE) {
        static const char *ONE_SHOT[] = {"Mantle Cancel To Jump", "DownSpikeBounce 1", "DownSpikeBounce 2",
                                         "Downspike Recovery", "Walljump", "Double Jump", "Harpoon Catch",
                                         "Harpoon Needle Return", "Super Jump Hit Roof Q", "Super Jump Loop Cancel",
                                         "Umbrella Deflate", "Super Jump Antic Cancel"};
        bool one_shot = false;
        for (unsigned i = 0; i < sizeof ONE_SHOT / sizeof *ONE_SHOT; i++)
            if (clip_is(h, ONE_SHOT[i]) && !h->body.done) one_shot = true;
        if (!h->ground) {
            if (h->floating) {
                if (!clip_is(h, "Umbrella Float") && (!clip_is(h, "Umbrella Inflate") || h->body.done))
                    body(h, "Umbrella Float", false);
            } else if (h->air_sprint_t >= 0) {
                if (!clip_is(h, "Sprint Air Loop") && (!clip_is(h, "Sprint Air") || h->body.done))
                    body(h, "Sprint Air Loop", false);
            } else if (!one_shot && !clip_is(h, "Airborne") && !clip_is(h, "Fall") && !clip_is(h, "Sprint Air"))
                body(h, h->was_ground && h->vy <= 0 ? "Fall" : "Airborne", false);
        } else if (h->turn_t > 0) {
            body(h, "Sprint Turn", false);
        } else if (h->sprint) {
            if (!(clip_is(h, "Land to Sprint") && !h->body.done)) body(h, "Sprint", false);
        } else if (h->skid_t > 0) {
            body(h, "Skid End 1", false);
        } else if (h->land_t > 0 && (clip_is(h, "Mantle Land To Run") || clip_is(h, "Mantle Land To Idle")) &&
                   !h->body.done) {
        } else if (dir && !(h->spike_rec > 0)) {
            body(h, "Run", false);
        } else if (h->land_t > 0 && (clip_is(h, "Land Q") || clip_is(h, "Downspike Recovery Land"))) {
        } else if (look > 0) {
            body(h, "LookUp", false);
        } else if (look < 0) {
            body(h, "LookDown", false);
        } else if (!clip_is(h, "Idle") && !(one_shot && clip_is(h, "Super Jump Antic Cancel"))) {
            body(h, "Idle", false);
        }
    }
    if (h->state == HS_NORMAL && h->attack == ATK_NONE && h->body.done && h->ground && !dir && !h->sprint &&
        h->skid_t <= 0) body(h, "Idle", true);
    anim_update(&h->body, dt);
    if (h->fx_on) {
        anim_update(&h->fx, dt);
        if (h->fx.done) h->fx_on = false;
    }
    return hit_frame;
}

void hero_draw(Hero *h) {
    if (!h->body.set) {
        gfx_rect_world(h->x - HERO_HW, h->y + HERO_FOOT, h->x + HERO_HW, h->y + HERO_HEAD, C2D_Color32(255, 80, 80, 255));
        return;
    }
    // blink while invulnerable
    if (h->invuln > 0 && h->state != HS_RESPAWN && h->state != HS_DEAD && ((int)(h->invuln * 12) & 1)) return;
    gfx_blend(BLEND_NORMAL);
    // the art faces left: flip it when facing right
    float sx = h->face > 0 ? -1.0f : 1.0f;
    sprite_draw(h->body.set, anim_frame(&h->body), h->x, h->y, sx, 1, 1, NULL);
    if (h->fx_on) {
        sprite_draw(h->fx.set, anim_frame(&h->fx), h->x, h->y, sx, 1, 1, NULL);
    }
}
