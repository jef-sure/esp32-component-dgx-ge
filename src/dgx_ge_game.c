#include <string.h>

#include "dgx_ge_game.h"

#define HEADER_BYTES 8u

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
    const uint8_t *v = g->values + 4 * index;
    return (int32_t)((uint32_t)v[0] | (uint32_t)v[1] << 8 | (uint32_t)v[2] << 16 | (uint32_t)v[3] << 24);
}
