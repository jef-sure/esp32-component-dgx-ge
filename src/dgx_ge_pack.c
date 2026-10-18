#include <string.h>

#include "dgx_ge_pack.h"

#define HEADER_BYTES 48u /* at least: a header with the fingerprint is 52 */
#define ID_AT        48u
#define SET_BYTES    8u
#define IMAGE_BYTES  16u
#define BLOB_BYTES   8u
#define MASK_BYTES   16u
#define POINT_BYTES  8u
#define MEETS_BYTES  64u

static inline uint16_t u16_at(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static inline uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Does [at, at + bytes) lie within the pack? */
static inline bool inside(const dgx_ge_pack_t *pack, uint32_t at, uint64_t bytes)
{
    return at <= pack->size && bytes <= pack->size - at;
}

static bool image_holds(const dgx_ge_pack_t *pack, const uint8_t *r)
{
    const uint16_t w = u16_at(r), h = u16_at(r + 2), set = u16_at(r + 10);
    const uint8_t  format = r[8];
    if (format != DGX_IMAGE_1BPP && format != DGX_IMAGE_4BPP && format != DGX_IMAGE_8BPP) return false;
    if (set >= pack->sets) return false;
    const uint64_t bytes = (uint64_t)((w * format + 7u) / 8u) * h;
    return inside(pack, u32_at(r + 12), bytes);
}

bool dgx_ge_pack_open(dgx_ge_pack_t *pack, const void *data, size_t size)
{
    memset(pack, 0, sizeof(*pack));
    const uint8_t *p = data;
    if (!p || size < HEADER_BYTES || u32_at(p) != DGX_GE_PACK_MAGIC || u16_at(p + 4) != DGX_GE_PACK_VERSION) return false;
    if (u16_at(p + 6) < HEADER_BYTES || u32_at(p + 8) > size) return false;
    pack->base      = p;
    pack->size      = u32_at(p + 8);
    pack->sets      = u16_at(p + 12);
    pack->images    = u16_at(p + 14);
    pack->blobs     = u16_at(p + 16);
    pack->sets_at   = u32_at(p + 20);
    pack->images_at = u32_at(p + 24);
    pack->blobs_at  = u32_at(p + 28);
    pack->masks     = u16_at(p + 18);
    pack->masks_at  = u32_at(p + 32);
    pack->points_at = u32_at(p + 36);
    pack->points    = u16_at(p + 40);
    pack->meets_at  = u32_at(p + 44);
    bool ok = inside(pack, pack->sets_at, (uint64_t)pack->sets * SET_BYTES) && inside(pack, pack->images_at, (uint64_t)pack->images * IMAGE_BYTES) &&
              inside(pack, pack->blobs_at, (uint64_t)pack->blobs * BLOB_BYTES) && inside(pack, pack->masks_at, (uint64_t)pack->masks * MASK_BYTES) &&
              inside(pack, pack->points_at, (uint64_t)pack->points * POINT_BYTES) && (!pack->meets_at || inside(pack, pack->meets_at, MEETS_BYTES));
    for (unsigned i = 0; ok && i < pack->sets; ++i) {
        const uint8_t *r = p + pack->sets_at + i * SET_BYTES;
        ok = inside(pack, u32_at(r), (uint64_t)u16_at(r + 4) * 3u) && u16_at(r + 4) <= 255;
    }
    for (unsigned i = 0; ok && i < pack->images; ++i) ok = image_holds(pack, p + pack->images_at + i * IMAGE_BYTES);
    for (unsigned i = 0; ok && i < pack->blobs; ++i) {
        const uint8_t *r = p + pack->blobs_at + i * BLOB_BYTES;
        ok = inside(pack, u32_at(r), u32_at(r + 4));
    }
    uint32_t last = 0;
    for (unsigned i = 0; ok && i < pack->masks; ++i) {
        const uint8_t *r = p + pack->masks_at + i * MASK_BYTES;
        const uint32_t key = (uint32_t)u16_at(r) << 8 | r[2]; /* by picture, then index, each once */
        ok = u16_at(r) < pack->images && r[2] < 16 && (i == 0 || key > last) &&
             inside(pack, u32_at(r + 12), (uint64_t)(u16_at(r + 8) + 7u) / 8u * u16_at(r + 10));
        last = key;
    }
    last = 0;
    for (unsigned i = 0; ok && i < pack->points; ++i) {
        const uint8_t *r = p + pack->points_at + i * POINT_BYTES;
        const uint32_t key = (uint32_t)u16_at(r) << 8 | r[2]; /* by picture, then name, each once */
        ok = u16_at(r) < pack->images && (i == 0 || key > last);
        last = key;
    }
    /* i meets j as j meets i */
    for (unsigned t = 0; ok && pack->meets_at && t < 2; ++t)
        for (unsigned i = 0; ok && i < 16; ++i)
            for (unsigned j = 0; ok && j < 16; ++j)
                ok = (u16_at(p + pack->meets_at + t * 32 + i * 2) >> j & 1) == (u16_at(p + pack->meets_at + t * 32 + j * 2) >> i & 1);
    if (!ok) memset(pack, 0, sizeof(*pack));
    return ok;
}

bool dgx_ge_pack_meets(const dgx_ge_pack_t *pack, dgx_ge_meets_t *meets)
{
    if (!pack->meets_at) return false;
    const uint8_t *r = pack->base + pack->meets_at;
    for (int i = 0; i < 16; ++i) meets->own[i] = u16_at(r + i * 2), meets->foreign[i] = u16_at(r + 32 + i * 2);
    return true;
}

bool dgx_ge_pack_point(const dgx_ge_pack_t *pack, int image, int name, dgx_ge_point_t *point)
{
    if (image < 0 || name < 0 || name > 255) return false;
    const uint32_t key = (uint32_t)image << 8 | (uint32_t)name;
    unsigned       lo = 0, hi = pack->points;
    while (lo < hi) {
        const unsigned mid = (lo + hi) / 2;
        const uint8_t *r   = pack->base + pack->points_at + mid * POINT_BYTES;
        const uint32_t k   = (uint32_t)u16_at(r) << 8 | r[2];
        if (k == key) {
            *point = (dgx_ge_point_t){.x = (int16_t)u16_at(r + 4), .y = (int16_t)u16_at(r + 6), .z = (int8_t)r[3]};
            return true;
        }
        if (k < key) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

int dgx_ge_pack_masks(const dgx_ge_pack_t *pack, int image, dgx_ge_mask_t *masks, int max)
{
    /* the first of the picture: they are sorted by picture */
    unsigned lo = 0, hi = pack->masks;
    while (lo < hi) {
        const unsigned mid = (lo + hi) / 2;
        if (u16_at(pack->base + pack->masks_at + mid * MASK_BYTES) < image) lo = mid + 1;
        else hi = mid;
    }
    int n = 0;
    for (unsigned i = lo; i < pack->masks; ++i, ++n) {
        const uint8_t *r = pack->base + pack->masks_at + i * MASK_BYTES;
        if (u16_at(r) != image) break;
        if (n < max)
            masks[n] = (dgx_ge_mask_t){.index = r[2], .x = (int16_t)u16_at(r + 4), .y = (int16_t)u16_at(r + 6), .width = u16_at(r + 8),
                                       .height = u16_at(r + 10), .bits = pack->base + u32_at(r + 12)};
    }
    return n;
}

bool dgx_ge_pack_image(const dgx_ge_pack_t *pack, int id, dgx_image_t *image)
{
    if (id < 0 || id >= pack->images) return false;
    const uint8_t *r = pack->base + pack->images_at + (uint32_t)id * IMAGE_BYTES;
    image->width    = u16_at(r);
    image->height   = u16_at(r + 2);
    image->origin_x = (int16_t)u16_at(r + 4);
    image->origin_y = (int16_t)u16_at(r + 6);
    image->format   = r[8];
    image->data     = pack->base + u32_at(r + 12);
    return true;
}

int dgx_ge_pack_image_set(const dgx_ge_pack_t *pack, int id)
{
    if (id < 0 || id >= pack->images) return -1;
    return u16_at(pack->base + pack->images_at + (uint32_t)id * IMAGE_BYTES + 10);
}

int dgx_ge_pack_set_colors(const dgx_ge_pack_t *pack, int set, const uint8_t (**rgb)[3])
{
    if (set < 0 || set >= pack->sets) return 0;
    const uint8_t *r = pack->base + pack->sets_at + (uint32_t)set * SET_BYTES;
    *rgb             = (const uint8_t (*)[3])(pack->base + u32_at(r));
    return u16_at(r + 4);
}

const uint8_t *dgx_ge_pack_blob(const dgx_ge_pack_t *pack, int id, size_t *size)
{
    if (id < 0 || id >= pack->blobs) return NULL;
    const uint8_t *r = pack->base + pack->blobs_at + (uint32_t)id * BLOB_BYTES;
    if (size) *size = u32_at(r + 4);
    return pack->base + u32_at(r);
}

uint32_t dgx_ge_pack_id(const dgx_ge_pack_t *pack)
{
    return pack->base && u16_at(pack->base + 6) >= ID_AT + 4 ? u32_at(pack->base + ID_AT) : 0;
}
