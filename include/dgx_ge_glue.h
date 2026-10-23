#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dgx_ge_anim.h"
#include "dgx_ge_frame.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_scene.h"

/*
 * What every game writes between its pack and the screen, the same in each: the colors of the pack's sets given to
 * the frame, a picture of the pack as an item of the scene, an actor as it is drawn. (The frame made for the display
 * is dgx_ge_frame_fit; the sound and the step of the loop on a board are the boards', dgx_ge_board.h.) Nothing is
 * kept: the memory is the game's, as it gives it.
 */

/*
 * The colors of every set of a pack given indices of the frame at once, set by set from the first
 * (dgx_ge_frame_add_colors): their maps one after another in maps, a byte a color, size bytes (as many as the sets
 * have colors: <PREFIX>_SET_COLOR_COUNT of the pack's header), and map_of[set] where the map of each begins
 * (pack->sets of them, <PREFIX>_SET_COUNT): what the parts of the engine take as maps; map_of[set][v - 1] is the
 * frame's index of the value v of that set. False when maps is too small (the sets from the first that does not fit
 * on get NULL, and nothing of theirs is given) or the frame's indices are not enough (those sets get the nearest
 * colors, as dgx_ge_frame_add_colors gives them).
 */
bool dgx_ge_frame_pack_colors(dgx_ge_frame_t *frame, const dgx_ge_pack_t *pack, uint8_t *maps, size_t size, const uint8_t **map_of);

/*
 * A picture of the pack (id) in an item: the picture read into image (the game's, until the scene has drawn it), the
 * map of its set from maps (NULL for one of 1 bit, or with no maps); one of 1 bit drawn in item->color of 1..15 gets
 * the frame's index of that color of its set instead. The rest of the item as it is. False when there is no such
 * picture (id -1: none).
 */
bool dgx_ge_item_picture(dgx_ge_item_t *item, const dgx_ge_pack_t *pack, const uint8_t *const *maps, int id, dgx_image_t *image);

/*
 * An actor as it is drawn, in an item: its picture (actor->image, dgx_ge_item_picture), where it stands with the
 * picture's offset (image_dx forward, image_dy down, of its axes: mirrored and turned as it faces), mirrored
 * (DGX_GE_BLIT_FLIP_X by actor->flip, the other flags as they are) and turned (item->turn by actor->turn). The rest of
 * the item (the layer, the key, the clip) as the game set it. False when it shows no picture.
 */
bool dgx_ge_item_actor(dgx_ge_item_t *item, const dgx_ge_actor_t *actor, const dgx_ge_pack_t *pack, const uint8_t *const *maps, dgx_image_t *image);

/* The same, as an actor of this frame of the scene on a layer (dgx_ge_scene_actor); false when it shows nothing or there is no room. */
bool dgx_ge_scene_show_actor(dgx_ge_scene_t *scene, const dgx_ge_actor_t *actor, int layer, const dgx_ge_pack_t *pack, const uint8_t *const *maps,
                             dgx_image_t *image);

#ifdef __cplusplus
}
#endif
