#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dgx_ge_shots.h"

#define FREE 0xFF

dgx_ge_shots_t *dgx_ge_shots_create(int capacity)
{
    if (capacity < 1) return NULL;
    dgx_ge_shots_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->shots = calloc((size_t)capacity, sizeof(dgx_ge_shot_t));
    if (!s->shots) {
        free(s);
        return NULL;
    }
    s->capacity = capacity;
    for (int i = 0; i < capacity; ++i) s->shots[i].kind = FREE;
    return s;
}

void dgx_ge_shots_destroy(dgx_ge_shots_t **s)
{
    if (!s || !*s) return;
    free((*s)->shots);
    free(*s);
    *s = NULL;
}

bool dgx_ge_shots_kind(dgx_ge_shots_t *s, uint16_t id, const dgx_ge_anim_t *anim)
{
    int k = -1;
    for (int i = 0; i < DGX_GE_SHOT_KINDS && k < 0; ++i)
        if (s->kinds[i].anim && s->kinds[i].id == id) k = i;
    for (int i = 0; i < DGX_GE_SHOT_KINDS && k < 0; ++i)
        if (!s->kinds[i].anim) k = i;
    if (k < 0) return false;
    /* what its first script says: an actor of it, its first steps made */
    dgx_ge_actor_t a;
    dgx_ge_actor_start(&a, anim, 0, 0, 0, 0);
    dgx_ge_actor_advance(&a, 0, NULL, NULL);
    s->kinds[k] = (dgx_ge_shot_kind_t){.id = id, .anim = anim, .image = a.image, .ay = (int16_t)(a.ay / 256), .masks = a.masks, .hits = a.hits};
    return true;
}

static int kind_place(const dgx_ge_shots_t *s, uint16_t id)
{
    for (int i = 0; i < DGX_GE_SHOT_KINDS; ++i)
        if (s->kinds[i].anim && s->kinds[i].id == id) return i;
    return -1;
}

int dgx_ge_shots_fire(dgx_ge_shots_t *s, uint16_t kind, int x, int y, int vx, int vy, uint32_t at_ms, uint8_t side, uint16_t owner)
{
    const int k = kind_place(s, kind);
    if (k < 0) return -1;
    for (int i = 0; i < s->capacity; ++i)
        if (s->shots[i].kind == FREE) {
            s->shots[i] = (dgx_ge_shot_t){.x = (int16_t)x, .y = (int16_t)y, .vx = (int16_t)vx, .vy = (int16_t)vy, .fired_ms = at_ms, .kind = (uint8_t)k,
                                          .side = side, .owner = owner};
            return i;
        }
    return -1; /* full: nothing fired */
}

void dgx_ge_shots_remove(dgx_ge_shots_t *s, int i)
{
    if (i >= 0 && i < s->capacity) s->shots[i].kind = FREE;
}

const dgx_ge_shot_kind_t *dgx_ge_shots_kind_of(const dgx_ge_shots_t *s, int i)
{
    return i >= 0 && i < s->capacity && s->shots[i].kind != FREE ? &s->kinds[s->shots[i].kind] : NULL;
}

/* Where it is at a moment, 1/256 of a point: a line, and a fall by its kind. */
static void place_at(const dgx_ge_shots_t *s, const dgx_ge_shot_t *sh, uint32_t ms, int32_t *x, int32_t *y)
{
    const int64_t dt = (int32_t)(ms - sh->fired_ms); /* ms */
    const int     ay = s->kinds[sh->kind].ay;
    *x = sh->x * 256 + (int32_t)((int64_t)sh->vx * 256 * dt / 1000);
    *y = sh->y * 256 + (int32_t)((int64_t)sh->vy * 256 * dt / 1000 + (int64_t)ay * 256 * dt * dt / 2000000);
}

void dgx_ge_shots_at(const dgx_ge_shots_t *s, int i, uint32_t ms, int *x, int *y)
{
    int32_t px, py;
    place_at(s, &s->shots[i], ms, &px, &py);
    *x = px >> 8, *y = py >> 8;
}

/* The mask a shot meets with: the lowest of its kind's. */
static int mask_of(const dgx_ge_shot_kind_t *k)
{
    for (int i = 0; i < 16; ++i)
        if (k->masks >> i & 1) return i;
    return -1;
}

void dgx_ge_shots_play(dgx_ge_shots_t *s, uint32_t now_ms, const dgx_ge_terrain_t *terrain, const dgx_ge_collisions_t *bodies, dgx_ge_rect_t bounds,
                       dgx_ge_shot_fn fn, void *ctx)
{
    for (int i = 0; i < s->capacity; ++i) {
        dgx_ge_shot_t *sh = &s->shots[i];
        if (sh->kind == FREE) continue;
        /* its way since the last look, or since it was fired */
        const uint32_t from_ms = (int32_t)(sh->fired_ms - s->looked_ms) > 0 ? sh->fired_ms : s->looked_ms;
        if ((int32_t)(now_ms - from_ms) <= 0) continue;
        int32_t x0, y0, x1, y1;
        place_at(s, sh, from_ms, &x0, &y0);
        place_at(s, sh, now_ms, &x1, &y1);
        /* the terrain first: a wall cuts the way; looked at every point of it, so a band of one (a floor solid at its top) is never passed */
        dgx_ge_shot_hit_t hit  = {.handle = -1};
        bool              wall = terrain && dgx_ge_terrain_ray(terrain, x0, y0, x1, y1, 1, &hit.contact);
        int32_t           ex = x1, ey = y1;
        if (wall) ex = hit.contact.x, ey = hit.contact.y;
        /* then the bodies, on the way left; the moment: as far along the whole way as what it met */
        const int64_t    full = (int64_t)(x1 - x0) * (x1 - x0) + (int64_t)(y1 - y0) * (y1 - y0);
        const int64_t    span = (int64_t)(ex - x0) * (ex - x0) + (int64_t)(ey - y0) * (ey - y0);
        const float      to_wall = wall && full > 0 ? sqrtf((float)span / (float)full) : 1.0f;
        const int        mask = mask_of(&s->kinds[sh->kind]);
        dgx_ge_ray_hit_t met;
        if (bodies && mask >= 0 && dgx_ge_collisions_ray(bodies, x0 >> 8, y0 >> 8, ex >> 8, ey >> 8, mask, sh->owner, sh->side, &met)) {
            hit       = (dgx_ge_shot_hit_t){.handle = met.handle, .index = met.index, .x = met.x, .y = met.y};
            hit.at_ms = from_ms + (uint32_t)(met.along * to_wall * (float)(now_ms - from_ms));
            if (fn) fn(s, i, &hit, ctx);
        } else if (wall) {
            hit.x = ex >> 8, hit.y = ey >> 8;
            hit.at_ms = from_ms + (uint32_t)(to_wall * (float)(now_ms - from_ms));
            if (fn) fn(s, i, &hit, ctx);
        }
        /* gone out of the scene */
        if (sh->kind != FREE && (x1 >> 8 < bounds.x || y1 >> 8 < bounds.y || x1 >> 8 >= bounds.x + bounds.w || y1 >> 8 >= bounds.y + bounds.h)) sh->kind = FREE;
    }
    s->looked_ms = now_ms;
}

bool dgx_ge_shots_hit(const dgx_ge_shots_t *s, int i, int on_mask, dgx_ge_hit_t *hit)
{
    const dgx_ge_shot_kind_t *k = dgx_ge_shots_kind_of(s, i);
    if (!k || !k->hits) return false;
    dgx_ge_actor_t a;
    memset(&a, 0, sizeof(a));
    a.anim = k->anim;
    a.hits = k->hits;
    return dgx_ge_actor_hit(&a, mask_of(k), on_mask, hit);
}

void dgx_ge_shots_shift(dgx_ge_shots_t *s, int dx, int dy)
{
    for (int i = 0; i < s->capacity; ++i)
        if (s->shots[i].kind != FREE) s->shots[i].x = (int16_t)(s->shots[i].x - dx), s->shots[i].y = (int16_t)(s->shots[i].y - dy);
}
