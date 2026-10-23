#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* A rectangle in pixels; empty when its width or height is not positive. */
typedef struct {
    int16_t x, y, w, h;
} dgx_ge_rect_t;

static inline dgx_ge_rect_t dgx_ge_rect(int x, int y, int w, int h)
{
    return (dgx_ge_rect_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
}

static inline bool dgx_ge_rect_empty(dgx_ge_rect_t r)
{
    return r.w <= 0 || r.h <= 0;
}

static inline int dgx_ge_rect_area(dgx_ge_rect_t r)
{
    return dgx_ge_rect_empty(r) ? 0 : (int)r.w * r.h;
}

static inline dgx_ge_rect_t dgx_ge_rect_intersect(dgx_ge_rect_t a, dgx_ge_rect_t b)
{
    int l = a.x > b.x ? a.x : b.x, t = a.y > b.y ? a.y : b.y;
    int r = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int d = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    if (r <= l || d <= t) return dgx_ge_rect(0, 0, 0, 0);
    return dgx_ge_rect(l, t, r - l, d - t);
}

static inline bool dgx_ge_rect_overlap(dgx_ge_rect_t a, dgx_ge_rect_t b)
{
    return !dgx_ge_rect_empty(dgx_ge_rect_intersect(a, b));
}

/* The smallest rectangle around both; an empty one adds nothing. */
static inline dgx_ge_rect_t dgx_ge_rect_union(dgx_ge_rect_t a, dgx_ge_rect_t b)
{
    if (dgx_ge_rect_empty(a)) return b;
    if (dgx_ge_rect_empty(b)) return a;
    int l = a.x < b.x ? a.x : b.x, t = a.y < b.y ? a.y : b.y;
    int r = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int d = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return dgx_ge_rect(l, t, r - l, d - t);
}

static inline bool dgx_ge_rect_equal(dgx_ge_rect_t a, dgx_ge_rect_t b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

/*
 * The way a picture lies, and an actor with it: mirrored left to right (flip, bit 0: facing the other way than its
 * pictures), then turned clockwise by quarter turns (bits 1-2: 0, 90, 180, 270 degrees), both about its origin. Eight
 * in all, the eight of DGX (dgx_ge_orient_output, dgx_ge_image.h). What is said in the axes of an actor (x forward,
 * y down of its pictures: a velocity, an offset, a point, a mask) goes into the scene so; 0 is as it is, 1 mirrored,
 * so a bool flip given where an orientation goes means what it always meant.
 */
static inline unsigned dgx_ge_orient(bool flip, int quarters)
{
    return (flip ? 1u : 0u) | ((unsigned)quarters & 3u) << 1;
}

/* Its three answers: are x and y of its axes swapped in the scene (a quarter turn either way), and is x, y of the scene against them? */
static inline bool dgx_ge_orient_swaps(unsigned o)
{
    return (o & 2u) != 0;
}
static inline bool dgx_ge_orient_x_back(unsigned o) /* x of the picture runs right to left in the scene (or up, swapped) */
{
    return (0x5Au >> (o & 7u) & 1u) != 0;
}
static inline bool dgx_ge_orient_y_back(unsigned o) /* y of the picture runs bottom to top (or right to left, swapped) */
{
    return (0x3Cu >> (o & 7u) & 1u) != 0;
}

/* The orientation that undoes it: a mirror is its own, a quarter turn the other way. */
static inline unsigned dgx_ge_orient_inverse(unsigned o)
{
    o &= 7u;
    return o == 2u ? 6u : o == 6u ? 2u : o;
}

/* A vector of its axes (x forward, y down) in the scene. */
static inline void dgx_ge_orient_apply(unsigned o, int32_t *x, int32_t *y)
{
    const int32_t a = dgx_ge_orient_x_back(o) ? -*x : *x, b = dgx_ge_orient_y_back(o) ? -*y : *y;
    if (dgx_ge_orient_swaps(o)) *x = b, *y = a;
    else *x = a, *y = b;
}

static inline void dgx_ge_orient_apply_f(unsigned o, float *x, float *y)
{
    const float a = dgx_ge_orient_x_back(o) ? -*x : *x, b = dgx_ge_orient_y_back(o) ? -*y : *y;
    if (dgx_ge_orient_swaps(o)) *x = b, *y = a;
    else *x = a, *y = b;
}

/*
 * Where a rectangle of its axes, the points x, y .. x + w - 1, y + h - 1 from the origin (a mask, a picture: -origin_x,
 * -origin_y, width, height), lies in the scene, the origin at ox, oy; a quarter turn swaps its width and height.
 */
static inline dgx_ge_rect_t dgx_ge_orient_rect(unsigned o, int ox, int oy, int x, int y, int w, int h)
{
    const bool xb = dgx_ge_orient_x_back(o), yb = dgx_ge_orient_y_back(o);
    const int  along = xb ? -(x + w - 1) : x, down = yb ? -(y + h - 1) : y; /* the first point of each axis in the scene */
    if (dgx_ge_orient_swaps(o)) return dgx_ge_rect(ox + down, oy + along, h, w);
    return dgx_ge_rect(ox + along, oy + down, w, h);
}

/* Which point of such a rectangle, column and row of its own, is the point u, v of where it lies (from its left, top). */
static inline void dgx_ge_orient_back(unsigned o, int w, int h, int u, int v, int *col, int *row)
{
    if (dgx_ge_orient_swaps(o)) {
        const int t = u;
        u = v, v = t;
    }
    *col = dgx_ge_orient_x_back(o) ? w - 1 - u : u;
    *row = dgx_ge_orient_y_back(o) ? h - 1 - v : v;
}

#ifdef __cplusplus
}
#endif
