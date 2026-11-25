#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "fsm.h"
#include "fsm_ops.h"
#include "autotest.h"

enum { T_NONE, T_FLOAT, T_INT, T_BOOL, T_STR, T_VAR, T_NODE, T_EVENT, T_VEC, T_LIST, T_TARGET };
#define N_OWNER (-2)
#define N_HERO  (-3)
#define N_NONE  (-1)

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
} Fsm;

typedef struct {
    u16 name;
    s16 parent, obj;
    float x, y;            // nodes without an object: their own position; else the object's
    float vx, vy;
    u8 active;
    u8 nocoll;             // its collider switched off
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
static Hero *s_hero;
static u16 s_ev_finished;
static Delayed s_delayed[32];
static int s_ndelayed;
static float s_dt;

// ------------------------------------------------------------------ PlayerData

typedef struct { char name[40]; int v; u8 kind; } PD;    // kind 0 bool, 1 int
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
bool pd_bool(const char *name) { PD *p = pd_find(name, false, 0); return p && p->v; }
void pd_set_bool(const char *name, bool v) { if (name[0]) pd_find(name, true, 0)->v = v; }
int pd_int(const char *name) { PD *p = pd_find(name, false, 1); return p ? p->v : 0; }
void pd_set_int(const char *name, int v) { if (name[0]) pd_find(name, true, 1)->v = v; }
void pd_save(FILE *f) {
    for (int i = 0; i < s_npd; i++) if (s_pd[i].v) fprintf(f, "pd %d %d %s\n", s_pd[i].kind, s_pd[i].v, s_pd[i].name);
}
void pd_load_line(const char *line) {
    int kind, v, n = 0;
    if (sscanf(line, "pd %d %d %n", &kind, &v, &n) >= 2 && n > 0) {
        char name[40];
        snprintf(name, sizeof name, "%s", line + n);
        char *e = name + strlen(name);
        while (e > name && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
        pd_find(name, true, (u8)kind)->v = v;
    }
}

// ------------------------------------------------------------------ loading

static void node_pos(int node, float *x, float *y);

static const u8 *rd_val(const u8 *p, Val *v) {
    memset(v, 0, sizeof *v);
    v->t = *p++;
    switch (v->t) {
    case T_FLOAT: v->f[0] = rdf(p); v->i = (s32)v->f[0]; p += 4; break;
    case T_INT: v->i = (s32)rd32(p); v->f[0] = (float)v->i; p += 4; break;
    case T_BOOL: v->i = *p++; v->f[0] = (float)v->i; break;
    case T_STR: case T_VAR: case T_EVENT: v->s = rd16(p); p += 2; break;
    case T_NODE: v->node = rds16(p); p += 2; break;
    case T_VEC: v->f[0] = rdf(p); v->f[1] = rdf(p + 4); v->f[2] = rdf(p + 8); p += 12; break;
    case T_LIST:
        v->n = *p++;
        v->list = calloc(v->n + 1, sizeof(Val));
        for (int i = 0; i < v->n; i++) p = rd_val(p, &v->list[i]);
        break;
    case T_TARGET: v->n = *p++; v->node = rds16(p); v->s = rd16(p + 2); p += 4; break;
    }
    return p;
}

static void free_val(Val *v) {
    if (v->t == T_LIST && v->list) {
        for (int i = 0; i < v->n; i++) free_val(&v->list[i]);
        free(v->list);
    }
}

void fsm_free(void) {
    for (int k = 0; k < s_nfsm; k++) {
        Fsm *f = &s_fsm[k];
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
        free(f->vars);
        free(f->var_names);
        free(f->glob);
        free(f->rt);
    }
    free(s_fsm); s_fsm = NULL; s_nfsm = 0;
    free(s_solids); s_solids = NULL; s_nsolids = 0;
    free(s_nodes); s_nodes = NULL; s_nnodes = 0;
    if (s_str) for (int i = 0; i < s_nstr; i++) free(s_str[i]);
    free(s_str); s_str = NULL; s_nstr = 0;
    free(s_data); s_data = NULL;
    s_ndelayed = 0;
    s_os = NULL;
}

static int str_find(const char *s) {
    for (int i = 0; i < s_nstr; i++) if (!strcmp(s_str[i], s)) return i;
    return -1;
}

void fsm_load(const char *scene, Objects *os) {
    fsm_free();
    s_os = os;
    for (int i = 0; i < os->n; i++) os->o[i].node = -1;
    size_t size;
    s_data = file_load(data_path("rooms/%s.ssf", scene), &size);
    if (!s_data || size < 8 || memcmp(s_data, "SSFM", 4)) { free(s_data); s_data = NULL; return; }
    const u8 *p = s_data + 6;
    s_nstr = rd16(p); p += 2;
    s_str = calloc(s_nstr + 1, sizeof(char *));
    for (int i = 0; i < s_nstr; i++) {
        int n = *p++;
        s_str[i] = malloc(n + 1);
        memcpy(s_str[i], p, n);
        s_str[i][n] = 0;
        p += n;
    }
    s_nnodes = rd16(p); p += 2;
    s_nodes = calloc(s_nnodes + 1, sizeof(Node));
    for (int i = 0; i < s_nnodes; i++, p += 18) {   // u16 name, s16 parent, s16 obj, f32 x, y, u8 active, 3 pad
        Node *n = &s_nodes[i];
        n->name = rd16(p); n->parent = rds16(p + 2); n->obj = rds16(p + 4);
        n->x = rdf(p + 6); n->y = rdf(p + 10); n->active = p[14];
        if (n->obj >= os->n) n->obj = -1;
        if (n->obj >= 0) {
            os->o[n->obj].node = i;
            // an inactive object (flag) is the inactive node itself or one of its parents: the nodes say which
            os->o[n->obj].alive = true;
        }
    }
    s_nfsm = rd16(p); p += 2;
    s_fsm = calloc(s_nfsm + 1, sizeof(Fsm));
    for (int k = 0; k < s_nfsm; k++) {
        Fsm *f = &s_fsm[k];
        f->owner = rds16(p); f->name = rd16(p + 2); f->start = rd16(p + 4); p += 6;
        f->nvars = rd16(p); p += 2;
        f->vars = calloc(f->nvars + 1, sizeof(Val));
        f->var_names = calloc(f->nvars + 1, sizeof(u16));
        for (int i = 0; i < f->nvars; i++) {
            f->var_names[i] = rd16(p); p += 2;
            p = rd_val(p, &f->vars[i]);
            if (f->vars[i].t == T_NODE && f->vars[i].node == N_OWNER) f->vars[i].node = f->owner;
        }
        f->nglob = rd16(p); p += 2;
        f->glob = malloc(4 * f->nglob + 4);
        for (int i = 0; i < f->nglob * 2; i++, p += 2) f->glob[i] = rd16(p);
        f->nstates = rd16(p); p += 2;
        f->states = calloc(f->nstates + 1, sizeof(State));
        for (int s = 0; s < f->nstates; s++) {
            State *st = &f->states[s];
            st->name = rd16(p); p += 2;
            st->ntr = rd16(p); p += 2;
            st->tr = malloc(4 * st->ntr + 4);
            for (int i = 0; i < st->ntr * 2; i++, p += 2) st->tr[i] = rd16(p);
            st->nact = rd16(p); p += 2;
            st->act = calloc(st->nact + 1, sizeof(Action));
            if (st->nact > f->maxact) f->maxact = st->nact;
            for (int a = 0; a < st->nact; a++) {
                Action *ac = &st->act[a];
                ac->op = p[0]; ac->every = p[1]; ac->nargs = p[2]; p += 3;
                ac->a = calloc(ac->nargs + 1, sizeof(Val));
                for (int j = 0; j < ac->nargs; j++) p = rd_val(p, &ac->a[j]);
            }
        }
        f->rt = calloc(f->maxact + 1, sizeof(ActRt));
        f->cur = -1;
        f->next = -1;
    }
    // the walls they control (version 2)
    if (rd16(s_data + 4) >= 2 && p + 2 <= s_data + size) {
        s_nsolids = rd16(p); p += 2;
        s_solids = calloc(s_nsolids + 1, sizeof(Solid));
        for (int i = 0; i < s_nsolids; i++, p += 18) {
            Solid *so = &s_solids[i];
            so->node = rds16(p);
            float nx = 0, ny = 0;
            if (so->node >= 0 && so->node < s_nnodes) node_pos(so->node, &nx, &ny);
            so->x0 = rdf(p + 2) - nx; so->y0 = rdf(p + 6) - ny; so->x1 = rdf(p + 10) - nx; so->y1 = rdf(p + 14) - ny;
        }
    }
    int fin = str_find("FINISHED");
    s_ev_finished = fin >= 0 ? fin : 0xFFFF;
    // enemies keep their own behaviour (enemy.c) for now: their FSMs need much more than the engine runs
    for (int k = 0; k < s_nfsm; k++) {
        Node *n = &s_nodes[s_fsm[k].owner];
        for (int nd = s_fsm[k].owner; nd >= 0; nd = s_nodes[nd].parent) {
            int o = s_nodes[nd].obj;
            if (o >= 0 && (os->o[o].flags & OF_ENEMY)) { s_fsm[k].stopped = true; break; }
        }
        (void)n;
    }
}

int fsm_count(void) { return s_nfsm; }

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
    int fname = target->t == T_TARGET && kind == 2 ? target->s : 0;
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
    Obj *o = node_obj(node);
    if (!o || !o->anim.set) return;
    anim_play_name(&o->anim, str_at(a_str(f, clip)), true);
}

// runs action a of the current state; enter: its first frame. Returns true when it is over (no more updates).
static bool run(Fsm *f, Action *ac, ActRt *rt, bool enter) {
    Val *A = ac->a;
    int n = ac->nargs;
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
    case OP_FindChild: set_node(f, ARG(2), find_child(a_node(f, ARG(0)), a_str(f, ARG(1)))); return true;
    case OP_GetParent: {
        int nd = a_node(f, ARG(0));
        set_node(f, ARG(1), nd >= 0 ? s_nodes[nd].parent : N_NONE);
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
        Obj *o = node_obj(nd);
        int ce = play ? 3 : 2;
        if (!o || !o->anim.set) { if (!enter || !play) { send_self(f, ARG(ce)); return true; } return false; }
        if (o->anim.done) { send_self(f, ARG(ce)); return true; }
        return false;
    }
    case OP_Trigger2dEvent: case OP_Trigger2dEventLayer: case OP_Collision2dEvent: {
        // only Hornet is tracked: tags other than the player's, layers other than hers, never fire
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
    case OP_PlayerDataBoolTest: send_self(f, ARG(pd_bool(str_at(a_str(f, ARG(0)))) ? 1 : 2)); return true;
    case OP_SetPlayerDataBool: pd_set_bool(str_at(a_str(f, ARG(0))), a_i(f, ARG(1)) != 0); return true;
    case OP_GetPlayerDataBool: set_i(f, ARG(1), pd_bool(str_at(a_str(f, ARG(0))))); return !ac->every;
    case OP_SetPlayerDataInt: pd_set_int(str_at(a_str(f, ARG(0))), a_i(f, ARG(1))); return true;
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
    case OP_CheckHeroPerformanceRegion: send_self(f, ARG(1)); return true;
    case OP_FinishFSM: f->stopped = true; return true;
    default: return true;                         // RunDialogue, ActivateInteractible, sounds: not yet
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

void fsm_update(float dt, Hero *h, Objects *os) {
    if (!s_nfsm && !s_nnodes) return;
    s_dt = dt;
    s_hero = h;
    s_os = os;
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
    // node velocities
    for (int i = 0; i < s_nnodes; i++) {
        Node *n = &s_nodes[i];
        if (n->vx == 0 && n->vy == 0) continue;
        float x, y;
        node_pos(i, &x, &y);
        node_set_pos(i, x + n->vx * dt, y + n->vy * dt);
    }
    for (int k = 0; k < s_nfsm; k++) {
        Fsm *f = &s_fsm[k];
        if (f->stopped || !fsm_node_active(f->owner)) continue;
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
