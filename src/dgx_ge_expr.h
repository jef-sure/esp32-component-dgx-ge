#pragma once

/*
 * Expressions of the blobs of the engine, made by the assembler of
 * animations (tools/dgx_ge_anim.py, Expr): its length u8, then steps for a
 * stack of floats: NUM f32, PARAM u8, T, the operators + - * / and NEG, MIN
 * MAX ABS SIN COS SQRT EXP, VX VY, the comparisons LT LE GT GE EQ NE and AND
 * OR NOT (1 or 0), NOW (the moment of the step, seconds of the clock of the
 * game), COUNT u16 kind (how many of a kind the actor has in its pockets),
 * AMOUNT (how many the actor itself is: a stack lying), MOD AND OR of
 * whole numbers (% & |), RND (rnd(n): the dice of the game, 0..n), TIMER
 * u8 name (timer(name): milliseconds from the start of a timer of the game,
 * dgx_ge_timers.h; -1 when none such is going), COND (c ? a : b: of the three
 * on the stack, a where c is not 0, else b), X Y (where the actor is: points of the scene),
 * and FIELD u8 word, u8 first bit, u8 bits:
 * a field of a word of 32 bits given by the caller (of a cell of tiles: its
 * modifier, its variant), exact. SMOOTH: smoothstep of 0..1.
 *
 * Bits of whole numbers (int32 from the stack, a negative one in two's
 * complement): XOR, BITS (how many ones), BIT (2^n; n outside 0..30: 0),
 * BITFIELD (x, first, count: (x >> first) & (2^count - 1); first outside
 * 0..31 or count <= 0: 0, count >= 32: all from first). TABLE u16: a table
 * of the animation at that offset of its tables (u16 length, int16 values),
 * read at the whole part of the index on the stack; outside: 0. ITS u8:
 * of the one a sense found (dgx_ge_expr_its): its param k, or below
 * ITS_VX and on: its velocity, picture, script, turn, a cell's tile,
 * modifier, number; 0 when nothing is found or it has none. GAME u8: the
 * game's answer to its question of that number (dgx_ge_expr_ask; none: 0).
 *
 * and, or and c ? a : b are lazy, as in C: the part not needed is jumped
 * over, never worked out (it matters to rnd, the one that changes
 * anything: the dice). a and b: a, AND_THEN u8 skip (a 0: 0 stays, jump
 * so many bytes; else a goes), b, TRUTH (1 or 0); a or b: the same with
 * OR_ELSE (a not 0: 1 stays, jump); c ? a : b: c, IF_NOT u8 skip (c taken;
 * 0: jump to b), a, JUMP u8 skip (over b), b. AND, OR, COND (all worked
 * out) are of blobs made before; the assembler writes them no more.
 * Inside the engine only.
 */
#include <stdint.h>

#include "dgx_ge_anim.h"

enum { E_NUM = 1, E_PARAM, E_T, E_ADD, E_SUB, E_MUL, E_DIV, E_NEG, E_MIN, E_MAX, E_ABS, E_SIN, E_COS, E_SQRT, E_EXP, E_VX, E_VY, E_FIELD,
       E_LT, E_LE, E_GT, E_GE, E_EQ, E_NE, E_AND, E_OR, E_NOT, E_NOW, E_COUNT, E_AMOUNT,
       E_MOD, E_BAND, E_BOR, E_RND, E_TIMER, E_COND, E_X, E_Y, E_SMOOTH,
       E_XOR, E_BITS, E_BIT, E_BITFIELD, E_TABLE, E_ITS, E_GAME,
       E_AND_THEN, E_OR_ELSE, E_TRUTH, E_IF_NOT, E_JUMP };

/* What ITS reads beyond the params of the one found. */
enum { ITS_VX = 0xF0, ITS_VY, ITS_IMAGE, ITS_SCRIPT, ITS_FLIP, ITS_TILE, ITS_MOD, ITS_CELL };

/* The one a sense found, for its. while its expression is worked out; NULL otherwise. */
typedef struct {
    const int16_t *params;
    int            count;
    float          vx, vy; /* points a second, x forward of the one looking */
    int            image, script, flip, tile, mod, cell;
} dgx_ge_its_t;
extern const dgx_ge_its_t *dgx_ge_expr_its;

/* How many of a kind (a blob of the pack) one has: count(KIND); kind 0xFFFF: how many it is itself (amount). */
typedef int (*dgx_ge_expr_count_fn)(const void *who, unsigned kind);

/*
 * What an expression reads beside its code: its params, words (NULL: none), the tables of its animation (NULL: none),
 * the time t, a velocity vx, vy, a place x, y, the moment now (ms), counts of kinds (NULL: 0). Made once by whoever
 * works out several expressions of one owner (the pieces of a cell, a path's four): a call takes a pointer, not
 * fifteen numbers (past the sixth, a number of a call goes through the stack of an Xtensa).
 */
typedef struct {
    const int16_t       *params;
    int                  param_count;
    const uint32_t      *words;
    int                  word_count;
    const uint8_t       *tables;
    uint32_t             tables_size;
    float                t, vx, vy, x, y;
    uint32_t             now_ms;
    dgx_ge_expr_count_fn count;
    const void          *who;
} dgx_ge_expr_ctx_t;

/* The value of an expression at p, of what c gives; 0 when it is broken. */
float dgx_ge_expr_eval(const uint8_t *p, const dgx_ge_expr_ctx_t *c);
