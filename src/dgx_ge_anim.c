#include <math.h>
#include <string.h>

#include "esp_log.h"

#include "dgx_ge_anim.h"
#include "dgx_ge_expr.h"
#include "dgx_ge_mask.h"
#include "dgx_ge_pockets.h"

/*
 * The steps of the code, a byte and what follows it, numbers little-endian:
 *
 *   FRAME     image u16, ms u16                 show a picture for a time
 *   FRAME_AT  image u16, ms u16, dx s8, dy s8   the same, the picture moved from the actor
 *   DX        s8                                move forward (back when negative)
 *   DY        s8                                move down
 *   FLIP                                        turn round
 *   SET       param u8, value s16               a param given a value
 *   VELOCITY  vx s16, vy s16                    pixels a second, x forward
 *   ACCEL     ax s16, ay s16                    pixels a second a second, x forward
 *   GOTO      offset u16                        go on there, in the same script
 *   SCRIPT    u8                                go on with that script from its start
 *   BRANCH                                      a transition waiting for it may be taken here
 *   HOLD                                        stay on the last frame
 *   SIGNAL    u8                                tell the game
 *   SOUND     u8                                a sound
 *   MASKS     u16                               the masks of collisions that are active now
 *   ADD       param u8, delta s16               a param changed, stopping at the ends of 16 bits
 *   PATH      x expr, y expr                    the place by a formula of t, seconds from now: x forward, y down
 *   VELOCITY_X, ACCEL_X  x expr, y expr         as VELOCITY, ACCEL, worked out now
 *   SET_X, ADD_X  param u8, expr                as SET, ADD, worked out now
 *   CONTACT   on u8, event u8 (0xFF none)        look for contacts with the terrain (1) or not (0); the event is sent at one
 *   CARRY     point u8, picture u16 (0xFFFF none)  what is held at a point from now
 *   RELEASE   point u8, vx expr, vy expr        let go what is held there, at that velocity (x forward)
 *   TELL      whom u8 (0 the holder), event u8  an event for another, through the game (a pot drunk tells the one drinking)
 *   SHOOT     point u8, kind u16, vx expr, vy expr  a shot of a kind from a point, at that velocity (x forward)
 *   STOW      point u8                          what is held there put into the pockets
 *   DRAW      point u8, kind u16                one of a kind out of the pockets into the hand there
 *   GOTO_IF   script u8 (0 a label), offset u16, expr  go there when the expression is not 0 (a script: its number in the offset)
 *   KEEP                                        the world keeps the actor as it is in this script (until it enters another)
 *   FRAME_X   ms u16, image expr                as FRAME, the picture worked out now (so many lying: amount)
 *   CONTACT_MASK  mask u8, from u8 (DGX_GE_TOUCH_* bits, 32 front, 64 back; 0: watched no more), reach u8, on u8, off u8 (0xFF none)
 *                                               a mask watched for touching anything solid on those sides: the events at the moments
 *   CONTACT_FIRST as CONTACT_MASK, then first u8: begun with nothing within so many points on those sides, the off event at once
 *   HITS      count u8, then each: mask u8, on u16, damage s16, event u8, recoil u8 (0xFF none)
 *                                               what masks do when they meet others, until the next MASKS or HITS
 *
 * An expression: its length u8, then steps for a stack of floats: NUM f32,
 * PARAM u8 (a param of the actor), T (the time of the path), VX VY (its
 * velocity now, points a second), the operators
 * + - * / and NEG, and MIN MAX ABS SIN COS SQRT EXP.
 */
enum {
    OP_FRAME = 1,
    OP_FRAME_AT,
    OP_DX,
    OP_DY,
    OP_FLIP,
    OP_SET,
    OP_VELOCITY,
    OP_ACCEL,
    OP_GOTO,
    OP_SCRIPT,
    OP_BRANCH,
    OP_HOLD,
    OP_SIGNAL,
    OP_SOUND,
    OP_MASKS,
    OP_ADD,
    OP_PATH,
    OP_VELOCITY_X,
    OP_ACCEL_X,
    OP_SET_X,
    OP_ADD_X,
    OP_CONTACT,
    OP_HITS,
    OP_CARRY,
    OP_RELEASE,
    OP_TELL,
    OP_SHOOT,
    OP_STOW,
    OP_DRAW,
    OP_GOTO_IF,
    OP_KEEP,
    OP_FRAME_X,
    OP_TIMER,
    OP_CELL,  /* event u8, cell expr, value expr: a cell of the map told an event */
    OP_PRESS, /* cell expr: a button of the map pressed */
    OP_CLEAR, /* cell expr: what lies in a cell of the map gone */
    OP_APPEAR, /* kind u16, event u8, left u8, x expr, y expr: an actor of a kind comes where the player is */
    OP_VANISH, /* the actor is gone from the game */
    OP_CONTACT_MASK,
    OP_SENSE, /* flags u8 (1: cells, 2: seen, 4: the radius an expr after where), kind u16, radius u16, where expr (length 0: none), params of d, dx, dy u8 (0xFF none), copies u8, then each
                 param u8, expr: the nearest one a query asks for */
    OP_STEER, /* way u8 (STEER_*), the param of the angle u8 (wander; 0xFF none), count u8, then so many exprs: the velocity steered */
    OP_APPEAR_WITH, /* as OP_APPEAR, then count u8 and so many param u8 (of the kind appearing), expr: given to it */
    OP_CONTACT_FIRST, /* as OP_CONTACT_MASK, then first u8 */
};

/* The ways of steer; its exprs: speed, force (the most the velocity changes at one steer), then seek, flee: dx, dy; arrive: the radius of
   slowing, dx, dy; pursue, evade: dx, dy, its vx, vy; wander: the spread of the angle, degrees */
enum { STEER_SEEK, STEER_FLEE, STEER_ARRIVE, STEER_PURSUE, STEER_EVADE, STEER_WANDER };
#define STEER_EXPRS 6

/* The numbers of the step being said (tell cell, press, clear, appear): read by the game in its say, through dgx_ge_anim_said. */
static int32_t said_args[4];

/* The params of an appear ... with being said: where they are in the code, how many, who works them out; NULL outside its say. */
static const uint8_t        *said_with;
static int                   said_with_count;
static const dgx_ge_actor_t *said_by;

static float eval(const dgx_ge_actor_t *a, const uint8_t *p, float t);

int dgx_ge_anim_said_params(dgx_ge_actor_t *child)
{
    if (!said_with || !child || !child->anim) return 0;
    int            n = 0;
    const uint8_t *p = said_with;
    for (int k = 0; k < said_with_count; ++k, p += 2 + p[1]) {
        if (p[0] >= child->anim->params) continue;
        const float   v = eval(said_by, p + 1, 0);
        const int32_t w = (int32_t)lroundf(v);
        child->params[p[0]] = child->entered_params[p[0]] = (int16_t)(w < INT16_MIN ? INT16_MIN : w > INT16_MAX ? INT16_MAX : w);
        ++n;
    }
    return n;
}

int32_t dgx_ge_anim_said(int i)
{
    return i >= 0 && i < 4 ? said_args[i] : 0;
}
#define HIT_BYTES 7u



#define HEADER_BYTES 20u
/* steps made without a frame before a script is taken for one that never shows one */
#define MAX_STEPS_WITHOUT_FRAME 256

static inline uint16_t u16_at(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static inline uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool dgx_ge_anim_open(dgx_ge_anim_t *anim, const void *data, size_t size)
{
    memset(anim, 0, sizeof(*anim));
    const uint8_t *p = data;
    if (!p || size < HEADER_BYTES || u32_at(p) != DGX_GE_ANIM_MAGIC || u16_at(p + 4) != DGX_GE_ANIM_VERSION) return false;
    const uint32_t scripts = u16_at(p + 6), events = u16_at(p + 8), params = u16_at(p + 10), code_size = u32_at(p + 12);
    const uint64_t need = HEADER_BYTES + 2ull * scripts + 2ull * params + 2ull * scripts * events + code_size;
    if (!scripts || scripts > 255 || params > DGX_GE_ANIM_PARAMS || need > size) return false;
    anim->base      = p;
    anim->scripts   = (uint16_t)scripts;
    anim->events    = (uint16_t)events;
    anim->params    = (uint16_t)params;
    anim->id        = 0xFFFF;
    anim->pockets   = p[16];
    anim->stack     = p[17] ? p[17] : 1;
    anim->code_size = code_size;
    anim->table     = p + HEADER_BYTES;
    anim->initial   = anim->table + 2 * scripts;
    anim->matrix    = anim->initial + 2 * params;
    anim->code      = anim->matrix + 2 * scripts * events;
    if (size > need) anim->tables = anim->code + code_size, anim->tables_size = (uint32_t)(size - need);
    for (uint32_t i = 0; i < scripts; ++i)
        if (u16_at(anim->table + 2 * i) >= code_size) return memset(anim, 0, sizeof(*anim)), false;
    return true;
}

static int count_of(const void *who, unsigned kind)
{
    const dgx_ge_actor_t *a = who;
    if (kind == 0xFFFF) return a->amount;
    return a->pockets ? dgx_ge_pockets_count(a->pockets, (int)kind) : 0;
}

/* What an expression of an actor reads: its params, its velocity now (x forward, as it is written), the time of its path, its pockets. */
static dgx_ge_expr_ctx_t ctx_of(const dgx_ge_actor_t *a, float t)
{
    return (dgx_ge_expr_ctx_t){.params      = a->params,
                               .param_count = a->anim->params,
                               .tables      = a->anim->tables,
                               .tables_size = a->anim->tables_size,
                               .t           = t,
                               .vx          = (float)(a->flip ? -a->vx : a->vx) / 256.0f,
                               .vy          = (float)a->vy / 256.0f,
                               .x           = (float)dgx_ge_actor_x(a),
                               .y           = (float)dgx_ge_actor_y(a),
                               .now_ms      = a->due_ms,
                               .count       = count_of,
                               .who         = a};
}

/* The value of an expression of an actor at the time t of its path. */
static float eval(const dgx_ge_actor_t *a, const uint8_t *p, float t)
{
    const dgx_ge_expr_ctx_t c = ctx_of(a, t);
    return dgx_ge_expr_eval(p, &c);
}

float dgx_ge_actor_eval(const dgx_ge_actor_t *actor, const uint8_t *expr)
{
    return eval(actor, expr, 0);
}

/* Where a path puts the actor at a moment, and how fast it goes there: 1/256 of a point, and of a point a second. */
static void path_at(const dgx_ge_actor_t *a, uint32_t ms, int32_t *x, int32_t *y, int32_t *vx, int32_t *vy)
{
    const uint8_t *px = a->anim->code + a->path, *py = px + 1 + px[0];
    const float    t = (float)(int32_t)(ms - a->path_ms) / 1000.0f, h = 0.001f;
    /* the four of one actor at one moment: what they read made once, the time changed */
    dgx_ge_expr_ctx_t c  = ctx_of(a, t);
    const float       fx = dgx_ge_expr_eval(px, &c), fy = dgx_ge_expr_eval(py, &c);
    c.t                  = t + h;
    const float gx = dgx_ge_expr_eval(px, &c), gy = dgx_ge_expr_eval(py, &c);
    *x  = a->path_x + a->path_sign * (int32_t)lroundf(fx * 256.0f);
    *y  = a->path_y + (int32_t)lroundf(fy * 256.0f);
    *vx = a->path_sign * (int32_t)lroundf((gx - fx) / h * 256.0f);
    *vy = (int32_t)lroundf((gy - fy) / h * 256.0f);
}

/* n / d to the nearest (a half away from zero), d > 0: the motion is counted in steps, cut short it would lag a little at each */
static int32_t nearest(int64_t n, int64_t d)
{
    return (int32_t)(n >= 0 ? (n + d / 2) / d : -((-n + d / 2) / d));
}

/* the way of a uniform acceleration over dt ms, counted in 64 bits: v dt + a dt^2 / 2 */
static int32_t way(int32_t v, int32_t acc, int32_t dt)
{
    return nearest((int64_t)v * dt * 2000 + (int64_t)acc * dt * dt, 2000000);
}

/* what an acceleration adds to the velocity over dt ms */
static int32_t gain(int32_t acc, int32_t dt)
{
    return nearest((int64_t)acc * dt, 1000);
}

/* Bring the motion up to a moment. */
static void move_to(dgx_ge_actor_t *a, uint32_t ms)
{
    const int32_t dt = (int32_t)(ms - a->moved_ms);
    if (dt <= 0) return;
    if (a->path) {
        path_at(a, ms, &a->x, &a->y, &a->vx, &a->vy);
        a->moved_ms = ms;
        return;
    }
    a->x += way(a->vx, a->ax, dt);
    a->y += way(a->vy, a->ay, dt);
    a->vx += gain(a->ax, dt);
    a->vy += gain(a->ay, dt);
    a->moved_ms = ms;
}

/* The slot of what is held at a point (a frame or a thing), -1 none. */
static int slot_of(const dgx_ge_actor_t *a, int point)
{
    for (int i = 0; i < DGX_GE_ANIM_CARRIED; ++i)
        if ((a->carried[i] >= 0 || a->items[i]) && a->carried_at[i] == point) return i;
    return -1;
}

/* Into a script at the moment due_ms, where it is now. */
static void enter(dgx_ge_actor_t *a, int script)
{
    a->entered_ms   = a->due_ms;
    a->entered_x    = a->x;
    a->entered_y    = a->y;
    a->entered_flip = a->flip;
    memcpy(a->entered_params, a->params, sizeof(a->params));
    a->script  = (uint8_t)script;
    a->pc      = u16_at(a->anim->table + 2 * script);
    a->pending = 0;
    a->holding = false;
    a->keep    = false; /* kept is a state: a pot lying is, its shards are not */
    a->hits    = 0;     /* a hit is of the move it was written in */
}

/* Has a transition waited for this place? Then the actor is in its script now. */
static bool take_pending(dgx_ge_actor_t *a, bool at_end)
{
    if (!a->pending || (!at_end && a->pending_when != DGX_GE_WHEN_BRANCH)) return false;
    enter(a, a->pending - 1);
    return true;
}

/* A param given a value at the moment of the step, kept within 16 bits; the game is told when it has changed. */
static void set_param(dgx_ge_actor_t *a, int i, int32_t v, dgx_ge_anim_say_fn say, void *ctx)
{
    const int16_t was = a->params[i];
    a->params[i]      = (int16_t)(v < INT16_MIN ? INT16_MIN : v > INT16_MAX ? INT16_MAX : v);
    if (say && a->params[i] != was) say(a, DGX_GE_ANIM_PARAM, (uint8_t)i, a->due_ms, ctx);
}

/* The one a sense found, as its. reads it: its velocity x forward of the one looking. */
static dgx_ge_its_t its_of(const dgx_ge_actor_t *looking, const dgx_ge_sensed_t *f, bool cells)
{
    dgx_ge_its_t w = {0};
    if (cells) {
        w.tile = f->tile, w.mod = f->mod, w.cell = f->cell;
    } else if (f->actor) {
        const dgx_ge_actor_t *o = f->actor;
        w.params = o->params, w.count = o->anim ? o->anim->params : 0;
        w.vx = (float)(looking->flip ? -o->vx : o->vx) / 256.0f, w.vy = (float)o->vy / 256.0f;
        w.image = o->image, w.script = o->script, w.flip = o->flip;
    }
    return w;
}

/* Does the one looking take it: where of its own, its. the one found (NULL where: yes)? */
static bool sees(const dgx_ge_actor_t *looking, const uint8_t *where, const dgx_ge_sensed_t *f, bool cells)
{
    if (!where) return true;
    const dgx_ge_its_t  w   = its_of(looking, f, cells);
    const dgx_ge_its_t *was = dgx_ge_expr_its;
    dgx_ge_expr_its         = &w;
    const bool yes          = dgx_ge_actor_eval(looking, where) != 0;
    dgx_ge_expr_its         = was;
    return yes;
}

bool dgx_ge_actor_sees(const dgx_ge_actor_t *looking, const uint8_t *where, const dgx_ge_sensed_t *found, bool cells)
{
    return sees(looking, where, found, cells);
}

typedef struct {
    const dgx_ge_collisions_t *c;
    const dgx_ge_actor_t      *looking;
    const dgx_ge_sense_t      *q;
} body_query_t;

static bool body_counts(int handle, const struct dgx_ge_actor *actor, void *ctx)
{
    const body_query_t *b = ctx;
    if (!actor || actor == b->looking || !actor->anim) return false;
    if (b->q->kind != 0xFFFF && actor->anim->id != b->q->kind) return false;
    /* seen: the look to it within the radius, as the terrain veils it */
    const dgx_ge_terrain_t *t = b->looking->terrain;
    if (b->q->seen && t && t->sight && t->sight(b->q->x, b->q->y, dgx_ge_actor_x(actor), dgx_ge_actor_y(actor), b->q->radius, t->ctx) < 0) return false;
    const dgx_ge_sensed_t f = {.actor = actor};
    return sees(b->looking, b->q->where, &f, false);
}

bool dgx_ge_sense_bodies(const struct dgx_ge_collisions *c, uint16_t owner, const dgx_ge_actor_t *actor, const dgx_ge_sense_t *query,
                         dgx_ge_sensed_t *found)
{
    body_query_t b = {c, actor, query};
    int          dx = 0, dy = 0;
    const int    h  = dgx_ge_collisions_nearest(c, query->x, query->y, query->radius, owner, body_counts, &b, &dx, &dy);
    if (h < 0) return false;
    memset(found, 0, sizeof(*found));
    found->dx = dx, found->dy = dy, found->actor = dgx_ge_collisions_actor_of(c, h);
    return true;
}

/* A vector no longer than most. */
static void no_longer(float *x, float *y, float most)
{
    const float l = sqrtf(*x * *x + *y * *y);
    if (l > most) *x = l > 0 ? *x * most / l : 0, *y = l > 0 ? *y * most / l : 0;
}

/*
 * Steering (Reynolds): the velocity wanted by the way, the change to it no larger than the force, the velocity no faster than the speed;
 * x forward. The velocity holds till the next step: the acceleration 0, a path left.
 */
static void steer(dgx_ge_actor_t *a, unsigned way, unsigned angle, const float *v, dgx_ge_anim_say_fn say, void *ctx)
{
    const int   sign  = a->flip ? -1 : 1;
    const float speed = v[0] > 0 ? v[0] : 0, force = v[1] > 0 ? v[1] : 0;
    float       vx = (float)(sign * a->vx) / 256.0f, vy = (float)a->vy / 256.0f, wx = 0, wy = 0;
    if (way == STEER_WANDER) {
        /* the angle moves by a cast of the dice within the spread, both ways */
        int       deg    = angle < a->anim->params ? a->params[angle] : 0;
        const int spread = v[2] > 0 ? (int)lroundf(v[2]) : 0;
        if (spread && dgx_ge_dice) {
            *dgx_ge_dice = *dgx_ge_dice * 214013u + 2531011u;
            deg += (int)((*dgx_ge_dice >> 16) % (uint32_t)(2 * spread + 1)) - spread;
        }
        deg = ((deg + 180) % 360 + 360) % 360 - 180;
        if (angle < a->anim->params) set_param(a, angle, deg, say, ctx);
        const float rad = (float)deg * 0.017453293f; /* pi / 180 */
        wx = cosf(rad) * speed, wy = sinf(rad) * speed;
    } else {
        const bool arrive = way == STEER_ARRIVE, ahead = way == STEER_PURSUE || way == STEER_EVADE;
        float      tx = v[arrive ? 3 : 2], ty = v[arrive ? 4 : 3];
        if (ahead) {
            /* where it will be by the time it is reached: as far ahead as the way is long */
            const float t = speed > 0 ? sqrtf(tx * tx + ty * ty) / speed : 0;
            tx += v[4] * t, ty += v[5] * t;
        }
        const float d = sqrtf(tx * tx + ty * ty);
        if (d > 0) {
            const float s = arrive && d < v[2] ? speed * d / v[2] : speed;
            const float k = (way == STEER_FLEE || way == STEER_EVADE ? -s : s) / d;
            wx = tx * k, wy = ty * k;
        }
    }
    float sx = wx - vx, sy = wy - vy;
    no_longer(&sx, &sy, force);
    vx += sx, vy += sy;
    no_longer(&vx, &vy, speed);
    a->vx = sign * (int32_t)lroundf(vx * 256.0f), a->vy = (int32_t)lroundf(vy * 256.0f);
    a->ax = a->ay = 0;
    a->path = 0;
}

/* Steps up to the next frame, made at the moment due_ms. False when the actor holds. */
/* The sides a watched mask (contacts[i]) touches anything on, the actor put at x, y: as its terrain says, of those watched (front and back by the way it faces). */
/*
 * The sides watched that the mask touches here. A mask wholly inside a solid
 * (inside, and no side found within its reach) touches it on every side: a
 * step that took it in past the side watched (fast, or a thin one) is a
 * touch all the same, found at its first ms. One partly in touches it on the
 * sides its points outside find it on (the edge it crossed), and inside.
 */
static uint8_t sides_now(const dgx_ge_actor_t *a, int32_t x, int32_t y, int i)
{
    const dgx_ge_terrain_t *w = a->terrain;
    if (!w || !w->sides) return 0;
    const uint8_t from   = a->contacts[i].from;
    unsigned      wanted = from & 31u;
    if (from & 32u) wanted |= a->flip ? DGX_GE_TOUCH_LEFT : DGX_GE_TOUCH_RIGHT; /* in front of it */
    if (from & 64u) wanted |= a->flip ? DGX_GE_TOUCH_RIGHT : DGX_GE_TOUCH_LEFT; /* behind it */
    const unsigned sides = w->sides(a, x, y, a->contacts[i].mask, a->contacts[i].reach, w->ctx);
    if (sides == DGX_GE_TOUCH_INSIDE && wanted) return DGX_GE_TOUCH_INSIDE;
    return (uint8_t)(sides & wanted);
}

/*
 * Is nothing within the first reach of a watched mask (contact ... first within m)? Then it was begun over nothing: as
 * if it had touched, so the look after tells it lost at once.
 */
static bool nothing_near(dgx_ge_actor_t *a, int i)
{
    const uint8_t reach = a->contacts[i].reach;
    a->contacts[i].reach = a->contacts[i].first;
    const uint8_t near  = sides_now(a, a->x, a->y, i);
    a->contacts[i].reach = reach;
    return !near;
}

static bool step(dgx_ge_actor_t *a, dgx_ge_anim_say_fn say, void *ctx)
{
    const dgx_ge_anim_t *anim = a->anim;
    for (int n = 0; n < MAX_STEPS_WITHOUT_FRAME; ++n) {
        if (a->pc >= anim->code_size) break;
        const uint8_t *p    = anim->code + a->pc;
        const uint32_t left = anim->code_size - a->pc;
        const int      sign = a->flip ? -1 : 1; /* forward is the way the actor faces now, after a flip in this very step too */
        switch (p[0]) {
        case OP_FRAME:
        case OP_FRAME_AT: {
            const bool at = p[0] == OP_FRAME_AT;
            if (left < (at ? 7u : 5u)) goto broken;
            a->image    = (int16_t)u16_at(p + 1);
            a->image_dx = at ? (int8_t)p[5] : 0;
            a->image_dy = at ? (int8_t)p[6] : 0;
            a->due_ms += u16_at(p + 3);
            a->pc += at ? 7 : 5;
            return true;
        }
        case OP_DX:
            if (left < 2) goto broken;
            a->x += sign * (int8_t)p[1] * 256;
            a->path_x += sign * (int8_t)p[1] * 256; /* a path goes on from the new place */
            a->pc += 2;
            break;
        case OP_DY:
            if (left < 2) goto broken;
            a->y += (int8_t)p[1] * 256;
            a->path_y += (int8_t)p[1] * 256;
            a->pc += 2;
            break;
        case OP_FLIP:
            a->flip = !a->flip;
            a->pc += 1;
            break;
        case OP_SET:
        case OP_ADD: {
            if (left < 4 || p[1] >= anim->params) goto broken;
            const int32_t v = (int16_t)u16_at(p + 2) + (p[0] == OP_ADD ? a->params[p[1]] : 0);
            a->pc += 4;
            set_param(a, p[1], v, say, ctx);
            break;
        }
        case OP_VELOCITY:
        case OP_ACCEL: {
            if (left < 5) goto broken;
            const int32_t x = sign * (int16_t)u16_at(p + 1) * 256, y = (int16_t)u16_at(p + 3) * 256;
            if (a->path) a->ax = a->ay = 0; /* out of a path: on as it went */
            a->path = 0;
            if (p[0] == OP_VELOCITY) a->vx = x, a->vy = y;
            else a->ax = x, a->ay = y;
            a->pc += 5;
            break;
        }
        case OP_VELOCITY_X:
        case OP_ACCEL_X:
        case OP_PATH: {
            if (left < 2 || 1u + 1 + p[1] >= left || 1u + 1 + p[1] + 1 + p[2 + p[1]] > left) goto broken;
            const uint8_t *ex = p + 1, *ey = ex + 1 + ex[0];
            const uint32_t size = 1 + 1 + ex[0] + 1 + ey[0];
            if (p[0] == OP_PATH) {
                a->path      = (uint16_t)(a->pc + 1); /* its x, right after the op */
                a->path_sign = (int8_t)sign;
                a->path_ms   = a->due_ms;
                a->path_x    = a->x;
                a->path_y    = a->y;
                a->ax = a->ay = 0;
            } else {
                const int32_t x = sign * (int32_t)lroundf(eval(a, ex, 0) * 256.0f), y = (int32_t)lroundf(eval(a, ey, 0) * 256.0f);
                if (a->path) a->ax = a->ay = 0;
                a->path = 0;
                if (p[0] == OP_VELOCITY_X) a->vx = x, a->vy = y;
                else a->ax = x, a->ay = y;
            }
            a->pc += size;
            break;
        }
        case OP_SET_X:
        case OP_ADD_X: {
            if (left < 3 || p[1] >= anim->params || 2u + 1 + p[2] > left) goto broken;
            const float   value = eval(a, p + 2, 0);
            const int32_t v = (int32_t)lroundf(value) + (p[0] == OP_ADD_X ? a->params[p[1]] : 0);
            a->pc += 2 + 1 + p[2];
            set_param(a, p[1], v, say, ctx);
            break;
        }
        case OP_BRANCH:
            a->pc += 1;
            take_pending(a, false);
            break;
        case OP_GOTO:
            if (left < 3) goto broken;
            if (!take_pending(a, true)) a->pc = u16_at(p + 1);
            break;
        case OP_SCRIPT:
            if (left < 2 || p[1] >= anim->scripts) goto broken;
            if (!take_pending(a, true)) enter(a, p[1]);
            break;
        case OP_HOLD:
            if (take_pending(a, true)) break;
            a->holding = true;
            return false;
        case OP_SIGNAL:
        case OP_SOUND:
            if (left < 2) goto broken;
            a->pc += 2;
            if (say) say(a, p[0] == OP_SIGNAL ? DGX_GE_ANIM_SIGNAL : DGX_GE_ANIM_SOUND, p[1], a->due_ms, ctx);
            break;
        case OP_CONTACT_MASK:
        case OP_CONTACT_FIRST: {
            const unsigned size = p[0] == OP_CONTACT_FIRST ? 7u : 6u;
            if (left < size) goto broken;
            const uint8_t mask = p[1], from = p[2], reach = p[3], on = p[4], off = p[5], first = p[0] == OP_CONTACT_FIRST ? p[6] : 0;
            a->pc += (uint16_t)size;
            int slot = -1, spare = -1;
            for (int i = 0; i < DGX_GE_ANIM_CONTACTS; ++i) {
                if (a->contacts[i].mask == mask) slot = i;
                else if (a->contacts[i].mask == 0xFF && spare < 0) spare = i;
            }
            if (!from) {
                if (slot >= 0) a->contacts[slot].mask = 0xFF;
                break;
            }
            /* the same mask watched on the same sides as before (its events changed, a frame on): what it touched at the last look stands */
            const bool same = slot >= 0 && a->contacts[slot].from == from && a->contacts[slot].reach == reach;
            if (slot < 0) slot = spare;
            if (slot < 0) {
                static bool told;
                if (!told) ESP_LOGE("dgx-ge", "contact: a script watches more than %d masks at once", DGX_GE_ANIM_CONTACTS);
                told = true;
                break;
            }
            a->contacts[slot].mask = mask, a->contacts[slot].from = from, a->contacts[slot].reach = reach;
            a->contacts[slot].on = on, a->contacts[slot].off = off, a->contacts[slot].first = first;
            /* begun before the terrain was given (the game gives it after the start): the first look with one is its start, no event */
            if (!same) a->contacts[slot].was = a->terrain && a->terrain->sides ? sides_now(a, a->x, a->y, slot) : 0xFF;
            if (!same && first && a->contacts[slot].was == 0 && nothing_near(a, slot)) a->contacts[slot].was = 0x80;
            break;
        }
        case OP_CONTACT:
            if (left < 3) goto broken;
            a->contact       = p[1] != 0;
            a->contact_event = p[2];
            a->touching  = a->contact && a->terrain && a->terrain->touches && a->terrain->touches(a, a->x, a->y, &(dgx_ge_contact_t){0}, a->terrain->ctx);
            a->looked_ms = a->due_ms;
            a->pc += 3;
            break;
        case OP_MASKS:
            if (left < 3) goto broken;
            a->masks = u16_at(p + 1);
            a->hits  = 0;
            a->pc += 3;
            break;
        case OP_CARRY:
            if (left < 4) goto broken;
            dgx_ge_actor_carry(a, p[1], u16_at(p + 2) == 0xFFFF ? -1 : (int16_t)u16_at(p + 2));
            a->pc += 4;
            break;
        case OP_RELEASE: {
            if (left < 3 || left < 4u + p[2] || left < 4u + p[2] + p[3 + p[2]]) goto broken;
            const uint8_t *ex = p + 2, *ey = ex + 1 + ex[0];
            const int      slot = slot_of(a, p[1]);
            a->pc += (uint16_t)(2 + 1 + ex[0] + 1 + ey[0]);
            if (slot < 0) break; /* nothing held there: nothing let go */
            dgx_ge_actor_t *thing = a->items[slot];
            a->released.image = thing ? (int16_t)dgx_ge_held_image(thing) : a->carried[slot];
            a->released.vx   = (int16_t)(sign * lroundf(eval(a, ex, 0)));
            a->released.vy   = (int16_t)lroundf(eval(a, ey, 0));
            a->carried[slot] = -1;
            if (thing) {
                /* free, where it is, thrown at this moment */
                a->items[slot] = NULL;
                thing->holder  = NULL;
                dgx_ge_actor_restart(thing, a->due_ms, thing->x, thing->y, a->released.vx * 256, a->released.vy * 256);
            }
            if (say) say(a, DGX_GE_ANIM_RELEASE, p[1], a->due_ms, ctx);
            break;
        }
        case OP_SHOOT: {
            if (left < 5 || left < 6u + p[4] || left < 6u + p[4] + p[5 + p[4]]) goto broken;
            const uint8_t *ex = p + 4, *ey = ex + 1 + ex[0];
            a->shot.kind = u16_at(p + 2);
            a->shot.vx   = (int16_t)(sign * lroundf(eval(a, ex, 0)));
            a->shot.vy   = (int16_t)lroundf(eval(a, ey, 0));
            a->pc += (uint16_t)(4 + 1 + ex[0] + 1 + ey[0]);
            /* one with pockets shoots what it has: one of the kind out of them, none there, none shot */
            if (a->pockets) {
                const int place = dgx_ge_pockets_find(a->pockets, a->shot.kind);
                if (place < 0) break;
                dgx_ge_pockets_take(a->pockets, place, 1, NULL);
            }
            if (say) say(a, DGX_GE_ANIM_SHOOT, p[1], a->due_ms, ctx);
            break;
        }
        case OP_STOW:
            if (left < 2) goto broken;
            a->pc += 2;
            dgx_ge_pockets_stow(a, p[1]);
            break;
        case OP_DRAW:
            if (left < 4) goto broken;
            a->pc += 4;
            dgx_ge_pockets_draw(a, p[1], u16_at(p + 2), a->due_ms);
            break;
        case OP_FRAME_X: {
            if (left < 4 || left < 4u + p[3]) goto broken;
            const long image = lroundf(eval(a, p + 3, 0));
            a->image    = (int16_t)(image < 0 ? -1 : image > INT16_MAX ? INT16_MAX : image);
            a->image_dx = a->image_dy = 0;
            a->due_ms += u16_at(p + 1);
            a->pc += (uint16_t)(3 + 1 + p[3]);
            return true;
        }
        case OP_KEEP:
            a->keep = true;
            a->pc += 1;
            break;
        case OP_GOTO_IF: {
            if (left < 5 || left < 5u + p[4] || (p[1] && u16_at(p + 2) >= anim->scripts)) goto broken;
            if (eval(a, p + 4, 0) == 0) {
                a->pc += (uint16_t)(4 + 1 + p[4]);
                break;
            }
            /* as goto: a transition waiting for the end is taken here instead */
            if (take_pending(a, true)) break;
            if (p[1]) enter(a, u16_at(p + 2));
            else a->pc = u16_at(p + 2);
            break;
        }
        case OP_TIMER:
            if (left < 3 || p[1] > 3) goto broken;
            a->pc += 3;
            if (say) say(a, (dgx_ge_anim_say_t)(DGX_GE_ANIM_TIMER_START + p[1]), p[2], a->due_ms, ctx);
            break;
        case OP_TELL:
            if (left < 3) goto broken;
            a->pc += 3;
            if (say && p[2] != 0xFF) say(a, DGX_GE_ANIM_TELL, p[2], a->due_ms, ctx);
            break;
        case OP_CELL: {
            if (left < 3 || 3u + p[2] >= left || 3u + p[2] + 1 + p[3 + p[2]] > left) goto broken;
            const uint8_t *cell = p + 2, *value = cell + 1 + cell[0];
            said_args[0]         = (int32_t)lroundf(eval(a, cell, 0));
            said_args[1]         = (int32_t)lroundf(eval(a, value, 0));
            a->pc += (uint16_t)(2 + 1 + cell[0] + 1 + value[0]);
            if (say && p[1] != 0xFF) say(a, DGX_GE_ANIM_CELL, p[1], a->due_ms, ctx);
            break;
        }
        case OP_APPEAR: {
            if (left < 6 || 6u + p[5] >= left || 6u + p[5] + 1 + p[6 + p[5]] > left) goto broken;
            const uint8_t *x = p + 5, *y = x + 1 + x[0];
            said_args[0] = (int32_t)lroundf(eval(a, x, 0)), said_args[1] = (int32_t)lroundf(eval(a, y, 0));
            said_args[2] = u16_at(p + 1), said_args[3] = p[4];
            a->pc += (uint16_t)(5 + 1 + x[0] + 1 + y[0]);
            if (say) say(a, DGX_GE_ANIM_APPEAR, p[3], a->due_ms, ctx);
            break;
        }
        case OP_SENSE: {
            if (left < 7 || 7u + p[6] + 4 > left) goto broken;
            const uint8_t *where = p + 6, *rexpr = (p[1] & 4) ? where + 1 + where[0] : NULL;
            if (rexpr && (uint32_t)(rexpr - p) + 1 + rexpr[0] + 4 > left) goto broken;
            const uint8_t *outs = rexpr ? rexpr + 1 + rexpr[0] : where + 1 + where[0];
            uint32_t       end   = (uint32_t)(outs - p) + 4;
            for (unsigned k = 0; k < outs[3]; ++k) {
                if (end + 2 > left || end + 2 + p[end + 1] > left) goto broken;
                end += 2 + p[end + 1];
            }
            const bool      cells = p[1] & 1;
            /* the radius: as written, or worked out now (hungry, it smells farther) */
            const long      r     = rexpr ? lroundf(eval(a, rexpr, 0)) : u16_at(p + 4);
            dgx_ge_sense_t  q     = {u16_at(p + 2), cells, dgx_ge_actor_x(a), dgx_ge_actor_y(a), (int)(r < 0 ? 0 : r > 65535 ? 65535 : r), where[0] ? where : NULL,
                                     (p[1] & 2) != 0};
            dgx_ge_sensed_t f;
            memset(&f, 0, sizeof(f));
            const bool found = a->terrain && a->terrain->sense && a->terrain->sense(a, &q, &f, a->terrain->ctx);
            const int  dx = found ? (a->flip ? -f.dx : f.dx) : 0, dy = found ? f.dy : 0; /* x forward */
            const int  got[3] = {found ? (int)lroundf(sqrtf((float)dx * dx + (float)dy * dy)) : -1, dx, dy};
            for (int k = 0; k < 3; ++k)
                if (outs[k] < anim->params) set_param(a, outs[k], got[k], say, ctx);
            if (found) {
                /* the copies: expressions of its own with its. the one found */
                const dgx_ge_its_t  w   = its_of(a, &f, cells);
                const dgx_ge_its_t *was = dgx_ge_expr_its;
                for (uint32_t at = (uint32_t)(outs - p) + 4; at < end; at += 2 + p[at + 1]) {
                    dgx_ge_expr_its = &w;
                    const float v   = eval(a, p + at + 1, 0);
                    dgx_ge_expr_its = was;
                    if (p[at] < anim->params) set_param(a, p[at], (int32_t)lroundf(v), say, ctx);
                }
            }
            a->pc += (uint16_t)end;
            break;
        }
        case OP_STEER: {
            if (left < 4 || p[1] > STEER_WANDER || p[3] > STEER_EXPRS) goto broken;
            float    v[STEER_EXPRS] = {0};
            uint32_t at = 4;
            for (unsigned k = 0; k < p[3]; ++k) {
                if (at >= left || at + 1 + p[at] > left) goto broken;
                v[k] = eval(a, p + at, 0);
                at += 1 + p[at];
            }
            a->pc += (uint16_t)at;
            steer(a, p[1], p[2], v, say, ctx);
            break;
        }
        case OP_APPEAR_WITH: {
            if (left < 6 || 6u + p[5] >= left || 6u + p[5] + 1 + p[6 + p[5]] >= left) goto broken;
            const uint8_t *x = p + 5, *y = x + 1 + x[0], *with = y + 1 + y[0];
            uint32_t       end = (uint32_t)(with - p) + 1;
            for (unsigned k = 0; k < with[0]; ++k) {
                if (end + 2 > left || end + 2 + p[end + 1] > left) goto broken;
                end += 2 + p[end + 1];
            }
            said_args[0] = (int32_t)lroundf(eval(a, x, 0)), said_args[1] = (int32_t)lroundf(eval(a, y, 0));
            said_args[2] = u16_at(p + 1), said_args[3] = p[4];
            a->pc += (uint16_t)end;
            said_with = with + 1, said_with_count = with[0], said_by = a;
            if (say) say(a, DGX_GE_ANIM_APPEAR, p[3], a->due_ms, ctx);
            said_with = NULL, said_with_count = 0, said_by = NULL;
            break;
        }
        case OP_VANISH:
            a->pc += 1;
            if (say) say(a, DGX_GE_ANIM_VANISH, 0, a->due_ms, ctx);
            break;
        case OP_PRESS:
        case OP_CLEAR: {
            if (left < 2 || 2u + p[1] > left) goto broken;
            said_args[0] = (int32_t)lroundf(eval(a, p + 1, 0)), said_args[1] = 0;
            a->pc += (uint16_t)(2 + p[1]);
            if (say) say(a, p[0] == OP_PRESS ? DGX_GE_ANIM_PRESS : DGX_GE_ANIM_CLEAR, 0, a->due_ms, ctx);
            break;
        }
        case OP_HITS:
            if (left < 2 || left < 2u + HIT_BYTES * p[1]) goto broken;
            a->hits = (uint16_t)(a->pc + 1);
            a->pc += (uint16_t)(2 + HIT_BYTES * p[1]);
            break;
        default: goto broken;
        }
    }
broken:
    /* a script that runs past its code or never shows a frame stops where it is */
    a->holding = true;
    return false;
}

void dgx_ge_actor_start(dgx_ge_actor_t *actor, const dgx_ge_anim_t *anim, int script, int x, int y, uint32_t now_ms)
{
    memset(actor, 0, sizeof(*actor));
    actor->anim     = anim;
    actor->image    = -1;
    actor->x        = x * 256;
    actor->y        = y * 256;
    actor->due_ms   = now_ms;
    actor->moved_ms  = now_ms;
    actor->looked_ms = now_ms;
    for (int i = 0; i < anim->params; ++i) actor->params[i] = (int16_t)u16_at(anim->initial + 2 * i);
    for (int i = 0; i < DGX_GE_ANIM_CARRIED; ++i) actor->carried[i] = -1;
    actor->amount         = 1;
    actor->released.image = -1;
    actor->contact_event  = 0xFF;
    for (int i = 0; i < DGX_GE_ANIM_CONTACTS; ++i) actor->contacts[i].mask = 0xFF;
    if (script < 0 || script >= anim->scripts) {
        actor->holding = true;
        return;
    }
    enter(actor, script);
}

/*
 * A contact as it was dealt with: the actor at that moment. Found again with
 * the actor exactly as it was (the game put it back at the moment it touched
 * and changed nothing), it would be found for ever: that is an error of the
 * game's handler, told in the log once, and the actor is left at rest where
 * it touched, not taken through. Nothing else stops a look: a ball that
 * bounces forty times on the way to now bounces forty times, each at its
 * moment.
 */
typedef struct {
    bool     any;
    uint32_t ms;
    int32_t  x, y, vx, vy;
    uint16_t pc;
    uint8_t  script;
} seen_t;

static bool seen_again(const seen_t *s, const dgx_ge_actor_t *a)
{
    return s->any && s->ms == a->moved_ms && s->x == a->x && s->y == a->y && s->vx == a->vx && s->vy == a->vy && s->pc == a->pc &&
           s->script == a->script;
}

/*
 * Look for a contact where the motion has come to. Going in: the terrain says
 * what follows; if the game put the actor back to the moment it touched, it
 * moves on from there to here and is looked at again. If the script names an
 * event for a contact, it is sent at the moment of the contact, the actor put
 * there: true, time has gone back to it.
 */
static bool look_point(dgx_ge_actor_t *a, seen_t *last, dgx_ge_anim_say_fn say, void *ctx)
{
    const dgx_ge_terrain_t *w = a->terrain;
    if (!a->contact || !w || !w->touches) {
        a->looked_ms = a->moved_ms;
        return false;
    }
    for (;;) {
        dgx_ge_contact_t c   = {0};
        const bool       in  = w->touches(a, a->x, a->y, &c, w->ctx);
        const uint32_t   now = a->moved_ms;
        if (!in || a->touching) {
            a->touching  = in;
            a->looked_ms = now;
            return false;
        }
        if (seen_again(last, a)) {
            /* the same contact once more, nothing changed by the game: at rest from the moment it touched, told once */
            static bool told;
            if (!told) ESP_LOGE("dgx-ge", "contact: the game put the actor back as it was; it rests at the surface");
            told = true;
            c.from_ms = a->looked_ms, c.to_ms = now;
            dgx_ge_contact_refine(a, &c);
            int32_t x, y, vx, vy;
            dgx_ge_actor_state_at(a, c.from_ms, &x, &y, &vx, &vy);
            dgx_ge_actor_restart(a, c.from_ms, x, y, 0, 0);
            a->ax = a->ay = 0;
            a->touching  = false;
            a->looked_ms = c.from_ms;
            move_to(a, now);
            return false;
        }
        *last       = (seen_t){true, now, a->x, a->y, a->vx, a->vy, a->pc, a->script};
        c.from_ms   = a->looked_ms;
        c.to_ms     = now;
        a->touching = true;
        if (w->contact) w->contact(a, &c, w->ctx);
        if (a->moved_ms == now && a->contact_event != 0xFF) {
            /* the script names an event for it: put where it touched, at that moment, and the matrix says what follows */
            dgx_ge_contact_refine(a, &c);
            int32_t x, y, vx, vy;
            dgx_ge_actor_state_at(a, c.to_ms, &x, &y, &vx, &vy);
            dgx_ge_actor_restart(a, c.to_ms, x, y, vx, vy);
            a->looked_ms = c.to_ms;
            dgx_ge_actor_event(a, a->contact_event, c.to_ms, say, ctx);
            return true;
        }
        if (a->moved_ms == now) {
            a->looked_ms = now;
            return false; /* left where it is */
        }
        /* put back to the moment it touched: on from there, and looked at again */
        a->touching  = false;
        a->looked_ms = a->moved_ms;
        move_to(a, now);
    }
}

/*
 * The masks watched (contact <mask> <side> on/off): one that touches now and
 * did not at the last look, or the other way round, changed somewhere
 * between; the first ms it is so is found by halving, the actor put there
 * and told the event at that moment: true, time has gone back. A change
 * with no event named is only noted.
 */
static bool look_masks(dgx_ge_actor_t *a, dgx_ge_anim_say_fn say, void *ctx)
{
    if (!a->terrain || !a->terrain->sides) return false;
    for (int i = 0; i < DGX_GE_ANIM_CONTACTS; ++i) {
        if (a->contacts[i].mask == 0xFF) continue;
        const uint8_t now = sides_now(a, a->x, a->y, i), was = a->contacts[i].was;
        a->contacts[i].was = now;
        if (was == 0xFF && !now && a->contacts[i].first && a->contacts[i].off != 0xFF && nothing_near(a, i)) {
            /* begun before the terrain was given, over nothing: lost now, at its first look with one */
            dgx_ge_actor_event(a, a->contacts[i].off, a->moved_ms, say, ctx);
            return true;
        }
        if (was == 0xFF || !now == !was) continue;
        const uint8_t event = now ? a->contacts[i].on : a->contacts[i].off;
        if (event == 0xFF) continue;
        uint32_t lo = a->looked_ms, hi = a->moved_ms;
        while ((int32_t)(hi - lo) > 1) {
            const uint32_t mid = lo + (hi - lo) / 2;
            int32_t        x, y, vx, vy;
            dgx_ge_actor_state_at(a, mid, &x, &y, &vx, &vy);
            if (!sides_now(a, x, y, i) == !now) hi = mid;
            else lo = mid;
        }
        if ((int32_t)(a->moved_ms - hi) > 0) {
            int32_t x, y, vx, vy;
            dgx_ge_actor_state_at(a, hi, &x, &y, &vx, &vy);
            dgx_ge_actor_restart(a, hi, x, y, vx, vy);
            a->looked_ms = hi;
        }
        dgx_ge_actor_event(a, event, hi, say, ctx);
        return true;
    }
    return false;
}

/* A look, where the motion has come to: the masks watched first (they need the last look as it was), then the point. */
static bool look(dgx_ge_actor_t *a, seen_t *last, dgx_ge_anim_say_fn say, void *ctx)
{
    if (look_masks(a, say, ctx)) return true;
    return look_point(a, last, say, ctx);
}

void dgx_ge_actor_state_at(const dgx_ge_actor_t *a, uint32_t ms, int32_t *x, int32_t *y, int32_t *vx, int32_t *vy)
{
    if (a->path) {
        path_at(a, ms, x, y, vx, vy);
        return;
    }
    const int32_t dt = (int32_t)(ms - a->moved_ms);
    if (dt < 0 && (int32_t)(ms - a->looked_ms) >= 0) {
        /*
         * Between the last look and now: the move from the look is undone
         * exactly (the same steps backwards), then made again as far as ms.
         * Counted back from now with a negative time it would be off by a
         * step or two of 1/256, and a point just out of a floor at the look
         * could come out as in it: a game putting the actor back there would
         * put it into the floor.
         */
        const int32_t back = (int32_t)(a->moved_ms - a->looked_ms), on = (int32_t)(ms - a->looked_ms);
        const int32_t vx0 = a->vx - gain(a->ax, back), vy0 = a->vy - gain(a->ay, back);
        const int32_t x0 = a->x - way(vx0, a->ax, back), y0 = a->y - way(vy0, a->ay, back);
        *x  = x0 + way(vx0, a->ax, on);
        *y  = y0 + way(vy0, a->ay, on);
        *vx = vx0 + gain(a->ax, on);
        *vy = vy0 + gain(a->ay, on);
        return;
    }
    *x  = a->x + way(a->vx, a->ax, dt);
    *y  = a->y + way(a->vy, a->ay, dt);
    *vx = a->vx + gain(a->ax, dt);
    *vy = a->vy + gain(a->ay, dt);
}

void dgx_ge_contact_refine(const dgx_ge_actor_t *a, dgx_ge_contact_t *c)
{
    const dgx_ge_terrain_t *w = a->terrain;
    if (!w || !w->touches) return;
    uint32_t lo = c->from_ms, hi = c->to_ms;
    while ((int32_t)(hi - lo) > 1) {
        const uint32_t   mid = lo + (hi - lo) / 2;
        int32_t          x, y, vx, vy;
        dgx_ge_contact_t probe = *c;
        dgx_ge_actor_state_at(a, mid, &x, &y, &vx, &vy);
        if (w->touches(a, x, y, &probe, w->ctx)) {
            hi = mid;
            probe.from_ms = c->from_ms, probe.to_ms = c->to_ms;
            *c = probe;
        } else {
            lo = mid;
        }
    }
    c->from_ms = lo;
    c->to_ms   = hi;
}

bool dgx_ge_terrain_ray(const dgx_ge_terrain_t *t, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int step, dgx_ge_contact_t *hit)
{
    if (!t) return false;
    if (t->ray) return t->ray(x0, y0, x1, y1, hit, t->ctx);
    if (!t->touches) return false;
    const float dx = (float)(x1 - x0), dy = (float)(y1 - y0), length = sqrtf(dx * dx + dy * dy) / 256.0f;
    const int   n  = step > 0 ? (int)ceilf(length / (float)step) : 1;
    float       clear = 0;
    for (int i = 0; i <= n; ++i) {
        const float f = n ? (float)i / (float)n : 0;
        if (!t->touches(NULL, x0 + (int32_t)lroundf(dx * f), y0 + (int32_t)lroundf(dy * f), hit, t->ctx)) {
            clear = f;
            continue;
        }
        /* between the last step clear and this one, to a point */
        float solid = f;
        while ((solid - clear) * length > 1.0f) {
            const float      mid = (clear + solid) / 2;
            dgx_ge_contact_t probe;
            if (t->touches(NULL, x0 + (int32_t)lroundf(dx * mid), y0 + (int32_t)lroundf(dy * mid), &probe, t->ctx)) {
                solid = mid;
                *hit  = probe;
            } else {
                clear = mid;
            }
        }
        return true;
    }
    return false;
}

void dgx_ge_actor_shift(dgx_ge_actor_t *a, int dx, int dy)
{
    a->x -= dx * 256, a->y -= dy * 256;
    a->path_x -= dx * 256, a->path_y -= dy * 256;
}

void dgx_ge_actor_restart(dgx_ge_actor_t *a, uint32_t at_ms, int32_t x, int32_t y, int32_t vx, int32_t vy)
{
    a->x        = x;
    a->y        = y;
    a->vx       = vx;
    a->vy       = vy;
    a->path     = 0;
    a->moved_ms = at_ms;
}

void dgx_ge_actor_advance(dgx_ge_actor_t *actor, uint32_t now_ms, dgx_ge_anim_say_fn say, void *ctx)
{
    seen_t last = {0};
    /* a contact that sends an event goes back to its moment: from there on again */
    do {
        while (!actor->holding && (int32_t)(now_ms - actor->due_ms) >= 0) {
            move_to(actor, actor->due_ms);
            if (look(actor, &last, say, ctx)) continue;
            if (!step(actor, say, ctx)) break;
        }
        move_to(actor, now_ms);
    } while (look(actor, &last, say, ctx));
}

bool dgx_ge_actor_event(dgx_ge_actor_t *actor, int event, uint32_t now_ms, dgx_ge_anim_say_fn say, void *ctx)
{
    const dgx_ge_anim_t *anim = actor->anim;
    if (!anim || event < 0 || event >= anim->events) return false;
    const uint8_t *cell = anim->matrix + 2 * ((uint32_t)actor->script * anim->events + (uint32_t)event);
    if (!cell[0] || cell[0] > anim->scripts) return false;
    if (cell[1] == DGX_GE_WHEN_NOW || actor->holding) {
        /*
         * the steps that were due before now are made first, then the actor goes on in the new script from now: a
         * step due at this very moment comes after the event (as a game's tick looks at its buttons, then steps)
         */
        dgx_ge_actor_advance(actor, now_ms - 1, say, ctx);
        actor->due_ms = now_ms;
        enter(actor, cell[0] - 1);
        dgx_ge_actor_advance(actor, now_ms, say, ctx);
        return true;
    }
    actor->pending      = cell[0];
    actor->pending_when = cell[1];
    return true;
}

bool dgx_ge_actor_hit(const dgx_ge_actor_t *a, int mask, int on_mask, dgx_ge_hit_t *hit)
{
    if (!a->hits || mask < 0 || mask > 15 || on_mask < 0 || on_mask > 15) return false;
    const uint8_t *p = a->anim->code + a->hits - 1;
    for (unsigned i = 0; i < p[1]; ++i) {
        const uint8_t *h = p + 2 + i * HIT_BYTES;
        if (h[0] != mask || !(u16_at(h + 1) & 1u << on_mask)) continue;
        *hit = (dgx_ge_hit_t){.mask = h[0], .on = u16_at(h + 1), .damage = (int16_t)u16_at(h + 3), .event = h[5] == 0xFF ? -1 : h[5],
                              .recoil = h[6] == 0xFF ? -1 : h[6]};
        return true;
    }
    return false;
}

bool dgx_ge_actor_carry(dgx_ge_actor_t *a, int point, int picture)
{
    /* the slot of this point, else a free one; none free: it is not kept */
    int slot = slot_of(a, point);
    for (int i = 0; i < DGX_GE_ANIM_CARRIED && slot < 0 && picture >= 0; ++i)
        if (a->carried[i] < 0 && !a->items[i]) slot = i;
    if (slot < 0) return picture < 0;
    a->carried[slot]    = (int16_t)picture;
    a->carried_at[slot] = (uint8_t)point;
    return true;
}

bool dgx_ge_actor_hold(dgx_ge_actor_t *a, int point, dgx_ge_actor_t *thing)
{
    int slot = slot_of(a, point);
    for (int i = 0; i < DGX_GE_ANIM_CARRIED && slot < 0; ++i)
        if (a->carried[i] < 0 && !a->items[i]) slot = i;
    if (slot < 0) return false;
    if (a->items[slot] && a->items[slot] != thing) a->items[slot]->holder = NULL;
    if (thing->holder && thing->holder != a) dgx_ge_actor_drop(thing);
    a->carried_at[slot] = (uint8_t)point;
    a->items[slot]      = thing;
    thing->holder       = a;
    thing->held_at      = (uint8_t)point;
    return true;
}

dgx_ge_actor_t *dgx_ge_actor_held(const dgx_ge_actor_t *a, int point)
{
    const int slot = slot_of(a, point);
    return slot >= 0 ? a->items[slot] : NULL;
}

void dgx_ge_actor_drop(dgx_ge_actor_t *thing)
{
    dgx_ge_actor_t *h = thing->holder;
    if (!h) return;
    for (int i = 0; i < DGX_GE_ANIM_CARRIED; ++i)
        if (h->items[i] == thing) h->items[i] = NULL;
    thing->holder = NULL;
}

int dgx_ge_held_image(const dgx_ge_actor_t *thing)
{
    const int frame = thing->holder ? dgx_ge_actor_carried(thing->holder, thing->held_at) : -1;
    return frame >= 0 ? frame : thing->image;
}

uint16_t dgx_ge_held_masks(const dgx_ge_actor_t *thing)
{
    return thing->holder ? thing->masks & thing->holder->masks : thing->masks;
}

int dgx_ge_actor_carried(const dgx_ge_actor_t *a, int point)
{
    const int slot = slot_of(a, point);
    return slot >= 0 ? a->carried[slot] : -1;
}
