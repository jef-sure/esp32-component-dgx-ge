#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The constants of a game: numbers its author writes and the engine takes as
 * they are, so much and no more — how many kinds of things it opens, lives of
 * cells at once, records of the world, boxes and their places, shots,
 * timers; and numbers of its own (an hour to rescue the princess). The game
 * reads them and gives the engine's parts their sizes.
 *
 * The blob "GAME" (dgx_ge_pack.py, Pack.add_game): "DGGM", version u16,
 * count u16, then the values, i32 each, in the order the author wrote them;
 * the header names their places: <PREFIX>_GAME_<name>.
 */
#define DGX_GE_GAME_MAGIC   0x4D474744u /* "DGGM" */
#define DGX_GE_GAME_VERSION 1u

typedef struct {
    const uint8_t *values;
    uint16_t       count;
} dgx_ge_game_t;

/* Take the blob of the constants; false when it is not one (no constants: count 0). */
bool dgx_ge_game_open(dgx_ge_game_t *game, const void *data, size_t size);

/* A constant by its place; otherwise when the game wrote none there. */
int32_t dgx_ge_game_value(const dgx_ge_game_t *game, int index, int32_t otherwise);

/*
 * The game's variables: numbers that change as the game plays and belong to no actor (its settings, a score, the
 * state of its rules), one store for the whole game, 4 bytes a variable. The pack declares them with the values they
 * begin with (dgx_ge_pack.py, Pack.add_variable; the document's "variables"); the header names their numbers,
 * <PREFIX>_GAME_VAR_<NAME>, and how many, <PREFIX>_GAME_VAR_COUNT. Scripts and the conditions of dialogs read them as
 * game_var.<name>; scripts write them (set game_var.<name> <expression>, add game_var.<name> <expression>: within 32
 * bits, as params), and controls of dialogs edit them; the game's C reads and writes values[] by those numbers.
 *
 * The blob "VARIABLES": "DGVR", version u16, count u16, then the values they begin with, i32 each, by their numbers.
 */
#define DGX_GE_VARS_MAGIC   0x52564744u /* "DGVR" */
#define DGX_GE_VARS_VERSION 1u

typedef struct dgx_ge_vars {
    int32_t       *values;  /* by the numbers of the header */
    uint16_t       count;
    const uint8_t *initial; /* the values they begin with, in the blob (i32 each); NULL none */
} dgx_ge_vars_t;

/*
 * The store of the variables of the blob (a pack without one: data NULL, no variables), each as it begins: 4 bytes of
 * the heap a variable. False when it is not one, or there is no memory (the store empty then).
 */
bool dgx_ge_vars_open(dgx_ge_vars_t *vars, const void *data, size_t size);
void dgx_ge_vars_free(dgx_ge_vars_t *vars);

/* Every variable as it begins (a new game: the game's own choice which it calls this for; its settings it keeps). */
void dgx_ge_vars_reset(dgx_ge_vars_t *vars);

/* A variable by its number; 0 when there is none such. */
static inline int32_t dgx_ge_var(const dgx_ge_vars_t *vars, int index)
{
    return vars && index >= 0 && index < vars->count ? vars->values[index] : 0;
}

/* A variable given a value, within its 32 bits (beyond them the nearest limit, as a param); none such: nothing. */
static inline void dgx_ge_var_set(dgx_ge_vars_t *vars, int index, int64_t value)
{
    if (vars && index >= 0 && index < vars->count) vars->values[index] = value < INT32_MIN ? INT32_MIN : value > INT32_MAX ? INT32_MAX : (int32_t)value;
}

/*
 * The variables expressions read and scripts write (game_var.<name>): the game points it at its store once; NULL: they
 * read 0 and a write changes nothing. A script writes one as its step is made, heard or not; a rehearsal
 * (dgx_ge_actor_rehearse: a thing made again from its record, its script from where it entered it played once already;
 * a control of a dialog) writes none. A write that changed it tells the game (DGX_GE_ANIM_VAR) when there is a say.
 */
extern dgx_ge_vars_t *dgx_ge_game_vars;

/*
 * Saved (a chunk VARS: the count, the values) and loaded. Refused, nothing changed: no such chunk (a save made before the
 * game had variables: they stay as they are), another count. The check alone reads and checks, changing nothing.
 */
struct dgx_ge_save;
struct dgx_ge_load;
void dgx_ge_vars_save(const dgx_ge_vars_t *vars, struct dgx_ge_save *s);
bool dgx_ge_vars_load(dgx_ge_vars_t *vars, struct dgx_ge_load *l);
bool dgx_ge_vars_load_check(const dgx_ge_vars_t *vars, const struct dgx_ge_load *l);

#ifdef __cplusplus
}
#endif
