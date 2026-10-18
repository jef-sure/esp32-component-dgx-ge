#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dgx_ge_anim.h"
#include "dgx_ge_mask.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_pockets.h"
#include "dgx_ge_scene.h"
#include "dgx_ge_tiles.h"

/*
 * A crowd: actors of kinds that live in a scene by their scripts alone, a
 * herd and the wolf after it, a swarm, a town. Each finds the others by sense,
 * steers by steer, meets them by its masks (hit: eaten, bitten), is born of
 * another (appear ... with its genes) and dies (vanish). What every such
 * world needs besides the scripts is here, not in the game: the actors in
 * their places, their bodies in a registry, the terrain they sense and touch
 * (the bounds of the world, solid beyond them, and a grid of walls if the game
 * gives one), the births and the deaths, the meetings of masks, the pictures
 * in the scene.
 *
 * The blob "DGCR" (dgx_ge_pack.py, Pack.add_crowd), little-endian:
 *   header  24 bytes: "DGCR", version u16, places u16 (how many actors at once), kinds u16, groups u16, the bounds x, y
 *           i16, w, h u16 (points of the scene), the seed of its dice u32
 *   kinds   8 bytes each: kind u16 (an animation of the pack), side u16 (in the registry: who meets whom, as the pack's
 *           meets say: own, foreign), layer u8 (of the scene), damage u8 (the param a hit's damage is taken off, 0xFF
 *           none), color u8 (of its pictures of 1 bit, as dgx_ge_show), order u8 (1: its key in the layer is its y, a
 *           world seen from above; 0: none)
 *   groups  16 bytes each: kind u8 (of the kinds above), script u8, count u16, a rectangle x, y i16, w, h u16 they
 *           are strewn in (by the dice of the seed: the same every time), flip u8 (0 right, 1 left, 2 either), 3 spare
 *
 * In a play (dgx_ge_crowd_play) every actor is moved on to now, the dice of
 * the game being the crowd's own. Its terrain: a sense of bodies is the
 * registry (each actor's body as it was at the end of the last play: itself
 * not seen), a sense of cells and the touching of masks the grid, a look
 * (sense ... seen) veiled by the grid's tiles (dgx_ge_grid_sight), and the
 * bounds as walls (contact <mask> <side> on/off: at the moment, as against a
 * grid). An appear puts an actor of a kind of the crowd into a free place,
 * its params given (appear ... with), told its event, moved on to now in the
 * same play; none free: none appears (told in the log). A vanish frees the
 * place after the play (seen till then). Then the bodies are given anew, owner
 * the place (its own masks never meet), side its kind's, and the masks that
 * meet do as their scripts say (dgx_ge_actor_hit of both): an event for the
 * one met, a recoil for itself, the damage off the met one's param. What
 * else the scripts say (a sound, a signal, a shot) goes to the game's say.
 *
 * Memory, from the start to the stop: a place an actor and a picture (about
 * 200 bytes on an ESP32), the registry of as many bodies; nothing before.
 * A sense looks at every body: actors squared, at each step that senses.
 */
#define DGX_GE_CROWD_MAGIC 0x52434744u /* "DGCR" */
#define DGX_GE_CROWD_FREE  0xFFFF

/* A place of a crowd. The actor first: the crowd knows a place by its actor. */
typedef struct {
    dgx_ge_actor_t actor;
    dgx_image_t    image; /* the picture it shows, for the scene */
    uint16_t       kind;  /* a blob of the pack; DGX_GE_CROWD_FREE: the place is free */
    uint8_t        entry; /* its kind among the crowd's */
    bool           gone;  /* it vanished: the place is freed after the play */
    bool           born;  /* it appeared in this play: it is moved on to now after the others */
} dgx_ge_member_t;

typedef struct {
    const uint8_t *base;
    uint16_t       places, kind_count, groups;
    dgx_ge_rect_t  bounds;
    uint32_t       seed; /* the dice of its actors: the blob's at the start, on from there */
    /* from the start to the stop; the crowd stays where it is meanwhile (its actors point at its terrain) */
    const dgx_ge_pack_t *pack;
    dgx_ge_kinds_t      *kinds;
    const dgx_ge_grid_t *grid; /* walls, the game's; NULL: none */
    dgx_ge_member_t     *members;
    dgx_ge_collisions_t *bodies;
    dgx_ge_terrain_t     terrain;
    /* during a play: what the crowd does not do itself goes to the game */
    dgx_ge_anim_say_fn say;
    void              *say_ctx;
    uint32_t           now_ms;
    bool               told_full; /* an appear found no free place, and the log said so */
} dgx_ge_crowd_t;

/* Take the blob of a crowd; false when it is not one. */
bool dgx_ge_crowd_open(dgx_ge_crowd_t *crowd, const void *data, size_t size);

/*
 * It begins at a moment: its places, its registry (the pack's meets), its groups strewn and started in their scripts,
 * the kinds opened as asked; grid: the walls (NULL none), kept by the game. Their first steps are made at now (an
 * appear, a vanish of them done; a sound or a signal of them told nobody). False when there is no memory or a kind of
 * it is missing (nothing is kept then); a blob of more than 255 kinds does not open.
 */
bool dgx_ge_crowd_start(dgx_ge_crowd_t *crowd, const dgx_ge_pack_t *pack, dgx_ge_kinds_t *kinds, const dgx_ge_grid_t *grid, uint32_t now_ms);

/* Its places and registry given back. */
void dgx_ge_crowd_stop(dgx_ge_crowd_t *crowd);

/* On to now: every actor moved on, born, gone, met; what it does not do itself, to say (NULL: nothing). */
void dgx_ge_crowd_play(dgx_ge_crowd_t *crowd, uint32_t now_ms, dgx_ge_anim_say_fn say, void *ctx);

/* Its actors as they are, actors of this frame of a scene: their pictures of the pack, by the maps of their sets. False when the scene had no room for all. */
bool dgx_ge_crowd_items(dgx_ge_crowd_t *crowd, const uint8_t *const *maps, dgx_ge_scene_t *scene);

/* How many of a kind (a blob of the pack) live now; DGX_GE_CROWD_FREE: all of them. */
int dgx_ge_crowd_count(const dgx_ge_crowd_t *crowd, uint16_t kind);

#ifdef __cplusplus
}
#endif
