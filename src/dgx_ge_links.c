#include <string.h>

#include "dgx_ge_links.h"

#define HEADER_BYTES 8u
#define LINK_BYTES   6u
#define LAST         1u

static inline uint16_t u16_at(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

bool dgx_ge_links_open(dgx_ge_links_t *l, const void *data, size_t size)
{
    memset(l, 0, sizeof(*l));
    const uint8_t *p = data;
    if (!p || size < HEADER_BYTES || (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24) != DGX_GE_LINKS_MAGIC ||
        u16_at(p + 4) != DGX_GE_LINKS_VERSION || HEADER_BYTES + (size_t)u16_at(p + 6) * LINK_BYTES > size)
        return false;
    l->count = u16_at(p + 6);
    l->links = p + HEADER_BYTES;
    return true;
}

int dgx_ge_links_fire(const dgx_ge_links_t *l, int first, int event, dgx_ge_cells_t *cells, dgx_ge_life_of_fn life_of, void *ctx, uint32_t now_ms)
{
    int told = 0;
    for (int i = first; i >= 0 && i < l->count; ++i) {
        const uint8_t *r     = l->links + (size_t)i * LINK_BYTES;
        const uint16_t cell  = u16_at(r);
        const int      ev    = r[2] == DGX_GE_LINK_ANY ? event : r[2];
        const int16_t  value = (int16_t)u16_at(r + 4);
        if (ev >= 0 && dgx_ge_cells_event(cells, cell, life_of(cell, ctx), ev, value == DGX_GE_LINK_NO_VALUE ? -1 : 0, value, now_ms)) ++told;
        if (r[3] & LAST) break;
    }
    return told;
}
