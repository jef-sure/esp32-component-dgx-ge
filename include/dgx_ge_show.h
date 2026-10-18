#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dgx_ge_anim.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_pockets.h"
#include "dgx_ge_scene.h"

/*
 * A show: a scene that plays by itself (a cut scene, a title, the end): a
 * backdrop of pictures, actors that live by their scripts, and cues — the
 * director as data: at so many ms from the start, an event for an actor;
 * the last cue is the end. An event comes at its own moment, however late the
 * call, and the actor plays on from it. A button may cut it short (the game
 * says: dgx_ge_show_skip). Over, the game goes on by an exit of its scene
 * (dgx_ge_graph.h: a show is a scene of the kind DGX_GE_SCENE_SHOW, its map
 * the show).
 *
 * The blob "DGSH", little-endian: "DGSH", version u16, counts of pictures
 * u16, actors u16, cues u16; pictures 10 bytes each: a picture of the pack
 * u16, x i16, y i16 (of its origin), layer u8, flags u8 (DGX_GE_BLIT_*), the
 * color of one of 1 bit u8 (a color of its set, 1..15: the frame's index of
 * it, as the maps given say; with no map of its set, the value itself), 0;
 * actors 10 bytes each: kind u16 (an animation of the pack), x i16, y i16,
 * script u8, layer u8, flip u8, color u8 (the same, for its pictures); cues 8
 * bytes each, in the order of their times: at u32 (ms from the start), actor
 * u8 (0xFF: the end), event u8 (of the pack), 0, 0.
 *
 * While it plays: its actors and the pictures they show (~200 bytes an
 * actor), allocated by dgx_ge_show_start, freed by dgx_ge_show_stop.
 */
#define DGX_GE_SHOW_MAGIC 0x48534744u /* "DGSH" */
#define DGX_GE_SHOW_END   0xFF

typedef struct {
    const uint8_t  *base;
    uint16_t        pictures, actors, cues;
    dgx_ge_actor_t *cast;    /* the actors while it plays, NULL else */
    dgx_image_t    *images;  /* what each shows now; then the backdrop */
    uint32_t        start_ms;
    uint16_t        next_cue;
    bool            over;
} dgx_ge_show_t;

/* Take the blob of a show; false when it is not one. */
bool dgx_ge_show_open(dgx_ge_show_t *show, const void *data, size_t size);

/* It begins at a moment: its actors in their scripts (their kinds opened as asked); false when there is no memory or a kind is missing. */
bool dgx_ge_show_start(dgx_ge_show_t *show, dgx_ge_kinds_t *kinds, uint32_t now_ms);

/* A picture of its backdrop, the i-th: a picture of the pack (its set, for the game to give colors); -1 none such. */
int dgx_ge_show_picture(const dgx_ge_show_t *show, int i);

/* Its actors and their pictures given back. */
void dgx_ge_show_stop(dgx_ge_show_t *show);

/* On to now: every cue come, each at its moment, the actors moved on (what they say, to say); true when it is over. */
bool dgx_ge_show_play(dgx_ge_show_t *show, uint32_t now_ms, dgx_ge_anim_say_fn say, void *ctx);

/* Cut short (a button): over. */
void dgx_ge_show_skip(dgx_ge_show_t *show);

/* Its backdrop into the set of a scene (the set the game has emptied): the pictures of the pack, by the maps of their sets. */
void dgx_ge_show_backdrop(dgx_ge_show_t *show, const dgx_ge_pack_t *pack, const uint8_t *const *maps, dgx_ge_scene_t *scene);

/*
 * How an actor of a show is drawn, when the game draws its people its own way
 * (PoP: by its tables of frames): the item of the actor of a kind (its layer
 * and color given; the picture, its map, the place the game's, the picture
 * kept by the game while the frame is drawn); false: not drawn now.
 */
typedef bool (*dgx_ge_show_place_fn)(const dgx_ge_actor_t *actor, int kind, dgx_ge_item_t *item, void *ctx);

/* Its actors as they are, actors of this frame of a scene: as the game places them (place; NULL: their pictures of the pack, at their places). */
void dgx_ge_show_items(dgx_ge_show_t *show, const dgx_ge_pack_t *pack, const uint8_t *const *maps, dgx_ge_scene_t *scene, dgx_ge_show_place_fn place,
                       void *ctx);

#ifdef __cplusplus
}
#endif
