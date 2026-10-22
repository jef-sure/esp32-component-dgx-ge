#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "dgx_ge_pockets.h"
#include "dgx_ge_save.h"

bool dgx_ge_kinds_init(dgx_ge_kinds_t *k, const dgx_ge_pack_t *pack, int capacity)
{
    memset(k, 0, sizeof(*k));
    k->pack  = pack;
    k->anims = capacity > 0 ? calloc((size_t)capacity, sizeof(dgx_ge_anim_t)) : NULL;
    if (!k->anims) return false;
    k->capacity = capacity;
    for (int i = 0; i < capacity; ++i) k->anims[i].id = 0xFFFF;
    return true;
}

void dgx_ge_kinds_free(dgx_ge_kinds_t *k)
{
    free(k->anims);
    memset(k, 0, sizeof(*k));
}

const dgx_ge_anim_t *dgx_ge_kind(dgx_ge_kinds_t *k, int id)
{
    if (!k || id < 0 || id >= 0xFFFF) return NULL;
    int free_at = -1;
    for (int i = 0; i < k->capacity; ++i) {
        if (k->anims[i].id == id) return &k->anims[i];
        if (k->anims[i].id == 0xFFFF && free_at < 0) free_at = i;
    }
    size_t         size;
    const uint8_t *data = dgx_ge_pack_blob(k->pack, id, &size);
    dgx_ge_anim_t  anim;
    if (!data || !dgx_ge_anim_open(&anim, data, size)) return NULL;
    if (free_at < 0) {
        /* more kinds than the game said: its constants are wrong, and what this kind would do is lost */
        if (!k->told) ESP_LOGE("dgx-ge", "kinds: no place for the kind of blob %d, the game said %d kinds", id, k->capacity);
        k->told = true;
        return NULL;
    }
    anim.id           = (uint16_t)id;
    k->anims[free_at] = anim;
    return &k->anims[free_at];
}

dgx_ge_pockets_t *dgx_ge_pockets_create(dgx_ge_kinds_t *kinds, int capacity, int hands)
{
    if (capacity < 1 || capacity > 255 || hands < 0 || hands > DGX_GE_ANIM_CARRIED) return NULL;
    dgx_ge_pockets_t *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->places = calloc((size_t)capacity, sizeof(dgx_ge_pocket_t));
    p->hands  = hands ? calloc((size_t)hands, sizeof(dgx_ge_actor_t)) : NULL;
    p->kinds      = kinds;
    p->capacity   = capacity;
    p->hand_count = hands;
    if (!p->places || (hands && !p->hands)) dgx_ge_pockets_destroy(&p);
    return p;
}

void dgx_ge_pockets_destroy(dgx_ge_pockets_t **p)
{
    if (!p || !*p) return;
    for (int i = 0; i < (*p)->hand_count && (*p)->hands; ++i) dgx_ge_actor_drop(&(*p)->hands[i]);
    free((*p)->hands);
    free((*p)->places);
    free(*p);
    *p = NULL;
}

void dgx_ge_pockets_clear(dgx_ge_pockets_t *p)
{
    memset(p->places, 0, (size_t)p->capacity * sizeof(dgx_ge_pocket_t));
}

int dgx_ge_pockets_count(const dgx_ge_pockets_t *p, int kind)
{
    int n = 0;
    for (int i = 0; i < p->capacity; ++i)
        if (p->places[i].count && p->places[i].kind == kind) n += p->places[i].count;
    return n;
}

/* Two things of a stack: the same kind and params. */
static bool same(const dgx_ge_pocket_t *a, const dgx_ge_pocket_t *b)
{
    return a->kind == b->kind && !memcmp(a->params, b->params, sizeof(a->params));
}

/* How many of a kind go into one place. */
static int stack_of(const dgx_ge_pockets_t *p, int kind)
{
    const dgx_ge_anim_t *anim = dgx_ge_kind(p->kinds, kind);
    return anim ? anim->stack : 1;
}

int dgx_ge_pockets_fits(const dgx_ge_pockets_t *p, const dgx_ge_pocket_t *thing, int count)
{
    const int stack = stack_of(p, thing->kind);
    int       room  = 0;
    for (int i = 0; i < p->capacity && room < count; ++i) {
        const dgx_ge_pocket_t *at = &p->places[i];
        if (!at->count) room += stack;
        else if (same(at, thing) && at->count < stack) room += stack - at->count;
    }
    return room < count ? room : count;
}

int dgx_ge_pockets_put(dgx_ge_pockets_t *p, const dgx_ge_pocket_t *thing, int count)
{
    const int stack = stack_of(p, thing->kind);
    int       left  = count;
    /* into stacks of the same first, then free places */
    for (int pass = 0; pass < 2 && left > 0; ++pass)
        for (int i = 0; i < p->capacity && left > 0; ++i) {
            dgx_ge_pocket_t *at = &p->places[i];
            if (pass == 0 ? !at->count || !same(at, thing) || at->count >= stack : at->count != 0) continue;
            if (!at->count) *at = *thing, at->count = 0;
            const int n = stack - at->count < left ? stack - at->count : left;
            at->count = (uint8_t)(at->count + n);
            left -= n;
        }
    return count - left;
}

int dgx_ge_pockets_take(dgx_ge_pockets_t *p, int place, int count, dgx_ge_pocket_t *out)
{
    if (place < 0 || place >= p->capacity || count < 1 || !p->places[place].count) return 0;
    dgx_ge_pocket_t *at = &p->places[place];
    const int        n  = count < at->count ? count : at->count;
    if (out) *out = *at, out->count = (uint8_t)n;
    at->count = (uint8_t)(at->count - n);
    return n;
}

void dgx_ge_pocket_event(dgx_ge_kinds_t *kinds, dgx_ge_pocket_t *r, int event, uint32_t now_ms)
{
    dgx_ge_actor_t a;
    if (event < 0 || !dgx_ge_pocket_actor(kinds, r, 0, 0, now_ms, &a)) return;
    dgx_ge_actor_advance(&a, now_ms, NULL, NULL);
    if (!dgx_ge_actor_event(&a, event, now_ms, NULL, NULL)) return;
    const uint8_t count = r->count;
    dgx_ge_pocket_of(r, &a);
    r->count = count;
}

int dgx_ge_pockets_move(dgx_ge_pockets_t *from, int place, int count, dgx_ge_pockets_t *to, int event, uint32_t now_ms)
{
    if (place < 0 || place >= from->capacity || !from->places[place].count) return 0;
    dgx_ge_pocket_t thing = from->places[place];
    dgx_ge_pocket_event(from->kinds, &thing, event, now_ms);
    const int             n     = dgx_ge_pockets_fits(to, &thing, count < thing.count ? count : thing.count);
    if (n < 1) return 0;
    dgx_ge_pockets_put(to, &thing, n);
    return dgx_ge_pockets_take(from, place, n, NULL);
}

bool dgx_ge_pocket_of(dgx_ge_pocket_t *r, const dgx_ge_actor_t *thing)
{
    if (!thing->anim || thing->anim->id == 0xFFFF) return false;
    memset(r, 0, sizeof(*r));
    r->kind   = thing->anim->id;
    r->script = thing->script;
    r->count  = 1;
    for (int i = 0; i < DGX_GE_POCKET_PARAMS && i < DGX_GE_ANIM_PARAMS; ++i) r->params[i] = thing->params[i];
    return true;
}

bool dgx_ge_pocket_actor(dgx_ge_kinds_t *kinds, const dgx_ge_pocket_t *r, int x, int y, uint32_t now_ms, dgx_ge_actor_t *a)
{
    const dgx_ge_anim_t *anim = dgx_ge_kind(kinds, r->kind);
    if (!anim) return false;
    dgx_ge_actor_start(a, anim, r->script, x, y, now_ms);
    for (int i = 0; i < DGX_GE_POCKET_PARAMS && i < anim->params && i < DGX_GE_ANIM_PARAMS; ++i) a->params[i] = a->entered_params[i] = r->params[i];
    return true;
}

int dgx_ge_pockets_find(const dgx_ge_pockets_t *p, int kind)
{
    for (int i = 0; i < p->capacity; ++i)
        if (p->places[i].count && p->places[i].kind == kind) return i;
    return -1;
}

bool dgx_ge_pockets_draw(dgx_ge_actor_t *holder, int point, int kind, uint32_t now_ms)
{
    dgx_ge_pockets_t *p = holder->pockets;
    if (!p || dgx_ge_actor_held(holder, point)) return false;
    const int place = dgx_ge_pockets_find(p, kind);
    int       hand  = -1;
    for (int i = 0; i < p->hand_count && hand < 0; ++i)
        if (!p->hands[i].holder) hand = i;
    if (place < 0 || hand < 0) return false;
    dgx_ge_actor_t *thing = &p->hands[hand];
    if (!dgx_ge_pocket_actor(p->kinds, &p->places[place], dgx_ge_actor_x(holder), dgx_ge_actor_y(holder), now_ms, thing)) return false;
    dgx_ge_actor_advance(thing, now_ms, NULL, NULL); /* its first steps: what it is in a hand (its masks) */
    if (!dgx_ge_actor_hold(holder, point, thing)) return false;
    dgx_ge_pockets_take(p, place, 1, NULL);
    return true;
}

bool dgx_ge_pockets_stow(dgx_ge_actor_t *holder, int point)
{
    dgx_ge_pockets_t *p     = holder->pockets;
    dgx_ge_actor_t   *thing = dgx_ge_actor_held(holder, point);
    dgx_ge_pocket_t   r;
    if (!p || !thing || !dgx_ge_pocket_of(&r, thing) || dgx_ge_pockets_fits(p, &r, 1) < 1) return false;
    dgx_ge_pockets_put(p, &r, 1);
    dgx_ge_actor_drop(thing);
    return true;
}

int32_t dgx_ge_pockets_lay(dgx_ge_pockets_t *p, int place, int count, dgx_ge_world_t *world, int scene, int x, int y, bool flip, int event,
                           uint32_t now_ms, const dgx_ge_lay_t *how)
{
    if (place < 0 || place >= p->capacity || !p->places[place].count || count < 1) return -1;
    if (count > p->places[place].count) count = p->places[place].count;
    int32_t handle;
    if (how) {
        handle = dgx_ge_world_lay(world, p->kinds, &p->places[place], count, scene, x, y, flip, event, now_ms, how);
    } else {
        /* just there: a new object of so many */
        dgx_ge_actor_t a;
        if (!dgx_ge_pocket_actor(p->kinds, &p->places[place], x, y, now_ms, &a)) return -1;
        a.flip = a.entered_flip = flip;
        a.amount               = (uint8_t)count;
        if (event >= 0) dgx_ge_actor_event(&a, event, now_ms, NULL, NULL);
        dgx_ge_object_t o = {.scene = (uint16_t)scene};
        dgx_ge_object_of(&o, &a);
        handle = dgx_ge_world_add(world, &o);
    }
    if (handle >= 0) dgx_ge_pockets_take(p, place, count, NULL);
    return handle;
}

void dgx_ge_pockets_write(const dgx_ge_pockets_t *p, dgx_ge_save_t *s)
{
    dgx_ge_save_u8(s, (uint8_t)p->capacity);
    dgx_ge_save_u8(s, DGX_GE_POCKET_PARAMS);
    for (int i = 0; i < p->capacity; ++i) {
        const dgx_ge_pocket_t *t = &p->places[i];
        dgx_ge_save_u16(s, t->kind);
        dgx_ge_save_u8(s, t->script);
        dgx_ge_save_u8(s, t->count);
        for (int k = 0; k < DGX_GE_POCKET_PARAMS; ++k) dgx_ge_save_i32(s, t->params[k]);
    }
}

#define PLACE_BYTES(l) (4 + DGX_GE_LOAD_PARAM_BYTES(l) * DGX_GE_POCKET_PARAMS)

bool dgx_ge_pockets_read_check(const dgx_ge_pockets_t *p, dgx_ge_load_t *l)
{
    const int capacity = dgx_ge_load_u8(l), params = dgx_ge_load_u8(l);
    if (l->failed || capacity != p->capacity || params != DGX_GE_POCKET_PARAMS || dgx_ge_load_left(l) < (size_t)capacity * PLACE_BYTES(l)) return false;
    l->at += (size_t)capacity * PLACE_BYTES(l);
    return true;
}

bool dgx_ge_pockets_read(dgx_ge_pockets_t *p, dgx_ge_load_t *l)
{
    const int capacity = dgx_ge_load_u8(l), params = dgx_ge_load_u8(l);
    if (l->failed || capacity != p->capacity || params != DGX_GE_POCKET_PARAMS) return false;
    if (dgx_ge_load_left(l) < (size_t)capacity * PLACE_BYTES(l)) return false;
    for (int i = 0; i < p->capacity; ++i) {
        dgx_ge_pocket_t *t = &p->places[i];
        t->kind   = dgx_ge_load_u16(l);
        t->script = dgx_ge_load_u8(l);
        t->count  = dgx_ge_load_u8(l);
        for (int k = 0; k < DGX_GE_POCKET_PARAMS; ++k) t->params[k] = dgx_ge_load_param(l);
    }
    return true;
}

void dgx_ge_pockets_save(const dgx_ge_pockets_t *p, dgx_ge_save_t *s, const char tag[4])
{
    dgx_ge_save_chunk(s, tag);
    dgx_ge_pockets_write(p, s);
    dgx_ge_save_chunk_end(s);
}

bool dgx_ge_pockets_load(dgx_ge_pockets_t *p, dgx_ge_load_t *l, const char tag[4])
{
    return dgx_ge_load_chunk(l, tag) && dgx_ge_pockets_read(p, l);
}

bool dgx_ge_pockets_load_check(const dgx_ge_pockets_t *p, const dgx_ge_load_t *l, const char tag[4])
{
    dgx_ge_load_t at = *l;
    return dgx_ge_load_chunk(&at, tag) && dgx_ge_pockets_read_check(p, &at);
}
