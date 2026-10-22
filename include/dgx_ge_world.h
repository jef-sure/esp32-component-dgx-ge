#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "dgx_ge_anim.h"

/*
 * Objects of the world: what lies in scenes nobody looks at, and lives on
 * there. Shards of a flask, a corpse that turns to bones, a stone thrown into
 * another room: an object of the world is remembered as a record, not as an
 * actor, and becomes an actor again when its scene is shown.
 *
 * A record is the point its actor entered the script it is in: when, where,
 * which way it faced, its params then (dgx_ge_actor_t.entered_*). Its script
 * makes it from there what it is at any later moment, so a scene shown again
 * half an hour later has its objects as they would be: started at their
 * points and moved on to now (dgx_ge_actor_advance), every step on the way
 * made. This holds as far as the script moves the object: what it sets
 * itself after it has entered (velocity, a path, masks, what it holds). The
 * game may not move an object of the world behind its script's back; it
 * sends it events, and the script it goes into is a new point.
 *
 * The records are a ring: a new one goes into a free place; when there is
 * none, it takes the place of the oldest. A record kept (its script says
 * keep: a sword laid down, a whole pot, not its shards) is passed over: the
 * oldest of the rest goes; when all are kept, the new one does not go in. A
 * record put into a place freed among the others counts as old as that place
 * in the ring. A record of DGX_GE_WORLD_PARAMS params (4 bytes each) costs 28 bytes with two.
 */
#ifndef DGX_GE_WORLD_PARAMS
#define DGX_GE_WORLD_PARAMS 2
#endif

typedef struct {
    uint16_t scene;    /* whose, a number of the game */
    uint16_t kind;     /* what: its animation, a blob of the pack (dgx_ge_kind) */
    uint8_t  script;   /* the script it entered */
    bool     flip : 1; /* which way it faced */
    bool     keep : 1; /* not given up to newer records */
    uint8_t  count;    /* how many lie here, a stack of the same (0 as 1) */
    uint8_t  box;      /* what it holds (a heap, a chest): its box + 1 (dgx_ge_world_boxes), 0 none */
    int32_t  x, y;   /* where it entered, 1/256 of a point of the scene */
    uint32_t since_ms; /* when, on the clock of the game */
    int32_t  params[DGX_GE_WORLD_PARAMS]; /* its params then; the rest start as declared */
} dgx_ge_object_t;

typedef struct dgx_ge_world dgx_ge_world_t;
struct dgx_ge_pockets;
struct dgx_ge_kinds;
struct dgx_ge_pocket;

/* Room for so many records. */
dgx_ge_world_t *dgx_ge_world_create(int capacity);
void            dgx_ge_world_destroy(dgx_ge_world_t **world);

/* Every record gone. */
void dgx_ge_world_clear(dgx_ge_world_t *world);

/* A new object; returns its handle. In a free place; none, the oldest record not kept is lost to it; all kept: -1, it is not added. */
int32_t dgx_ge_world_add(dgx_ge_world_t *world, const dgx_ge_object_t *object);

/* The record of a handle; false when it is gone (removed, or lost to a newer one). */
bool dgx_ge_world_get(const dgx_ge_world_t *world, int32_t handle, dgx_ge_object_t *object);

/* The record of a handle given anew; false when it is gone. */
bool dgx_ge_world_put(dgx_ge_world_t *world, int32_t handle, const dgx_ge_object_t *object);

/* An object gone from the world. */
void dgx_ge_world_remove(dgx_ge_world_t *world, int32_t handle);

/*
 * Saved (a chunk WRLD: the ring as it is, every record with its serial, then the boxes' places) and loaded, times
 * moved on by later_ms (the clock of the game at the load less the one at the save). A load into a world of
 * another capacity, count of params or boxes, or of an object in a box past them, is refused and changes nothing.
 * The check alone reads and checks, changing nothing (not even l).
 */
struct dgx_ge_save;
struct dgx_ge_load;
void dgx_ge_world_save(const dgx_ge_world_t *world, struct dgx_ge_save *s);
bool dgx_ge_world_load(dgx_ge_world_t *world, struct dgx_ge_load *l, uint32_t later_ms);
bool dgx_ge_world_load_check(const dgx_ge_world_t *world, const struct dgx_ge_load *l);

/* The objects of a scene, oldest first: the one after handle (-1: the first); -1 when there is none. */
int32_t dgx_ge_world_next(const dgx_ge_world_t *world, int scene, int32_t after);

/* The record of an actor: the point it entered its script, its kind if its animation knows it, kept as its script says; the scene is left as it is. */
void dgx_ge_object_of(dgx_ge_object_t *object, const dgx_ge_actor_t *actor);

/*
 * Boxes: what objects hold inside, a heap of things on the floor, a chest.
 * So many, of so many places each, pockets without hands, of the kinds of a
 * pack; an object holds one (its box). False when there is no memory: none
 * is made then.
 */
bool dgx_ge_world_boxes(dgx_ge_world_t *world, struct dgx_ge_kinds *kinds, int count, int places);

/* The box of an object, NULL none. */
struct dgx_ge_pockets *dgx_ge_world_box(dgx_ge_world_t *world, int32_t handle);

/*
 * A box for a new object: its number + 1 (for dgx_ge_object_t.box), empty. It is the object's once it is added. None
 * free: the oldest record not kept that holds one gives it up and is forgotten, as a record gives way in
 * dgx_ge_world_add (the world remembers a thing whole or not at all); 0 when every box is held by a kept record.
 */
int dgx_ge_world_new_box(dgx_ge_world_t *world);

/*
 * An object added with what a character holds (an actor leaving the scene): a box for it when the pockets hold
 * anything (dgx_ge_world_new_box), the object, then all of the pockets that fits moved into the box (no event: as it
 * is held). Its handle; -1 when there is no box or no place for it, the world and the pockets as they were.
 */
int32_t dgx_ge_world_add_holding(dgx_ge_world_t *world, const dgx_ge_object_t *object, struct dgx_ge_pockets *pockets, uint32_t now_ms);

/*
 * Laying a thing down: so many of it, where one stands. The same things
 * lie in one stack; others side by side, so they can be told apart; where
 * there is no room, in a heap (an object with a box) that shows one picture.
 */
typedef struct {
    int16_t step;  /* points between things lying side by side */
    int16_t reach; /* how far from where one stands they may lie */
    int     heap;  /* the kind of a heap (a blob of the pack), -1 none: with no room nothing is laid */
    bool (*can_lie)(int x, int y, void *ctx); /* may a thing lie there (a floor, no wall)? NULL: anywhere */
    void *ctx;
} dgx_ge_lay_t;

/*
 * A thing (a record of pockets, script and params as it is) laid at x, y of a
 * scene (points), facing so, sent an event (dropped: its kind's matrix makes
 * it lie, or break; -1 none), from now on: onto a stack of the same lying
 * within reach, else at the nearest free place (x, then a step right, left,
 * two right... while within reach, where nothing lies nearer than a step and
 * it can lie), else into a heap within reach, else into a new heap at x (the
 * thing lying there goes into it too). The handle of the object it went to,
 * -1 when it could not (the world or the boxes full).
 */
int32_t dgx_ge_world_lay(dgx_ge_world_t *world, struct dgx_ge_kinds *kinds, const struct dgx_ge_pocket *thing, int count, int scene, int x, int y,
                         bool flip, int event, uint32_t now_ms, const dgx_ge_lay_t *how);

/* A heap after it was searched: empty, it is gone; one thing left, the heap is that thing lying where it was. Not a heap: nothing. */
void dgx_ge_world_tidy(dgx_ge_world_t *world, int32_t handle, int heap, uint32_t now_ms);

/*
 * The actor of a record, at the point it entered its script, of the
 * animation the game knows its kind by. The game gives it the terrain of the
 * scene, if it looks for contacts, and moves it on to now.
 */
void dgx_ge_object_actor(const dgx_ge_object_t *object, const dgx_ge_anim_t *anim, dgx_ge_actor_t *actor);

#ifdef __cplusplus
}
#endif
