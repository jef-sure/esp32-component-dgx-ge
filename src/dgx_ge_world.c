#include <stdlib.h>
#include <string.h>

#include "dgx_ge_pockets.h"
#include "dgx_ge_world.h"
#include "dgx_ge_save.h"

/*
 * A ring of records. A handle is the place of the record and its serial
 * number: a place taken by a newer record has another serial, so the handle
 * of the old one is gone. Serial 0 marks a free place.
 */
#define SERIALS 0x7FFF /* serials 1 .. SERIALS: a handle stays positive */

struct dgx_ge_world {
    dgx_ge_object_t *objects;
    uint16_t        *serials;
    int              capacity, next; /* the place the next record goes to: the oldest when the ring is full */
    uint16_t         serial;         /* the last given */
    dgx_ge_pockets_t **boxes;        /* what objects hold: box n is boxes[n - 1] */
    int                box_count;
};

dgx_ge_world_t *dgx_ge_world_create(int capacity)
{
    if (capacity < 1 || capacity > 0xFFFF) return NULL;
    dgx_ge_world_t *w = calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->objects  = calloc((size_t)capacity, sizeof(dgx_ge_object_t));
    w->serials  = calloc((size_t)capacity, sizeof(uint16_t));
    w->capacity = capacity;
    if (!w->objects || !w->serials) dgx_ge_world_destroy(&w);
    return w;
}

void dgx_ge_world_destroy(dgx_ge_world_t **w)
{
    if (!w || !*w) return;
    for (int i = 0; i < (*w)->box_count && (*w)->boxes; ++i) dgx_ge_pockets_destroy(&(*w)->boxes[i]);
    free((*w)->boxes);
    free((*w)->objects);
    free((*w)->serials);
    free(*w);
    *w = NULL;
}

/* A record gone (its box free: emptied when it is given again, dgx_ge_world_new_box). */
static void forget(dgx_ge_world_t *w, int place)
{
    w->serials[place] = 0;
}

void dgx_ge_world_clear(dgx_ge_world_t *w)
{
    for (int i = 0; i < w->capacity; ++i) forget(w, i);
    w->next = 0;
}

/* The place of a handle that still holds its record, -1 else. */
static int place_of(const dgx_ge_world_t *w, int32_t handle)
{
    if (handle < 0) return -1;
    const int place = handle & 0xFFFF;
    return place < w->capacity && w->serials[place] && w->serials[place] == (uint16_t)(handle >> 16) ? place : -1;
}

int32_t dgx_ge_world_add(dgx_ge_world_t *w, const dgx_ge_object_t *object)
{
    /* a free place, the oldest first; none: the oldest not kept */
    int place = -1;
    for (int i = 0; i < w->capacity && place < 0; ++i)
        if (!w->serials[(w->next + i) % w->capacity]) place = (w->next + i) % w->capacity;
    const bool hole = place >= 0 && place != w->next;
    for (int i = 0; i < w->capacity && place < 0; ++i)
        if (!w->objects[(w->next + i) % w->capacity].keep) place = (w->next + i) % w->capacity;
    if (place < 0) return -1;
    forget(w, place);
    w->serial       = (uint16_t)(w->serial % SERIALS + 1);
    w->objects[place] = *object;
    w->serials[place] = w->serial;
    /* past it the ring goes on; a place freed among the records leaves it where it was: the oldest stay next */
    if (!hole) w->next = (place + 1) % w->capacity;
    return place | (int32_t)w->serial << 16;
}

bool dgx_ge_world_get(const dgx_ge_world_t *w, int32_t handle, dgx_ge_object_t *object)
{
    const int place = place_of(w, handle);
    if (place < 0) return false;
    *object = w->objects[place];
    return true;
}

bool dgx_ge_world_put(dgx_ge_world_t *w, int32_t handle, const dgx_ge_object_t *object)
{
    const int place = place_of(w, handle);
    if (place < 0) return false;
    w->objects[place] = *object;
    return true;
}

void dgx_ge_world_remove(dgx_ge_world_t *w, int32_t handle)
{
    const int place = place_of(w, handle);
    if (place >= 0) forget(w, place);
}

int32_t dgx_ge_world_next(const dgx_ge_world_t *w, int scene, int32_t after)
{
    /* oldest first: from the place the next record goes to, round the ring; after the place of after, even if it is gone */
    const int from  = after < 0 || (after & 0xFFFF) >= w->capacity ? -1 : after & 0xFFFF;
    int       steps = from < 0 ? 0 : (from - w->next + w->capacity) % w->capacity + 1;
    for (; steps < w->capacity; ++steps) {
        const int place = (w->next + steps) % w->capacity;
        if (w->serials[place] && w->objects[place].scene == scene) return place | (int32_t)w->serials[place] << 16;
    }
    return -1;
}

bool dgx_ge_world_boxes(dgx_ge_world_t *w, dgx_ge_kinds_t *kinds, int count, int places)
{
    if (w->boxes || count < 1 || count > 254) return false;
    w->boxes = calloc((size_t)count, sizeof(dgx_ge_pockets_t *));
    if (!w->boxes) return false;
    w->box_count = count;
    for (int i = 0; i < count; ++i)
        if (!(w->boxes[i] = dgx_ge_pockets_create(kinds, places, 0))) {
            /* all or none: no box missing among them, and they may be asked for again */
            for (int k = 0; k < i; ++k) dgx_ge_pockets_destroy(&w->boxes[k]);
            free(w->boxes);
            w->boxes = NULL, w->box_count = 0;
            return false;
        }
    return true;
}

dgx_ge_pockets_t *dgx_ge_world_box(dgx_ge_world_t *w, int32_t handle)
{
    const int place = place_of(w, handle);
    const int box   = place >= 0 ? w->objects[place].box : 0;
    return box && box <= w->box_count ? w->boxes[box - 1] : NULL;
}

int dgx_ge_world_new_box(dgx_ge_world_t *w)
{
    for (int b = 1; b <= w->box_count; ++b) {
        bool used = false;
        for (int i = 0; i < w->capacity && !used; ++i) used = w->serials[i] && w->objects[i].box == b;
        if (!used) {
            dgx_ge_pockets_clear(w->boxes[b - 1]);
            return b;
        }
    }
    /* none free: the oldest record not kept that holds one gives it up, forgotten whole (not left without its things) */
    for (int i = 0; i < w->capacity; ++i) {
        const int at = (w->next + i) % w->capacity, b = w->objects[at].box;
        if (!w->serials[at] || w->objects[at].keep || !b || b > w->box_count) continue;
        forget(w, at);
        dgx_ge_pockets_clear(w->boxes[b - 1]);
        return b;
    }
    return 0;
}

int32_t dgx_ge_world_add_holding(dgx_ge_world_t *w, const dgx_ge_object_t *object, dgx_ge_pockets_t *pockets, uint32_t now_ms)
{
    bool holds = false;
    for (int i = 0; pockets && i < pockets->capacity && !holds; ++i) holds = pockets->places[i].count > 0;
    dgx_ge_object_t o = *object;
    o.box             = holds ? (uint8_t)dgx_ge_world_new_box(w) : 0;
    if (holds && !o.box) return -1;
    const int32_t handle = dgx_ge_world_add(w, &o);
    if (handle < 0 || !o.box) return handle; /* the box not taken stays free */
    dgx_ge_pockets_t *box = w->boxes[o.box - 1];
    for (int i = 0; i < pockets->capacity; ++i)
        if (pockets->places[i].count) dgx_ge_pockets_move(pockets, i, pockets->places[i].count, box, -1, now_ms);
    return handle;
}

/* Two records of things lying that make one stack: the same kind, state and params, and no box. */
static bool stack_of(const dgx_ge_object_t *a, const dgx_ge_object_t *b)
{
    return a->kind == b->kind && a->script == b->script && !a->box && !b->box && !memcmp(a->params, b->params, sizeof(a->params));
}

/* The record of a scene lying nearest x, y, nearer than a step (by x; on the same floor: y within a step), -1 none. */
static int near(const dgx_ge_world_t *w, int scene, int x, int y, int step)
{
    int best = -1, distance = step;
    for (int i = 0; i < w->capacity; ++i) {
        const dgx_ge_object_t *o = &w->objects[i];
        if (!w->serials[i] || o->scene != scene || abs((o->y >> 8) - y) >= step) continue;
        const int d = abs((o->x >> 8) - x);
        if (d < distance) distance = d, best = i;
    }
    return best;
}

static int32_t handle_of(const dgx_ge_world_t *w, int place)
{
    return place | (int32_t)w->serials[place] << 16;
}

/* A record as a thing of pockets: kind, state, params, how many. */
static dgx_ge_pocket_t pocket_of(const dgx_ge_object_t *o)
{
    dgx_ge_pocket_t r = {.kind = o->kind, .script = o->script, .count = (uint8_t)(o->count ? o->count : 1)};
    for (int i = 0; i < DGX_GE_POCKET_PARAMS && i < DGX_GE_WORLD_PARAMS; ++i) r.params[i] = o->params[i];
    return r;
}

int32_t dgx_ge_world_lay(dgx_ge_world_t *w, dgx_ge_kinds_t *kinds, const dgx_ge_pocket_t *thing, int count, int scene, int x, int y, unsigned orient,
                         int event, uint32_t now_ms, const dgx_ge_lay_t *how)
{
    /* the thing as it lies: made, sent the event, kept */
    dgx_ge_actor_t a;
    if (count < 1 || count > 255 || !dgx_ge_pocket_actor(kinds, thing, x, y, now_ms, &a)) return -1;
    a.flip = a.entered_flip = orient & 1u;
    a.turn = a.entered_turn = (uint8_t)(orient >> 1 & 3u);
    a.amount               = (uint8_t)count;
    if (event >= 0) dgx_ge_actor_event(&a, event, now_ms, NULL, NULL);
    dgx_ge_object_t lying = {.scene = (uint16_t)scene};
    dgx_ge_object_of(&lying, &a);
    const int step = how->step > 0 ? how->step : 1, reach = how->reach > 0 ? how->reach : 0;
    /* onto a stack of the same within reach */
    for (int i = 0; i < w->capacity; ++i) {
        dgx_ge_object_t *o = &w->objects[i];
        if (!w->serials[i] || o->scene != scene || abs((o->y >> 8) - y) >= step || abs((o->x >> 8) - x) > reach || !stack_of(o, &lying)) continue;
        const int n = (o->count ? o->count : 1) + count;
        if (n > 255) continue;
        o->count = (uint8_t)n;
        return handle_of(w, i);
    }
    /* the nearest free place: x, a step right, left, two right, ... */
    for (int k = 0; k * step <= reach * 2; ++k) {
        const int dx = (k + 1) / 2 * step * (k % 2 ? 1 : -1);
        if (abs(dx) > reach) continue;
        if (near(w, scene, x + dx, y, step) >= 0 || (how->can_lie && !how->can_lie(x + dx, y, how->ctx))) continue;
        lying.x += dx * 256;
        return dgx_ge_world_add(w, &lying);
    }
    if (how->heap < 0) return -1;
    /* a heap within reach that takes it */
    dgx_ge_pocket_t as_thing = pocket_of(&lying);
    for (int i = 0; i < w->capacity; ++i) {
        const dgx_ge_object_t *o = &w->objects[i];
        if (!w->serials[i] || o->scene != scene || o->kind != how->heap || !o->box || abs((o->x >> 8) - x) > reach || abs((o->y >> 8) - y) >= step)
            continue;
        dgx_ge_pockets_t *box = w->boxes[o->box - 1];
        if (dgx_ge_pockets_fits(box, &as_thing, count) < count) continue;
        dgx_ge_pockets_put(box, &as_thing, count);
        return handle_of(w, i);
    }
    /* a new heap at x: what lies there goes into it too; a heap (a chest) there already, full: no room */
    const int            there = near(w, scene, x, y, step);
    const int            box   = there >= 0 && w->objects[there].box ? 0 : dgx_ge_world_new_box(w);
    const dgx_ge_anim_t *heap  = dgx_ge_kind(kinds, how->heap);
    if (!box || !heap) return -1;
    dgx_ge_pockets_t *pockets = w->boxes[box - 1];
    if (there >= 0) {
        const dgx_ge_pocket_t under = pocket_of(&w->objects[there]);
        if (dgx_ge_pockets_put(pockets, &under, under.count) < under.count) return dgx_ge_pockets_clear(pockets), -1;
    }
    if (dgx_ge_pockets_put(pockets, &as_thing, count) < count) return dgx_ge_pockets_clear(pockets), -1;
    if (there >= 0) forget(w, there);
    dgx_ge_actor_t h;
    dgx_ge_actor_start(&h, heap, 0, x, y, now_ms);
    dgx_ge_actor_advance(&h, now_ms, NULL, NULL);
    dgx_ge_object_t o = {.scene = (uint16_t)scene, .box = (uint8_t)box};
    dgx_ge_object_of(&o, &h);
    o.keep = true; /* a heap holds what is kept */
    return dgx_ge_world_add(w, &o);
}

void dgx_ge_world_tidy(dgx_ge_world_t *w, int32_t handle, int heap, uint32_t now_ms)
{
    const int place = place_of(w, handle);
    if (place < 0 || w->objects[place].kind != heap || !w->objects[place].box) return;
    dgx_ge_pockets_t *box = w->boxes[w->objects[place].box - 1];
    int               left = 0, at = -1;
    for (int i = 0; i < box->capacity; ++i)
        if (box->places[i].count) ++left, at = i;
    if (left == 0) {
        forget(w, place);
    } else if (left == 1) {
        /* the heap is the one thing lying there, as it lay in it */
        const dgx_ge_pocket_t r = box->places[at];
        dgx_ge_object_t      *o = &w->objects[place];
        dgx_ge_pockets_clear(box);
        *o = (dgx_ge_object_t){.scene = o->scene, .kind = r.kind, .script = r.script, .keep = true, .count = r.count, .x = o->x, .y = o->y,
                               .since_ms = now_ms};
        for (int i = 0; i < DGX_GE_POCKET_PARAMS && i < DGX_GE_WORLD_PARAMS; ++i) o->params[i] = r.params[i];
    }
}

void dgx_ge_object_of(dgx_ge_object_t *o, const dgx_ge_actor_t *a)
{
    if (a->anim && a->anim->id != 0xFFFF) o->kind = a->anim->id;
    o->keep     = a->keep;
    o->count    = a->amount;
    o->script   = a->script;
    o->flip     = a->entered_flip;
    o->turn     = a->entered_turn & 3u;
    o->x        = a->entered_x;
    o->y        = a->entered_y;
    o->since_ms = a->entered_ms;
    for (int i = 0; i < DGX_GE_WORLD_PARAMS; ++i) o->params[i] = i < DGX_GE_ANIM_PARAMS ? a->entered_params[i] : 0;
}

void dgx_ge_object_actor(const dgx_ge_object_t *o, const dgx_ge_anim_t *anim, dgx_ge_actor_t *a)
{
    dgx_ge_actor_start(a, anim, o->script, 0, 0, o->since_ms);
    a->x = a->entered_x = o->x;
    a->y = a->entered_y = o->y;
    a->flip = a->entered_flip = o->flip;
    a->turn = a->entered_turn = o->turn;
    a->amount = o->count ? o->count : 1;
    for (int i = 0; i < DGX_GE_WORLD_PARAMS && i < anim->params && i < DGX_GE_ANIM_PARAMS; ++i) a->params[i] = a->entered_params[i] = o->params[i];
}

#define RECORD_BYTES(l) (2 + 2 + 2 + 1 + 1 + 1 + 1 + 4 + 4 + 4 + DGX_GE_LOAD_PARAM_BYTES(l) * DGX_GE_WORLD_PARAMS)

void dgx_ge_world_save(const dgx_ge_world_t *w, dgx_ge_save_t *s)
{
    dgx_ge_save_chunk(s, "WRLD");
    dgx_ge_save_u16(s, (uint16_t)w->capacity);
    dgx_ge_save_u16(s, (uint16_t)w->next);
    dgx_ge_save_u16(s, w->serial);
    dgx_ge_save_u8(s, DGX_GE_WORLD_PARAMS);
    for (int i = 0; i < w->capacity; ++i) {
        const dgx_ge_object_t *o = &w->objects[i];
        dgx_ge_save_u16(s, w->serials[i]);
        dgx_ge_save_u16(s, o->scene);
        dgx_ge_save_u16(s, o->kind);
        dgx_ge_save_u8(s, o->script);
        dgx_ge_save_u8(s, (uint8_t)((o->flip ? 1 : 0) | (o->keep ? 2 : 0) | o->turn << 2));
        dgx_ge_save_u8(s, o->count);
        dgx_ge_save_u8(s, o->box);
        dgx_ge_save_i32(s, o->x);
        dgx_ge_save_i32(s, o->y);
        dgx_ge_save_u32(s, o->since_ms);
        for (int k = 0; k < DGX_GE_WORLD_PARAMS; ++k) dgx_ge_save_i32(s, o->params[k]);
    }
    dgx_ge_save_u8(s, (uint8_t)w->box_count);
    for (int i = 0; i < w->box_count; ++i) dgx_ge_pockets_write(w->boxes[i], s);
    dgx_ge_save_chunk_end(s);
}

/* The chunk read and checked; taken into the world when into is given (it is w), else w is as it was. */
static bool load(const dgx_ge_world_t *w, dgx_ge_world_t *into, dgx_ge_load_t *l, uint32_t later_ms)
{
    if (!dgx_ge_load_chunk(l, "WRLD")) return false;
    const int capacity = dgx_ge_load_u16(l), next = dgx_ge_load_u16(l);
    const uint16_t serial = dgx_ge_load_u16(l);
    const int params = dgx_ge_load_u8(l);
    if (l->failed || capacity != w->capacity || next >= capacity || params != DGX_GE_WORLD_PARAMS) return false;
    if (dgx_ge_load_left(l) < (size_t)capacity * RECORD_BYTES(l) + 1) return false;
    for (int i = 0; i < w->capacity; ++i) {
        dgx_ge_object_t o = {0};
        const uint16_t  serial_of = dgx_ge_load_u16(l);
        o.scene                   = dgx_ge_load_u16(l);
        o.kind                    = dgx_ge_load_u16(l);
        o.script                  = dgx_ge_load_u8(l);
        const uint8_t flags       = dgx_ge_load_u8(l);
        o.flip  = flags & 1;
        o.keep  = (flags & 2) != 0;
        o.turn  = flags >> 2 & 3u; /* a save before version 4 has none: it faced so */
        o.count = dgx_ge_load_u8(l);
        o.box   = dgx_ge_load_u8(l);
        o.x     = dgx_ge_load_i32(l);
        o.y     = dgx_ge_load_i32(l);
        o.since_ms = dgx_ge_load_u32(l) + later_ms;
        for (int k = 0; k < DGX_GE_WORLD_PARAMS; ++k) o.params[k] = dgx_ge_load_param(l);
        if (o.box > w->box_count) return false; /* box n is boxes[n - 1]: one of the world's or none */
        if (into) into->serials[i] = serial_of, into->objects[i] = o;
    }
    const int boxes = dgx_ge_load_u8(l);
    if (boxes != w->box_count) return false;
    for (int i = 0; i < w->box_count; ++i)
        if (!(into ? dgx_ge_pockets_read(into->boxes[i], l) : dgx_ge_pockets_read_check(w->boxes[i], l))) return false;
    if (l->failed) return false;
    if (into) into->next = next, into->serial = serial;
    return true;
}

bool dgx_ge_world_load_check(const dgx_ge_world_t *w, const dgx_ge_load_t *l)
{
    dgx_ge_load_t at = *l;
    return load(w, NULL, &at, 0);
}

bool dgx_ge_world_load(dgx_ge_world_t *w, dgx_ge_load_t *l, uint32_t later_ms)
{
    return dgx_ge_world_load_check(w, l) && load(w, w, l, later_ms);
}
