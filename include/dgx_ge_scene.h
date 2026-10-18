#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "dgx_ge_frame.h"
#include "dgx_ge_image.h"
#include "dgx_ge_pack.h"

/*
 * The 2.5D of the engine: a scene is pictures in layers, drawn layer after
 * layer, the first one at the back. How many layers and what each is, the
 * game says: a game of rooms has its floor and walls behind the hero, the
 * hero, and the fronts of walls and columns before him; a game that scrolls
 * may have far hills that move slower than the ground.
 *
 * - A layer moves with the window of the scene by its factor: 256 moves as the
 *   window does, 128 half as far (further away), 0 not at all (a sky, a panel).
 * - Within a layer pictures go in the order they were given, or by a key, a
 *   larger key nearer: the y of the feet, for a game seen from above.
 * - A picture stands with its origin at x, y of the scene and may have a clip
 *   of its own, a rectangle of the scene it is not drawn outside of: a hero
 *   under a ledge, in a doorway.
 *
 * Two kinds of pictures:
 *
 * - the set: what a scene is made of, given once when the scene is shown. A
 *   piece of it can be changed later through its handle: a flame, a gate.
 * - the actors: everything that moves, given anew for every frame between
 *   dgx_ge_scene_actors_begin() and dgx_ge_scene_render().
 *
 * Nothing is drawn at once. dgx_ge_scene_render() finds the places that have
 * changed: where an actor was and is, a piece of the set that changed, all of
 * it when the window moved. Every such place is drawn anew from the paper up,
 * every picture that touches it cut by it, and sent to the display. Nothing is
 * kept from under a sprite: the frame is in RAM, a place is made again.
 */
#define DGX_GE_SCENE_MAX_LAYERS 8

typedef enum {
    DGX_GE_ORDER_ADDED = 0, /* the order pictures were given in, the set before the actors */
    DGX_GE_ORDER_KEY   = 1, /* by key, the smaller first; equal keys as they were given */
} dgx_ge_order_t;

/*
 * What of a body is seen where it stands (dgx_ge_space_clip of a grid of
 * tiles, or the game's own): a clip in points of the scene. body: its
 * outline; feet: the point it stands at; inside: gone into an opening
 * (DGX_GE_ITEM_INSIDE).
 */
typedef dgx_ge_rect_t (*dgx_ge_space_fn)(const void *space, dgx_ge_rect_t body, int feet_x, int feet_y, bool inside);

typedef struct {
    int16_t         factor;   /* of the window, 256 = 1 */
    uint8_t         order;    /* dgx_ge_order_t */
    dgx_ge_space_fn space_fn; /* the open space actors of this layer are seen in; NULL none */
    const void     *space;
    bool            pixels;   /* in pixels of the display (dgx_ge_scene_layer_pixels) */
} dgx_ge_layer_t;


typedef struct {
    dgx_ge_frame_t *frame;
    uint8_t         paper;
    uint8_t         layer_count;
    dgx_ge_layer_t  layers[DGX_GE_SCENE_MAX_LAYERS];
    int16_t         window_x, window_y; /* the point of the scene at the top-left of the frame, for a factor of 256 */
    int16_t         shown_window_x, shown_window_y;
    bool            window_shown;
    bool            quiet; /* the set is being given anew: what is added marks nothing */
    dgx_ge_item_t  *set;
    uint16_t        set_count, set_capacity;
    dgx_ge_item_t  *actors;
    int8_t         *actor_z; /* of each actor: 0 a carrier or one on its own, else the z of a child next to its carrier */
    dgx_ge_item_t  *shown; /* the actors as they were drawn last, pointing at shown_images */
    dgx_image_t    *shown_images; /* their pictures as they were: the game may write another into its own meanwhile */
    uint16_t        actor_count, shown_count, actor_capacity;
    uint16_t        group_start; /* where the last carrier given and its children begin among the actors */
    uint32_t       *order; /* set and actors in the order they are drawn, see dgx_ge_scene.c */
} dgx_ge_scene_t;

/* A scene of layers, all moving with the window and in the order given, that holds so many pieces of the set and actors. */
dgx_ge_scene_t *dgx_ge_scene_create(dgx_ge_frame_t *frame, int layers, int set_capacity, int actor_capacity);
void            dgx_ge_scene_destroy(dgx_ge_scene_t **scene);

static inline void dgx_ge_scene_layer(dgx_ge_scene_t *scene, int layer, int factor, dgx_ge_order_t order)
{
    if (layer < 0 || layer >= scene->layer_count) return;
    scene->layers[layer].factor = (int16_t)factor;
    scene->layers[layer].order  = (uint8_t)order;
}

/* Forget the set and the actors; the frame gets the paper everywhere when it is drawn next: a new room. */
void dgx_ge_scene_reset(dgx_ge_scene_t *scene, uint8_t paper);

/*
 * A layer in pixels of the display, not points of the scene: the interface (text, a dialog), drawn at the display's
 * own resolution whatever the frame's scale (dgx_ge_frame_scale), over everything of the frame. Its actors go to the
 * frame as overlays (dgx_ge_frame_overlay): x, y and clip in pixels of the display; no window, no factor, no open
 * space, no children. Pictures are not scaled: text is laid for the display (dgx_ge_talk_view_pixels).
 */
static inline void dgx_ge_scene_layer_pixels(dgx_ge_scene_t *scene, int layer, bool pixels)
{
    if (layer >= 0 && layer < scene->layer_count) scene->layers[layer].pixels = pixels;
}

/* The open space the actors of a layer are seen in (DGX_GE_ITEM_OPEN_SPACE): fn and what it looks at, kept by the game; NULL none. */
void dgx_ge_scene_space(dgx_ge_scene_t *scene, int layer, dgx_ge_space_fn fn, const void *space);

/* Where the window of the scene is: the point at the top-left of the frame. */
void dgx_ge_scene_window(dgx_ge_scene_t *scene, int x, int y);
/*
 * Moved a little, with every layer of factor 256 (or in pixels of the display), the frame scrolls instead
 * (dgx_ge_frame_scroll: the display moves its picture itself when it can) and only the strips that came in are
 * drawn; a layer of another factor makes all of it drawn anew.
 */

/*
 * Move the origin of the scene by dx, dy points: the set, the window and what
 * was shown last go dx, dy towards it, so nothing on the display changes and
 * nothing is drawn anew. Points of the scene are int16: a world without end
 * (a flight over a land made as it goes) moves its origin before its window
 * runs out of them, and keeps where the origin is itself, best as a number
 * that goes round (uint32): only differences mean anything. The actors, the
 * grid (x, y) and the bodies of collisions go with it by their own calls
 * (dgx_ge_actor_shift, dgx_ge_collisions_shift). A layer of a factor f moves
 * by d * f / 256: false, and nothing moved, unless that is whole for every
 * layer; a shift by a multiple of 256 always is.
 */
bool dgx_ge_scene_shift(dgx_ge_scene_t *scene, int dx, int dy);

/*
 * Give the set anew without drawing all of it anew: between these two, what
 * is added marks nothing changed, and the game marks the places that did with
 * dgx_ge_scene_invalidate(). For a world that changes a tile at a time: build
 * it all again, it is quick; send only the tile.
 */
void dgx_ge_scene_set_begin(dgx_ge_scene_t *scene);
void dgx_ge_scene_set_end(dgx_ge_scene_t *scene);

/* A place of a layer of the scene has changed: it is drawn anew. */
void dgx_ge_scene_invalidate(dgx_ge_scene_t *scene, int layer, dgx_ge_rect_t place);

/* Add a piece of the set; returns its handle, or -1 when the scene is full. */
int dgx_ge_scene_add(dgx_ge_scene_t *scene, const dgx_ge_item_t *item);

/* Show a piece of the set otherwise: another frame of a flame, a gate raised. */
void dgx_ge_scene_change(dgx_ge_scene_t *scene, int handle, const dgx_ge_item_t *item);

/* Begin the actors of a frame: those of the last one are taken away. */
void dgx_ge_scene_actors_begin(dgx_ge_scene_t *scene);

/* An actor of this frame; false when there is no room for it. */
bool dgx_ge_scene_actor(dgx_ge_scene_t *scene, const dgx_ge_item_t *item);

/*
 * A child of the actor given last (see dgx_ge_item_child()): drawn next to it,
 * behind it when z < 0, before it when z > 0, children by their z, the larger
 * nearer; never between it and another actor. The child should keep the
 * layer and key of its carrier. False when there is no room or no carrier.
 */
bool dgx_ge_scene_actor_child(dgx_ge_scene_t *scene, const dgx_ge_item_t *item, int z);

/*
 * A picture carried by another: a sword in a hand, a stone held. It stands
 * at dx, dy from the origin of the carrier as its picture is drawn unturned
 * (right, down), so it turns round with it; it is in the layer and at the key
 * of the carrier, cut as it is, turned and a silhouette as it is. Give it with
 * dgx_ge_scene_actor_child(), right after its carrier.
 */
static inline dgx_ge_item_t dgx_ge_item_child(const dgx_ge_item_t *carrier, const dgx_image_t *image, const uint8_t *map, int dx, int dy)
{
    const bool flip = carrier->flags & DGX_GE_BLIT_FLIP_X;
    return (dgx_ge_item_t){
        .image = image,
        .map   = map,
        .x     = (int16_t)(carrier->x + (flip ? -dx : dx)),
        .y     = (int16_t)(carrier->y + dy),
        .key   = carrier->key,
        .clip  = carrier->clip,
        .layer = carrier->layer,
        .flags = (uint8_t)(carrier->flags & (DGX_GE_BLIT_FLIP_X | DGX_GE_BLIT_SILHOUETTE | DGX_GE_ITEM_OPEN_SPACE | DGX_GE_ITEM_INSIDE)),
        .color = carrier->color,
    };
}

/*
 * A child held at a point of its carrier's picture (a sword in a hand,
 * dgx_ge_pack_point): the child's own point of that name, if its picture has
 * one (where it is held: the hilt), goes to it, else its origin; mirrored with
 * the carrier, as dgx_ge_item_child. carrier_image, image: the numbers of the
 * pictures in the pack. z: the point's, for dgx_ge_scene_actor_child. False
 * when the carrier's picture has no such point: nothing is held in that frame.
 */
static inline bool dgx_ge_item_held(const dgx_ge_pack_t *pack, const dgx_ge_item_t *carrier, int carrier_image, int point, const dgx_image_t *image,
                                    const uint8_t *map, int image_id, dgx_ge_item_t *child, int *z)
{
    dgx_ge_point_t at, grip = {0, 0, 1};
    if (!dgx_ge_pack_point(pack, carrier_image, point, &at)) return false;
    dgx_ge_pack_point(pack, image_id, point, &grip);
    *child = dgx_ge_item_child(carrier, image, map, at.x - grip.x, at.y - grip.y);
    if (z) *z = at.z;
    return true;
}

/*
 * Between the display and the scene: where the frame is on the display, its
 * scale (dgx_ge_frame_scale: pixels a point), the window and the factor of
 * the layer, for a layer far behind moves slower than the window. A touch, a
 * pointer, a marker on the screen.
 */

/* The point of the scene under a pixel of the display, in a layer; false when the pixel is not on the frame. */
bool dgx_ge_scene_point_at(const dgx_ge_scene_t *scene, int layer, int display_x, int display_y, int *x, int *y);

/* The pixel of the display a point of the scene is at, in a layer; it may lie outside the frame. */
void dgx_ge_scene_pixel_at(const dgx_ge_scene_t *scene, int layer, int x, int y, int *display_x, int *display_y);

/* Draw what has changed into the frame only; dgx_ge_frame_present() sends it. */
void dgx_ge_scene_draw(dgx_ge_scene_t *scene);

/* Draw what has changed into the frame and send it to the display. */
static inline void dgx_ge_scene_render(dgx_ge_scene_t *scene)
{
    dgx_ge_scene_draw(scene);
    dgx_ge_frame_present(scene->frame);
}

#ifdef __cplusplus
}
#endif
