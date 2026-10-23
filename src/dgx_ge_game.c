#include <stdlib.h>
#include <string.h>

#include "dgx_ge_game.h"
#include "dgx_ge_save.h"

#define HEADER_BYTES 8u

static int32_t i32_at(const uint8_t *v)
{
    return (int32_t)((uint32_t)v[0] | (uint32_t)v[1] << 8 | (uint32_t)v[2] << 16 | (uint32_t)v[3] << 24);
}

bool dgx_ge_game_open(dgx_ge_game_t *g, const void *data, size_t size)
{
    memset(g, 0, sizeof(*g));
    const uint8_t *p = data;
    if (!p || size < HEADER_BYTES || (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24) != DGX_GE_GAME_MAGIC ||
        (p[4] | p[5] << 8) != DGX_GE_GAME_VERSION)
        return false;
    const unsigned count = (unsigned)(p[6] | p[7] << 8);
    if (HEADER_BYTES + 4u * count > size) return false;
    g->count  = (uint16_t)count;
    g->values = p + HEADER_BYTES;
    return true;
}

int32_t dgx_ge_game_value(const dgx_ge_game_t *g, int index, int32_t otherwise)
{
    if (!g || index < 0 || index >= g->count) return otherwise;
    return i32_at(g->values + 4 * index);
}

bool dgx_ge_vars_open(dgx_ge_vars_t *v, const void *data, size_t size)
{
    memset(v, 0, sizeof(*v));
    const uint8_t *p = data;
    if (!p) return true; /* no variables */
    if (size < HEADER_BYTES || (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24) != DGX_GE_VARS_MAGIC ||
        (p[4] | p[5] << 8) != DGX_GE_VARS_VERSION)
        return false;
    const unsigned count = (unsigned)(p[6] | p[7] << 8);
    if (HEADER_BYTES + 4u * count > size) return false;
    if (count && !(v->values = malloc(4u * count))) return false;
    v->count   = (uint16_t)count;
    v->initial = p + HEADER_BYTES;
    dgx_ge_vars_reset(v);
    return true;
}

void dgx_ge_vars_free(dgx_ge_vars_t *v)
{
    if (dgx_ge_game_vars == v) dgx_ge_game_vars = NULL;
    free(v->values);
    memset(v, 0, sizeof(*v));
}

void dgx_ge_vars_reset(dgx_ge_vars_t *v)
{
    for (int i = 0; i < v->count; ++i) v->values[i] = i32_at(v->initial + 4 * i);
}

void dgx_ge_vars_save(const dgx_ge_vars_t *v, dgx_ge_save_t *s)
{
    dgx_ge_save_chunk(s, "VARS");
    dgx_ge_save_u16(s, v->count);
    for (int i = 0; i < v->count; ++i) dgx_ge_save_i32(s, v->values[i]);
    dgx_ge_save_chunk_end(s);
}

/* The chunk read and checked; taken into the variables when into is given (they are v), else v is as it was. */
static bool load(const dgx_ge_vars_t *v, dgx_ge_vars_t *into, dgx_ge_load_t *l)
{
    if (!dgx_ge_load_chunk(l, "VARS")) return false;
    const unsigned count = dgx_ge_load_u16(l);
    if (l->failed || count != v->count || dgx_ge_load_left(l) < 4u * count) return false;
    for (unsigned i = 0; i < count; ++i) {
        const int32_t value = dgx_ge_load_i32(l);
        if (into) into->values[i] = value;
    }
    return true;
}

bool dgx_ge_vars_load_check(const dgx_ge_vars_t *v, const dgx_ge_load_t *l)
{
    dgx_ge_load_t at = *l;
    return load(v, NULL, &at);
}

bool dgx_ge_vars_load(dgx_ge_vars_t *v, dgx_ge_load_t *l)
{
    return dgx_ge_vars_load_check(v, l) && load(v, v, l);
}
