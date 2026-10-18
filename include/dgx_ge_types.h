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

#ifdef __cplusplus
}
#endif
