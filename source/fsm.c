#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <sys/stat.h>
#include "fsm.h"
#include "fsm_ops.h"
#include "autotest.h"
#include "dialogue.h"
#include "audio.h"
#include "fx.h"
#include "room.h"
#include "particles.h"
#include "currency.h"
#include "shop.h"
#include "map.h"

enum { T_NONE, T_FLOAT, T_INT, T_BOOL, T_STR, T_VAR, T_NODE, T_EVENT, T_VEC, T_LIST, T_TARGET };
#define N_OWNER (-2)
#define N_HERO  (-3)
#define N_NONE  (-1)
#define N_SHOP  (-4)    // the shop menu (GetShopObject)
#define N_VAR0  (-16)   // event targets: -16 - i is the object in the FSM's variable i

typedef struct Val {
    u8 t;
    u8 n;                  // list: count; target: kind
    s16 node;
    u16 s;                 // string, event, var index, target fsm name
    s32 i;
    float f[3];
    struct Val *list;
} Val;

typedef struct {
    u8 op, every, nargs;
    Val *a;
} Action;

typedef struct {
    u16 name;
    u16 ntr;
    u16 *tr;               // event, state pairs
    u16 nact;
    Action *act;
} State;

typedef struct {
    float t, dur;
    float x0, y0, dx, dy;
    u8 done, inside;
} ActRt;

typedef struct {
    s16 owner;
    u16 name;
    u16 start;
    u16 nvars;
    u16 *var_names;
    Val *vars;
    u16 nglob;
    u16 *glob;
    u16 nstates;
    State *states;
    int cur, next;         // state, -1; next: a transition waiting, -1
    bool started, stopped, finished_sent;
    int maxact;
    ActRt *rt;
    int host;              // a template's FSM (RunFSM): the FSM that runs it, -1 for the others
    bool running;          // a template's FSM: started by its host's RunFSM, until it finishes or the host moves on
    int sub;               // the template FSM this one runs (RunFSM in its state), -1
    Val *vars0;            // the variables' first values (a template's FSM starts again from them)
} Fsm;

typedef struct {
    u16 name;
    s16 parent, obj;
    float x, y;            // nodes without an object: their own position; else the object's
    float vx, vy;
    u8 active;
    u8 nocoll;             // its collider switched off
    float rx, ry, rw, rh;  // its colliders, relative to its position (w 0: none)
    s8 body;               // Rigidbody2D: -1 none, 0 dynamic, 1 kinematic, 2 static
    bool kin;              // made kinematic by an FSM
    float grav, sx;        // gravity scale, scale x (when it has no object)
    u8 coll, coll_prev;    // the sides it touched this frame and the last: 1 top, 2 right, 4 bottom, 8 left
} Node;

typedef struct {
    s16 node;
    float x0, y0, x1, y1;  // relative to the node's position
} Solid;
static Solid *s_solids;
static int s_nsolids;

typedef struct {
    int fsm;               // -1: to the node's FSMs
    s16 node;
    u16 fsm_name;
    u16 ev;
    float t;
} Delayed;

static u8 *s_data;
static char **s_str;
static int s_nstr;
static Node *s_nodes;
static int s_nnodes;
static Fsm *s_fsm;
static int s_nfsm;
static Objects *s_os;
static const Room *s_room;
static Hero *s_hero;
static u32 s_in_down, s_in_held, s_in_up;   // this frame's buttons (ListenFor... actions)
float g_fsm_fade;                           // ScreenFader: the black over the screen, 0..1
void fsm_input(u32 down, u32 held, u32 up) { s_in_down = down; s_in_held = held; s_in_up = up; }
static u16 s_ev_finished;
static Delayed s_delayed[32];
static int s_ndelayed;
static float s_dt;

// ------------------------------------------------------------------ PlayerData

typedef struct { char name[64]; int v; u8 kind; } PD;    // kind 0 bool, 1 int
static PD *s_pd;
static int s_npd, s_pdcap;

static PD *pd_find(const char *name, bool make, u8 kind) {
    for (int i = 0; i < s_npd; i++) if (!strcmp(s_pd[i].name, name)) return &s_pd[i];
    if (!make) return NULL;
    if (s_npd == s_pdcap) {
        s_pdcap = s_pdcap ? s_pdcap * 2 : 64;
        s_pd = realloc(s_pd, s_pdcap * sizeof(PD));
    }
    PD *p = &s_pd[s_npd++];
    snprintf(p->name, sizeof p->name, "%s", name);
    p->v = 0;
    p->kind = kind;
    return p;
}
void pd_clear(void) { s_npd = 0; }
void pd_clear_semi(void) {   // a bench rest: the semi-persistent kills come back
    for (int i = 0; i < s_npd; i++) if (!strncmp(s_pd[i].name, "semi:", 5)) s_pd[i].v = 0;
}
bool pd_bool(const char *name) {
    // PlayerData's computed properties
    if (!strcmp(name, "HasAnyMap")) return map_has_any();
    if (!strcmp(name, "CanUpdateMap")) return pd_bool("hasQuill") && map_has_any();
    PD *p = pd_find(name, false, 0); return p && p->v; }
void pd_set_bool(const char *name, bool v) { if (name[0]) pd_find(name, true, 0)->v = v; }
int pd_int(const char *name) { PD *p = pd_find(name, false, 1); return p ? p->v : 0; }
void pd_set_int(const char *name, int v) { if (name[0]) pd_find(name, true, 1)->v = v; }
void pd_save(FILE *f) {
    for (int i = 0; i < s_npd; i++) if (s_pd[i].v) fprintf(f, "pd %d %d %s\n", s_pd[i].kind, s_pd[i].v, s_pd[i].name);
}
void pd_load_line(const char *line) {
    int kind, v, n = 0;
    if (sscanf(line, "pd %d %d %n", &kind, &v, &n) >= 2 && n > 0) {
        char name[64];
        snprintf(name, sizeof name, "%s", line + n);
        char *e = name + strlen(name);
        while (e > name && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
        pd_find(name, true, (u8)kind)->v = v;
    }
}

// ------------------------------------------------------------------ loading

static void node_pos(int node, float *x, float *y);





// While a file is read: its strings (a pack's are its own, mapped into the room's table), and where its nodes,
// objects and FSMs go (0 for the room's own file; past the room's for a spawned pack)
static char **s_lstr;          // the file's strings (NULL: the room's, s_str)
static int s_nlstr;
static u16 *s_map;             // a pack's string -> the room's
static int s_nbase, s_obase;
static int s_node_cap, s_fsm_cap;   // nodes and FSMs allocated (the room's, plus room kept for spawns)
static const char *str_at_load(int i) {
    if (s_lstr) return i >= 0 && i < s_nlstr ? s_lstr[i] : "";
    return i >= 0 && i < s_nstr ? s_str[i] : "";
}
static u16 map_s(int i) { return s_map ? (i >= 0 && i < s_nlstr ? s_map[i] : 0) : (u16)i; }
static s16 map_node(int n) { return n >= 0 ? (s16)(n + s_nbase) : (s16)n; }

static const u8 *rd_val(const u8 *p, Val *v) {
    memset(v, 0, sizeof *v);
    v->t = *p++;
    switch (v->t) {
    case T_FLOAT: v->f[0] = rdf(p); v->i = (s32)v->f[0]; p += 4; break;
    case T_INT: v->i = (s32)rd32(p); v->f[0] = (float)v->i; p += 4; break;
    case T_BOOL: v->i = *p++; v->f[0] = (float)v->i; break;
    case T_STR: case T_EVENT: v->s = map_s(rd16(p)); p += 2; break;
    case T_VAR: v->s = rd16(p); p += 2; break;
    case T_NODE: v->node = map_node(rds16(p)); p += 2; break;
    case T_VEC: v->f[0] = rdf(p); v->f[1] = rdf(p + 4); v->f[2] = rdf(p + 8); p += 12; break;
    case T_LIST:
        v->n = *p++;
        v->list = calloc(v->n + 1, sizeof(Val));
        for (int i = 0; i < v->n; i++) p = rd_val(p, &v->list[i]);
        break;
    case T_TARGET: v->n = *p++; v->node = map_node(rds16(p)); v->s = map_s(rd16(p + 2)); p += 4; break;
    }
    return p;
}

static void free_val(Val *v) {
    if (v->t == T_LIST && v->list) {
        for (int i = 0; i < v->n; i++) free_val(&v->list[i]);
        free(v->list);
    }
}

static void free_fsm(Fsm *f) {
    for (int s = 0; s < f->nstates; s++) {
        State *st = &f->states[s];
        for (int a = 0; a < st->nact; a++) {
            for (int j = 0; j < st->act[a].nargs; j++) free_val(&st->act[a].a[j]);
            free(st->act[a].a);
        }
        free(st->act);
        free(st->tr);
    }
    free(f->states);
    for (int i = 0; i < f->nvars; i++) free_val(&f->vars[i]);
    free(f->vars);
    free(f->var_names);
    free(f->glob);
    free(f->rt);
    free(f->vars0);
    memset(f, 0, sizeof *f);
}

static void spawn_reset(void);

void fsm_free(void) {
    for (int k = 0; k < s_nfsm; k++) free_fsm(&s_fsm[k]);
    free(s_fsm); s_fsm = NULL; s_nfsm = 0;
    free(s_solids); s_solids = NULL; s_nsolids = 0;
    free(s_nodes); s_nodes = NULL; s_nnodes = 0;
    s_node_cap = s_fsm_cap = 0;
    if (s_str) for (int i = 0; i < s_nstr; i++) free(s_str[i]);
    free(s_str); s_str = NULL; s_nstr = 0;
    free(s_data); s_data = NULL;
    s_ndelayed = 0;
    s_os = NULL;
    spawn_reset();
}

static int str_find(const char *s) {
    for (int i = 0; i < s_nstr; i++) if (!strcmp(s_str[i], s)) return i;
    return -1;
}

static char s_scene_name[48];   // the room scene, for its shops
static void fsm_event(Fsm *f, int ev);

void fsm_broadcast(const char *ev) {
    int e = str_find(ev);
    if (e >= 0)
        for (int k = 0; k < s_nfsm; k++) fsm_event(&s_fsm[k], e);
}

// room for what the scripts spawn, kept past the room's own so nothing moves while a script runs
#define SPAWN_NODES 1536
#define SPAWN_FSMS  256

// Reads a .ssf: the room's own (pack false), or a spawned pack's, into the nodes and FSMs from nbase / fbase, its
// objects from obase, moved by (dx, dy). Returns false if it doesn't fit.
static bool load_ssf(const u8 *data, size_t size, const char *scene, Objects *os, bool pack, int nbase, int obase,
                     int fbase, float dx, float dy, int *nn_out, int *nf_out) {
    if (!data || size < 8 || memcmp(data, "SSFM", 4)) return false;
    int ver = rd16(data + 4);
    const u8 *p = data + 6;
    // strings
    int nstr = rd16(p); p += 2;
    char **str = calloc(nstr + 1, sizeof(char *));
    for (int i = 0; i < nstr; i++) {
        int n = *p++;
        str[i] = malloc(n + 1);
        memcpy(str[i], p, n);
        str[i][n] = 0;
        p += n;
    }
    if (!pack) {
        s_str = str; s_nstr = nstr;
        s_lstr = NULL; s_map = NULL;
    } else {   // into the room's table: the ones it has, the others added
        s_lstr = str; s_nlstr = nstr;
        s_map = malloc(sizeof(u16) * (nstr + 1));
        for (int i = 0; i < nstr; i++) {
            int k = str_find(str[i]);
            if (k < 0) {
                char **ns = realloc(s_str, sizeof(char *) * (s_nstr + 1));
                if (!ns) { s_map[i] = 0; continue; }
                s_str = ns;
                s_str[s_nstr] = strdup(str[i]);
                k = s_nstr++;
            }
            s_map[i] = (u16)k;
        }
    }
    s_nbase = nbase;
    s_obase = obase;
    bool ok = true;
    // nodes
    int nn = rd16(p); p += 2;
    if (!pack) {
        s_node_cap = nn + SPAWN_NODES;
        s_nodes = calloc(s_node_cap + 1, sizeof(Node));
        s_nnodes = nn;
    } else if (nbase + nn > s_node_cap) { ok = false; goto done; }
    int nsize = ver >= 4 ? 42 : 18;
    for (int i = 0; i < nn; i++, p += nsize) {
        Node *n = &s_nodes[nbase + i];
        memset(n, 0, sizeof *n);
        n->name = map_s(rd16(p)); n->parent = map_node(rds16(p + 2)); n->obj = rds16(p + 4);
        n->x = rdf(p + 6) + dx; n->y = rdf(p + 10) + dy; n->active = p[14];
        n->body = -1;
        n->sx = 1;
        if (ver >= 4) {
            n->body = (s8)p[15];
            float rx = rdf(p + 18) + dx, ry = rdf(p + 22) + dy;
            n->rw = rdf(p + 26); n->rh = rdf(p + 30);
            n->rx = rx - n->x; n->ry = ry - n->y;
            n->grav = rdf(p + 34);
            n->sx = rdf(p + 38);
        }
        if (n->obj >= 0) n->obj += obase;
        if (n->obj >= os->n) n->obj = -1;
        if (n->obj >= 0) {
            os->o[n->obj].node = nbase + i;
            // an inactive object (flag) is the inactive node itself or one of its parents: the nodes say which
            os->o[n->obj].alive = true;
        }
    }
    if (pack && nbase + nn > s_nnodes) s_nnodes = nbase + nn;
    // FSMs
    int nf = rd16(p); p += 2;
    if (!pack) {
        s_fsm_cap = nf + SPAWN_FSMS;
        s_fsm = calloc(s_fsm_cap + 1, sizeof(Fsm));
        s_nfsm = nf;
    } else if (fbase + nf > s_fsm_cap) { ok = false; goto done; }
    for (int k = 0; k < nf; k++) {
        Fsm *f = &s_fsm[fbase + k];
        if (pack) free_fsm(f);
        f->owner = map_node(rds16(p)); f->name = map_s(rd16(p + 2)); f->start = rd16(p + 4); p += 6;
        f->host = -1;
        f->sub = -1;
        if (ver >= 9) { int h = rds16(p); p += 2; f->host = h >= 0 ? fbase + h : -1; }
        f->nvars = rd16(p); p += 2;
        f->vars = calloc(f->nvars + 1, sizeof(Val));
        f->var_names = calloc(f->nvars + 1, sizeof(u16));
        for (int i = 0; i < f->nvars; i++) {
            f->var_names[i] = map_s(rd16(p)); p += 2;
            p = rd_val(p, &f->vars[i]);
            if (f->vars[i].t == T_NODE && f->vars[i].node == N_OWNER) f->vars[i].node = f->owner;
        }
        f->nglob = rd16(p); p += 2;
        f->glob = malloc(4 * f->nglob + 4);
        for (int i = 0; i < f->nglob * 2; i++, p += 2) f->glob[i] = (i & 1) ? rd16(p) : map_s(rd16(p));
        f->nstates = rd16(p); p += 2;
        f->states = calloc(f->nstates + 1, sizeof(State));
        for (int s = 0; s < f->nstates; s++) {
            State *st = &f->states[s];
            st->name = map_s(rd16(p)); p += 2;
            st->ntr = rd16(p); p += 2;
            st->tr = malloc(4 * st->ntr + 4);
            for (int i = 0; i < st->ntr * 2; i++, p += 2) st->tr[i] = (i & 1) ? rd16(p) : map_s(rd16(p));
            st->nact = rd16(p); p += 2;
            st->act = calloc(st->nact + 1, sizeof(Action));
            if (st->nact > f->maxact) f->maxact = st->nact;
            for (int a = 0; a < st->nact; a++) {
                Action *ac = &st->act[a];
                ac->op = p[0]; ac->every = p[1]; ac->nargs = p[2]; p += 3;
                ac->a = calloc(ac->nargs + 1, sizeof(Val));
                for (int j = 0; j < ac->nargs; j++) p = rd_val(p, &ac->a[j]);
                // RunFSM: its template FSM's index, in this file's FSMs
                if (ac->op == OP_RunFSM && ac->nargs && ac->a[0].t == T_INT && ac->a[0].i >= 0) ac->a[0].i += fbase;
            }
        }
        f->rt = calloc(f->maxact + 1, sizeof(ActRt));
        if (f->host >= 0) {
            f->vars0 = malloc(sizeof(Val) * (f->nvars + 1));
            if (f->vars0) memcpy(f->vars0, f->vars, sizeof(Val) * f->nvars);
        }
        f->cur = -1;
        f->next = -1;
        // the "Hero" variables (set in the game by GetHero / FindGameObject at start)
        for (int i = 0; i < f->nvars; i++)
            if (!strcmp(s_str[f->var_names[i]], "Hero")) { f->vars[i].t = T_NODE; f->vars[i].node = N_HERO; }
    }
    if (pack && fbase + nf > s_nfsm) s_nfsm = fbase + nf;
    // the walls they control (version 2): the room's only
    if (ver >= 2 && p + 2 <= data + size) {
        int n = rd16(p); p += 2;
        if (!pack) {
            s_nsolids = n;
            s_solids = calloc(s_nsolids + 1, sizeof(Solid));
        }
        for (int i = 0; i < n; i++, p += 18) {
            if (pack) continue;
            Solid *so = &s_solids[i];
            so->node = rds16(p);
            float nx = 0, ny = 0;
            if (so->node >= 0 && so->node < s_nnodes) node_pos(so->node, &nx, &ny);
            so->x0 = rdf(p + 2) - nx; so->y0 = rdf(p + 6) - ny; so->x1 = rdf(p + 10) - nx; so->y1 = rdf(p + 14) - ny;
        }
    }
    // what a hit and a kill do, per enemy (version 3)
    if (ver >= 3 && p + 2 <= data + size) {
        int n = rd16(p); p += 2;
        for (int i = 0; i < n && p + 22 <= data + size; i++) {
            int oi = rds16(p);
            if (oi >= 0) oi += obase;
            int hit = rd16(p + 2), death = rd16(p + 4), corpse = rd16(p + 6);
            float fling = rdf(p + 8);
            u8 fl = p[12];
            float rv = rdf(p + 13), rd = rdf(p + 17);
            int ns = p[21];
            p += 22;
            Obj *o = oi >= 0 && oi < os->n ? &os->o[oi] : NULL;
            if (o) {
                o->hitp = fx_profile(str_at_load(hit));
                o->deathp = fx_profile(str_at_load(death));
                o->fling = fling;
                o->eflags = fl;
                if (fl & 16) { o->recoil_v = rv; o->recoil_dur = rd > 0 ? rd : 0.15f; }
                if (corpse && str_at_load(corpse)[0]) {
                    char path[80];
                    snprintf(path, sizeof path, "sprites/%s.sss", str_at_load(corpse));
                    o->corpse_set = sprites_get(path);
                }
                o->ndsnd = 0;
            }
            for (int k = 0; k < ns; k++, p += 2)
                if (o && o->ndsnd < 4) o->dsnd[o->ndsnd++] = s_str[map_s(rd16(p))];
            if (ver >= 5) {
                int w = *p++;
                if (w) {
                    if (o) {
                        o->has_walker = true;
                        o->w_speed[0] = rdf(p); o->w_speed[1] = rdf(p + 4);
                        o->w_pauses = rdf(p + 8) != 0;
                        o->w_pause[0] = rdf(p + 12); o->w_pause[1] = rdf(p + 16);
                        o->w_wait[0] = rdf(p + 20); o->w_wait[1] = rdf(p + 24);
                        o->w_ignore_holes = p[28];
                        o->walking = !p[29];
                        o->w_edge = rdf(p + 34);
                    }
                    p += 38;
                }
            }
            if (ver >= 6) {
                if (*p++) {        // Crawler
                    if (o) {
                        o->crawler = true;
                        o->crawl_speed = rdf(p);
                        o->crawl_sfr = p[4];
                        o->crawl_anim = s_str[map_s(rd16(p + 5))];
                        o->turn_anim = "Turn";
                        o->idle_anim = "Idle";
                        o->walking = !p[7];
                    }
                    p += 8;
                }
                if (*p++) {        // WalkerV2
                    if (o) {
                        o->crawler = true;
                        o->crawl_speed = fabsf(rdf(p));
                        o->crawl_sfr = false;
                        o->crawl_anim = s_str[map_s(rd16(p + 4))];
                        o->turn_anim = s_str[map_s(rd16(p + 6))];
                        o->idle_anim = s_str[map_s(rd16(p + 8))];
                        o->walking = p[18];
                    }
                    p += 19;
                }
            }
            if (ver >= 7) {   // what a kill flings out: rosaries (small, medium, large, smooth), shards
                if (o) {
                    for (int k = 0; k < 5; k++) o->geo[k] = rd16(p + k * 2);
                    o->mega_fling = p[10];
                }
                p += 11;
            }
        }
    }
    // remembered kills, what PlayerData switches off (version 6), objects switched by a PlayerData test (version 7)
    if (ver >= 6 && p + 2 <= data + size) {
        int n = rd16(p); p += 2;
        for (int i = 0; i < n && p + 10 <= data + size; i++) {
            const u8 *q = p;
            p += 10;
            int oi = rds16(q);
            if (oi >= 0) oi += obase;
            Obj *o = oi >= 0 && oi < os->n ? &os->o[oi] : NULL;
            if (o && !pack && q[2] && (o->flags & OF_ENEMY)) {
                snprintf(o->persist, sizeof o->persist, "%s%s:%s:%d", q[3] ? "semi:" : "", scene, str_at_load(rd16(q + 4)), oi);
                if (pd_bool(o->persist)) o->alive = false;      // killed before: not there
            }
            if (o && q[6]) {
                bool v = pd_bool(str_at_load(rd16(q + 8)));
                if (v == (q[7] != 0) && o->node >= 0) s_nodes[o->node].active = 0;
            }
            if (ver >= 7 && *p++) {   // TestGameObjectActivator: any group whose tests all pass
                int an = map_node(rds16(p)), dn = map_node(rds16(p + 2)), ng = p[4];
                p += 5;
                bool pass = ng == 0;
                for (int g = 0; g < ng; g++) {
                    int nt = *p++;
                    bool all = true;
                    for (int t = 0; t < nt; t++, p += 13) {
                        int type = p[0], bv = p[3], nty = p[4];
                        const char *field = str_at_load(rd16(p + 1));
                        s32 iv = (s32)rd32(p + 5);
                        float fv = rdf(p + 9);
                        bool okt;
                        if (type == 0) okt = pd_bool(field) == (bv != 0);
                        else if (type == 4) okt = false;
                        else {
                            float x = (float)pd_int(field), y = type == 2 ? fv : (float)iv;
                            okt = nty == 0 ? x == y : nty == 1 ? x != y : nty == 2 ? x < y : x > y;
                        }
                        if (!okt) all = false;
                    }
                    if (all) pass = true;
                }
                if (an >= 0 && an < s_nnodes) s_nodes[an].active = pass;
                if (dn >= 0 && dn < s_nnodes) s_nodes[dn].active = !pass;
            }
        }
    }
    // the room's particle emitters (their index in its .spt) and their nodes (version 8)
    if (ver >= 8 && p + 2 <= data + size) {
        int n = rd16(p); p += 2;
        for (int i = 0; i < n && p + 4 <= data + size; i++, p += 4)
            if (!pack) particles_bind(rd16(p), map_node(rds16(p + 2)));
    }
done:
    if (pack) {
        for (int i = 0; i < nstr; i++) free(str[i]);
        free(str);
        free(s_map);
    }
    s_lstr = NULL; s_map = NULL; s_nlstr = 0; s_nbase = s_obase = 0;
    int fin = str_find("FINISHED");
    s_ev_finished = fin >= 0 ? fin : 0xFFFF;
    if (nn_out) *nn_out = nn;
    if (nf_out) *nf_out = nf;
    return ok;
}

void fsm_load(const char *scene, Objects *os) {
    fsm_free();
    snprintf(s_scene_name, sizeof s_scene_name, "%s", scene);
    s_os = os;
    for (int i = 0; i < os->n; i++) os->o[i].node = -1;
    static int nofsm = -1;   // testing: SD:/3ds/3dsilksong/nofsm turns the FSMs off
    if (nofsm < 0) { struct stat st; nofsm = stat("sdmc:/3ds/3dsilksong/nofsm", &st) == 0; }
    if (nofsm) return;
    size_t size;
    s_data = file_load(data_path("rooms/%s.ssf", scene), &size);
    if (!load_ssf(s_data, size, scene, os, false, 0, 0, 0, 0, 0, NULL, NULL)) {
        free(s_data); s_data = NULL;
        // no FSMs: an empty table still takes what the scripts would spawn... none without scripts
    }
}

// ------------------------------------------------------------------ spawned prefabs (the game's global pool)
// A pack (rooms/pf_<name>.sso / .ssf, fx/pf_<name>.spt, tools/build_prefabs.py) is put in the room where a script
// spawns it: its objects past the room's, its nodes and FSMs in the space kept for them. An instance that is done
// (its root switched off: RecycleSelf, a script, the game's own recycling) gives its space to the next spawn of the
// same prefab, as the game's pool does.

typedef struct {
    char name[48];
    u8 *ssf, *sso;
    size_t ssf_size, sso_size;
    int nn, nf, no;          // its nodes, FSMs, objects (-1: not read yet)
    float rx, ry;            // its root's position in the pack
    bool missing;
} Pack;

typedef struct {
    s16 pack;                // -1: free
    int nbase, fbase, obase;
    bool live;
    float t;
} Inst;

#define MAX_PACKS 48
#define MAX_INST 192
static Pack s_packs[MAX_PACKS];
static int s_npacks;
static Inst s_inst[MAX_INST];
static int s_ninst;
static int s_node_top, s_fsm_top, s_obj_top;   // the first free node, FSM, object past what is placed

static void spawn_reset(void) {
    for (int i = 0; i < s_npacks; i++) { free(s_packs[i].ssf); free(s_packs[i].sso); }
    memset(s_packs, 0, sizeof s_packs);
    s_npacks = 0;
    memset(s_inst, 0, sizeof s_inst);
    s_ninst = 0;
    s_node_top = s_fsm_top = s_obj_top = -1;
}

static int pack_get(const char *name) {
    for (int i = 0; i < s_npacks; i++) if (!strcmp(s_packs[i].name, name)) return s_packs[i].missing ? -1 : i;
    if (s_npacks == MAX_PACKS) return -1;
    Pack *pk = &s_packs[s_npacks];
    memset(pk, 0, sizeof *pk);
    snprintf(pk->name, sizeof pk->name, "%s", name);
    pk->ssf = file_load(data_path("rooms/%s.ssf", name), &pk->ssf_size);
    pk->sso = file_load(data_path("rooms/%s.sso", name), &pk->sso_size);
    pk->no = pk->sso && pk->sso_size >= 8 ? rd16(pk->sso + 6) : 0;
    pk->nn = pk->nf = 0;
    if (pk->ssf && pk->ssf_size > 8) {   // its node and FSM counts, and its root: read the header
        const u8 *p = pk->ssf + 8;
        int nstr = rd16(pk->ssf + 6);
        for (int i = 0; i < nstr; i++) p += 1 + *p;
        pk->nn = rd16(p);
        int nsize = rd16(pk->ssf + 4) >= 4 ? 42 : 18;
        for (int i = 0; i < pk->nn; i++)
            if (rds16(p + 2 + i * nsize + 2) < 0) { pk->rx = rdf(p + 2 + i * nsize + 6); pk->ry = rdf(p + 2 + i * nsize + 10); break; }
        p += 2 + pk->nn * nsize;
        pk->nf = rd16(p);
    }
    pk->missing = !pk->ssf && !pk->sso;
    s_npacks++;
    return pk->missing ? -1 : s_npacks - 1;
}

static bool inst_done(const Inst *in) {
    if (!in->live) return true;
    if (in->nbase < s_nnodes && !s_nodes[in->nbase].active) return true;   // recycled (its root off)
    return false;
}

// a pack spawned at (x, y): its root node, N_NONE if it can't be
static int spawn_pack(const char *name, float x, float y) {
    if (!s_os || !name || !name[0]) return N_NONE;
    if (s_node_top < 0) { s_node_top = s_nnodes; s_fsm_top = s_nfsm; s_obj_top = s_os->n; }
    int pi = pack_get(name);
    if (pi < 0) return N_NONE;
    Pack *pk = &s_packs[pi];
    // an instance of the same prefab that is done, else new space
    Inst *in = NULL;
    for (int i = 0; i < s_ninst && !in; i++)
        if (s_inst[i].pack == pi && inst_done(&s_inst[i])) in = &s_inst[i];
    if (!in) {
        if (s_ninst == MAX_INST || s_node_top + pk->nn > s_node_cap || s_fsm_top + pk->nf > s_fsm_cap ||
            !objects_room(s_os, s_obj_top + pk->no))
            return N_NONE;
        in = &s_inst[s_ninst++];
        in->pack = pi;
        in->nbase = s_node_top; in->fbase = s_fsm_top; in->obase = s_obj_top;
        s_node_top += pk->nn; s_fsm_top += pk->nf; s_obj_top += pk->no;
    }
    float dx = x - pk->rx, dy = y - pk->ry;
    if (pk->no) objects_put(s_os, pk->sso, pk->sso_size, in->obase, dx, dy);
    for (int i = 0; i < pk->no; i++) s_os->o[in->obase + i].node = -1;
    // its FSMs from scratch (a reused instance's are freed and read again)
    for (int k = 0; k < pk->nf; k++) {
        s_fsm[in->fbase + k].stopped = true;
    }
    if (pk->ssf) load_ssf(pk->ssf, pk->ssf_size, pk->name, s_os, true, in->nbase, in->obase, in->fbase, dx, dy, NULL, NULL);
    in->live = true;
    in->t = 0;
    particles_fx(pk->name, x, y, 0, 0);
    objects_sorted(s_os);
    return pk->nn ? in->nbase : N_NONE;
}

// the instance a node belongs to, -1 if it is the room's own
static int inst_of(int node) {
    for (int i = 0; i < s_ninst; i++)
        if (node >= s_inst[i].nbase && node < s_inst[i].nbase + s_packs[s_inst[i].pack].nn) return i;
    return -1;
}

// an instance put back in the pool: its root off, its FSMs stopped, its objects gone
static void recycle(int node) {
    int i = inst_of(node);
    if (i < 0) { if (node >= 0 && node < s_nnodes) s_nodes[node].active = 0; return; }
    Inst *in = &s_inst[i];
    Pack *pk = &s_packs[in->pack];
    s_nodes[in->nbase].active = 0;
    for (int k = 0; k < pk->nf; k++) s_fsm[in->fbase + k].stopped = true;
    for (int k = 0; k < pk->no; k++) s_os->o[in->obase + k].alive = false;
    in->live = false;
}

// instances that ran out: far from the room, or switched off by a script
static void spawn_update(float dt) {
    for (int i = 0; i < s_ninst; i++) {
        Inst *in = &s_inst[i];
        if (!in->live) continue;
        in->t += dt;
        if (inst_done(in)) { recycle(in->nbase); continue; }
        float x, y;
        node_pos(in->nbase, &x, &y);
        if (s_room && (x < s_room->bx0 - 40 || x > s_room->bx1 + 40 || y < s_room->by0 - 40 || y > s_room->by1 + 40))
            recycle(in->nbase);
        else if (s_packs[in->pack].nf == 0 && in->t > 4) recycle(in->nbase);   // a plain effect: gone in a while
    }
}

int fsm_count(void) { return s_nfsm; }

int fsm_node_fsms(int node) {
    int n = 0;
    for (int k = 0; k < s_nfsm; k++) if (s_fsm[k].owner == node) n++;
    return n;
}

void fsm_node_stop(int node) {
    for (int k = 0; k < s_nfsm; k++) {
        int o = s_fsm[k].owner;
        for (int g = 0; o >= 0 && g < 64; o = s_nodes[o].parent, g++)
            if (o == node) { s_fsm[k].stopped = true; break; }
    }
    if (node >= 0 && node < s_nnodes) s_nodes[node].vx = s_nodes[node].vy = 0;
}

void fsm_node_velocity(int node, float *vx, float *vy, bool set) {
    if (node < 0 || node >= s_nnodes) { if (!set) *vx = *vy = 0; return; }
    if (set) { s_nodes[node].vx = *vx; s_nodes[node].vy = *vy; }
    else { *vx = s_nodes[node].vx; *vy = s_nodes[node].vy; }
}

// ------------------------------------------------------------------ nodes

bool fsm_node_active(int node) {
    for (int g = 0; node >= 0 && node < s_nnodes && g < 64; node = s_nodes[node].parent, g++)
        if (!s_nodes[node].active) return false;
    return true;
}

static Obj *node_obj(int node) {
    if (node < 0 || node >= s_nnodes || s_nodes[node].obj < 0 || !s_os) return NULL;
    return &s_os->o[s_nodes[node].obj];
}

static void node_pos(int node, float *x, float *y) {
    if (node == N_HERO && s_hero) { *x = s_hero->x; *y = s_hero->y; return; }
    if (node < 0 || node >= s_nnodes) { *x = *y = 0; return; }
    Obj *o = node_obj(node);
    if (o) { *x = o->x; *y = o->y; return; }
    *x = s_nodes[node].x; *y = s_nodes[node].y;
}

static void node_set_pos(int node, float x, float y) {
    if (node == N_HERO && s_hero) { s_hero->x = x; s_hero->y = y; return; }
    if (node < 0 || node >= s_nnodes) return;
    Obj *o = node_obj(node);
    if (o) obj_move(s_os, s_nodes[node].obj, x - o->x, y - o->y);
    // the node and the nodes under it move together (their own positions)
    float dx = x - s_nodes[node].x, dy = y - s_nodes[node].y;
    s_nodes[node].x = x; s_nodes[node].y = y;
    for (int i = node + 1; i < s_nnodes; i++) {
        int p = s_nodes[i].parent;
        while (p >= 0 && p != node) p = s_nodes[p].parent;
        if (p == node && s_nodes[i].obj < 0) { s_nodes[i].x += dx; s_nodes[i].y += dy; }
    }
}

// ------------------------------------------------------------------ values

static Val *var_of(Fsm *f, const Val *v) {
    if (v->t != T_VAR || v->s >= f->nvars) return NULL;
    return &f->vars[v->s];
}
static const Val *deref(Fsm *f, const Val *v) {
    const Val *r = var_of(f, v);
    return r ? r : v;
}
static float a_f(Fsm *f, const Val *v) {
    v = deref(f, v);
    if (v->t == T_INT || v->t == T_BOOL) return (float)v->i;
    return v->f[0];
}
static int a_i(Fsm *f, const Val *v) {
    v = deref(f, v);
    if (v->t == T_FLOAT) return (int)v->f[0];
    return v->i;
}
static bool a_none(Fsm *f, const Val *v) { (void)f; return v->t == T_NONE || (v->t == T_VAR && v->s >= f->nvars); }
static int a_node(Fsm *f, const Val *v) {
    v = deref(f, v);
    if (v->t != T_NODE) return N_NONE;
    return v->node == N_OWNER ? f->owner : v->node;
}
static int a_str(Fsm *f, const Val *v) {
    v = deref(f, v);
    return (v->t == T_STR || v->t == T_EVENT) ? v->s : -1;
}
static const char *str_at(int i) { return i >= 0 && i < s_nstr ? s_str[i] : ""; }
static void set_f(Fsm *f, const Val *dst, float x) {
    Val *v = var_of(f, dst);
    if (!v) return;
    if (v->t == T_INT || v->t == T_BOOL) v->i = (int)x;
    else { v->t = T_FLOAT; v->f[0] = x; }
}
static void set_i(Fsm *f, const Val *dst, int x) {
    Val *v = var_of(f, dst);
    if (!v) return;
    if (v->t == T_FLOAT) v->f[0] = (float)x;
    else v->i = x;
}
static void set_node(Fsm *f, const Val *dst, int node) {
    Val *v = var_of(f, dst);
    if (v) { v->t = T_NODE; v->node = node; }
}
static float frand(float a, float b) { return a + (b - a) * (rand() / (float)RAND_MAX); }

// ------------------------------------------------------------------ events

static void fsm_event(Fsm *f, int ev) {
    if (ev < 0 || ev == 0xFFFF || f->stopped || !f->started) return;
    if (f->cur >= 0) {
        State *st = &f->states[f->cur];
        for (int i = 0; i < st->ntr; i++)
            if (st->tr[i * 2] == ev && st->tr[i * 2 + 1] < f->nstates) { f->next = st->tr[i * 2 + 1]; return; }
    }
    for (int i = 0; i < f->nglob; i++)
        if (f->glob[i * 2] == ev && f->glob[i * 2 + 1] < f->nstates) { f->next = f->glob[i * 2 + 1]; return; }
}

static void node_event(int node, int fsm_name, int ev) {
    for (int k = 0; k < s_nfsm; k++)
        if (s_fsm[k].owner == node && (fsm_name <= 0 || s_fsm[k].name == fsm_name)) fsm_event(&s_fsm[k], ev);
}

// sends through an FsmEventTarget (0 self, 1 the object's FSMs, 2 one FSM of an object, 4 every FSM)
static void send_target(Fsm *f, const Val *target, int ev, float delay) {
    int kind = target->t == T_TARGET ? target->n : 0;
    int node = target->t == T_TARGET ? (target->node == N_OWNER ? f->owner : target->node) : f->owner;
    if (target->t == T_TARGET && target->node <= N_VAR0) {   // the object in one of the FSM's variables
        int vi = N_VAR0 - target->node;
        const Val *v = vi < f->nvars ? &f->vars[vi] : NULL;
        node = v && v->t == T_NODE ? (v->node == N_OWNER ? f->owner : v->node) : N_NONE;
        if (node == N_SHOP) { if (kind == 1 && ev >= 0) shop_event(str_at(ev), f->owner); return; }
    }
    int fname = target->t == T_TARGET && kind == 2 ? target->s : 0;
    if (kind == 5 || kind == 6) {   // HostFSM: the FSM that runs this template's; SubFSMs: the one this runs
        int k = kind == 5 ? f->host : f->sub;
        if (k < 0 || k >= s_nfsm) return;
        if (delay > 0 && s_ndelayed < 32) {
            Delayed *d = &s_delayed[s_ndelayed++];
            d->fsm = k; d->node = N_NONE; d->fsm_name = 0; d->ev = ev; d->t = delay;
        } else fsm_event(&s_fsm[k], ev);
        return;
    }
    if (delay > 0 && s_ndelayed < 32) {
        Delayed *d = &s_delayed[s_ndelayed++];
        d->fsm = kind == 0 ? (int)(f - s_fsm) : -1;
        d->node = node; d->fsm_name = fname; d->ev = ev; d->t = delay;
        if (kind == 4) { d->fsm = -2; }
        return;
    }
    if (kind == 0) fsm_event(f, ev);
    else if (kind == 4) for (int k = 0; k < s_nfsm; k++) fsm_event(&s_fsm[k], ev);
    else if (node >= 0) node_event(node, fname, ev);
}

static int ev_of(Fsm *f, const Val *v) {
    v = deref(f, v);
    if (v->t == T_EVENT || v->t == T_STR) {
        if (v->t == T_STR) return v->s;
        return v->s;
    }
    return -1;
}

static void send_self(Fsm *f, const Val *v) { int e = ev_of(f, v); if (e >= 0) fsm_event(f, e); }

// ------------------------------------------------------------------ actions

static void settle(Fsm *f);
static void settle_later(Fsm *f) { (void)f; }   // fsm_update settles every FSM with a transition waiting

static bool hero_in(int node) {
    Obj *o = node_obj(node);
    if (!o || !s_hero || o->rw <= 0 || o->nocoll) return false;
    return rects_overlap(s_hero->x - HERO_HW, s_hero->y + HERO_FOOT, 2 * HERO_HW, HERO_HEAD - HERO_FOOT,
                         o->rx, o->ry, o->rw, o->rh);
}

static Fsm *fsm_on(int node, int name) {
    for (int k = 0; k < s_nfsm; k++) if (s_fsm[k].owner == node && s_fsm[k].name == name) return &s_fsm[k];
    return NULL;
}
static Val *fsm_var(Fsm *g, int name) {
    if (!g) return NULL;
    for (int i = 0; i < g->nvars; i++) if (g->var_names[i] == name) return &g->vars[i];
    return NULL;
}

static int find_child(int parent, int name) {
    if (parent < 0) return N_NONE;
    for (int i = 0; i < s_nnodes; i++) if (s_nodes[i].parent == parent && s_nodes[i].name == name) return i;
    for (int i = 0; i < s_nnodes; i++) {        // deeper
        if (s_nodes[i].name != name) continue;
        for (int p = s_nodes[i].parent; p >= 0; p = s_nodes[p].parent) if (p == parent) return i;
    }
    return N_NONE;
}

static void play_clip(int node, const Val *clip, Fsm *f) {
    if (node == N_HERO && s_hero) { anim_play_name(&s_hero->body, str_at(a_str(f, clip)), true); return; }
    Obj *o = node_obj(node);
    if (!o || !o->anim.set) return;
    anim_play_name(&o->anim, str_at(a_str(f, clip)), true);
}

// a sound at a node: quieter with the distance from the view's centre, panned to its side
static void play_sfx(Fsm *f, const Val *clip, float vol, int node) {
    int s = a_str(f, clip);
    if (s < 0) return;
    float x, y;
    if (node == N_NONE) node = f->owner;
    node_pos(node, &x, &y);
    float dx = x - g_view.cx, dy = y - g_view.cy, d = hypotf(dx, dy);
    float k = d < 12 ? 1 : d > 30 ? 0 : 1 - (d - 12) / 18;
    sfx_play(str_at(s), vol * k, clampf(dx / 14, -0.8f, 0.8f));
}

static const Val *pick(Fsm *f, const Val *list, const Val *weights) {
    if (list->t != T_LIST || !list->n) return list;
    float tot = 0;
    for (int i = 0; i < list->n; i++) tot += (weights->t == T_LIST && i < weights->n) ? a_f(f, &weights->list[i]) : 1;
    float r = frand(0, tot);
    for (int i = 0; i < list->n; i++) {
        r -= (weights->t == T_LIST && i < weights->n) ? a_f(f, &weights->list[i]) : 1;
        if (r <= 0) return &list->list[i];
    }
    return &list->list[list->n - 1];
}

static void convo_end(void *ctx) {
    static int ev = -2;
    if (ev == -2 || ev >= s_nstr) ev = str_find("CONVO_END");
    Fsm *f = ctx;
    if (f >= s_fsm && f < s_fsm + s_nfsm) { fsm_event(f, ev); settle_later(f); }
}

static void line_end(void *ctx) {   // DialogueBox: a line over (the conversation goes on)
    static int ev = -2;
    if (ev == -2 || ev >= s_nstr) ev = str_find("LINE_END");
    Fsm *f = ctx;
    if (ev >= 0 && f >= s_fsm && f < s_fsm + s_nfsm) fsm_event(f, ev);
}

// ------------------------------------------------------------------ enemies: physics and their actions

// a node's box in the world: its object's collider, else its own colliders, else nothing (false)
static bool node_box(int node, float *x0, float *y0, float *x1, float *y1) {
    if (node < 0 || node >= s_nnodes) return false;
    Obj *o = node_obj(node);
    if (o && o->rw > 0) { *x0 = o->rx; *y0 = o->ry; *x1 = o->rx + o->rw; *y1 = o->ry + o->rh; return true; }
    Node *n = &s_nodes[node];
    if (n->rw <= 0) return false;
    float x, y;
    node_pos(node, &x, &y);
    *x0 = x + n->rx; *y0 = y + n->ry; *x1 = *x0 + n->rw; *y1 = *y0 + n->rh;
    return true;
}

static float node_scale(int node) {
    Obj *o = node_obj(node);
    if (o) return o->scx;
    return node >= 0 && node < s_nnodes ? s_nodes[node].sx : 1;
}
static void node_set_scale(int node, float sx) {
    Obj *o = node_obj(node);
    if (o) o->scx = sx;
    if (node >= 0 && node < s_nnodes) s_nodes[node].sx = sx;
}

static bool solid(float x0, float y0, float x1, float y1) {
    return s_room && room_box_solid(s_room, x0, y0, x1, y1);
}

// moves a body by (dx, dy) against the room, in small steps; notes the sides it hits
static void body_move(int node, float dx, float dy) {
    Node *n = &s_nodes[node];
    float x0, y0, x1, y1;
    if (!node_box(node, &x0, &y0, &x1, &y1)) {
        float x, y;
        node_pos(node, &x, &y);
        node_set_pos(node, x + dx, y + dy);
        return;
    }
    for (int axis = 0; axis < 2; axis++) {
        float d = axis ? dy : dx;
        while (fabsf(d) > 1e-4f) {
            float st = clampf(d, -0.2f, 0.2f);
            d -= st;
            node_box(node, &x0, &y0, &x1, &y1);
            bool hit = axis ? solid(x0 + 0.02f, y0 + st, x1 - 0.02f, y1 + st) : solid(x0 + st, y0 + 0.02f, x1 + st, y1 - 0.02f);
            if (hit) {
                if (axis) { n->coll |= st < 0 ? 4 : 1; n->vy = 0; }       // 1 top, 2 right, 4 bottom, 8 left
                else { n->coll |= st > 0 ? 2 : 8; n->vx = 0; }
                break;
            }
            float x, y;
            node_pos(node, &x, &y);
            node_set_pos(node, x + (axis ? 0 : st), y + (axis ? st : 0));
        }
    }
}

static void physics(float dt) {
    for (int i = 0; i < s_nnodes; i++) {
        Node *n = &s_nodes[i];
        n->coll_prev = n->coll;
        n->coll = 0;
        Obj *o = node_obj(i);
        if (o && (o->dead || o->recoil_t > 0)) continue;           // enemy.c moves those
        bool body = n->body >= 0;
        if (!body && n->vx == 0 && n->vy == 0) continue;
        if (n->body == 2) continue;                                // static
        if (!fsm_node_active(i)) continue;
        bool dynamic = n->body == 0 && !n->kin;
        if (dynamic && n->grav != 0) n->vy = fmaxf(n->vy - 60.0f * n->grav * dt, -40.0f);
        if (n->vx == 0 && n->vy == 0) {
            // resting on the floor still counts as touching it
            float x0, y0, x1, y1;
            if (dynamic && node_box(i, &x0, &y0, &x1, &y1) && solid(x0 + 0.02f, y0 - 0.05f, x1 - 0.02f, y0)) n->coll |= 4;
            continue;
        }
        if (dynamic) body_move(i, n->vx * dt, n->vy * dt);
        else {
            float x, y;
            node_pos(i, &x, &y);
            node_set_pos(i, x + n->vx * dt, y + n->vy * dt);
        }
    }
}

static float facing_sign(int node, bool sprite_faces_right) {
    float s = node_scale(node);
    return (s >= 0) == sprite_faces_right ? 1.0f : -1.0f;      // +1: looking right
}

static void face_to(int node, bool right, bool sprite_faces_right) {
    float s = fabsf(node_scale(node));
    if (s == 0) s = 1;
    node_set_scale(node, (right == sprite_faces_right) ? s : -s);
}

// a ray against the room from (x, y) along (dx, dy) up to len: the distance to the first solid, or -1
static float ray(float x, float y, float dx, float dy, float len) {
    float l = hypotf(dx, dy);
    if (l < 1e-5f || len <= 0) return -1;
    dx /= l; dy /= l;
    for (float d = 0; d <= len; d += 0.1f) {
        float px = x + dx * d, py = y + dy * d;
        if (solid(px - 0.02f, py - 0.02f, px + 0.02f, py + 0.02f)) return d;
    }
    return -1;
}

static bool alert_in(int node) {
    float x0, y0, x1, y1;
    if (!s_hero || !node_box(node, &x0, &y0, &x1, &y1)) return false;
    return s_hero->x > x0 && s_hero->x < x1 && s_hero->y + HERO_FOOT < y1 && s_hero->y + HERO_HEAD > y0;
}

static int child_named(int parent, const char *name) {
    int s = str_find(name);
    return s < 0 ? N_NONE : find_child(parent, s);
}


// the enemy actions; *handled false when the op isn't one of them
static bool run_enemy(Fsm *f, Action *ac, ActRt *rt, bool enter, bool *handled) {
    Val *A = ac->a;
    int n = ac->nargs;
#define ARG(i) ((i) < n ? &A[(i)] : &A[n])
    *handled = true;
    switch (ac->op) {
    case OP_GetHero: set_node(f, ARG(0), N_HERO); return true;
    case OP_FindGameObject: {
        const char *nm = str_at(a_str(f, ARG(0))), *tag = str_at(a_str(f, ARG(1)));
        int nd = N_NONE;
        if (!strcmp(tag, "Player") || !strncmp(nm, "Hero", 4) || !strcmp(nm, "Knight")) nd = N_HERO;
        else if (nm[0]) { int s = str_find(nm); for (int i = 0; s >= 0 && i < s_nnodes; i++) if (s_nodes[i].name == s) { nd = i; break; } }
        set_node(f, ARG(2), nd);
        return true;
    }
    case OP_FindAlertRange: {
        int nd = a_node(f, ARG(0));
        set_node(f, ARG(1), child_named(nd, str_at(a_str(f, ARG(2)))));
        return true;
    }
    case OP_CheckAlertRangeByName: case OP_CheckAlertRange: {
        int nd;
        if (ac->op == OP_CheckAlertRangeByName) {
            nd = child_named(f->owner, str_at(a_str(f, ARG(1))));
            if (nd < 0) nd = child_named(f->owner, "Alert Range");
        } else nd = a_node(f, ARG(0));
        bool in = alert_in(nd);
        if (ac->op == OP_CheckAlertRangeByName) {
            set_i(f, ARG(2), in);
            if (in) send_target(f, ARG(0), ev_of(f, ARG(3)), 0);
            else send_target(f, ARG(0), ev_of(f, ARG(4)), 0);
        } else {
            set_i(f, ARG(1), in);
            send_self(f, ARG(in ? 2 : 3));
        }
        return !ac->every;
    }
    case OP_RayCast2d: case OP_RayCast2dV2: {
        int nd = a_node(f, ARG(0));
        if (nd == N_NONE) nd = f->owner;
        float x, y;
        node_pos(nd, &x, &y);
        const Val *off = deref(f, ARG(1)), *dir = deref(f, ARG(2));
        bool local = a_i(f, ARG(3)) == 1;
        float sx = local ? (node_scale(nd) < 0 ? -1.0f : 1.0f) : 1.0f;
        if (off->t == T_VEC) { x += off->f[0] * sx; y += off->f[1]; }
        float dx = dir->t == T_VEC ? dir->f[0] * sx : 0, dy = dir->t == T_VEC ? dir->f[1] : -1;
        float len = a_f(f, ARG(4));
        float d = ray(x, y, dx, dy, len);
        bool hit = d >= 0;
        set_i(f, ARG(7), hit);
        if (hit) {
            Val *p = var_of(f, ARG(8));
            float l = hypotf(dx, dy);
            if (p && l > 0) { p->t = T_VEC; p->f[0] = x + dx / l * d; p->f[1] = y + dy / l * d; }
            set_f(f, ARG(9), d);
        }
        send_self(f, ARG(hit ? 5 : 6));
        return !ac->every;
    }
    case OP_CheckCollisionSide: case OP_CheckCollisionSideEnter: {
        int nd = a_node(f, ARG(0));
        if (nd == N_NONE) nd = f->owner;
        if (nd < 0) return true;
        u8 c = s_nodes[nd].coll, pc = s_nodes[nd].coll_prev;
        if (ac->op == OP_CheckCollisionSideEnter) c &= ~pc;
        static const u8 SIDE[4] = {1, 2, 4, 8};            // top, right, bottom, left
        for (int k = 0; k < 4; k++) {
            set_i(f, ARG(1 + k), (c & SIDE[k]) != 0);
            if (c & SIDE[k]) send_self(f, ARG(5 + k));
        }
        return false;                                       // listens every frame
    }
    case OP_CheckIsCharacterGrounded: {
        int nd = a_node(f, ARG(0));
        float x0, y0, x1, y1;
        bool g = false;
        if (node_box(nd, &x0, &y0, &x1, &y1)) {
            float depth = a_none(f, ARG(5)) ? 0.1f : fmaxf(0.05f, a_f(f, ARG(5)) + 0.05f);
            g = solid(x0 + 0.05f, y0 - depth, x1 - 0.05f, y0);
        }
        set_i(f, ARG(2), g);
        send_self(f, ARG(g ? 3 : 4));
        return !ac->every;
    }
    case OP_FaceDirection: {
        int nd = a_node(f, ARG(0));
        if (nd >= 0 && fabsf(s_nodes[nd].vx) > 0.01f) face_to(nd, s_nodes[nd].vx > 0, a_i(f, ARG(1)) != 0);
        return !ac->every;
    }
    case OP_FaceObject: {
        int a = a_node(f, ARG(0));
        float x0, y0, x1, y1;
        node_pos(a, &x0, &y0);
        node_pos(a_node(f, ARG(1)), &x1, &y1);
        if (a >= 0) face_to(a, x1 > x0, a_i(f, ARG(2)) != 0);
        return !ac->every;
    }
    case OP_FlipScale: {
        int nd = a_node(f, ARG(0));
        if (nd >= 0 && a_i(f, ARG(1))) node_set_scale(nd, -node_scale(nd));
        return !ac->every;
    }
    case OP_RandomlyFlipScale: {
        int nd = a_node(f, ARG(0));
        if (nd >= 0 && (rand() & 1)) node_set_scale(nd, -node_scale(nd));
        return true;
    }
    case OP_GetScale: {
        int nd = a_node(f, ARG(0));
        Obj *o = node_obj(nd);
        set_f(f, ARG(1), node_scale(nd));
        set_f(f, ARG(2), o ? o->scy : 1);
        return !ac->every;
    }
    case OP_SendEventByScale:
        send_target(f, ARG(1), ev_of(f, ARG(node_scale(a_node(f, ARG(0))) >= 0 ? 2 : 3)), 0);
        return !ac->every;
    case OP_CheckFacingTarget: {
        int nd = a_node(f, ARG(0));
        float x0, y0, x1, y1;
        node_pos(nd, &x0, &y0);
        node_pos(a_node(f, ARG(1)), &x1, &y1);
        bool facing = (x1 > x0) == (facing_sign(nd, a_i(f, ARG(2)) != 0) > 0);
        set_i(f, ARG(5), facing);
        set_i(f, ARG(6), !facing);
        send_self(f, ARG(facing ? 3 : 4));
        return !ac->every;
    }
    case OP_GetXDistance: case OP_GetYDistance: {
        float x0, y0, x1, y1;
        node_pos(a_node(f, ARG(0)), &x0, &y0);
        node_pos(a_node(f, ARG(1)), &x1, &y1);
        float d = ac->op == OP_GetXDistance ? fabsf(x1 - x0) : (a_i(f, ARG(3)) ? y1 - y0 : fabsf(y1 - y0));
        set_f(f, ARG(2), d);
        return !ac->every;
    }
    case OP_GetAngleToTarget2D: {
        float x0, y0, x1, y1;
        node_pos(a_node(f, ARG(0)), &x0, &y0);
        node_pos(a_node(f, ARG(1)), &x1, &y1);
        float a = atan2f(y1 + a_f(f, ARG(3)) - y0, x1 + a_f(f, ARG(2)) - x0) * 180 / (float)M_PI;
        if (a < 0) a += 360;
        set_f(f, ARG(4), a);
        return !ac->every;
    }
    case OP_SetVelocityAsAngle: case OP_FireAtTarget: {
        int nd = a_node(f, ARG(0));
        if (nd < 0) return true;
        float a, sp = a_f(f, ARG(2));
        if (ac->op == OP_FireAtTarget) {
            float x0, y0, x1, y1;
            node_pos(nd, &x0, &y0);
            node_pos(a_node(f, ARG(1)), &x1, &y1);
            a = atan2f(y1 - y0, x1 - x0);
        } else a = a_f(f, ARG(1)) * (float)M_PI / 180;
        s_nodes[nd].vx = cosf(a) * sp;
        s_nodes[nd].vy = sinf(a) * sp;
        return !ac->every;
    }
    case OP_ChaseObject: case OP_ChaseObjectV2: case OP_DistanceFly: case OP_AccelerateToY: case OP_ChaseObjectGround: {
        int nd = a_node(f, ARG(0));
        if (nd < 0) return false;
        Node *nn = &s_nodes[nd];
        float x0, y0, x1, y1;
        node_pos(nd, &x0, &y0);
        node_pos(a_node(f, ARG(1)), &x1, &y1);
        float k = s_dt * 60;
        if (ac->op == OP_AccelerateToY) {
            nn->vy = approachf(nn->vy, a_f(f, ARG(2)), a_f(f, ARG(1)) * k);
            return false;
        }
        if (ac->op == OP_ChaseObjectGround) {
            float tx = x1 + a_f(f, ARG(2)), sm = a_f(f, ARG(3));
            nn->vx = approachf(nn->vx, tx > x0 ? sm : -sm, a_f(f, ARG(4)) * k);
            return false;
        }
        float sm = a_f(f, ARG(2)), acc;
        float tx = x1, ty = y1;
        if (ac->op == OP_DistanceFly) {
            // keep its distance: towards the target when too far, away when too near, and above Hornet
            float dist = a_f(f, ARG(2)), dd = hypotf(x1 - x0, y1 - y0);
            sm = a_f(f, ARG(3));
            acc = a_f(f, ARG(4));
            float dirk = dd > dist ? 1 : -1;
            tx = x0 + (x1 - x0) * dirk;
            ty = y0 + (y1 - y0) * dirk;
            if (y0 < y1 + a_f(f, ARG(5))) ty = y1 + a_f(f, ARG(5)) + 1;
        } else {
            tx += a_f(f, ARG(4));
            ty += a_f(f, ARG(5));
            acc = ac->op == OP_ChaseObjectV2 ? a_f(f, ARG(3)) * s_dt : a_f(f, ARG(3)) * k;
        }
        float dx = tx - x0, dy = ty - y0, d = hypotf(dx, dy);
        if (d > 0.01f) {
            float a = ac->op == OP_ChaseObjectV2 ? acc : acc;
            nn->vx += dx / d * a;
            nn->vy += dy / d * a;
        }
        float v = hypotf(nn->vx, nn->vy);
        if (v > sm && v > 0) { nn->vx *= sm / v; nn->vy *= sm / v; }
        return false;
    }
    case OP_IdleBuzz: case OP_IdleBuzzV3: {
        // drifting around where it started: a random point within the range every so often
        int nd = a_node(f, ARG(0));
        if (nd < 0) return false;
        Node *nn = &s_nodes[nd];
        float x, y;
        node_pos(nd, &x, &y);
        if (enter) { rt->x0 = x; rt->y0 = y; rt->t = 0; rt->dur = 0; }
        rt->t += s_dt;
        if (rt->t >= rt->dur) {
            rt->t = 0;
            rt->dur = frand(a_f(f, ARG(1)), a_f(f, ARG(2)));
            rt->dx = rt->x0 + frand(-1, 1) * a_f(f, ARG(5));
            rt->dy = rt->y0 + frand(-1, 1) * a_f(f, ARG(6));
        }
        float dx = rt->dx - x, dy = rt->dy - y, d = hypotf(dx, dy), sm = a_f(f, ARG(3)), acc = a_f(f, ARG(4)) * s_dt;
        if (d > 0.05f) { nn->vx += dx / d * acc; nn->vy += dy / d * acc; }
        float v = hypotf(nn->vx, nn->vy);
        if (v > sm && v > 0) { nn->vx *= sm / v; nn->vy *= sm / v; }
        return false;
    }
    case OP_SetPositionToObject: case OP_SetPositionToObject2D: {
        int nd = a_node(f, ARG(0));
        float x, y;
        node_pos(a_node(f, ARG(1)), &x, &y);
        if (nd >= 0) node_set_pos(nd, x + a_f(f, ARG(2)), y + a_f(f, ARG(3)));
        return !ac->every;
    }
    case OP_FloatTestToBool: {
        float x = a_f(f, ARG(0)), y = a_f(f, ARG(1)), tol = a_f(f, ARG(2));
        set_i(f, ARG(3), fabsf(x - y) <= tol);
        set_i(f, ARG(4), x < y - tol);
        set_i(f, ARG(5), x > y + tol);
        return !ac->every;
    }
    case OP_FloatInRange: {
        float x = a_f(f, ARG(0));
        bool in = x >= a_f(f, ARG(1)) && x <= a_f(f, ARG(2));
        set_i(f, ARG(3), in);
        send_self(f, ARG(in ? 4 : 5));
        return !ac->every;
    }
    case OP_Tk2dPlayFrame: {
        Obj *o = node_obj(a_node(f, ARG(0)));
        if (o && o->anim.set && o->anim.clip >= 0)
            o->anim.t = a_i(f, ARG(1)) / fmaxf(1, o->anim.set->clips[o->anim.clip].fps);
        return true;
    }
    case OP_SetIsKinematic2d: { int nd = a_node(f, ARG(0)); if (nd >= 0) s_nodes[nd].kin = a_i(f, ARG(1)) != 0; return true; }
    case OP_SetInvincible: { Obj *o = node_obj(a_node(f, ARG(0))); if (o) o->invincible = a_i(f, ARG(1)) != 0; return true; }
    case OP_SetRecoilSpeed: { Obj *o = node_obj(a_node(f, ARG(0))); if (o) o->recoil_v = a_f(f, ARG(1)); return true; }
    case OP_DoCameraShake: fx_shake(0.15f); return true;
    case OP_SetDamageHeroAmount: { Obj *o = node_obj(a_node(f, ARG(0))); if (o) o->damage = a_i(f, ARG(1)); return true; }
    case OP_StartCrawler: case OP_StopCrawler: {
        Obj *o = node_obj(a_node(f, ARG(0)));
        if (o) {
            o->walking = ac->op == OP_StartCrawler;
            if (!o->walking && o->node >= 0) s_nodes[o->node].vx = 0;
        }
        return true;
    }
    case OP_StartWalker: case OP_StartWalkerV2: case OP_StopWalker: {
        Obj *o = node_obj(a_node(f, ARG(0)));
        if (o) {
            o->walking = ac->op != OP_StopWalker;
            if (ac->op == OP_StartWalker && !a_none(f, ARG(1))) o->dir = a_i(f, ARG(1)) ? 1 : -1;
            if (!o->walking && o->node >= 0) s_nodes[o->node].vx = 0;
        }
        return true;
    }
    case OP_GetPosition2d: {
        float x, y;
        node_pos(a_node(f, ARG(0)), &x, &y);
        Val *v = var_of(f, ARG(1));
        if (v) { v->t = T_VEC; v->f[0] = x; v->f[1] = y; }
        set_f(f, ARG(2), x);
        set_f(f, ARG(3), y);
        return !ac->every;
    }
    default:
        *handled = false;
        return true;
    }
#undef ARG
}

// runs action a of the current state; enter: its first frame. Returns true when it is over (no more updates).
static int s_trace = -1;   // SD:/3ds/3dsilksong/fsmtrace: every action in the autotest log (finding a hang)

static bool run(Fsm *f, Action *ac, ActRt *rt, bool enter) {
    Val *A = ac->a;
    int n = ac->nargs;
    if (s_trace < 0) { struct stat st; s_trace = stat("sdmc:/3ds/3dsilksong/fsmtrace", &st) == 0; }
    if (s_trace) {
        char msg[96];
        snprintf(msg, sizeof msg, "act %s/%s op %d %s", str_at(s_nodes[f->owner].name), str_at(f->name), ac->op,
                 enter ? "enter" : "");
        autotest_note(msg);
    }
#define ARG(i) ((i) < n ? &A[(i)] : &A[n])
    switch (ac->op) {
    case OP_Wait: case OP_WaitRandom: {
        if (enter) {
            rt->dur = ac->op == OP_Wait ? a_f(f, ARG(0)) : frand(a_f(f, ARG(0)), a_f(f, ARG(1)));
            rt->t = 0;
        } else rt->t += s_dt;
        if (rt->t >= rt->dur) { send_self(f, ARG(ac->op == OP_Wait ? 1 : 2)); return true; }
        return false;
    }
    case OP_NextFrameEvent:
        if (enter) return false;
        send_self(f, ARG(0));
        return true;
    case OP_SendEvent: case OP_SendEventByName:
        send_target(f, ARG(0), ev_of(f, ARG(1)), a_f(f, ARG(2)));
        return !ac->every;
    case OP_SendRandomEvent: case OP_SendRandomEventV3: case OP_SendRandomEventV4: {
        const Val *ev = ARG(0), *w = ARG(1);
        if (ev->t != T_LIST || !ev->n) return true;
        float tot = 0;
        for (int i = 0; i < ev->n; i++) tot += (w->t == T_LIST && i < w->n) ? a_f(f, &w->list[i]) : 1;
        float r = frand(0, tot);
        for (int i = 0; i < ev->n; i++) {
            r -= (w->t == T_LIST && i < w->n) ? a_f(f, &w->list[i]) : 1;
            if (r <= 0 || i == ev->n - 1) { send_self(f, &ev->list[i]); break; }
        }
        return true;
    }
    case OP_BoolTest: send_self(f, ARG(a_i(f, ARG(0)) ? 1 : 2)); return !ac->every;
    case OP_SetBoolValue: set_i(f, ARG(0), a_i(f, ARG(1)) != 0); return !ac->every;
    case OP_BoolFlip: set_i(f, ARG(0), !a_i(f, ARG(0))); return true;
    case OP_BoolTestMulti: {
        const Val *vs = ARG(0), *ss = ARG(1);
        bool ok = vs->t == T_LIST;
        for (int i = 0; ok && i < vs->n; i++) {
            int want = (ss->t == T_LIST && i < ss->n) ? a_i(f, &ss->list[i]) : 1;
            if ((a_i(f, &vs->list[i]) != 0) != (want != 0)) ok = false;
        }
        set_i(f, ARG(4), ok);
        send_self(f, ARG(ok ? 2 : 3));
        return !ac->every;
    }
    case OP_BoolAllTrue: case OP_BoolNoneTrue: {
        const Val *vs = ARG(0);
        bool all = true, none = true;
        for (int i = 0; vs->t == T_LIST && i < vs->n; i++) { if (a_i(f, &vs->list[i])) none = false; else all = false; }
        bool ok = ac->op == OP_BoolAllTrue ? all : none;
        set_i(f, ARG(2), ok);
        if (ok) send_self(f, ARG(1));
        return !ac->every;
    }
    case OP_SetFloatValue: set_f(f, ARG(0), a_f(f, ARG(1))); return !ac->every;
    case OP_FloatAdd: set_f(f, ARG(0), a_f(f, ARG(0)) + a_f(f, ARG(1)) * (a_i(f, ARG(2)) ? s_dt : 1)); return !ac->every;
    case OP_FloatSubtract: set_f(f, ARG(0), a_f(f, ARG(0)) - a_f(f, ARG(1)) * (a_i(f, ARG(2)) ? s_dt : 1)); return !ac->every;
    case OP_FloatMultiply: set_f(f, ARG(0), a_f(f, ARG(0)) * a_f(f, ARG(1))); return !ac->every;
    case OP_FloatClamp: set_f(f, ARG(0), clampf(a_f(f, ARG(0)), a_f(f, ARG(1)), a_f(f, ARG(2)))); return !ac->every;
    case OP_FloatAbs: set_f(f, ARG(0), fabsf(a_f(f, ARG(0)))); return !ac->every;
    case OP_FloatOperator: {
        float x = a_f(f, ARG(0)), y = a_f(f, ARG(1)), r = x;
        switch (a_i(f, ARG(2))) {
        case 0: r = x + y; break; case 1: r = x - y; break; case 2: r = x * y; break;
        case 3: r = y != 0 ? x / y : 0; break; case 4: r = fminf(x, y); break; case 5: r = fmaxf(x, y); break;
        }
        set_f(f, ARG(3), r);
        return !ac->every;
    }
    case OP_FloatCompare: {
        float x = a_f(f, ARG(0)), y = a_f(f, ARG(1)), tol = a_f(f, ARG(2));
        send_self(f, ARG(fabsf(x - y) <= tol ? 3 : x < y ? 4 : 5));
        return !ac->every;
    }
    case OP_FloatSignTest: send_self(f, ARG(a_f(f, ARG(0)) >= 0 ? 1 : 2)); return !ac->every;
    case OP_RandomFloat: set_f(f, ARG(2), frand(a_f(f, ARG(0)), a_f(f, ARG(1)))); return true;
    case OP_SetIntValue: set_i(f, ARG(0), a_i(f, ARG(1))); return !ac->every;
    case OP_IntAdd: set_i(f, ARG(0), a_i(f, ARG(0)) + a_i(f, ARG(1))); return !ac->every;
    case OP_IntOperator: {
        int x = a_i(f, ARG(0)), y = a_i(f, ARG(1)), r = x;
        switch (a_i(f, ARG(2))) {
        case 0: r = x + y; break; case 1: r = x - y; break; case 2: r = x * y; break;
        case 3: r = y ? x / y : 0; break; case 4: r = x > y ? x : y; break; case 5: r = x < y ? x : y; break;
        }
        set_i(f, ARG(3), r);
        return !ac->every;
    }
    case OP_IntCompare: {
        int x = a_i(f, ARG(0)), y = a_i(f, ARG(1));
        send_self(f, ARG(x == y ? 2 : x < y ? 3 : 4));
        return !ac->every;
    }
    case OP_RandomInt: {
        int lo = a_i(f, ARG(0)), hi = a_i(f, ARG(1)) + (a_i(f, ARG(3)) ? 1 : 0);
        set_i(f, ARG(2), hi > lo ? lo + rand() % (hi - lo) : lo);
        return true;
    }
    case OP_ConvertIntToFloat: set_f(f, ARG(1), (float)a_i(f, ARG(0))); return !ac->every;
    case OP_ConvertBoolToFloat: set_f(f, ARG(1), a_f(f, ARG(a_i(f, ARG(0)) ? 3 : 2))); return !ac->every;
    case OP_SetStringValue: { Val *v = var_of(f, ARG(0)); if (v) { v->t = T_STR; v->s = a_str(f, ARG(1)); } return !ac->every; }
    case OP_StringCompare: send_self(f, ARG(a_str(f, ARG(0)) == a_str(f, ARG(1)) ? 2 : 3)); return !ac->every;
    case OP_EnumCompare: send_self(f, ARG(a_i(f, ARG(0)) == a_i(f, ARG(1)) ? 2 : 3)); return !ac->every;
    case OP_SetGameObject: set_node(f, ARG(0), a_node(f, ARG(1))); return !ac->every;
    case OP_GetOwner: set_node(f, ARG(0), f->owner); return true;
    case OP_FindChild: case OP_FindNamedChild:
        set_node(f, ARG(2), find_child(a_node(f, ARG(0)), a_str(f, ARG(1)))); return true;
    case OP_GetParent: {
        int nd = a_node(f, ARG(0));
        set_node(f, ARG(1), nd >= 0 ? s_nodes[nd].parent : N_NONE);
        return true;
    }
    case OP_SetParent: {
        int nd = a_node(f, ARG(0));
        if (nd >= 0) s_nodes[nd].parent = (s16)a_node(f, ARG(1));
        return true;
    }
    case OP_GameObjectIsNull: send_self(f, ARG(a_node(f, ARG(0)) == N_NONE ? 1 : 2)); return !ac->every;
    case OP_GameObjectCompare: send_self(f, ARG(a_node(f, ARG(0)) == a_node(f, ARG(1)) ? 2 : 3)); return !ac->every;
    case OP_ActivateGameObject: {
        int nd = a_node(f, ARG(0));
        if (nd >= 0) s_nodes[nd].active = a_i(f, ARG(1)) != 0;
        return !ac->every;
    }
    case OP_ActivateAllChildren: {
        int nd = a_node(f, ARG(0));
        for (int i = 0; nd >= 0 && i < s_nnodes; i++) if (s_nodes[i].parent == nd) s_nodes[i].active = a_i(f, ARG(1)) != 0;
        return true;
    }
    case OP_SetPosition: case OP_SetPosition2d: case OP_SetPosition2D: case OP_Translate: {
        int nd = a_node(f, ARG(0));
        if (nd == N_NONE) return true;
        float x, y, nx, ny;
        node_pos(nd, &x, &y);
        const Val *vec = deref(f, ARG(1));
        if (ac->op == OP_Translate) {
            float k = a_i(f, ARG(5)) ? s_dt : 1;
            nx = x + (vec->t == T_VEC ? vec->f[0] : 0) * k + (a_none(f, ARG(2)) ? 0 : a_f(f, ARG(2)) * k);
            ny = y + (vec->t == T_VEC ? vec->f[1] : 0) * k + (a_none(f, ARG(3)) ? 0 : a_f(f, ARG(3)) * k);
        } else {
            bool local = a_i(f, ARG(4)) == 1;
            float px = 0, py = 0;
            if (local && nd >= 0 && s_nodes[nd].parent >= 0) node_pos(s_nodes[nd].parent, &px, &py);
            nx = vec->t == T_VEC ? vec->f[0] + px : x;
            ny = vec->t == T_VEC ? vec->f[1] + py : y;
            if (!a_none(f, ARG(2))) nx = a_f(f, ARG(2)) + px;
            if (!a_none(f, ARG(3))) ny = a_f(f, ARG(3)) + py;
        }
        node_set_pos(nd, nx, ny);
        return !ac->every;
    }
    case OP_GetPosition: case OP_GetPosition2D: {
        float x, y;
        node_pos(a_node(f, ARG(0)), &x, &y);
        Val *v = var_of(f, ARG(1));
        if (v) { v->t = T_VEC; v->f[0] = x; v->f[1] = y; }
        set_f(f, ARG(2), x);
        set_f(f, ARG(3), y);
        return !ac->every;
    }
    case OP_SetScale: {
        Obj *o = node_obj(a_node(f, ARG(0)));
        if (o) {
            const Val *vec = deref(f, ARG(1));
            if (vec->t == T_VEC) { o->scx = vec->f[0]; o->scy = vec->f[1]; }
            if (!a_none(f, ARG(2))) o->scx = a_f(f, ARG(2));
            if (!a_none(f, ARG(3))) o->scy = a_f(f, ARG(3));
        }
        return !ac->every;
    }
    case OP_SetVelocity2d: {
        int nd = a_node(f, ARG(0));
        if (nd < 0) return true;
        const Val *vec = deref(f, ARG(1));
        if (vec->t == T_VEC) { s_nodes[nd].vx = vec->f[0]; s_nodes[nd].vy = vec->f[1]; }
        if (!a_none(f, ARG(2))) s_nodes[nd].vx = a_f(f, ARG(2));
        if (!a_none(f, ARG(3))) s_nodes[nd].vy = a_f(f, ARG(3));
        return !ac->every;
    }
    case OP_SetVelocityByScale: {
        int nd = a_node(f, ARG(0));
        Obj *o = node_obj(nd);
        if (nd < 0) return true;
        s_nodes[nd].vx = a_f(f, ARG(1)) * (o && o->scx < 0 ? -1 : 1);
        if (!a_none(f, ARG(2))) s_nodes[nd].vy = a_f(f, ARG(2));
        return !ac->every;
    }
    case OP_GetVelocity2d: {
        int nd = a_node(f, ARG(0));
        if (nd < 0) return true;
        Val *v = var_of(f, ARG(1));
        if (v) { v->t = T_VEC; v->f[0] = s_nodes[nd].vx; v->f[1] = s_nodes[nd].vy; }
        set_f(f, ARG(2), s_nodes[nd].vx);
        set_f(f, ARG(3), s_nodes[nd].vy);
        return !ac->every;
    }
    case OP_DecelerateV2: case OP_DecelerateXY: {
        int nd = a_node(f, ARG(0));
        if (nd < 0) return true;
        float kx = a_f(f, ARG(1)), ky = ac->op == OP_DecelerateV2 ? kx : (a_none(f, ARG(2)) ? 1 : a_f(f, ARG(2)));
        s_nodes[nd].vx *= powf(kx, s_dt * 60);
        s_nodes[nd].vy *= powf(ky, s_dt * 60);
        return false;
    }
    case OP_AccelerateToX: {
        int nd = a_node(f, ARG(0));
        if (nd >= 0) s_nodes[nd].vx = approachf(s_nodes[nd].vx, a_f(f, ARG(2)), a_f(f, ARG(1)) * s_dt * 60);
        return false;
    }
    case OP_GetDistance: {
        float x0, y0, x1, y1;
        node_pos(a_node(f, ARG(0)), &x0, &y0);
        node_pos(a_node(f, ARG(1)), &x1, &y1);
        set_f(f, ARG(2), hypotf(x1 - x0, y1 - y0));
        return !ac->every;
    }
    case OP_FaceObjectV2: {
        Obj *o = node_obj(a_node(f, ARG(0)));
        float x0, y0, x1, y1;
        node_pos(a_node(f, ARG(0)), &x0, &y0);
        node_pos(a_node(f, ARG(1)), &x1, &y1);
        if (o) {
            bool right = x1 > x0, faces_right = a_i(f, ARG(2)) != 0;
            float s = fabsf(o->scx);
            o->scx = (right == faces_right) ? s : -s;
        }
        return !ac->every;
    }
    case OP_CheckTargetDirection: {
        float x0, y0, x1, y1;
        node_pos(a_node(f, ARG(0)), &x0, &y0);
        node_pos(a_node(f, ARG(1)), &x1, &y1);
        send_self(f, ARG(y1 > y0 ? 2 : 3));
        send_self(f, ARG(x1 > x0 ? 4 : 5));
        return !ac->every;
    }
    case OP_AnimatePositionBy: case OP_AnimatePositionTo: {
        int nd = a_node(f, ARG(0));
        bool by = ac->op == OP_AnimatePositionBy;
        int ti = by ? 2 : 3, fe = by ? 3 : 4;
        if (nd == N_NONE) return true;
        if (enter) {
            node_pos(nd, &rt->x0, &rt->y0);
            const Val *v = deref(f, ARG(1));
            float tx = v->t == T_VEC ? v->f[0] : 0, ty = v->t == T_VEC ? v->f[1] : 0;
            if (by) { rt->dx = tx; rt->dy = ty; }
            else {
                float px = 0, py = 0;
                if (a_i(f, ARG(2)) && s_nodes[nd].parent >= 0) node_pos(s_nodes[nd].parent, &px, &py);
                rt->dx = tx + px - rt->x0; rt->dy = ty + py - rt->y0;
            }
            rt->dur = a_f(f, ARG(ti));
            rt->t = 0;
        } else rt->t += s_dt;
        float k = rt->dur > 0 ? clampf(rt->t / rt->dur, 0, 1) : 1;
        k = k * k * (3 - 2 * k);
        node_set_pos(nd, rt->x0 + rt->dx * k, rt->y0 + rt->dy * k);
        if (rt->t >= rt->dur) { send_self(f, ARG(fe)); return true; }
        return false;
    }
    case OP_SetMeshRenderer: case OP_SetSpriteRenderer: {
        Obj *o = node_obj(a_node(f, ARG(0)));
        if (o) o->hidden = !a_i(f, ARG(1));
        return !ac->every;
    }
    case OP_SetCollider: {
        int nd = a_node(f, ARG(0));
        Obj *o = node_obj(nd);
        if (o) o->nocoll = !a_i(f, ARG(1));
        if (nd >= 0) s_nodes[nd].nocoll = !a_i(f, ARG(1));
        return !ac->every;
    }
    case OP_SetDamageHero: { Obj *o = node_obj(a_node(f, ARG(0))); if (o) o->nodamage = !a_i(f, ARG(1)); return true; }
    case OP_Tk2dPlayAnimation: play_clip(a_node(f, ARG(0)), ARG(1), f); return true;
    case OP_Tk2dPlayAnimationWithEvents: case OP_Tk2dWatchAnimationEvents: {
        int nd = a_node(f, ARG(0));
        bool play = ac->op == OP_Tk2dPlayAnimationWithEvents;
        if (enter && play) play_clip(nd, ARG(1), f);
        int ce = play ? 3 : 2;
        if (nd == N_HERO && s_hero) {   // Hornet's clip, played by a script
            if (s_hero->body.done) { send_self(f, ARG(ce)); return true; }
            return false;
        }
        Obj *o = node_obj(nd);
        if (!o || !o->anim.set) { if (!enter || !play) { send_self(f, ARG(ce)); return true; } return false; }
        if (o->anim.done) { send_self(f, ARG(ce)); return true; }
        return false;
    }
    case OP_Trigger2dEvent: case OP_Trigger2dEventLayer: case OP_Collision2dEvent: {
        // Hornet (her layers 9, 20 and the player's tags), or the terrain (layer 8: a projectile hitting a wall)
        if (ac->op == OP_Trigger2dEventLayer && a_i(f, ARG(2)) == 8) {
            int nd = a_node(f, ARG(0));
            if (nd == N_NONE) nd = f->owner;
            float x0, y0, x1, y1;
            // touching counts (a moving body stops just short of the wall)
            bool in = node_box(nd, &x0, &y0, &x1, &y1) && s_room &&
                      room_box_solid(s_room, x0 - 0.15f, y0 - 0.15f, x1 + 0.15f, y1 + 0.15f);
            int kind = a_i(f, ARG(1));
            if (enter) { rt->inside = in; if (kind == 0 && in) send_self(f, ARG(3)); return false; }
            if ((kind == 0 && in && !rt->inside) || (kind == 1 && in) || (kind == 2 && !in && rt->inside)) send_self(f, ARG(3));
            rt->inside = in;
            return false;
        }
        if (ac->op == OP_Trigger2dEventLayer && a_i(f, ARG(2)) != 9 && a_i(f, ARG(2)) != 20) return false;
        if (ac->op != OP_Trigger2dEventLayer) {
            const char *tag = str_at(a_str(f, ARG(2)));
            if (tag[0] && strcmp(tag, "Player") && strcmp(tag, "HeroBox") && strcmp(tag, "Untagged")) return false;
        }
        int nd = a_node(f, ARG(0));
        if (nd == N_NONE) nd = f->owner;
        bool in = hero_in(nd);
        int kind = a_i(f, ARG(1));    // 0 enter, 1 stay, 2 exit
        if (enter) { rt->inside = in; if (kind == 0 && in) send_self(f, ARG(3)); return false; }
        if ((kind == 0 && in && !rt->inside) || (kind == 1 && in) || (kind == 2 && !in && rt->inside)) send_self(f, ARG(3));
        rt->inside = in;
        return false;
    }
    case OP_RunFSM: {   // the template's FSM from its start, with the inputs; done when it finishes (FinishFSM)
        int si = a_i(f, ARG(0));
        if (si < 0 || si >= s_nfsm || s_fsm[si].host != (int)(f - s_fsm)) return true;
        Fsm *g = &s_fsm[si];
        if (enter) {
            if (g->vars0)
                for (int i = 0; i < g->nvars; i++) {
                    g->vars[i] = g->vars0[i];
                }
            const Val *in = ARG(1);
            for (int i = 0; in->t == T_LIST && i + 1 < in->n; i += 2) {   // name, value
                Val *v = fsm_var(g, in->list[i].s);
                const Val *src = deref(f, &in->list[i + 1]);
                if (v && src->t != T_LIST && src->t != T_NONE) *v = *src;
            }
            g->started = false;
            g->stopped = false;
            g->running = true;
            g->cur = -1;
            g->next = -1;
            f->sub = si;
            return false;
        }
        if (!g->stopped) return false;
        g->running = false;
        f->sub = -1;
        if (!a_none(f, ARG(2))) { f->finished_sent = true; send_self(f, ARG(2)); }
        return true;
    }
    case OP_CheckTrackTriggerCount: {   // TrackTriggerObjects: what is in the trigger (here, Hornet: 0 or 1)
        int nd = a_node(f, ARG(0));
        if (nd == N_NONE) nd = f->owner;
        float x0, y0, x1, y1;
        int c = s_hero && node_box(nd, &x0, &y0, &x1, &y1) &&
                rects_overlap(s_hero->x - HERO_HW, s_hero->y + HERO_FOOT, 2 * HERO_HW, HERO_HEAD - HERO_FOOT,
                              x0, y0, x1 - x0, y1 - y0) ? 1 : 0;
        int want = a_i(f, ARG(1)), t = a_i(f, ARG(2));   // IntTest: = < > <= >=
        bool ok = t == 0 ? c == want : t == 1 ? c < want : t == 2 ? c > want : t == 3 ? c <= want : c >= want;
        if (ok) { send_self(f, ARG(3)); return true; }
        return !ac->every;
    }
    case OP_CheckQuestState: {   // QuestManager: no quest taken yet unless PlayerData says ("quest:<name>" 1 taken, 2 done)
        char k[72];
        snprintf(k, sizeof k, "quest:%s", str_at(a_str(f, ARG(0))));
        int q = pd_int(k);
        send_self(f, ARG(q == 0 ? 1 : q == 2 && !a_none(f, ARG(3)) ? 3 : 2));
        return true;
    }
    case OP_GetShopObject: {   // ShopOwnerBase.ShopObject: the shop menu, with this owner's stock
        int nd = a_node(f, ARG(1));
        shop_select(nd >= 0 && nd < s_nnodes ? shop_of(s_scene_name, str_at(s_nodes[nd].name)) : -1);
        if (autotest_on()) {
            char m[128];
            snprintf(m, sizeof m, "shop object of %s in %s: %d", nd >= 0 ? str_at(s_nodes[nd].name) : "?", s_scene_name,
                     nd >= 0 && nd < s_nnodes ? shop_of(s_scene_name, str_at(s_nodes[nd].name)) : -1);
            autotest_note(m);
        }
        set_node(f, ARG(0), N_SHOP);
        return true;
    }
    case OP_ShopHasStock: case OP_ShopHasStockNotInfinite: case OP_ShopWasItemPurchased: {
        int nd = a_node(f, ARG(0));
        int st = nd >= 0 && nd < s_nnodes ? shop_of(s_scene_name, str_at(s_nodes[nd].name)) : -1;
        bool b = ac->op == OP_ShopWasItemPurchased ? shop_was_purchased()
                                                   : shop_has_stock(st, ac->op == OP_ShopHasStockNotInfinite);
        set_i(f, ARG(3), b);
        send_self(f, ARG(b ? 1 : 2));
        return true;
    }
    case OP_PlayerDataVariableTest: {
        const char *nm = str_at(a_str(f, ARG(0)));
        const Val *e = deref(f, ARG(1));
        bool same = e->t == T_BOOL ? pd_bool(nm) == (e->i != 0)
                  : e->t == T_FLOAT ? (float)pd_int(nm) == e->f[0]
                  : e->t == T_INT ? pd_int(nm) == e->i : false;   // strings: not kept in PlayerData here
        send_self(f, ARG(same ? 2 : 3));
        return true;
    }
    case OP_PlayerDataBoolTest: send_self(f, ARG(pd_bool(str_at(a_str(f, ARG(0)))) ? 1 : 2)); return true;
    case OP_SetPlayerDataBool: pd_set_bool(str_at(a_str(f, ARG(0))), a_i(f, ARG(1)) != 0); return true;
    case OP_GetPlayerDataBool: set_i(f, ARG(1), pd_bool(str_at(a_str(f, ARG(0))))); return !ac->every;
    case OP_SetPlayerDataInt: {
        const char *nm = str_at(a_str(f, ARG(0)));
        pd_set_int(nm, a_i(f, ARG(1)));
        if (s_hero && !strcmp(nm, "health")) s_hero->hp = a_i(f, ARG(1));      // Hornet's masks are PlayerData's
        if (s_hero && !strcmp(nm, "maxHealth") && a_i(f, ARG(1)) > 0) s_hero->maxhp = a_i(f, ARG(1));
        return true;
    }
    case OP_CallMethodProper: case OP_SendMessage: case OP_HeroControllerMethods: {
        // HeroController's methods the scripts call: control, animation control, facing, health
        static const char *const M[] = {"SetAllowNailChargingWhileRelinquished", "RelinquishControlNotVelocity",
                                        "StopAnimationControl", "FlipSprite", "ShouldHardLand", "StartAnimationControlToIdle",
                                        "RegainControl", "RelinquishControl", "DoHardLanding", "", "ResetGravity", "", "",
                                        "", "", "", "AffectedByGravity", "StartAnimationControl"};
        const char *m;
        int nd;
        float num;
        if (ac->op == OP_HeroControllerMethods) {
            int k = a_i(f, ARG(0));
            m = k >= 0 && k < (int)(sizeof M / sizeof *M) ? M[k] : "";
            nd = N_HERO;
            num = a_i(f, ARG(1));
        } else if (ac->op == OP_CallMethodProper) {
            nd = a_node(f, ARG(0));
            if (strcmp(str_at(a_str(f, ARG(1))), "HeroController")) return true;
            m = str_at(a_str(f, ARG(2)));
            num = a_f(f, ARG(3));
        } else {
            nd = a_node(f, ARG(0));
            m = str_at(a_str(f, ARG(1)));
            num = a_f(f, ARG(2));
        }
        if (nd != N_HERO || !s_hero) return true;
        Hero *h = s_hero;
        if (!strcmp(m, "RelinquishControl") || !strcmp(m, "RelinquishControlNotVelocity") || !strcmp(m, "IgnoreInput")) {
            h->ctrl_off = true;
            if (!strcmp(m, "RelinquishControl")) h->vx = 0;
        } else if (!strcmp(m, "RegainControl") || !strcmp(m, "AcceptInput")) h->ctrl_off = false;
        else if (!strcmp(m, "StopAnimationControl")) h->anim_ext = true;
        else if (!strcmp(m, "StartAnimationControl") || !strcmp(m, "StartAnimationControlToIdle")) h->anim_ext = false;
        else if (!strcmp(m, "FlipSprite")) h->face = -h->face;
        else if (!strcmp(m, "FaceLeft")) h->face = -1;
        else if (!strcmp(m, "FaceRight")) h->face = 1;
        else if (!strcmp(m, "AddHealth")) { h->hp += (int)num; if (h->hp > h->maxhp) h->hp = h->maxhp; }
        return true;
    }
    case OP_ScreenFader: {   // the screen to black (or back) over a time
        if (enter) { rt->x0 = a_f(f, ARG(0)); rt->dx = a_f(f, ARG(1)); rt->dur = a_f(f, ARG(2)); rt->t = 0; }
        else rt->t += s_dt;
        float k = rt->dur > 0 ? rt->t / rt->dur : 1;
        if (k > 1) k = 1;
        g_fsm_fade = rt->x0 + (rt->dx - rt->x0) * k;
        return k >= 1;
    }
    case OP_ListenForCast: case OP_ListenForJump: case OP_ListenForAttack: case OP_ListenForDash: case OP_ListenForUp:
    case OP_ListenForDown: case OP_ListenForLeft: case OP_ListenForRight: case OP_ListenForDreamNail:
    case OP_ListenForQuickCast: {
        // the game's actions on the 3DS buttons (its default layout)
        u32 key = ac->op == OP_ListenForCast ? KEY_A : ac->op == OP_ListenForJump ? KEY_B :
                  ac->op == OP_ListenForAttack ? KEY_Y : ac->op == OP_ListenForDash ? KEY_ZR :
                  ac->op == OP_ListenForUp ? KEY_UP : ac->op == OP_ListenForDown ? KEY_DOWN :
                  ac->op == OP_ListenForLeft ? KEY_LEFT : ac->op == OP_ListenForRight ? KEY_RIGHT :
                  ac->op == OP_ListenForDreamNail ? KEY_X : KEY_R;
        if (s_in_down & key) send_self(f, ARG(0));
        if (s_in_up & key) send_self(f, ARG(1));
        if (s_in_held & key) send_self(f, ARG(2));
        else send_self(f, ARG(3));
        return false;
    }
    case OP_SendEventToRegister: {   // EventRegister: every script listening for that name
        int e = str_find(str_at(a_str(f, ARG(0))));
        if (e >= 0)
            for (int k = 0; k < s_nfsm; k++)
                if (s_fsm[k].started && !s_fsm[k].stopped) fsm_event(&s_fsm[k], e);
        return true;
    }
    case OP_iTweenMoveBy: case OP_iTweenMoveTo: {
        bool by = ac->op == OP_iTweenMoveBy;
        int nd = a_node(f, ARG(0));
        float time = a_f(f, by ? ARG(2) : ARG(3)), delay = a_f(f, by ? ARG(3) : ARG(4));
        if (enter) {
            node_pos(nd, &rt->x0, &rt->y0);
            if (by) {
                const Val *v = deref(f, ARG(1));
                rt->dx = v->t == T_VEC ? v->f[0] : 0;
                rt->dy = v->t == T_VEC ? v->f[1] : 0;
            } else {
                float tx = rt->x0, ty = rt->y0;
                int tn = a_node(f, ARG(1));
                const Val *v = deref(f, ARG(2));
                if (tn != N_NONE) node_pos(tn, &tx, &ty);
                else if (v->t == T_VEC) { tx = v->f[0]; ty = v->f[1]; }
                rt->dx = tx - rt->x0;
                rt->dy = ty - rt->y0;
            }
            rt->t = 0;
            rt->done = 0;
        } else rt->t += s_dt;
        if (rt->done) return true;
        float k = time > 0 ? (rt->t - delay) / time : 1;
        if (k < 0) return false;
        if (k > 1) k = 1;
        float e = k * k * (3 - 2 * k);   // iTween's easeInOut, about
        node_set_pos(nd, rt->x0 + rt->dx * e, rt->y0 + rt->dy * e);
        if (k >= 1) { rt->done = 1; send_self(f, by ? ARG(4) : ARG(5)); return true; }
        return false;
    }
    case OP_ActivateGameObjectDelay: {
        if (enter) rt->t = 0; else rt->t += s_dt;
        if (rt->t < a_f(f, ARG(2))) return false;
        int nd = a_node(f, ARG(0));
        if (nd >= 0) s_nodes[nd].active = a_i(f, ARG(1)) != 0;
        return true;
    }
    case OP_CheckHeroPerformanceRegion: case OP_CheckHeroPerformanceRegionV2: {
        // the Needolin heard: inside the region around Hornet (17 x 12), its outer part (26 x 19), or not at all
        bool v2 = ac->op == OP_CheckHeroPerformanceRegionV2;
        int o = v2 ? 1 : 0;
        int state = 0;
        if (s_hero && s_hero->performing) {
            if (a_i(f, ARG(6 + o))) state = 1;
            else {
                float x, y, rad = v2 ? a_f(f, ARG(1)) : 0;
                node_pos(a_node(f, ARG(0)), &x, &y);
                float dx = fabsf(x - s_hero->x) - rad, dy = fabsf(y - (s_hero->y - 0.6f)) - rad;
                state = dx <= 8.5f && dy <= 6 ? 1 : dx <= 13 && dy <= 9.5f ? 2 : 0;
            }
        }
        if (enter) { rt->done = 0xFF; rt->t = -1; }
        if (state != rt->done && rt->t < 0) {   // a change: heard after a short reaction delay
            rt->dx = (float)state;
            rt->t = frand(a_f(f, ARG(1 + o)), a_f(f, ARG(2 + o)));
        }
        if (rt->t >= 0) {
            rt->t -= s_dt;
            if (rt->t < 0) {
                rt->done = (u8)rt->dx;
                send_self(f, ARG(3 + o + rt->done));
            }
        }
        return !ac->every && rt->t < 0 && rt->done != 0xFF;
    }
    case OP_SpawnObjectFromGlobalPool: case OP_CreateObject: case OP_SpawnObjectFromGlobalPoolDelay: {
        if (ac->op == OP_SpawnObjectFromGlobalPoolDelay) {
            if (enter) { rt->t = 0; rt->dur = frand(a_f(f, ARG(5)), a_f(f, ARG(6))); }
            else rt->t += s_dt;
            if (rt->t < rt->dur) return false;
        }
        float x = 0, y = 0;
        int sp = a_node(f, ARG(1));
        if (sp != N_NONE) node_pos(sp, &x, &y);
        const Val *pv = deref(f, ARG(2));
        if (pv->t == T_VEC) { x += pv->f[0]; y += pv->f[1]; }
        int root = spawn_pack(str_at(a_str(f, ARG(0))), x, y);
        set_node(f, ARG(4), root);
        return true;
    }
    case OP_FlingObjectsFromGlobalPool: {
        float x = 0, y = 0;
        int sp = a_node(f, ARG(1));
        if (sp != N_NONE) node_pos(sp, &x, &y);
        const Val *pv = deref(f, ARG(2));
        if (pv->t == T_VEC) { x += pv->f[0]; y += pv->f[1]; }
        const char *name = str_at(a_str(f, ARG(0)));
        int lo = a_i(f, ARG(3)), hi = a_i(f, ARG(4));
        int count = lo + (hi > lo ? rand() % (hi - lo + 1) : 0);
        // rosaries and shell shards: the engine's own (currency.c)
        int cur = !strcmp(name, "pf_geo_small") ? CUR_SMALL : !strcmp(name, "pf_geo_med") ? CUR_MED :
                  !strcmp(name, "pf_geo_large") ? CUR_LARGE : !strncmp(name, "pf_shell_shard", 14) ? CUR_SHARD : -1;
        if (cur >= 0) { currency_fling(cur, count, x, y, false); return true; }
        for (int k = 0; k < count && k < 24; k++) {
            float ox = frand(-1, 1) * a_f(f, ARG(9)), oy = frand(-1, 1) * a_f(f, ARG(10));
            int root = spawn_pack(name, x + ox, y + oy);
            if (root < 0) break;
            float sp2 = frand(a_f(f, ARG(5)), a_f(f, ARG(6))), ang = frand(a_f(f, ARG(7)), a_f(f, ARG(8))) * (float)M_PI / 180;
            s_nodes[root].vx = cosf(ang) * sp2;
            s_nodes[root].vy = sinf(ang) * sp2;
        }
        return true;
    }
    case OP_EaseFloat: {   // a float from one value to another over a time (iTween's curves, the common ones)
        float t0 = a_none(f, ARG(4)) ? 0 : a_f(f, ARG(4)), dur = a_f(f, ARG(3));
        if (enter) rt->t = 0; else rt->t += s_dt;
        float k = dur > 0 ? (rt->t - t0) / dur : 1;
        if (k < 0) return false;
        if (k > 1) k = 1;
        float e = k;
        int type = a_i(f, ARG(5));
        if (a_i(f, ARG(6))) e = 1 - e;
        switch (type) {
        case 0: e = e * e; break;
        case 1: e = e * (2 - e); break;
        case 2: e = e < 0.5f ? 2 * e * e : -1 + (4 - 2 * e) * e; break;
        case 3: e = e * e * e; break;
        case 4: e = 1 - (1 - e) * (1 - e) * (1 - e); break;
        case 5: e = e < 0.5f ? 4 * e * e * e : 1 - powf(-2 * e + 2, 3) / 2; break;
        case 12: e = 1 - cosf(e * (float)M_PI / 2); break;
        case 13: e = sinf(e * (float)M_PI / 2); break;
        case 14: e = -(cosf((float)M_PI * e) - 1) / 2; break;
        case 15: e = e <= 0 ? 0 : powf(2, 10 * e - 10); break;
        case 16: e = e >= 1 ? 1 : 1 - powf(2, -10 * e); break;
        default: break;   // linear, and the rarer ones
        }
        float a = a_f(f, ARG(0)), b = a_f(f, ARG(1));
        set_f(f, ARG(2), a + (b - a) * e);
        if (k >= 1) { send_self(f, ARG(7)); return true; }
        return false;
    }
    case OP_SetRotation: case OP_Rotate: {
        Obj *o = node_obj(a_node(f, ARG(0)));
        if (o && !a_none(f, ARG(1))) {
            if (ac->op == OP_SetRotation) o->rot = a_f(f, ARG(1));
            else o->rot += a_f(f, ARG(1)) * (a_i(f, ARG(2)) ? s_dt : 1);
        }
        return !ac->every;
    }
    case OP_FaceAngle: {   // turned to where it flies
        int nd = a_node(f, ARG(0));
        Obj *o = node_obj(nd);
        if (o && nd >= 0 && (s_nodes[nd].vx != 0 || s_nodes[nd].vy != 0))
            o->rot = atan2f(s_nodes[nd].vy, s_nodes[nd].vx) * 180 / (float)M_PI + a_f(f, ARG(1));
        return !ac->every;
    }
    case OP_PlayParticleEmitter: particles_play(a_node(f, ARG(0)), a_i(f, ARG(1))); return true;
    case OP_PlayParticleEmitterInState: particles_play(a_node(f, ARG(0)), 0); return true;
    case OP_StopParticleEmitter: particles_stop(a_node(f, ARG(0))); return true;
    case OP_SetParticleEmission: {
        int nd = a_node(f, ARG(0));
        if (a_i(f, ARG(1))) particles_play(nd, 0); else particles_stop(nd);
        return true;
    }
    case OP_RecycleSelf: recycle(f->owner); return true;
    case OP_RecycleObject: recycle(a_node(f, ARG(0))); return true;
    case OP_RandomFloatEither: set_f(f, ARG(2), (rand() & 1) ? a_f(f, ARG(0)) : a_f(f, ARG(1))); return true;
    case OP_GetPlayerDataVariable: {
        const char *nm = str_at(a_str(f, ARG(0)));
        Val *v = var_of(f, ARG(1));
        if (!v) return true;
        if (v->t == T_BOOL) v->i = pd_bool(nm);
        else { v->i = pd_int(nm); v->f[0] = (float)v->i; }
        return true;
    }
    case OP_SetPlayerDataVariable: {
        const char *nm = str_at(a_str(f, ARG(0)));
        const Val *v = deref(f, ARG(1));
        if (v->t == T_BOOL) pd_set_bool(nm, v->i != 0);
        else pd_set_int(nm, v->t == T_FLOAT ? (int)v->f[0] : v->i);
        return true;
    }
    case OP_GetPlayerDataInt: set_i(f, ARG(1), pd_int(str_at(a_str(f, ARG(0))))); return !ac->every;
    case OP_SetFsmBool: case OP_SetFsmFloat: case OP_SetFsmInt: case OP_SetFsmString: {
        Val *v = fsm_var(fsm_on(a_node(f, ARG(0)), a_str(f, ARG(1))), a_str(f, ARG(2)));
        if (v) {
            const Val *src = deref(f, ARG(3));
            if (ac->op == OP_SetFsmFloat) { v->f[0] = a_f(f, ARG(3)); v->i = (int)v->f[0]; }
            else if (ac->op == OP_SetFsmString) { v->t = T_STR; v->s = a_str(f, ARG(3)); }
            else { v->i = a_i(f, ARG(3)); v->f[0] = (float)v->i; }
            (void)src;
        }
        return !ac->every;
    }
    case OP_GetFsmBool: case OP_GetFsmFloat: {
        Val *v = fsm_var(fsm_on(a_node(f, ARG(0)), a_str(f, ARG(1))), a_str(f, ARG(2)));
        if (v) { if (ac->op == OP_GetFsmFloat) set_f(f, ARG(3), v->t == T_FLOAT ? v->f[0] : v->i); else set_i(f, ARG(3), v->i); }
        return !ac->every;
    }
    case OP_SetBoolValueAtTime:
        if (enter) { rt->t = 0; set_i(f, ARG(0), !a_i(f, ARG(1))); return false; }
        rt->t += s_dt;
        if (rt->t >= a_f(f, ARG(2))) { set_i(f, ARG(0), a_i(f, ARG(1))); return true; }
        return false;
    case OP_SetVector2XY: case OP_SetVector3XYZ: {
        Val *v = var_of(f, ARG(0));
        if (v) {
            const Val *src = deref(f, ARG(1));
            if (src->t == T_VEC) memcpy(v->f, src->f, sizeof v->f);
            v->t = T_VEC;
            if (!a_none(f, ARG(2))) v->f[0] = a_f(f, ARG(2));
            if (!a_none(f, ARG(3))) v->f[1] = a_f(f, ARG(3));
        }
        return !ac->every;
    }
    case OP_GetVector2XY: case OP_GetVector3XYZ: {
        const Val *v = deref(f, ARG(0));
        if (v->t == T_VEC) { set_f(f, ARG(1), v->f[0]); set_f(f, ARG(2), v->f[1]); }
        return !ac->every;
    }
    case OP_DamageHeroDirectly:
        if (s_hero) { float x, y; node_pos(f->owner, &x, &y); hero_hurt(s_hero, a_i(f, ARG(1)), x); }
        return true;
    case OP_ReceivedDamage: return false;          // fsm_strike looks for it
    case OP_WaitForHeroInPosition: send_self(f, ARG(0)); return true;
    case OP_FinishFSM: f->stopped = true; return true;
    case OP_PlayAudioEvent: case OP_AudioPlayerOneShotSingle: case OP_AudioPlaySimple:
        play_sfx(f, ARG(0), a_none(f, ARG(1)) ? 1 : a_f(f, ARG(1)), a_node(f, ARG(2)));
        return true;
    case OP_AudioPlayRandom: play_sfx(f, pick(f, ARG(0), ARG(1)), 1, a_node(f, ARG(2))); return true;
    case OP_PlayRandomAudioClipTable: case OP_PlayRandomAudioClipTableV2: case OP_AudioPlayRandomVoiceFromTable:
    case OP_AudioPlayRandomVoiceFromTableV2: {
        static const Val none = {0};
        play_sfx(f, pick(f, ARG(0), &none), 1, a_node(f, ARG(1)));
        return true;
    }
    case OP_AudioPlayerOneShot:
        play_sfx(f, pick(f, ARG(0), ARG(1)), a_none(f, ARG(3)) ? 1 : a_f(f, ARG(3)), a_node(f, ARG(2)));
        return true;
    case OP_RunDialogue: case OP_RunDialogueV2: case OP_RunDialogueV3: case OP_RunDialogueV4:
        // the text box; the FSM gets CONVO_END when it closes
        if (enter) {
            dlg_open(str_at(a_str(f, ARG(0))), str_at(a_str(f, ARG(1))), convo_end, f);
            dlg_on_line(line_end);
            return false;
        }
        return !dlg_active();
    default: {
        bool handled;
        bool r = run_enemy(f, ac, rt, enter, &handled);
        return handled ? r : true;
    }
    }
#undef ARG
}

static void enter_state(Fsm *f, int s) {
    if (autotest_on() && f->cur != s) {
        char msg[160];
        snprintf(msg, sizeof msg, "fsm %s/%s -> %s", str_at(s_nodes[f->owner].name), str_at(f->name),
                 str_at(f->states[s].name));
        autotest_note(msg);
    }
    if (f->sub >= 0 && f->sub < s_nfsm) {   // RunFSM's FSM stops when its state is left
        s_fsm[f->sub].running = false;
        s_fsm[f->sub].stopped = true;
        f->sub = -1;
    }
    f->cur = s;
    f->finished_sent = false;
    State *st = &f->states[s];
    memset(f->rt, 0, sizeof(ActRt) * (f->maxact + 1));
    for (int a = 0; a < st->nact && f->next < 0; a++) {
        if (run(f, &st->act[a], &f->rt[a], true) && !st->act[a].every) f->rt[a].done = 1;
    }
}

// transitions waiting, a few in a row at most (FSMs that loop through states in one frame)
static void settle(Fsm *f) {
    for (int g = 0; g < 24 && f->next >= 0; g++) {
        int s = f->next;
        f->next = -1;
        enter_state(f, s);
        if (f->next < 0 && f->cur >= 0) {
            State *st = &f->states[f->cur];
            bool all = true;
            for (int a = 0; a < st->nact; a++) if (!f->rt[a].done) all = false;
            if (all && !f->finished_sent) { f->finished_sent = true; fsm_event(f, s_ev_finished); }
        }
    }
}

void fsm_update(float dt, Hero *h, Objects *os, const Room *r) {
    if (!s_nfsm && !s_nnodes) return;
    s_dt = dt;
    s_hero = h;
    s_os = os;
    s_room = r;
    // delayed events
    for (int i = 0; i < s_ndelayed;) {
        Delayed *d = &s_delayed[i];
        d->t -= dt;
        if (d->t > 0) { i++; continue; }
        if (d->fsm >= 0 && d->fsm < s_nfsm) fsm_event(&s_fsm[d->fsm], d->ev);
        else if (d->fsm == -2) for (int k = 0; k < s_nfsm; k++) fsm_event(&s_fsm[k], d->ev);
        else if (d->node >= 0) node_event(d->node, d->fsm_name, d->ev);
        *d = s_delayed[--s_ndelayed];
    }
    physics(dt);
    spawn_update(dt);
    for (int k = 0; k < s_nfsm; k++) {
        Fsm *f = &s_fsm[k];
        if (f->stopped || !fsm_node_active(f->owner) || (f->host >= 0 && !f->running)) continue;
        if (!f->started) { f->started = true; f->next = f->start < f->nstates ? f->start : -1; settle(f); continue; }
        if (f->cur < 0) continue;
        State *st = &f->states[f->cur];
        for (int a = 0; a < st->nact && f->next < 0; a++) {
            if (f->rt[a].done) continue;
            if (run(f, &st->act[a], &f->rt[a], false) && !st->act[a].every) f->rt[a].done = 1;
        }
        if (f->next < 0) {
            bool all = true;
            for (int a = 0; a < st->nact; a++) if (!f->rt[a].done) all = false;
            if (all && !f->finished_sent) { f->finished_sent = true; fsm_event(f, s_ev_finished); }
        }
        settle(f);
    }
    // events sent to FSMs already updated this frame
    for (int k = 0; k < s_nfsm; k++) if (s_fsm[k].next >= 0 && s_fsm[k].started) settle(&s_fsm[k]);
}

static bool interact(float x0, float y0, float x1, float y1, bool send) {
    static int ev = -2;
    if (ev == -2 || ev >= s_nstr) ev = str_find("INTERACT");
    if (ev < 0) return false;
    for (int k = 0; k < s_nfsm; k++) {
        Fsm *f = &s_fsm[k];
        if (f->stopped || !f->started || f->cur < 0 || !fsm_node_active(f->owner)) continue;
        State *st = &f->states[f->cur];
        bool listens = false;
        for (int i = 0; i < st->ntr; i++) if (st->tr[i * 2] == ev) listens = true;
        if (!listens) continue;
        float nx, ny;
        node_pos(f->owner, &nx, &ny);
        Obj *o = node_obj(f->owner);
        bool near = o && o->rw > 0 ? rects_overlap(x0 - 1, y0 - 1, x1 - x0 + 2, y1 - y0 + 2, o->rx, o->ry, o->rw, o->rh)
                                   : fabsf(nx - (x0 + x1) * 0.5f) < 2.5f && fabsf(ny - (y0 + y1) * 0.5f) < 3;
        if (!near) continue;
        if (send) { fsm_event(f, ev); settle(f); }
        return true;
    }
    return false;
}
bool fsm_interact(float x0, float y0, float x1, float y1) { return interact(x0, y0, x1, y1, true); }
bool fsm_can_interact(float x0, float y0, float x1, float y1) { return interact(x0, y0, x1, y1, false); }

void fsm_node_send(int node, const char *ev) {
    if (node < 0 || node >= s_nnodes) return;
    int e = str_find(ev);
    if (e < 0) return;
    for (int k = 0; k < s_nfsm; k++)
        if (s_fsm[k].owner == node && s_fsm[k].started && !s_fsm[k].stopped) { fsm_event(&s_fsm[k], e); settle(&s_fsm[k]); }
}

bool fsm_solid(float x0, float y0, float x1, float y1) {
    for (int i = 0; i < s_nsolids; i++) {
        const Solid *so = &s_solids[i];
        if (so->node < 0 || s_nodes[so->node].nocoll || !fsm_node_active(so->node)) continue;
        float nx, ny;
        node_pos(so->node, &nx, &ny);
        if (x1 > nx + so->x0 && x0 < nx + so->x1 && y1 > ny + so->y0 && y0 < ny + so->y1) return true;
    }
    return false;
}

bool fsm_strike(float x0, float y0, float x1, float y1) {
    bool any = false;
    static int ev_hit = -2, ev_dmg, ev_needle;
    if (ev_hit == -2 || !s_nstr) { ev_hit = str_find("HIT"); ev_dmg = str_find("TAKE DAMAGE"); ev_needle = str_find("NEEDLE"); }
    for (int k = 0; k < s_nfsm; k++) {
        Fsm *f = &s_fsm[k];
        if (f->stopped || !f->started || f->cur < 0 || !fsm_node_active(f->owner)) continue;
        Obj *o = node_obj(f->owner);
        if (!o || o->rw <= 0 || o->nocoll || !rects_overlap(x0, y0, x1 - x0, y1 - y0, o->rx, o->ry, o->rw, o->rh)) continue;
        State *st = &f->states[f->cur];
        int before = f->cur;
        for (int a = 0; a < st->nact; a++)
            if (st->act[a].op == OP_ReceivedDamage) send_self(f, &st->act[a].a[1]);
        if (f->next < 0) fsm_event(f, ev_needle);
        if (f->next < 0) fsm_event(f, ev_hit);
        if (f->next < 0) fsm_event(f, ev_dmg);
        if (f->next >= 0 || f->cur != before) any = true;
        settle(f);
    }
    return any;
}

int fsm_spawned(void) {   // instances live (the test log)
    int n = 0;
    for (int i = 0; i < s_ninst; i++) n += s_inst[i].live;
    return n;
}

void fsm_spawn_debug(char *out, int cap) {
    out[0] = 0;
    for (int i = 0; i < s_ninst; i++)
        if (s_inst[i].live) {
            Node *n = &s_nodes[s_inst[i].nbase];
            float x, y;
            node_pos(s_inst[i].nbase, &x, &y);
            Fsm *f = s_inst[i].fbase < s_nfsm ? &s_fsm[s_inst[i].fbase] : NULL;
            snprintf(out, cap, " s0=%.1f,%.1f v%.1f,%.1f b%d k%d c%d o%d st%s", x, y, n->vx, n->vy, n->body, n->kin,
                     n->coll_prev, n->obj, f && f->cur >= 0 ? str_at(f->states[f->cur].name) : "-");
            return;
        }
}

void fsm_node_pos(int node, float *x, float *y) { node_pos(node, x, y); }
