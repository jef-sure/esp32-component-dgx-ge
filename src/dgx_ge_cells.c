#include <stdlib.h>
#include <string.h>

#include "dgx_ge_cells.h"
#include "dgx_ge_save.h"
#include "dgx_ge_timers.h"

#define PARAM_MOD  0
#define PARAM_TILE 1
#define PARAM_OWN  2

bool dgx_ge_cells_init(dgx_ge_cells_t *c, dgx_ge_kinds_t *kinds, int capacity, const dgx_ge_cells_io_t *io)
{
    memset(c, 0, sizeof(*c));
    c->kinds = kinds;
    c->io    = *io;
    c->lives = capacity > 0 ? calloc((size_t)capacity, sizeof(dgx_ge_life_t)) : NULL;
    c->capacity = c->lives ? capacity : 0;
    return c->lives != NULL;
}

void dgx_ge_cells_free(dgx_ge_cells_t *c)
{
    free(c->lives);
    memset(c, 0, sizeof(*c));
}

void dgx_ge_cells_clear(dgx_ge_cells_t *c)
{
    c->count = 0;
}

static int find(const dgx_ge_cells_t *c, uint16_t cell)
{
    for (int i = 0; i < c->count; ++i)
        if (c->lives[i].cell == cell) return i;
    return -1;
}

bool dgx_ge_cells_set(dgx_ge_cells_t *c, uint16_t cell, int own, int32_t value)
{
    const int i = find(c, cell);
    if (i < 0 || own < 0 || own >= DGX_GE_CELL_OWN) return false;
    c->lives[i].own[own] = value;
    return true;
}

bool dgx_ge_cells_living(const dgx_ge_cells_t *c, uint16_t cell)
{
    return find(c, cell) >= 0;
}

static void forget(dgx_ge_cells_t *c, int i)
{
    /* the rest keep their order: they play as they began */
    memmove(&c->lives[i], &c->lives[i + 1], (size_t)(c->count - i - 1) * sizeof(dgx_ge_life_t));
    --c->count;
}

void dgx_ge_cells_stop(dgx_ge_cells_t *c, uint16_t cell)
{
    const int i = find(c, cell);
    if (i >= 0) forget(c, i);
}

typedef struct {
    dgx_ge_cells_t *cells;
    uint16_t        cell;
} heard_t;

static void say(dgx_ge_actor_t *a, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    (void)a;
    const heard_t *h = ctx;
    if (what == DGX_GE_ANIM_SIGNAL && h->cells->io.signal) h->cells->io.signal(h->cell, value, at_ms, h->cells->io.ctx);
    else if (what == DGX_GE_ANIM_SOUND) {
        if (h->cells->io.sound) h->cells->io.sound(h->cell, value, at_ms, h->cells->io.ctx);
    } else dgx_ge_timers_said(h->cells->timers, what, value, DGX_GE_OWNER_CELL, h->cell, h->cells->map, 0xFFFF, at_ms);
}

/*
 * A life played: an actor of its record and its cell, the event (-1 none), on
 * to now, given back to its record (found again: what the game did on a
 * signal may have moved it, or ended it). True when it holds: it ends.
 */
static bool play(dgx_ge_cells_t *c, const dgx_ge_life_t *was, const dgx_ge_anim_t *anim, int event, uint32_t now_ms)
{
    dgx_ge_life_t  copy = *was, *life = &copy;
    dgx_ge_actor_t a;
    dgx_ge_actor_start(&a, anim, life->script, 0, 0, life->due_ms);
    a.pc = life->pc, a.pending = life->pending, a.pending_when = life->pending_when;
    int tile = 0, mod = 0;
    c->io.get(life->cell, &tile, &mod, c->io.ctx);
    a.params[PARAM_MOD] = mod, a.params[PARAM_TILE] = tile;
    for (int i = 0; i < DGX_GE_CELL_OWN && PARAM_OWN + i < DGX_GE_ANIM_PARAMS; ++i) a.params[PARAM_OWN + i] = life->own[i];
    heard_t heard = {c, life->cell};
    if (event >= 0) dgx_ge_actor_event(&a, event, now_ms, say, &heard);
    dgx_ge_actor_advance(&a, now_ms, say, &heard);
    if (a.params[PARAM_MOD] != mod || a.params[PARAM_TILE] != tile) c->io.set(life->cell, a.params[PARAM_TILE], a.params[PARAM_MOD], c->io.ctx);
    life->script = a.script, life->pc = a.pc, life->pending = a.pending, life->pending_when = a.pending_when, life->due_ms = a.due_ms;
    for (int i = 0; i < DGX_GE_CELL_OWN && PARAM_OWN + i < DGX_GE_ANIM_PARAMS; ++i) life->own[i] = a.params[PARAM_OWN + i];
    const int at = find(c, copy.cell);
    if (at >= 0) c->lives[at] = copy;
    return a.holding;
}

bool dgx_ge_cells_event(dgx_ge_cells_t *c, uint16_t cell, uint16_t kind, int event, int own, int32_t value, uint32_t now_ms)
{
    if (c->io.cells && cell >= c->io.cells) return false;
    int i = find(c, cell);
    if (i < 0) {
        /* none: a new one at rest, its own params as its kind starts them */
        const dgx_ge_anim_t *anim = dgx_ge_kind(c->kinds, kind);
        if (!anim || c->count == c->capacity) return false;
        dgx_ge_actor_t a;
        dgx_ge_actor_start(&a, anim, 0, 0, 0, now_ms);
        dgx_ge_life_t *life = &c->lives[i = c->count++];
        *life = (dgx_ge_life_t){.cell = cell, .kind = kind, .script = a.script, .pc = a.pc, .due_ms = now_ms};
        for (int k = 0; k < DGX_GE_CELL_OWN && PARAM_OWN + k < DGX_GE_ANIM_PARAMS; ++k) life->own[k] = a.params[PARAM_OWN + k];
    }
    dgx_ge_life_t       *life = &c->lives[i];
    const dgx_ge_anim_t *anim = dgx_ge_kind(c->kinds, life->kind);
    if (!anim) {
        forget(c, i);
        return false;
    }
    if (own >= 0 && own < DGX_GE_CELL_OWN) life->own[own] = value;
    if (play(c, life, anim, event, now_ms)) {
        const int at = find(c, cell);
        if (at >= 0) forget(c, at);
    }
    return true;
}

bool dgx_ge_cells_timer(dgx_ge_cells_t *c, const dgx_ge_timer_t *timer, uint32_t at_ms, uint16_t (*life_of)(uint16_t cell, void *ctx), void *ctx)
{
    if (timer->owner_kind != DGX_GE_OWNER_CELL || timer->event == 0xFF || !life_of || (c->io.cells && timer->owner >= c->io.cells)) return false;
    return dgx_ge_cells_event(c, timer->owner, life_of(timer->owner, ctx), timer->event, -1, 0, at_ms);
}

void dgx_ge_cells_play(dgx_ge_cells_t *c, uint32_t now_ms)
{
    for (int i = 0; i < c->count;) {
        dgx_ge_life_t       *life = &c->lives[i];
        const dgx_ge_anim_t *anim = dgx_ge_kind(c->kinds, life->kind);
        const uint16_t       cell = life->cell;
        if (!anim || play(c, life, anim, -1, now_ms)) {
            /* a signal heard while it played may have begun or ended others: it is found again */
            const int at = find(c, cell);
            if (at >= 0) forget(c, at);
            i = at >= 0 && at < i ? i - 1 : i;
            continue;
        }
        const int at = find(c, cell);
        i = (at >= 0 ? at : i - 1) + 1;
    }
}

#define LIFE_BYTES(l) (2 + 2 + 1 + 1 + 1 + 2 + 4 + DGX_GE_LOAD_PARAM_BYTES(l) * DGX_GE_CELL_OWN)

void dgx_ge_cells_save(const dgx_ge_cells_t *c, dgx_ge_save_t *s)
{
    dgx_ge_save_chunk(s, "CELL");
    dgx_ge_save_u16(s, (uint16_t)c->count);
    dgx_ge_save_u8(s, DGX_GE_CELL_OWN);
    for (int i = 0; i < c->count; ++i) {
        const dgx_ge_life_t *f = &c->lives[i];
        dgx_ge_save_u16(s, f->cell);
        dgx_ge_save_u16(s, f->kind);
        dgx_ge_save_u8(s, f->script);
        dgx_ge_save_u8(s, f->pending);
        dgx_ge_save_u8(s, f->pending_when);
        dgx_ge_save_u16(s, f->pc);
        dgx_ge_save_u32(s, f->due_ms);
        for (int k = 0; k < DGX_GE_CELL_OWN; ++k) dgx_ge_save_i32(s, f->own[k]);
    }
    dgx_ge_save_chunk_end(s);
}

/* The chunk read and checked; taken into the lives when into is given (they are c), else c is as it was. */
static bool load(const dgx_ge_cells_t *c, dgx_ge_cells_t *into, dgx_ge_load_t *l, uint32_t later_ms)
{
    if (!dgx_ge_load_chunk(l, "CELL")) return false;
    const int count = dgx_ge_load_u16(l), own = dgx_ge_load_u8(l);
    if (l->failed || count > c->capacity || own != DGX_GE_CELL_OWN || dgx_ge_load_left(l) < (size_t)count * LIFE_BYTES(l)) return false;
    for (int i = 0; i < count; ++i) {
        dgx_ge_life_t f;
        f.cell         = dgx_ge_load_u16(l);
        f.kind         = dgx_ge_load_u16(l);
        f.script       = dgx_ge_load_u8(l);
        f.pending      = dgx_ge_load_u8(l);
        f.pending_when = dgx_ge_load_u8(l);
        f.pc           = dgx_ge_load_u16(l);
        f.due_ms       = dgx_ge_load_u32(l) + later_ms;
        for (int k = 0; k < DGX_GE_CELL_OWN; ++k) f.own[k] = dgx_ge_load_param(l);
        /* its kind's: its script and the passing it waits for (a script + 1, 0 none) of it; where it is in the code is checked as it plays */
        const dgx_ge_anim_t *anim = dgx_ge_kind(c->kinds, f.kind);
        if (!anim || f.script >= anim->scripts || f.pending > anim->scripts || (c->io.cells && f.cell >= c->io.cells)) return false;
        if (into) into->lives[i] = f;
    }
    if (into) into->count = count;
    return true;
}

bool dgx_ge_cells_load_check(const dgx_ge_cells_t *c, const dgx_ge_load_t *l)
{
    dgx_ge_load_t at = *l;
    return load(c, NULL, &at, 0);
}

bool dgx_ge_cells_load(dgx_ge_cells_t *c, dgx_ge_load_t *l, uint32_t later_ms)
{
    return dgx_ge_cells_load_check(c, l) && load(c, c, l, later_ms);
}
