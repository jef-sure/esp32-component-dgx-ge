#include <string.h>

#include "dgx_ge_chase.h"

#define HEADER_BYTES 8u
#define KIND_BYTES   18u

static uint16_t u16_at(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

bool dgx_ge_chase_of(const void *blob, size_t size, int kind, dgx_ge_chase_t *c)
{
    memset(c, 0, sizeof(*c));
    const uint8_t *p = blob;
    if (!p || size < HEADER_BYTES || (u16_at(p) | (uint32_t)u16_at(p + 2) << 16) != DGX_GE_CHASE_MAGIC || u16_at(p + 4) != 1) return false;
    const unsigned count = u16_at(p + 6);
    if (HEADER_BYTES + count * KIND_BYTES > size) return false;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *k = p + HEADER_BYTES + i * KIND_BYTES;
        if (u16_at(k) != kind) continue;
        c->sense = k[2], c->sides = k[3], c->hearing = (int16_t)u16_at(k + 4);
        for (int s = 0; s < 4; ++s) c->near[s] = (int16_t)u16_at(k + 6 + 2 * s);
        c->steps = k[14], c->calm_ms = u16_at(k + 16);
        return true;
    }
    return false;
}

bool dgx_ge_chase_senses(const dgx_ge_chase_t *c, int to_hero, bool sees)
{
    switch (c->sense) {
    case DGX_GE_SENSE_SIGHT: return sees;
    case DGX_GE_SENSE_HEARING: return to_hero <= c->hearing;
    case DGX_GE_SENSE_ALWAYS: return true;
    default: return false;
    }
}

bool dgx_ge_chase_follows(const dgx_ge_chase_t *c, bool ready, int side, int to_side, int to_hero, bool sees, int steps)
{
    if (!ready || side < 0 || side > 4 || !(c->sides & (1u << side))) return false;
    if (side < 4 && to_side > c->near[side]) return false; /* too far from that side */
    if (c->steps && steps >= c->steps) return false;        /* so far from home already */
    return dgx_ge_chase_senses(c, to_hero, sees);
}

bool dgx_ge_chase_calm(const dgx_ge_chase_t *c, uint32_t lost_ms, uint32_t now_ms)
{
    return c->calm_ms && lost_ms && now_ms - lost_ms >= c->calm_ms;
}
