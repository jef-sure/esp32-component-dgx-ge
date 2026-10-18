#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dgx_ge_image.h"

/*
 * A pack of resources: what a game draws and reads, made on a host by
 * tools/dgx_ge_pack.py and read where it lies, in flash or in RAM, without a
 * copy. It holds:
 *
 * - sets of pictures: a set has its colors, RGB888, those of the values 1.. of
 *   its pictures, and the frame gives them indices when the set is used;
 * - pictures, each of a set, unpacked, 1, 4 or 8 bits a pixel, with an origin;
 * - blobs: bytes the game makes sense of itself, levels for one;
 * - points of pictures: named places a child is held at (a hand), with the
 *   side of the body it is drawn on;
 * - which masks meet: pairs of mask indices looked at between bodies of one
 *   side and of different sides (dgx_ge_collisions_meets), when the game says.
 *
 * Everything is known by its number; the packer writes a C header that names
 * the numbers. All numbers in the pack are little-endian and are read byte by
 * byte, so it may lie at any address.
 *
 *   header      52 bytes: "DGPK", version, its own size, the size of the pack,
 *               the counts of sets, pictures, blobs and masks and where their tables
 *               are, where the points are and how many, where the meets are (0: none),
 *               the fingerprint of the pack (a header of 48 bytes, of a pack made
 *               before, has none: 0)
 *   sets        8 bytes each: where the colors are, how many
 *   pictures    16 bytes each: width, height, origin, depth, set, where the pixels are
 *   blobs       8 bytes each: where, how many bytes
 *   masks       16 bytes each, by picture and index: picture, index, its rectangle
 *               from the origin of the picture, where its bits are
 *   points      8 bytes each, by picture and name: picture, name, z, x, y from its origin
 *   meets       64 bytes when given: 16 rows of own, 16 of foreign, 16 bits each
 *   data        colors, pixels, blobs and the bits of masks
 *
 * - masks of collisions: 1-bit pictures of the zones of a picture (body,
 *   blade, shield) by an index 0..15, from its origin, mirrored with it.
 */
#define DGX_GE_PACK_MAGIC   0x4B504744u /* "DGPK" */
#define DGX_GE_PACK_VERSION 4u

typedef struct {
    const uint8_t *base;
    uint32_t       size;
    uint16_t       sets, images, blobs, masks, points;
    uint32_t       sets_at, images_at, blobs_at, masks_at, points_at, meets_at;
} dgx_ge_pack_t;

/*
 * A point of a picture, by a name the pack gives a number (<PREFIX>_POINT_<NAME>):
 * from its origin, unturned; z the side of its owner what is held there is
 * drawn on, as the z of a child (dgx_ge_scene_actor_child): > 0 before, < 0 behind.
 */
typedef struct {
    int16_t x, y;
    int8_t  z;
} dgx_ge_point_t;

/* A mask of collisions: 1 bit a point, rows from a new byte, the leftmost point in the high bit. */
typedef struct {
    uint8_t        index;
    int16_t        x, y; /* its top-left point from the origin of its picture */
    uint16_t       width, height;
    const uint8_t *bits;
} dgx_ge_mask_t;

/*
 * Which masks of two bodies are looked at: by the index of a mask of one, a
 * bit an index of the other (row i, bit j: i meets j; j meets i alike). own
 * for bodies of one side (a thrown stone and its thrower), foreign for those
 * of different sides.
 */
typedef struct {
    uint16_t own[16];
    uint16_t foreign[16];
} dgx_ge_meets_t;

/* Take a pack that lies at data; false when it is not one or does not hold together. Nothing is copied. */
bool dgx_ge_pack_open(dgx_ge_pack_t *pack, const void *data, size_t size);

/* A picture; false when there is no such. The pixels stay in the pack. */
bool dgx_ge_pack_image(const dgx_ge_pack_t *pack, int id, dgx_image_t *image);

/* The set a picture is of, -1 when there is no such picture. */
int dgx_ge_pack_image_set(const dgx_ge_pack_t *pack, int id);

/* The colors of a set, three bytes each; returns how many, 0 when there is no such set. */
int dgx_ge_pack_set_colors(const dgx_ge_pack_t *pack, int set, const uint8_t (**rgb)[3]);

/* The masks of a picture, by index: how many it has; up to max of them are given. */
int dgx_ge_pack_masks(const dgx_ge_pack_t *pack, int image, dgx_ge_mask_t *masks, int max);

/* A point of a picture by its name; false when the picture has none of that name (nothing held there in that frame). */
bool dgx_ge_pack_point(const dgx_ge_pack_t *pack, int image, int name, dgx_ge_point_t *point);

/* Which masks meet, as the pack says; false when it says nothing. */
bool dgx_ge_pack_meets(const dgx_ge_pack_t *pack, dgx_ge_meets_t *meets);

/* A blob; NULL when there is no such. */
const uint8_t *dgx_ge_pack_blob(const dgx_ge_pack_t *pack, int id, size_t *size);

/*
 * The fingerprint of the pack: the CRC-32 of all its bytes, the fingerprint
 * 0 meanwhile, as the packer wrote it (not checked here: read, it costs
 * nothing). A save of a game knows its data by it. 0: the pack has none.
 */
uint32_t dgx_ge_pack_id(const dgx_ge_pack_t *pack);

#ifdef __cplusplus
}
#endif
