#include <math.h>
#include "hero.h"

// tuning, in units and seconds (1 unit = 64 px of the original art)
#define RUN_SPEED   8.6f
// gravity: rising / around the apex (a little float) / falling (a snappier drop)
#define GRAV_UP     52.0f
#define GRAV_APEX   34.0f
#define GRAV_DOWN   66.0f
// the jump holds its speed as long as B is held, up to JUMP_HOLD (as in Hollow Knight): a tap is ~1.5 units high,
// a full jump ~5.3 (Hornet is ~2.3 tall)
#define JUMP_V      16.5f
#define JUMP_HOLD   0.17f
#define JUMP_MIN    0.05f
#define JUMP_CUT    6.0f
#define MAX_FALL    22.0f
#define DASH_V      21.0f
#define DASH_TIME   0.20f
#define DASH_CD     0.35f
#define ATK_TIME    0.26f
#define ATK_CD      0.30f
#define COYOTE      0.08f
#define JUMP_BUF    0.12f
#define INVULN      1.3f
#define STEP_UP     0.55f

static SpriteSet *s_set;

bool hero_init(Hero *h) {
    memset(h, 0, sizeof *h);
    if (!s_set) {
        s_set = sprites_get("sprites/knight.sss");
        if (!s_set) s_set = sprites_get("sprites/hero.sss");
        if (s_set) {
            s_set->pinned = true;
            static const char *warm[] = {"Idle", "Run", "Airborne", "Fall", "Land Q", "Slash", "SlashAlt", "SlashEffect",
                                         "SlashEffectAlt", "UpSlash", "UpSlashEffect", "DownSpike", "DownSlashEffect",
                                         "Dash", "Air Dash"};
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
    if (kind == ATK_DOWN) { b = "DownSpike"; f = "DownSlashEffect"; }
    body(h, b, true);
    if (!anim_play_name(&h->fx, f, true)) h->fx_on = false;
}

void hero_pogo(Hero *h) {
    h->vy = 16.0f;
    h->jumping = false;
    h->air_dash = true;
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

bool hero_update(Hero *h, Room *r, const Input *in, float dt, float box[4]) {
    bool hit_frame = false;
    int dir = (in->held & KEY_RIGHT ? 1 : 0) - (in->held & KEY_LEFT ? 1 : 0);
    bool jump_down = in->down & KEY_B, jump_held = in->held & KEY_B;
    bool dash_down = in->down & (KEY_R | KEY_ZR), atk_down = in->down & KEY_Y;

    h->st += dt;
    if (h->invuln > 0) h->invuln -= dt;
    if (h->atk_cd > 0) h->atk_cd -= dt;
    if (h->dash_cd > 0) h->dash_cd -= dt;
    if (h->land_t > 0) h->land_t -= dt;
    h->was_ground = h->ground;

    switch (h->state) {
    case HS_DEAD:
        h->vy = fmaxf(h->vy - GRAV_DOWN * dt, -MAX_FALL);
        if (move_y(h, r, h->vy * dt) && h->vy < 0) h->vy = 0;
        anim_update(&h->body, dt);
        return false;
    case HS_SIT:
        anim_update(&h->body, dt);
        if (h->st > 0.4f && (dir || jump_down || atk_down || dash_down)) {
            h->state = HS_NORMAL;
            body(h, "Get Off", true);
            break;
        }
        return false;
    case HS_RESPAWN:
        anim_update(&h->body, dt);
        if (h->body.done || h->st > 1.2f) { h->state = HS_NORMAL; body(h, "Idle", true); }
        return false;
    case HS_HURT:
        if (h->st > 0.25f) h->state = HS_NORMAL;
        dir = 0;
        jump_down = dash_down = atk_down = false;
        break;
    case HS_DASH:
        if (h->st >= DASH_TIME) {
            h->state = HS_NORMAL;
            h->vx = dir * RUN_SPEED;
        }
        break;
    }

    // ---- input
    if (h->state == HS_NORMAL) {
        if (dir && h->attack == ATK_NONE) h->face = dir;
        h->vx = dir * RUN_SPEED;
        if (jump_down) h->jump_buf = JUMP_BUF;
        if (h->jump_buf > 0 && (h->ground || h->coyote > 0)) {
            h->vy = JUMP_V;
            h->jumping = true;
            h->jump_t = 0;
            h->ground = false;
            h->coyote = h->jump_buf = 0;
            if (h->attack == ATK_NONE) body(h, "Airborne", true);
        }
        if (dash_down && h->dash_cd <= 0 && (h->ground || h->air_dash)) {
            if (!h->ground) h->air_dash = false;
            if (dir) h->face = dir;
            h->state = HS_DASH;
            h->st = 0;
            h->dash_cd = DASH_CD;
            h->attack = ATK_NONE;
            h->fx_on = false;
            h->vy = 0;
            h->jumping = false;
            body(h, h->ground ? "Dash" : "Air Dash", true);
        } else if (atk_down && h->atk_cd <= 0) {
            int kind = ATK_SIDE;
            if (in->held & KEY_UP) kind = ATK_UP;
            else if ((in->held & KEY_DOWN) && !h->ground) kind = ATK_DOWN;
            start_attack(h, kind);
        }
    }
    if (h->jump_buf > 0) h->jump_buf -= dt;
    if (h->jumping) {
        h->jump_t += dt;
        if (h->jump_t >= JUMP_MIN && (!jump_held || h->jump_t >= JUMP_HOLD)) {
            if (!jump_held && h->vy > JUMP_CUT) h->vy = JUMP_CUT;
            h->jumping = false;
        }
    }

    // ---- motion
    if (h->state == HS_DASH) {
        h->vx = h->face * DASH_V;
        h->vy = 0;
    } else {
        if (h->jumping) h->vy = JUMP_V;
        else h->vy -= (h->vy > 3 ? GRAV_UP : h->vy > -3 ? GRAV_APEX : GRAV_DOWN) * dt;
        if (h->attack == ATK_DOWN && h->vy < -6) h->vy = -6;   // the down spike holds Hornet up a bit
        if (h->vy < -MAX_FALL) h->vy = -MAX_FALL;
    }
    move_x(h, r, h->vx * dt);
    bool hit = move_y(h, r, h->vy * dt);
    if (hit) {
        if (h->vy < 0) {
            if (!h->was_ground && h->vy < -8 && h->attack == ATK_NONE && h->state == HS_NORMAL) {
                h->land_t = 0.25f;
                body(h, "Land Q", true);
            }
            h->ground = true;
        }
        h->vy = 0;
        h->jumping = false;
    } else {
        h->ground = !hero_box_free(r, h->x, h->y - 0.03f);
        // walking down slopes and steps: stick to the floor
        if (!h->ground && h->was_ground && h->vy <= 0 && h->state != HS_DASH) {
            float y0 = h->y;
            if (move_y(h, r, -STEP_UP - 0.05f)) h->ground = true;
            else h->y = y0;
        }
    }
    if (h->ground) {
        h->coyote = COYOTE;
        h->air_dash = true;
        if (h->vy < 0) h->vy = 0;
        if (h->attack == ATK_DOWN) { h->attack = ATK_NONE; h->fx_on = false; }
    } else if (h->coyote > 0) h->coyote -= dt;

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
            } else if (h->attack == ATK_UP) {
                box[0] = h->x - 1.2f; box[2] = h->x + 1.2f; box[1] = h->y; box[3] = h->y + 3.0f;
            } else {
                box[0] = h->x - 1.0f + f * 0.6f; box[2] = h->x + 1.0f + f * 0.6f; box[1] = h->y - 3.2f; box[3] = h->y - 0.5f;
            }
        }
        float len = h->attack == ATK_DOWN ? 0.45f : ATK_TIME;
        if (t >= len) h->attack = ATK_NONE;
    }

    // ---- look up / down when idle
    int look = 0;
    if (h->ground && !dir && h->state == HS_NORMAL && h->attack == ATK_NONE)
        look = (in->held & KEY_UP) ? 1 : (in->held & KEY_DOWN) ? -1 : 0;
    if (look != h->look) { h->look = look; h->look_t = 0; }
    else h->look_t += dt;

    // ---- animation
    if (h->state == HS_NORMAL && h->attack == ATK_NONE) {
        if (!h->ground) {
            if (!clip_is(h, "Airborne") && !clip_is(h, "Fall")) body(h, h->was_ground && h->vy <= 0 ? "Fall" : "Airborne", false);
        } else if (dir) {
            body(h, "Run", false);
        } else if (h->land_t > 0 && clip_is(h, "Land Q")) {
        } else if (look > 0) {
            body(h, "LookUp", false);
        } else if (look < 0) {
            body(h, "LookDown", false);
        } else if (!clip_is(h, "Idle")) {
            body(h, "Idle", false);
        }
    }
    if (h->state == HS_NORMAL && h->attack == ATK_NONE && h->body.done && h->ground && !dir) body(h, "Idle", true);
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
