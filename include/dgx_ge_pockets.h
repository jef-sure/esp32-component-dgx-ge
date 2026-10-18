#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "dgx_ge_anim.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_world.h"

/*
 * Kinds and pockets.
 *
 * A kind of thing is its animation in the pack, and its number is the number
 * of that blob: a shot names its kind so (shoot hand DAGGER), so do pockets
 * and the objects of the world. The kinds open their animations from the
 * pack when first asked, and keep them.
 *
 * Pockets are what a character has besides what is in its hands: a sword in
 * its sheath, daggers in the belt, a potion. A character's animation says how
 * many places it has (pockets N), a thing's kind how many of it go into one
 * place (stack N; none: one). A thing in a pocket does nothing, so it is a
 * record of 8 bytes, not an actor: its kind, the script it was in, its params
 * then, how many; the same (kind, params) stack, a poisoned dagger does not
 * lie with the plain ones (the script is of no matter in a pocket: a dagger
 * picked up lying is one thrown, the stack keeps the script it has).
 *
 * The scripts of the one who has them take things out and put them away, at
 * the moment of the move: draw hand SWORD, stow hand; shoot takes the shot
 * from them (none there: none shot); count(KIND) tells how many there are. A
 * thing drawn is an actor of the pockets, one for each point it may be held
 * at (DGX_GE_ANIM_CARRIED): taken out it lives as one, at the point it was
 * drawn to; put away it is a record again. One let go (release) is free and
 * stays the game's while it flies: the game keeps it as an object of the
 * world before it lets the same hand draw again.
 *
 * Looting is the game's choice: the engine says how many fit, and moves them.
 * What is laid out of a pocket goes into the world where one stands, as an
 * object of its kind, and can be taken again.
 */
#ifndef DGX_GE_POCKET_PARAMS
#define DGX_GE_POCKET_PARAMS 2
#endif

/*
 * The kinds of a pack: the animations of kinds opened as they are asked for,
 * each once (its blob read into a description), a place each that is never
 * given up (actors point at it). As many places as the game says (its
 * constants, dgx_ge_game.h): more kinds than that is an error, told in the
 * log once.
 */
typedef struct dgx_ge_kinds {
    const dgx_ge_pack_t *pack;
    dgx_ge_anim_t       *anims; /* id 0xFFFF: a free place */
    int                  capacity;
    bool                 told;  /* full, and said so */
} dgx_ge_kinds_t;

/* Places for so many kinds of a pack; false when there is no memory. */
bool dgx_ge_kinds_init(dgx_ge_kinds_t *kinds, const dgx_ge_pack_t *pack, int capacity);
void dgx_ge_kinds_free(dgx_ge_kinds_t *kinds);

/* The animation of a kind (a blob of the pack); NULL when it is none, or there is no room for more kinds (an error of the game's constants). */
const dgx_ge_anim_t *dgx_ge_kind(dgx_ge_kinds_t *kinds, int id);

typedef struct dgx_ge_pocket {
    uint16_t kind;   /* its animation: a blob of the pack */
    uint8_t  script; /* the script it was in */
    uint8_t  count;  /* how many: a stack; 0 a free place */
    int16_t  params[DGX_GE_POCKET_PARAMS];
} dgx_ge_pocket_t;

typedef struct dgx_ge_pockets {
    dgx_ge_kinds_t  *kinds;
    dgx_ge_pocket_t *places;
    int              capacity;
    dgx_ge_actor_t  *hands;      /* what is drawn, while it is held: an actor a hand; NULL none (a box) */
    int              hand_count;
} dgx_ge_pockets_t;

/* Pockets of so many places, of the kinds of a pack, and so many hands to draw into (a character: DGX_GE_ANIM_CARRIED; a box: 0). */
dgx_ge_pockets_t *dgx_ge_pockets_create(dgx_ge_kinds_t *kinds, int capacity, int hands);
void              dgx_ge_pockets_destroy(dgx_ge_pockets_t **pockets);

/* All emptied. */
void dgx_ge_pockets_clear(dgx_ge_pockets_t *pockets);

/* The first place with a thing of a kind, -1 none. */
int dgx_ge_pockets_find(const dgx_ge_pockets_t *pockets, int kind);

/* How many things of a kind there are, in all places. */
int dgx_ge_pockets_count(const dgx_ge_pockets_t *pockets, int kind);

/* How many of so many things like this one would go in: into stacks of the same while they are not full, then free places. */
int dgx_ge_pockets_fits(const dgx_ge_pockets_t *pockets, const dgx_ge_pocket_t *thing, int count);

/* So many things like this one put in, as many as go; how many went. */
int dgx_ge_pockets_put(dgx_ge_pockets_t *pockets, const dgx_ge_pocket_t *thing, int count);

/* So many out of a place (as many as there are): what they are into out (NULL: gone); how many. */
int dgx_ge_pockets_take(dgx_ge_pockets_t *pockets, int place, int count, dgx_ge_pocket_t *out);

/* So many from a place of one into the other, as many as go, each sent an event on the way (taken: a sword lying is one held; -1 none); how many went. */
int dgx_ge_pockets_move(dgx_ge_pockets_t *from, int place, int count, dgx_ge_pockets_t *to, int event, uint32_t now_ms);

/* A record as its kind's matrix makes it on an event (taken out of a heap: a sword lying becomes one held); as it was when the event means nothing. */
void dgx_ge_pocket_event(dgx_ge_kinds_t *kinds, dgx_ge_pocket_t *record, int event, uint32_t now_ms);

/* The record of a thing as it is now: its kind (its animation's blob), script and params; count 1. False: its animation is of no kind. */
bool dgx_ge_pocket_of(dgx_ge_pocket_t *record, const dgx_ge_actor_t *thing);

/* An actor of a record, at x, y (pixels), from now on; false when its kind is none. */
bool dgx_ge_pocket_actor(dgx_ge_kinds_t *kinds, const dgx_ge_pocket_t *record, int x, int y, uint32_t now_ms, dgx_ge_actor_t *actor);

/*
 * One thing of a kind drawn from the holder's pockets into its hand at a
 * point (draw in a script): an actor of the pockets held there, in the script
 * it was put away in, its first steps made (its masks in a hand). False: none there, the point holds something, or no
 * hand is free.
 */
bool dgx_ge_pockets_draw(dgx_ge_actor_t *holder, int point, int kind, uint32_t now_ms);

/* What the holder holds at a point put into its pockets (stow in a script); false when nothing is held there or it does not go in. */
bool dgx_ge_pockets_stow(dgx_ge_actor_t *holder, int point);

/*
 * So many things of a place laid out into the world at x, y of a scene
 * (points), facing so, sent an event (the kind's matrix: it falls, it lies,
 * it breaks; -1 none): onto a stack, beside others or into a heap as how
 * says (dgx_ge_world_lay; NULL: just there, a new object). The handle of the
 * object they went to, -1 when the world had no room (they stay in the
 * pocket).
 */
/*
 * Saved and loaded (dgx_ge_save.h): the places only (what is in hand is drawn anew by the game). write/read put the
 * places where the save stands (inside another chunk: the boxes of the world); save/load make a chunk of them by a
 * tag. A load into pockets of another capacity, or of another count of params, is refused and changes nothing.
 * The checks change nothing: read_check reads past the places (as read would) without taking them, load_check
 * leaves even l as it was.
 */
struct dgx_ge_save;
struct dgx_ge_load;
void dgx_ge_pockets_write(const dgx_ge_pockets_t *pockets, struct dgx_ge_save *s);
bool dgx_ge_pockets_read(dgx_ge_pockets_t *pockets, struct dgx_ge_load *l);
bool dgx_ge_pockets_read_check(const dgx_ge_pockets_t *pockets, struct dgx_ge_load *l);
void dgx_ge_pockets_save(const dgx_ge_pockets_t *pockets, struct dgx_ge_save *s, const char tag[4]);
bool dgx_ge_pockets_load(dgx_ge_pockets_t *pockets, struct dgx_ge_load *l, const char tag[4]);
bool dgx_ge_pockets_load_check(const dgx_ge_pockets_t *pockets, const struct dgx_ge_load *l, const char tag[4]);

int32_t dgx_ge_pockets_lay(dgx_ge_pockets_t *pockets, int place, int count, dgx_ge_world_t *world, int scene, int x, int y, bool flip, int event,
                           uint32_t now_ms, const dgx_ge_lay_t *how);

#ifdef __cplusplus
}
#endif
