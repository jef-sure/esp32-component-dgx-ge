#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "dgx_ge_anim.h"
#include "dgx_ge_pockets.h"

/*
 * Living cells: a cell of a grid (the game's) that does something by itself
 * for a while — a gate going up and down, a loose floor shaking, spikes
 * coming out, a torch burning. What it does is data: the scripts of an
 * animation (its life: the kind the tile names), worked over the cell. Their
 * params are param 0, the modifier of the cell, param 1, the tile in it, and
 * the life's own params from 2 on; a step waits by a frame without a
 * picture (frame 0 83ms).
 *
 * A life is a record of a few bytes (the cell, its kind, where its script
 * is, when its next step is, its own params); an actor is made of it for the
 * moment it plays and given back to it. It begins with an event (a floor
 * trodden on, a gate told to open by a link, a room shown: its torches): with
 * no life the cell gets one in script 0 of its kind (a rest), and the event
 * goes into its matrix. It ends when its script holds: what the cell has
 * become stays in its modifier and tile, and the next event begins it again
 * from there (script 0 looks at the modifier). The game may end one itself
 * (a torch out of sight).
 *
 * The cells are the game's: it reads and writes them (get, set), and hears
 * the signals of their scripts (a floor gives way: the game lets a piece of
 * it fall) at the moment of their steps.
 *
 * A life may start timers (timer start <name>: a button stays down so long):
 * their owner is the cell, in the map the cells are of; the game gives a
 * timer gone off of a cell back to its life (dgx_ge_cells_timer). The
 * timers of the cells of a map go with it (reset map: their cells are no
 * more); a cell does not know its scene, so reset scene stops none of them.
 */
struct dgx_ge_timers;
struct dgx_ge_timer;
#ifndef DGX_GE_CELL_OWN
#define DGX_GE_CELL_OWN 2 /* own params of a life, from param 2 on */
#endif

typedef struct {
    uint16_t cell; /* the game's number of the cell */
    uint16_t kind; /* its life: an animation of the pack */
    uint8_t  script, pending, pending_when;
    uint16_t pc;
    uint32_t due_ms;
    int16_t  own[DGX_GE_CELL_OWN];
} dgx_ge_life_t;

typedef struct {
    void (*get)(uint16_t cell, int *tile, int *mod, void *ctx);              /* the tile and the modifier of a cell now */
    void (*set)(uint16_t cell, int tile, int mod, void *ctx);                /* changed by its script: given anew */
    void (*signal)(uint16_t cell, int value, uint32_t at_ms, void *ctx);     /* a signal of its script; NULL: none heard */
    void *ctx;
    uint16_t cells; /* how many cells the game has: a life of a cell past them is none (an event, a timer, a load); 0: not said */
    void (*sound)(uint16_t cell, int sound, uint32_t at_ms, void *ctx);      /* a sound of its script (the game: heard there?); NULL: none */
} dgx_ge_cells_io_t;

typedef struct {
    dgx_ge_kinds_t   *kinds;
    dgx_ge_cells_io_t io;
    dgx_ge_life_t    *lives; /* in the order they began */
    int               count, capacity;
    struct dgx_ge_timers *timers; /* the timers lives start (NULL: none) */
    uint8_t           map;        /* the map the cells are of: where their timers are */
} dgx_ge_cells_t;

/* Room for so many lives at once, of the kinds of a pack, on the game's cells; false when there is no memory. */
bool dgx_ge_cells_init(dgx_ge_cells_t *cells, dgx_ge_kinds_t *kinds, int capacity, const dgx_ge_cells_io_t *io);
void dgx_ge_cells_free(dgx_ge_cells_t *cells);

/* Every life gone (another map: its cells are others). */
void dgx_ge_cells_clear(dgx_ge_cells_t *cells);

/*
 * An event for a cell of a life of a kind, at a moment: its life, or a new
 * one in script 0 (no room for it: nothing), told the event, moved on to
 * now. own (-1 none): an own param of it given a value first (how long a
 * gate stays up: the link that opened it says). False when no life was
 * there or could begin.
 */
bool dgx_ge_cells_event(dgx_ge_cells_t *cells, uint16_t cell, uint16_t kind, int event, int own, int16_t value, uint32_t now_ms);

/*
 * A timer gone off (dgx_ge_timers_play): one of a cell, its event to the life
 * of the cell (life_of: the kind its tile names) at the moment it went off;
 * false when it was not of a cell, or had no event.
 */
bool dgx_ge_cells_timer(dgx_ge_cells_t *cells, const struct dgx_ge_timer *timer, uint32_t at_ms, uint16_t (*life_of)(uint16_t cell, void *ctx),
                        void *ctx);

/* Every life moved on to now, one after another as they began; those that hold end. */
void dgx_ge_cells_play(dgx_ge_cells_t *cells, uint32_t now_ms);

/*
 * Saved (a chunk CELL: the lives as they are) and loaded, their times moved on by later_ms; the tiles and
 * modifiers the lives have set are the game's to save. Refused, nothing changed: more lives than fit, another
 * count of own params, a kind that is no animation, a script or a passing waited for past its scripts, a cell past
 * the game's. The check alone reads and checks, changing nothing (not even l).
 */
struct dgx_ge_save;
struct dgx_ge_load;
void dgx_ge_cells_save(const dgx_ge_cells_t *cells, struct dgx_ge_save *s);
bool dgx_ge_cells_load(dgx_ge_cells_t *cells, struct dgx_ge_load *l, uint32_t later_ms);
bool dgx_ge_cells_load_check(const dgx_ge_cells_t *cells, const struct dgx_ge_load *l);

/* An own param of the life of a cell given a value (what the game knows and its script reads at its next step); false: not living. */
bool dgx_ge_cells_set(dgx_ge_cells_t *cells, uint16_t cell, int own, int16_t value);

/* Is a cell living? */
bool dgx_ge_cells_living(const dgx_ge_cells_t *cells, uint16_t cell);

/* The life of a cell ended by the game. */
void dgx_ge_cells_stop(dgx_ge_cells_t *cells, uint16_t cell);

#ifdef __cplusplus
}
#endif
