#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

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

#ifdef __cplusplus
}
#endif
