#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "dgx_ge_pack.h"
#include "dgx_ge_types.h"

/*
 * Collisions by masks. A picture has masks of zones by index (body, blade,
 * shield), 1 bit a point, from its origin, mirrored with it (see
 * dgx_ge_pack.h). Two bodies collide where an active mask of one meets an
 * active mask of the other. What an index means is the game's.
 *
 * The answer is not the points where they meet, but areas: the overlap cut by
 * a grid of tiles of the scene, neighbouring tiles one area. A blade across
 * an arm and a leg is two areas. Each has its box, its middle and how many
 * points meet there: enough for a spark where they met, and the game works
 * out the rest by its own rules. Nothing is pushed: this only tells.
 */
typedef struct {
    dgx_ge_rect_t box;    /* the tiles of the area, cut to the overlap, points of the scene */
    int16_t       cx, cy; /* its middle, weighed by how many points meet in each tile */
    uint16_t      points; /* how many points meet (0xFFFF: so many or more) */
} dgx_ge_area_t;

/* areas given at most for one pair of masks; more are joined to the last */
#define DGX_GE_AREAS_MAX 8

/*
 * Where two masks meet, each by the origin of its picture at x, y of the
 * scene, mirrored or not: the areas, in tiles of tile x tile points (larger
 * when the overlap is too wide for 16 x 16 tiles). Returns how many areas,
 * 0 when they do not meet.
 */
int dgx_ge_masks_overlap(const dgx_ge_mask_t *a, int ax, int ay, bool a_flip, const dgx_ge_mask_t *b, int bx, int by, bool b_flip, int tile,
                         dgx_ge_area_t *areas);

/* Where a mask lies in the scene, its picture's origin at x, y, mirrored or not. */
dgx_ge_rect_t dgx_ge_mask_place(const dgx_ge_mask_t *m, int x, int y, bool flip);

/* Is a point of the scene in a mask placed at r (dgx_ge_mask_place)? It lies in r. */
bool dgx_ge_mask_has(const dgx_ge_mask_t *m, dgx_ge_rect_t r, bool flip, int sx, int sy);

/* A picture of the pack standing in the scene with its masks: which are active is the script's (dgx_ge_actor_t.masks). */
typedef struct {
    const dgx_ge_pack_t *pack;
    int                  image;
    int                  x, y; /* the origin, points of the scene */
    bool                 flip;
    uint16_t             active; /* a bit an index */
} dgx_ge_body_t;

/* A pair of masks met: index of the one of a, of b, and where. */
typedef void (*dgx_ge_overlap_fn)(int index_a, int index_b, const dgx_ge_area_t *areas, int count, void *ctx);

/* Every active mask of a against every active mask of b; fn for each pair that meets. Returns how many pairs met. */
int dgx_ge_bodies_overlap(const dgx_ge_body_t *a, const dgx_ge_body_t *b, int tile, dgx_ge_overlap_fn fn, void *ctx);

/*
 * What a body touches: where the solid is, as to a point of a mask of it:
 * inside it, below the point (a floor one stands on), above (a ceiling),
 * on its left, on its right; within reach points (1: next to it). The points
 * of a mask that touch so are an area (box, middle, how many). One question
 * for a floor, a wall, a ceiling, an edge (nothing below), water (inside a
 * body of that kind), a crate one stands on (a body of the registry).
 */
enum { DGX_GE_TOUCH_INSIDE = 1, DGX_GE_TOUCH_BELOW = 2, DGX_GE_TOUCH_ABOVE = 4, DGX_GE_TOUCH_LEFT = 8, DGX_GE_TOUCH_RIGHT = 16 };
#define DGX_GE_TOUCH_SIDES 5 /* areas by the place of the bit: inside, below, above, left, right */

typedef struct {
    uint8_t       mask;   /* the mask of the body that touches */
    uint8_t       from;   /* one DGX_GE_TOUCH_*: where what it touches is */
    dgx_ge_area_t area;   /* the points of the mask touching so */
    int           handle; /* a body of the registry, and its mask; handle -1: a tile */
    int           index;
    int16_t       col, row;   /* a tile: its cell */
    uint16_t      tile, kind; /* the tile, and the game's number for it */
    dgx_ge_rect_t solid;      /* the body touched, where it lies (a tile's body; empty for a body of the registry) */
} dgx_ge_touch_t;

typedef void (*dgx_ge_touch_fn)(const dgx_ge_touch_t *touch, void *ctx);

/* The sides (bits) a mask, placed (dgx_ge_mask_place), touches a rectangle of solid on, within reach points; the areas by side. */
unsigned dgx_ge_mask_touch_rect(const dgx_ge_mask_t *m, dgx_ge_rect_t place, bool flip, dgx_ge_rect_t solid, int reach,
                                dgx_ge_area_t areas[DGX_GE_TOUCH_SIDES]);

/*
 * The same among other solids (count rectangles, this one among them or
 * not): a point of the mask inside any of them is inside the solid, not
 * beside this one. The seam of two slabs side by side is no edge: feet on
 * it touch the floor below, and nothing on the left or the right.
 */
unsigned dgx_ge_mask_touch_rect_among(const dgx_ge_mask_t *m, dgx_ge_rect_t place, bool flip, dgx_ge_rect_t solid, int reach,
                                      const dgx_ge_rect_t *solids, int count, dgx_ge_area_t areas[DGX_GE_TOUCH_SIDES]);

/* The same, the other solids told by a function: is a point inside one of them (the bodies of a grid, however many)? */
typedef bool (*dgx_ge_solid_fn)(int x, int y, void *ctx);
unsigned dgx_ge_mask_touch_rect_where(const dgx_ge_mask_t *m, dgx_ge_rect_t place, bool flip, dgx_ge_rect_t solid, int reach,
                                      dgx_ge_solid_fn inside, void *ctx, dgx_ge_area_t areas[DGX_GE_TOUCH_SIDES]);

/* The same against another mask placed: a point of a touches b where b has the point, or one up to reach points away on that side. */
unsigned dgx_ge_mask_touch_mask(const dgx_ge_mask_t *a, dgx_ge_rect_t pa, bool fa, const dgx_ge_mask_t *b, dgx_ge_rect_t pb, bool fb, int reach,
                                dgx_ge_area_t areas[DGX_GE_TOUCH_SIDES]);

/*
 * Bodies of a scene, looked at by what has changed. The game gives a body
 * again whenever it may have changed (its place, picture, turn, active
 * masks); one that has not is not looked at. A look takes every body changed
 * since the last look against the bodies whose masks it may reach: pairs are
 * found by sweeping the boxes of their active masks along x, so many bodies
 * cost little.
 *
 * Who meets whom:
 * - bodies of one owner never meet: they are one whole, a hand and the sword
 *   held in it, a stone not yet thrown;
 * - bodies of one side meet by the masks own lets (dgx_ge_meets_t): a thrown
 *   stone keeps the side of its thrower, so it may pass his hand and still
 *   fall on his head; guards of one side do not cut each other;
 * - bodies of different sides meet by the masks foreign lets.
 * Owner or side 0: nobody's, never the same as another's. Until the game says
 * otherwise, own lets nothing and foreign everything.
 */
typedef struct dgx_ge_collisions dgx_ge_collisions_t;

/* Room for so many bodies, by handles 0 .. capacity - 1; areas in tiles of tile x tile points. */
dgx_ge_collisions_t *dgx_ge_collisions_create(int capacity, int tile);
void                 dgx_ge_collisions_destroy(dgx_ge_collisions_t **c);

/* Which masks meet within a side and across sides, as dgx_ge_pack_meets gives them; NULL: own none, foreign all. */
void dgx_ge_collisions_meets(dgx_ge_collisions_t *c, const dgx_ge_meets_t *meets);

/* A body by its handle, as it is now, of an owner and a side (0: nobody's). */
void dgx_ge_collisions_set(dgx_ge_collisions_t *c, int handle, const dgx_ge_body_t *body, uint16_t owner, uint16_t side);

/* A body gone. */
void dgx_ge_collisions_remove(dgx_ge_collisions_t *c, int handle);

/* The origin of the scene moved by dx, dy points (dgx_ge_scene_shift): every body goes with it, none counts as changed. */
void dgx_ge_collisions_shift(dgx_ge_collisions_t *c, int dx, int dy);

/* A pair of masks of two bodies met: the handle and index of each, where. */
typedef void (*dgx_ge_meet_fn)(int handle_a, int index_a, int handle_b, int index_b, const dgx_ge_area_t *areas, int count, void *ctx);

/* Look at the bodies changed since the last look; fn for every pair of masks that meets. Returns how many pairs of masks met. */
int dgx_ge_collisions_look(dgx_ge_collisions_t *c, dgx_ge_meet_fn fn, void *ctx);

/* What a path met first: the body (its handle), its mask, the point of the scene, how far along the path (0..1). */
typedef struct {
    int   handle, index;
    int   x, y;
    float along;
} dgx_ge_ray_hit_t;

/*
 * The first body a path from x0, y0 to x1, y1 (points of the scene) meets,
 * as a thing with the mask index of an owner and a side would (a shot): the
 * bodies of its owner are passed by, the others met as the meets say (own,
 * foreign) by their active masks, point by point along the path. False when
 * it meets none.
 */
bool dgx_ge_collisions_ray(const dgx_ge_collisions_t *c, int x0, int y0, int x1, int y1, int index, uint16_t owner, uint16_t side,
                           dgx_ge_ray_hit_t *hit);

/*
 * What a body touches among the bodies of the registry: not those of its
 * owner (one whole), by the masks the meets let for its side (own, foreign),
 * its mask given (-1: every active one), within reach points. fn for every
 * mask met on every side it is met on; how many. The body itself need not
 * be in the registry.
 */
int dgx_ge_collisions_touching(const dgx_ge_collisions_t *c, const dgx_ge_body_t *body, uint16_t owner, uint16_t side, int mask, int reach,
                               dgx_ge_touch_fn fn, void *ctx);

/* The sides (DGX_GE_TOUCH_* bits) a mask of a body touches any body of the registry on, as above. */
unsigned dgx_ge_collisions_touch_sides(const dgx_ge_collisions_t *c, const dgx_ge_body_t *body, uint16_t owner, uint16_t side, int mask, int reach);

/*
 * The actor a body is (NULL: none), for those who look for it by what it is:
 * its kind, its params (sense). Kept until the body is removed. The first
 * actor given makes room for a pointer a body; a registry never given one
 * costs nothing more.
 */
struct dgx_ge_actor;
void                       dgx_ge_collisions_actor(dgx_ge_collisions_t *c, int handle, const struct dgx_ge_actor *actor);
const struct dgx_ge_actor *dgx_ge_collisions_actor_of(const dgx_ge_collisions_t *c, int handle);

/* Does a body count (its handle, its actor or NULL)? */
typedef bool (*dgx_ge_near_fn)(int handle, const struct dgx_ge_actor *actor, void *ctx);

/*
 * The body nearest to x, y (points of the scene) by its origin, within
 * radius points: not of owner (0: none passed by), one fn lets (NULL: any).
 * Its handle, -1 when there is none; dx, dy from x, y to its origin. Of
 * equally near ones the lower handle. Every body is looked at: a sense of
 * every actor of a scene is so many bodies squared.
 */
int dgx_ge_collisions_nearest(const dgx_ge_collisions_t *c, int x, int y, int radius, uint16_t owner, dgx_ge_near_fn fn, void *ctx, int *dx,
                              int *dy);

#ifdef __cplusplus
}
#endif
