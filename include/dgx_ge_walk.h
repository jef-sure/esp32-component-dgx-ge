#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "dgx_ge_anim.h"

/*
 * Walking a grid: from the middle of a cell to the middle of the next, at a
 * speed (points a second), as in a maze (Pac-Man), a board (Bomberman,
 * Sokoban), a snake. A walker goes on the way it goes; a turn asked for
 * (want) is taken at the first middle of a cell where that side is open,
 * kept till then; where the way on is closed it stands at the middle. Back
 * the way it came it turns at once, wherever it is. Past the edge of the grid,
 * where the grid says so, it comes in at the other side (a tunnel).
 *
 * Where it may go is the game's: open(walker, col, row, side) — into that cell
 * by that side (a wall: never; the door of the ghosts' house: only for eyes).
 * At every middle of a cell the game hears it came (arrive, at the very moment
 * it came there, however late the call) and may say where next (want: a ghost
 * choosing its way, dgx_ge_walk_toward) or change its speed.
 *
 * The way it went is exact: the distance is whole 1/256 of a point and the
 * time it took is kept in thousandths, so no step is ever lost or gained.
 *
 * Cutting corners (corner, points; 0 never: a ghost): a turn asked for no
 * farther than so many points before the middle of a cell, or past it (a
 * cell at most), is taken at once where that side is open there: it goes the
 * new way already, and what it is off the middle across it (off) goes as far
 * toward it as it goes on — a corner cut on the slant, as the arcade's
 * Pac-Man. Turning before the middle is coming to that cell: the game hears
 * it (arrive) then. A turn asked while it is still off the middle is taken
 * at the next middle; turned back in a cut and then off it, it goes to the
 * middle it is off first.
 * Sides are DGX_GE_SIDE_* (dgx_ge_graph.h: left 0, right 1, up 2, down 3).
 */
#define DGX_GE_WALK_STILL (-1)

typedef struct dgx_ge_walker {
    int16_t  col, row;  /* the cell whose middle it passed last (or stands at) */
    int32_t  pos;       /* 1/256 of a point from that middle, the way it goes (below 0: short of it, out of a corner) */
    int8_t   dir;       /* the side it goes to, DGX_GE_WALK_STILL standing */
    int8_t   want;      /* the side asked for, -1 none */
    uint8_t  kind;      /* the game's: who it is (for open) */
    uint32_t speed;     /* points a second, × 256 */
    uint32_t at_ms;     /* the moment it is so */
    uint32_t rest;      /* of the way to the next 1/256 of a point: thousandths of it */
    uint8_t  corner;    /* points it may cut a corner by, 0 none */
    int32_t  off_x, off_y; /* 1/256 of a point off the middle across its way (cutting a corner): to 0 as it goes */
} dgx_ge_walker_t;

typedef struct {
    int16_t cols, rows;
    int16_t cell_w, cell_h; /* points */
    bool    wrap_x, wrap_y; /* past a side of the grid: the other side (a tunnel) */
    bool (*open)(const dgx_ge_walker_t *walker, int col, int row, int side, void *ctx);
    void *ctx;
} dgx_ge_walk_grid_t;

/* It came to the middle of a cell at a moment: the game may set want, speed. */
typedef void (*dgx_ge_walk_arrive_fn)(dgx_ge_walker_t *walker, uint32_t at_ms, void *ctx);

/* A walker at the middle of a cell, standing, from a moment, at a speed (points a second × 256); its corner and kind kept. */
void dgx_ge_walk_start(dgx_ge_walker_t *walker, int col, int row, uint32_t speed, uint32_t now_ms);

/* On to now: through every middle of a cell on the way, each at its moment (arrive told), turning as asked. */
void dgx_ge_walk_play(dgx_ge_walker_t *walker, const dgx_ge_walk_grid_t *grid, uint32_t now_ms, dgx_ge_walk_arrive_fn arrive, void *ctx);

/*
 * A side asked for at a moment (on to it first): back the way it goes, it
 * turns at once; standing, it goes at once if that side is open; else it is
 * kept for the next middle where it is open. -1: nothing asked.
 */
void dgx_ge_walk_want(dgx_ge_walker_t *walker, const dgx_ge_walk_grid_t *grid, int side, uint32_t now_ms, dgx_ge_walk_arrive_fn arrive,
                      void *ctx);

/* Where it is: the point of the scene (the middle of its cell moved along its way). */
void dgx_ge_walk_xy(const dgx_ge_walker_t *walker, const dgx_ge_walk_grid_t *grid, int *x, int *y);

/*
 * Which way toward a target cell from the middle of a cell, going a way (not
 * back it, unless nothing else is open): of the open sides, the one whose
 * next cell is nearest the target (straight, squared); equal: up, left,
 * down, right. -1 when nothing is open.
 */
int dgx_ge_walk_toward(const dgx_ge_walker_t *walker, const dgx_ge_walk_grid_t *grid, int target_col, int target_row);

/*
 * A walker bound to an actor: the walker moves it, as a maze game's runner
 * and its chasers. Kept beside the actor, not in it: an actor that walks no
 * grid pays nothing (a binding is 16 bytes more than its walker).
 *
 * While bound (actor not NULL) the binding is the actor's mover: each play
 * (and want) puts the actor where the walker is, at the walker's moment, at
 * its velocity along the way (diagonal while it cuts a corner), so between
 * the calls it goes on smoothly by itself; a path or a velocity of its
 * script is overwritten there. At every middle of a cell it is put there at
 * that moment, advanced to it first, before the game hears arrive (an
 * event the game tells it then begins where it is). To let its script move
 * it (into a house in the maze, a jump), unbind it (actor = NULL, in arrive
 * too: from that moment on the binding does not touch it, it goes on at
 * the velocity it was last given till its script moves it; the walker
 * walks alone) and start it again from where the script left it.
 *
 * What it is told: the side it goes, as it changes, at the moment it
 * changes (the middle where it turned, the moment of want): the event
 * turn + side (DGX_GE_SIDE_*: its scripts face that way), and the side in
 * the param way. With way the side is told when it differs from the param
 * (its scripts may set it), else from the side told last. Standing, the
 * event stop, once; after a stop told, the side it goes is told again.
 * Its speed: the param speed (points a second x 256) is taken at every
 * middle, before arrive (so the game may set another there). -1: none of
 * them (the game sets walker.speed itself).
 */
typedef struct {
    dgx_ge_walker_t walker;
    dgx_ge_actor_t *actor;  /* bound to, NULL none */
    int16_t         dx, dy; /* where the grid's corner is in the scene, points: the actor is at the walker's point moved so */
    int16_t         turn;   /* the event of the side it goes: turn + side, -1 none */
    int16_t         stop;   /* the event of standing, -1 none */
    int8_t          speed;  /* the param of its speed, -1 none */
    int8_t          way;    /* the param of the side it goes, -1 none */
    int8_t          side;   /* the side told last, -1 none since the start (the way it goes, or went last) */
    bool            stood;  /* stop told since */
} dgx_ge_walk_bind_t;

/*
 * Bound to an actor from where it is: the cell its point is in (its place
 * less dx, dy), going a side from there (DGX_GE_WALK_STILL: standing), the
 * actor's place kept to the point. Short of the cell's middle on that side,
 * it is past the middle of the cell behind (it hears arrive at its own);
 * off the line of the middles across its way, it goes to it as it goes on,
 * as out of a corner. Its speed the param's (or walker.speed as it is), its
 * corner and kind kept. Nothing is told yet: the side at the first play.
 */
void dgx_ge_walk_bind_start(dgx_ge_walk_bind_t *bind, dgx_ge_actor_t *actor, const dgx_ge_walk_grid_t *grid, int side, uint32_t now_ms);

/*
 * On to now as dgx_ge_walk_play, the actor put and told as it goes; ctx is
 * given to arrive and to say (what the actor's steps tell the game).
 */
void dgx_ge_walk_bind_play(dgx_ge_walk_bind_t *bind, const dgx_ge_walk_grid_t *grid, uint32_t now_ms, dgx_ge_walk_arrive_fn arrive,
                           dgx_ge_anim_say_fn say, void *ctx);

/* A side asked for at a moment, as dgx_ge_walk_want, the actor put and told. */
void dgx_ge_walk_bind_want(dgx_ge_walk_bind_t *bind, const dgx_ge_walk_grid_t *grid, int side, uint32_t now_ms, dgx_ge_walk_arrive_fn arrive,
                           dgx_ge_anim_say_fn say, void *ctx);

#ifdef __cplusplus
}
#endif
