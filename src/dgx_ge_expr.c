#include <math.h>
#include <string.h>

#include "dgx_ge_expr.h"
#include "dgx_ge_timers.h"

#define EXPR_STACK 16

uint32_t *dgx_ge_dice;
const dgx_ge_its_t *dgx_ge_expr_its;
dgx_ge_ask_fn       dgx_ge_expr_ask;
void               *dgx_ge_expr_ask_ctx;
const dgx_ge_input_t *dgx_ge_expr_input;

/* Of the one found: a param, or what else ITS names; 0 when none. */
static float its_value(unsigned k)
{
    const dgx_ge_its_t *w = dgx_ge_expr_its;
    if (!w) return 0;
    if (k < ITS_VX) return w->params && (int)k < w->count ? w->params[k] : 0;
    switch (k) {
    case ITS_VX: return w->vx;
    case ITS_VY: return w->vy;
    case ITS_IMAGE: return (float)w->image;
    case ITS_SCRIPT: return (float)w->script;
    case ITS_FLIP: return (float)w->flip;
    case ITS_TILE: return (float)w->tile;
    case ITS_MOD: return (float)w->mod;
    case ITS_CELL: return (float)w->cell;
    default: return 0;
    }
}

/*
 * One switch on the code of a step: the codes are dense (1..51), so it is a table of jumps, not a chain of comparisons.
 * Each step checks what it needs (the bytes after it, the stack) itself; broken, the value is 0.
 */
float dgx_ge_expr_eval(const uint8_t *p, const dgx_ge_expr_ctx_t *c)
{
    float     stack[EXPR_STACK];
    int       depth = 0;
    const int n     = p[0];
    /* a step that puts a value: room for it on the stack, and so many bytes after its code */
#define PUSHES(bytes)                                \
    do {                                             \
        if (depth == EXPR_STACK) return 0;           \
        if (i + (bytes) > n + 1) return 0;           \
    } while (0)
    /* a step that takes so many values */
#define TAKES(k)                                     \
    do {                                             \
        if (depth < (k)) return 0;                   \
    } while (0)
    for (int i = 1; i <= n;) {
        const uint8_t op = p[i++];
        switch (op) {
        case E_NUM:
            PUSHES(4);
            memcpy(&stack[depth++], &p[i], 4); /* the blob is little-endian, as the chips are */
            i += 4;
            break;
        case E_PARAM:
            PUSHES(0);
            stack[depth++] = p[i] < c->param_count ? c->params[p[i]] : 0;
            ++i;
            break;
        case E_T: PUSHES(0); stack[depth++] = c->t; break;
        case E_VX: PUSHES(0); stack[depth++] = c->vx; break;
        case E_VY: PUSHES(0); stack[depth++] = c->vy; break;
        case E_X: PUSHES(0); stack[depth++] = c->x; break;
        case E_Y: PUSHES(0); stack[depth++] = c->y; break;
        case E_NOW: PUSHES(0); stack[depth++] = (float)c->now_ms / 1000.0f; break;
        case E_FIELD: {
            PUSHES(3);
            const uint32_t word  = p[i] < c->word_count && c->words ? c->words[p[i]] : 0;
            const unsigned first = p[i + 1] & 31, bits = p[i + 2];
            const uint32_t v     = word >> first;
            stack[depth++]       = (float)(bits >= 32 ? v : v & ((1u << bits) - 1));
            i += 3;
            break;
        }
        case E_COUNT:
            PUSHES(2);
            stack[depth++] = c->count ? (float)c->count(c->who, (unsigned)(p[i] | p[i + 1] << 8)) : 0;
            i += 2;
            break;
        case E_AMOUNT: PUSHES(0); stack[depth++] = c->count ? (float)c->count(c->who, 0xFFFF) : 1; break;
        case E_TIMER:
            PUSHES(1);
            stack[depth++] = (float)dgx_ge_timers_elapsed(dgx_ge_timers_seen, p[i++], c->now_ms);
            break;
        case E_ITS:
            PUSHES(1);
            stack[depth++] = its_value(p[i++]);
            break;
        case E_GAME:
            PUSHES(1);
            stack[depth++] = dgx_ge_expr_ask ? dgx_ge_expr_ask(p[i], dgx_ge_expr_ask_ctx) : 0;
            ++i;
            break;
        case E_AXIS:
            PUSHES(1);
            stack[depth++] = dgx_ge_expr_input && p[i] < DGX_GE_AXES ? dgx_ge_expr_input->axes[p[i]] * (1.0f / DGX_GE_AXIS_MAX) : 0;
            ++i;
            break;
        case E_ABS:
        case E_SIN:
        case E_COS:
        case E_SQRT:
        case E_EXP: {
            TAKES(1);
            float *x = &stack[depth - 1];
            *x       = op == E_ABS ? fabsf(*x) : op == E_SIN ? sinf(*x) : op == E_COS ? cosf(*x) : op == E_SQRT ? (*x > 0 ? sqrtf(*x) : 0) : expf(*x);
            break;
        }
        case E_SMOOTH: {
            /* smoothstep: 0 below 0, 1 above 1, between them s^2 (3 - 2 s): an easing that starts and stops without a jerk */
            TAKES(1);
            float      *x = &stack[depth - 1];
            const float s = *x < 0 ? 0 : *x > 1 ? 1 : *x;
            *x            = s * s * (3 - 2 * s);
            break;
        }
        case E_TABLE: {
            /* a table of the animation: u16 length, int16 values; outside it (or no tables) 0 */
            if (depth < 1 || i + 2 > n + 1) return 0;
            const uint32_t at = (uint32_t)(p[i] | p[i + 1] << 8);
            i += 2;
            float         *x   = &stack[depth - 1];
            const uint32_t len = c->tables && at + 2 <= c->tables_size ? (uint32_t)(c->tables[at] | c->tables[at + 1] << 8) : 0;
            const int32_t  k   = (int32_t)*x;
            *x = k >= 0 && (uint32_t)k < len && at + 2 + 2 * (uint32_t)k + 2 <= c->tables_size
                     ? (float)(int16_t)(c->tables[at + 2 + 2 * k] | c->tables[at + 3 + 2 * k] << 8)
                     : 0;
            break;
        }
        case E_BITS:
        case E_BIT: {
            TAKES(1);
            float        *x = &stack[depth - 1];
            const int32_t v = (int32_t)*x;
            *x              = op == E_BITS ? (float)__builtin_popcount((uint32_t)v) : v >= 0 && v <= 30 ? (float)(1u << v) : 0;
            break;
        }
        case E_BITFIELD: {
            /* (x >> first) & (2^count - 1), of the three on the stack */
            TAKES(3);
            depth -= 2;
            const uint32_t v     = (uint32_t)(int32_t)stack[depth - 1];
            const int32_t  first = (int32_t)stack[depth], bits = (int32_t)stack[depth + 1];
            stack[depth - 1] = first < 0 || first > 31 || bits <= 0 ? 0 : (float)(bits >= 32 ? v >> first : (v >> first) & ((1u << bits) - 1));
            break;
        }
        case E_RND: {
            /* the dice of the game: the next of its numbers, 0..n */
            TAKES(1);
            float *x = &stack[depth - 1];
            if (!dgx_ge_dice || *x < 0) {
                *x = 0;
                break;
            }
            *dgx_ge_dice = *dgx_ge_dice * 214013u + 2531011u;
            *x           = (float)((*dgx_ge_dice >> 16) % ((uint32_t)*x + 1));
            break;
        }
        case E_JUMP:
            if (i + 1 > n + 1 || i + 1 + p[i] > n + 1) return 0;
            i += 1 + p[i];
            break;
        case E_IF_NOT: {
            if (i + 1 > n + 1 || i + 1 + p[i] > n + 1) return 0;
            const int skip = p[i++];
            TAKES(1);
            if (stack[--depth] == 0) i += skip; /* c taken; 0: jump to b */
            break;
        }
        case E_AND_THEN:
        case E_OR_ELSE: {
            if (i + 1 > n + 1 || i + 1 + p[i] > n + 1) return 0;
            const int skip = p[i++];
            TAKES(1);
            float *x = &stack[depth - 1];
            if ((op == E_AND_THEN) == (*x == 0)) {
                *x = op == E_OR_ELSE; /* the answer known: 0 of and, 1 of or; the other part jumped over */
                i += skip;
            } else {
                --depth; /* the other part says */
            }
            break;
        }
        case E_TRUTH:
            TAKES(1);
            stack[depth - 1] = stack[depth - 1] != 0;
            break;
        case E_COND:
            TAKES(3);
            depth -= 2;
            stack[depth - 1] = stack[depth - 1] != 0 ? stack[depth] : stack[depth + 1];
            break;
        case E_NEG: TAKES(1); stack[depth - 1] = -stack[depth - 1]; break;
        case E_NOT: TAKES(1); stack[depth - 1] = stack[depth - 1] == 0; break;
        /* of two: x op y, the result where x was */
#define BINARY(code, expr)                           \
    case code: {                                     \
        TAKES(2);                                    \
        const float y = stack[--depth];              \
        float      *x = &stack[depth - 1];           \
        expr;                                        \
        break;                                       \
    }
        BINARY(E_ADD, *x += y)
        BINARY(E_SUB, *x -= y)
        BINARY(E_MUL, *x *= y)
        BINARY(E_DIV, *x = y != 0 ? *x / y : 0)
        BINARY(E_MIN, *x = *x < y ? *x : y)
        BINARY(E_MAX, *x = *x > y ? *x : y)
        BINARY(E_LT, *x = *x < y)
        BINARY(E_LE, *x = *x <= y)
        BINARY(E_GT, *x = *x > y)
        BINARY(E_GE, *x = *x >= y)
        BINARY(E_EQ, *x = *x == y)
        BINARY(E_NE, *x = *x != y)
        BINARY(E_AND, *x = *x != 0 && y != 0)
        BINARY(E_OR, *x = *x != 0 || y != 0)
        BINARY(E_MOD, *x = (int32_t)y != 0 ? (float)((int32_t)*x % (int32_t)y) : 0)
        BINARY(E_BAND, *x = (float)((int32_t)*x & (int32_t)y))
        BINARY(E_BOR, *x = (float)((int32_t)*x | (int32_t)y))
        BINARY(E_XOR, *x = (float)((int32_t)*x ^ (int32_t)y))
#undef BINARY
        default: return 0;
        }
    }
#undef PUSHES
#undef TAKES
    return depth == 1 ? stack[0] : 0;
}
