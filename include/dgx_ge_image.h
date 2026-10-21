#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dgx_image.h"
#include "dgx_ge_types.h"

/*
 * Pictures are those of DGX (dgx_image.h): unpacked, 1, 4 or 8 bits a pixel,
 * 0 always transparent, an origin they stand by and are mirrored about, and
 * a map of their values to indices of the frame. dgx_image_blit() draws them.
 * Here is what the engine adds: the frame as rows of indices to fill, and
 * where a picture lies.
 */

/* The flags of a picture in a scene, besides those of dgx_ge_scene.h. */
#define DGX_GE_BLIT_FLIP_X     1u /* mirrored left to right about the column of its origin */
#define DGX_GE_BLIT_SILHOUETTE 2u /* every pixel that is not 0 becomes the color, whatever the map */

/* Where a picture drawn with its origin at x, y lies, each of its pixels a square of scale (0, 1: as it is). */
static inline dgx_ge_rect_t dgx_ge_image_box_scaled(const dgx_image_t *image, int x, int y, bool flip, int scale)
{
    const int n    = scale > 1 ? scale : 1;
    const int left = flip ? x - (image->width - 1 - image->origin_x) * n : x - image->origin_x * n;
    return dgx_ge_rect(left, y - image->origin_y * n, image->width * n, image->height * n);
}

/* Where a picture drawn with its origin at x, y lies. */
static inline dgx_ge_rect_t dgx_ge_image_box(const dgx_image_t *image, int x, int y, bool flip)
{
    return dgx_ge_image_box_scaled(image, x, y, flip, 1);
}

/* Rows of indices and the rectangle that may be changed. */
typedef struct {
    uint8_t      *pixels;
    int           stride;
    dgx_ge_rect_t clip;
} dgx_ge_canvas_t;

/* Fill a rectangle of the canvas, cut by its clip. */
void dgx_ge_canvas_fill(const dgx_ge_canvas_t *canvas, dgx_ge_rect_t r, uint8_t color);

/*
 * Besides the flags of the blit (DGX_GE_BLIT_*), an item may be:
 * - a fill: no picture, its clip filled with its color: a strip of black a
 *   game wipes a place with, a brick of a color;
 * - on a card: its picture drawn on a rectangle of its color as large as the
 *   picture, as a game of old drew a picture that hides what is behind it.
 */
#define DGX_GE_ITEM_FILL 0x40u
#define DGX_GE_ITEM_CARD 0x80u
/*
 * Repeated: its picture laid again and again over its clip, the first with
 * its origin at x, y, the next a picture's width on, a height down (the edge
 * of a frame, a floor of tiles of one picture). Its clip is where it lies.
 */
#define DGX_GE_ITEM_REPEAT 0x10u
/*
 * An actor seen only in the open space it is in, as the space of its layer
 * says (dgx_ge_scene_space): a person under a ceiling, behind a wall. Its clip
 * is its body (empty: the box of its picture), its feet at the bottom of the
 * body, feet_dx from its middle (where it stands, which may not be the middle
 * of what it shows); the scene puts there what of it is seen. A child of it
 * (dgx_ge_item_child) is seen where its carrier is.
 */
#define DGX_GE_ITEM_OPEN_SPACE 0x20u
/*
 * Seen in open space, and gone into an opening (dgx_ge_tiles.h: a door it
 * goes up the stairs behind): seen only through the opening by its feet as
 * well. The game says when, as it makes the item: by the picture of the frame.
 */
#define DGX_GE_ITEM_INSIDE 0x04u

typedef struct {
    const dgx_image_t *image; /* NULL: nothing is drawn, unless it is a fill */
    const uint8_t        *map;   /* the values 1.. to indices of the frame, see dgx_ge_image.h */
    int16_t               x, y;  /* of the origin, in the scene (an overlay: pixels of the display) */
    int16_t               key;   /* in a layer ordered by key */
    dgx_ge_rect_t         clip;  /* in the scene (an overlay of the frame: in pixels of the display); empty: no clip of its own */
    uint8_t               layer;
    uint8_t               flags; /* DGX_GE_BLIT_*, DGX_GE_ITEM_* */
    uint8_t               color; /* of a 1-bit picture, a silhouette, a fill, a card */
    int8_t                feet_dx; /* seen in open space: its feet from the middle of the bottom of its body (no more room than the padding) */
    uint8_t               scale;   /* each pixel of its picture a square of so many (0, 1: as it is): a picture in display pixels at the frame's scale */
} dgx_ge_item_t;

/* Are two items drawn the same (where, what, how)? */
static inline bool dgx_ge_item_same(const dgx_ge_item_t *a, const dgx_ge_item_t *b)
{
    return a->image == b->image && a->map == b->map && a->x == b->x && a->y == b->y && a->key == b->key && dgx_ge_rect_equal(a->clip, b->clip) &&
           a->layer == b->layer && a->flags == b->flags && a->color == b->color && a->scale == b->scale;
}

/*
 * An item drawn into a screen of 8 bits (the frame's canvas, a strip of it): its origin at x, y of that screen, its
 * fill or card over box, all of it cut by clip (both in that screen's pixels); a repeated picture laid over the clip.
 */
void dgx_ge_item_draw(dgx_screen_t *into, const dgx_ge_item_t *item, int x, int y, dgx_ge_rect_t box, dgx_ge_rect_t clip);

#ifdef __cplusplus
}
#endif
