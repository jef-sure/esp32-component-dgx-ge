#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "dgx_ge_anim.h"
#include "dgx_ge_mask.h"

/*
 * Shots: bullets, arrows, darts. There are many of them, they live a short
 * while and have no states but flying and hitting, so a shot is no actor but
 * a record of 16 bytes in a pool: where and when it was fired, how fast,
 * of what kind, whose. Where it is at any moment is a formula: a line, or a
 * fall when its kind falls.
 *
 * A kind is an animation, its first script telling what the shot is: its
 * picture (frame), whether it falls (accel), its mask (masks) and what it
 * does on meeting (hit): read once, when the game gives the kind.
 *
 * Hits: between two looks a shot goes along a line. The terrain is looked at
 * first, by a ray (a wall cuts the way); then the bodies of the collisions,
 * by their boxes and, those touched, by their masks point by point; the first
 * met along the way counts. The bodies of its owner are passed by, sides as
 * the meets say. The game is told what was met and decides: the shot gone, a
 * spark, a wound (dgx_ge_shots_hit gives the kind's hit), a shot that goes on.
 *
 * A full pool fires nothing (the game is told): a shot vanishing in flight
 * would be seen.
 */
#ifndef DGX_GE_SHOT_KINDS
#define DGX_GE_SHOT_KINDS 8
#endif

typedef struct {
    int16_t  x, y;   /* where it was fired, points of the scene */
    int16_t  vx, vy; /* points a second */
    uint32_t fired_ms;
    uint8_t  kind;   /* its place among the kinds; 0xFF: a free place */
    uint8_t  side;   /* 0: nobody's */
    uint16_t owner;  /* the one who fired it is never hit by it; 0 nobody */
} dgx_ge_shot_t;

typedef struct {
    uint16_t             id;   /* the game's number for it (a blob of the pack) */
    const dgx_ge_anim_t *anim; /* NULL: no kind here */
    int16_t              image;
    int16_t              ay;    /* points a second a second down: it falls */
    uint16_t             masks;
    uint16_t             hits;  /* where its hit lines are in the code + 1 */
} dgx_ge_shot_kind_t;

typedef struct {
    dgx_ge_shot_t     *shots;
    int                capacity;
    dgx_ge_shot_kind_t kinds[DGX_GE_SHOT_KINDS];
    uint32_t           looked_ms;
} dgx_ge_shots_t;

/* Room for so many shots in flight at once. */
dgx_ge_shots_t *dgx_ge_shots_create(int capacity);
void            dgx_ge_shots_destroy(dgx_ge_shots_t **shots);

/* A kind of shot, by the game's number for it (shoot names it so) and its animation; false when there is no room for more kinds. */
bool dgx_ge_shots_kind(dgx_ge_shots_t *s, uint16_t id, const dgx_ge_anim_t *anim);

/* Fire one from x, y (points of the scene) at vx, vy (points a second) at a moment; its place, -1 when the pool is full or the kind unknown. */
int dgx_ge_shots_fire(dgx_ge_shots_t *s, uint16_t kind, int x, int y, int vx, int vy, uint32_t at_ms, uint8_t side, uint16_t owner);

/* A shot gone. */
void dgx_ge_shots_remove(dgx_ge_shots_t *s, int i);

/* Where a shot is at a moment, points of the scene. */
void dgx_ge_shots_at(const dgx_ge_shots_t *s, int i, uint32_t ms, int *x, int *y);

/* Its kind, the picture it shows. */
const dgx_ge_shot_kind_t *dgx_ge_shots_kind_of(const dgx_ge_shots_t *s, int i);

/* What a shot met on its way: a body (handle >= 0, its mask), or the terrain (handle -1); where and when. */
typedef struct {
    int              handle, index;
    int              x, y;
    uint32_t         at_ms;
    dgx_ge_contact_t contact; /* the terrain met */
} dgx_ge_shot_hit_t;

typedef void (*dgx_ge_shot_fn)(dgx_ge_shots_t *s, int i, const dgx_ge_shot_hit_t *hit, void *ctx);

/*
 * Every shot on its way from the last look to now: fn for what it met first
 * (the game removes it, or not). Shots leaving bounds (points of the scene)
 * are removed.
 */
void dgx_ge_shots_play(dgx_ge_shots_t *s, uint32_t now_ms, const dgx_ge_terrain_t *terrain, const dgx_ge_collisions_t *bodies, dgx_ge_rect_t bounds,
                       dgx_ge_shot_fn fn, void *ctx);

/* What a shot of a kind does with its mask meeting a mask (its hit lines), as dgx_ge_actor_hit. */
bool dgx_ge_shots_hit(const dgx_ge_shots_t *s, int i, int on_mask, dgx_ge_hit_t *hit);

/* The origin of the scene moved (dgx_ge_scene_shift): the shots go with it. */
void dgx_ge_shots_shift(dgx_ge_shots_t *s, int dx, int dy);

#ifdef __cplusplus
}
#endif
