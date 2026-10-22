/* The pack: what make_test_pack.py has put is read back, its fingerprint, and a broken pack is refused. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_scene.h"
#include "test_pack.h"

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*size + 1);
    if (data && fread(data, 1, *size, f) != *size) free(data), data = NULL;
    fclose(f);
    return data;
}

/* CRC-32 as zlib's (the packer's) */
static uint32_t crc32_of(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

int main(int argc, char **argv)
{
    size_t   size = 0;
    uint8_t *data = argc > 1 ? read_file(argv[1], &size) : NULL;
    CHECK(data);
    if (!data) CHECK_DONE();

    dgx_ge_pack_t pack;
    CHECK(dgx_ge_pack_open(&pack, data, size));
    CHECK(pack.sets == TEST_SET_COUNT && pack.images == TEST_IMG_COUNT && pack.blobs == TEST_BLOB_COUNT);

    const uint8_t (*rgb)[3];
    CHECK(dgx_ge_pack_set_colors(&pack, TEST_SET_HERO, &rgb) == 3 && rgb[2][2] == 255 && rgb[0][0] == 255);
    CHECK(dgx_ge_pack_set_colors(&pack, TEST_SET_WIDE, &rgb) == 200 && rgb[199][1] == 200);
    CHECK(dgx_ge_pack_set_colors(&pack, 7, &rgb) == 0);

    dgx_image_t im;
    CHECK(dgx_ge_pack_image(&pack, TEST_IMG_ONE, &im));
    CHECK(im.width == 9 && im.height == 2 && im.format == DGX_IMAGE_1BPP && im.origin_x == 4 && im.origin_y == 1);
    CHECK(im.data[0] == 0xA0 && im.data[1] == 0x80 && im.data[2] == 0x40 && im.data[3] == 0);
    CHECK(dgx_ge_pack_image(&pack, TEST_IMG_FOUR, &im));
    CHECK(im.format == DGX_IMAGE_4BPP && im.data[0] == 0x10 && im.data[1] == 0x30 && im.data[2] == 0x22 && im.data[3] == 0x00);
    CHECK(dgx_ge_pack_image_set(&pack, TEST_IMG_FOUR) == TEST_SET_HERO);
    CHECK(dgx_ge_pack_image(&pack, TEST_IMG_EIGHT, &im));
    CHECK(im.format == DGX_IMAGE_8BPP && im.data[0] == 200 && im.data[1] == 7 && im.origin_x == -3 && im.origin_y == 5);
    CHECK(dgx_ge_pack_image_set(&pack, TEST_IMG_EIGHT) == TEST_SET_WIDE);
    CHECK(!dgx_ge_pack_image(&pack, TEST_IMG_COUNT, &im) && dgx_ge_pack_image_set(&pack, -1) == -1);

    size_t         n;
    const uint8_t *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_LEVEL, &n);
    CHECK(blob && n == 12 && !memcmp(blob, "hello, level", 12));
    CHECK(!dgx_ge_pack_blob(&pack, TEST_BLOB_COUNT, &n));

    /* its fingerprint: the CRC-32 of all of it, the fingerprint 0 meanwhile; a header of 48 bytes (a pack of before) has none */
    uint8_t *zeroed = malloc(size);
    memcpy(zeroed, data, size);
    memset(zeroed + 48, 0, 4);
    CHECK(dgx_ge_pack_id(&pack) != 0 && dgx_ge_pack_id(&pack) == crc32_of(zeroed, size));
    zeroed[6] = 48; /* its own size said 48: the 4 bytes after are of the tables then, and it holds together still */
    CHECK(dgx_ge_pack_open(&pack, zeroed, size) && dgx_ge_pack_id(&pack) == 0);
    free(zeroed);
    CHECK(dgx_ge_pack_open(&pack, data, size));

    /* the pack may lie at any address */
    uint8_t *odd = malloc(size + 1);
    memcpy(odd + 1, data, size);
    CHECK(dgx_ge_pack_open(&pack, odd + 1, size));
    free(odd);

    /* refused: too short, another magic, another version, a table or pixels past the end */
    CHECK(!dgx_ge_pack_open(&pack, data, 16) && pack.base == NULL);
    data[0] ^= 1;
    CHECK(!dgx_ge_pack_open(&pack, data, size));
    data[0] ^= 1, data[4] ^= 1;
    CHECK(!dgx_ge_pack_open(&pack, data, size));
    data[4] ^= 1;
    CHECK(!dgx_ge_pack_open(&pack, data, size - 1));
    uint8_t *broken = malloc(size);
    for (size_t at = 20; at < 40; at += 4) { /* the tables of sets, pictures, blobs, masks, points */
        memcpy(broken, data, size);
        broken[at + 3] = 0x7f; /* a table far away */
        CHECK(!dgx_ge_pack_open(&pack, broken, size));
    }
    const uint32_t images_at = (uint32_t)data[24] | (uint32_t)data[25] << 8 | (uint32_t)data[26] << 16 | (uint32_t)data[27] << 24; /* from the header */
    memcpy(broken, data, size);
    broken[images_at + 12 + 3] = 0x7f; /* the pixels of the first picture far away */
    CHECK(!dgx_ge_pack_open(&pack, broken, size));
    memcpy(broken, data, size);
    broken[images_at + 8] = 3; /* a depth there is not */
    CHECK(!dgx_ge_pack_open(&pack, broken, size));
    const uint32_t meets_at = (uint32_t)data[44] | (uint32_t)data[45] << 8 | (uint32_t)data[46] << 16 | (uint32_t)data[47] << 24;
    CHECK(meets_at != 0);
    memcpy(broken, data, size);
    broken[meets_at + 32 + 2 * 2] ^= 1 << 3; /* foreign: 2 meets 3, but 3 not 2 */
    CHECK(!dgx_ge_pack_open(&pack, broken, size));
    memcpy(broken, data, size);
    broken[44] = broken[45] = broken[46] = broken[47] = 0; /* no meets: the pack says nothing */
    dgx_ge_meets_t meets;
    CHECK(dgx_ge_pack_open(&pack, broken, size) && !dgx_ge_pack_meets(&pack, &meets));
    CHECK(dgx_ge_pack_open(&pack, data, size));

    /* masks: by index, from the origin; a picture without masks has none */
    dgx_ge_mask_t m[4];
    CHECK(pack.masks == 7); /* three here, the crowd's wolf (body, jaws) and sheep (body), and the slab of the tiles' test */
    CHECK(dgx_ge_pack_masks(&pack, TEST_IMG_FOUR, m, 4) == 2);
    CHECK(m[0].index == 1 && m[0].x == 0 && m[0].y == -1 && m[0].width == 3 && m[0].height == 2 && m[0].bits[0] == 0xA0 && m[0].bits[1] == 0xC0);
    CHECK(m[1].index == 2 && m[1].x == 3 && m[1].width == 2 && m[1].bits[0] == 0xC0);
    CHECK(dgx_ge_pack_masks(&pack, TEST_IMG_FOUR, m, 1) == 2 && m[0].index == 1); /* how many, only one given */
    CHECK(dgx_ge_pack_masks(&pack, TEST_IMG_ONE, m, 4) == 1 && m[0].index == 1 && m[0].width == 9);
    CHECK(dgx_ge_pack_masks(&pack, TEST_IMG_EIGHT, m, 4) == 0);
    memcpy(broken, data, size);
    broken[pack.masks_at + 2 * 16 + 2] = 1; /* two masks of an index on one picture: the blade of FOUR made its body */
    CHECK(!dgx_ge_pack_open(&pack, broken, size));
    CHECK(dgx_ge_pack_open(&pack, data, size));

    /* points: a hand of FOUR before its body, a hand of ONE behind; EIGHT is held by its own point of that name */
    dgx_ge_point_t pt;
    CHECK(pack.points == 3);
    CHECK(dgx_ge_pack_point(&pack, TEST_IMG_FOUR, TEST_POINT_HAND, &pt) && pt.x == 3 && pt.y == -2 && pt.z == 2);
    CHECK(dgx_ge_pack_point(&pack, TEST_IMG_ONE, TEST_POINT_HAND, &pt) && pt.x == -1 && pt.z == -1);
    CHECK(!dgx_ge_pack_point(&pack, TEST_IMG_FOUR, TEST_POINT_HAND + 1, &pt) && !dgx_ge_pack_point(&pack, TEST_IMG_EIGHT + 1, TEST_POINT_HAND, &pt));
    /* held: the hilt (1, 1) of EIGHT at the hand (3, -2) of FOUR, and turned round with it */
    dgx_image_t   eight;
    dgx_ge_item_t held;
    int           z = 0;
    CHECK(dgx_ge_pack_image(&pack, TEST_IMG_EIGHT, &eight));
    CHECK(dgx_ge_item_held(&pack, &(dgx_ge_item_t){.x = 50, .y = 40, .layer = 2}, TEST_IMG_FOUR, TEST_POINT_HAND, &eight, NULL, TEST_IMG_EIGHT, &held, &z));
    CHECK(held.x == 50 + 3 - 1 && held.y == 40 - 2 - 1 && held.layer == 2 && z == 2 && held.image == &eight);
    CHECK(dgx_ge_item_held(&pack, &(dgx_ge_item_t){.x = 50, .y = 40, .flags = DGX_GE_BLIT_FLIP_X}, TEST_IMG_FOUR, TEST_POINT_HAND, &eight, NULL,
                           TEST_IMG_EIGHT, &held, &z));
    CHECK(held.x == 50 - (3 - 1) && (held.flags & DGX_GE_BLIT_FLIP_X));
    CHECK(!dgx_ge_item_held(&pack, &(dgx_ge_item_t){.x = 50}, TEST_IMG_EIGHT + 1, TEST_POINT_HAND, &eight, NULL, TEST_IMG_EIGHT, &held, &z)); /* none: nothing held */
    memcpy(broken, data, size);
    broken[pack.points_at + 8] = (uint8_t)TEST_IMG_ONE; /* a picture with its point twice: out of their order */
    CHECK(!dgx_ge_pack_open(&pack, broken, size));
    free(broken);
    free(data);
    CHECK_DONE();
}
