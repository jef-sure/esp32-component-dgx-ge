/* Chasing: what a kind does, from the pack; whether it follows the hero out by a side; whether it calms down. */
#include <stdio.h>
#include <stdlib.h>

#include "check.h"
#include "dgx_ge_chase.h"
#include "dgx_ge_graph.h"
#include "dgx_ge_pack.h"
#include "test_pack.h"

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*size);
    if (data && fread(data, 1, *size, f) != *size) free(data), data = NULL;
    fclose(f);
    return data;
}

int main(int argc, char **argv)
{
    size_t   size = 0;
    uint8_t *data = read_file(argc > 1 ? argv[1] : "build/test.pack", &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    size_t         n;
    const uint8_t *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_CHASE, &n);
    dgx_ge_chase_t wave, hero, none;
    CHECK(blob && dgx_ge_chase_of(blob, n, TEST_BLOB_WAVE, &wave) && dgx_ge_chase_of(blob, n, TEST_BLOB_HERO, &hero));
    CHECK(!dgx_ge_chase_of(blob, n, TEST_BLOB_DART, &none) && !dgx_ge_chase_follows(&none, true, DGX_GE_SIDE_LEFT, 0, 0, true, 0));
    CHECK(!dgx_ge_chase_of(data, 8, TEST_BLOB_WAVE, &none));
    CHECK(wave.sense == DGX_GE_SENSE_HEARING && wave.hearing == 50 && wave.near[DGX_GE_SIDE_LEFT] == 20 && wave.steps == 2 && wave.calm_ms == 3000);

    /* by hearing: near the side and near the hero; not ready, too far from either, a side it does not go by, too far from home: not */
    CHECK(dgx_ge_chase_follows(&wave, true, DGX_GE_SIDE_LEFT, 20, 50, false, 1));
    CHECK(!dgx_ge_chase_follows(&wave, false, DGX_GE_SIDE_LEFT, 20, 50, false, 1));
    CHECK(!dgx_ge_chase_follows(&wave, true, DGX_GE_SIDE_LEFT, 21, 50, false, 1));
    CHECK(!dgx_ge_chase_follows(&wave, true, DGX_GE_SIDE_RIGHT, 0, 51, true, 1));
    CHECK(!dgx_ge_chase_follows(&wave, true, DGX_GE_SIDE_UP, 0, 0, true, 0));
    CHECK(!dgx_ge_chase_follows(&wave, true, DGX_GE_SIDE_LEFT, 0, 0, true, 2));
    CHECK(dgx_ge_chase_follows(&wave, true, 4, 999, 10, false, 0)); /* by a door: how near it is to a side means nothing */
    /* by sight, down only, past the floor already */
    CHECK(dgx_ge_chase_follows(&hero, true, DGX_GE_SIDE_DOWN, -1, 999, true, 0));
    CHECK(!dgx_ge_chase_follows(&hero, true, DGX_GE_SIDE_DOWN, 0, 0, true, 0));
    CHECK(!dgx_ge_chase_follows(&hero, true, DGX_GE_SIDE_DOWN, -1, 0, false, 0));
    CHECK(!dgx_ge_chase_follows(&hero, true, DGX_GE_SIDE_LEFT, -100, 0, true, 0));
    /* calm: out of its hearing 3 s; it senses him: never; a kind that never calms */
    CHECK(!dgx_ge_chase_calm(&wave, 1000, 3999) && dgx_ge_chase_calm(&wave, 1000, 4000) && !dgx_ge_chase_calm(&wave, 0, 9000));
    CHECK(!dgx_ge_chase_calm(&hero, 1000, 100000));
    CHECK(dgx_ge_chase_senses(&wave, 50, false) && !dgx_ge_chase_senses(&wave, 51, true) && dgx_ge_chase_senses(&hero, 999, true));
    free(data);
    CHECK_DONE();
}
