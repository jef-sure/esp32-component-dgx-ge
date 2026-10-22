/*
 * A saved game: the world, its boxes, the lives of cells, the timers and pockets as bytes and back, times moved on;
 * of its data alone (a fingerprint); refused all or nothing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_cells.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_pockets.h"
#include "dgx_ge_save.h"
#include "dgx_ge_timers.h"
#include "dgx_ge_world.h"
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

static void nothing_get(uint16_t cell, int *tile, int *mod, void *ctx) { (void)cell, (void)ctx; *tile = *mod = 0; }
static void nothing_set(uint16_t cell, int tile, int mod, void *ctx) { (void)cell, (void)tile, (void)mod, (void)ctx; }
static void fired(const dgx_ge_timer_t *timer, uint32_t at_ms, void *ctx) { (void)timer, (void)at_ms, (void)ctx; }

int main(int argc, char **argv)
{
    if (argc < 2) return fprintf(stderr, "usage: %s test.pack\n", argv[0]), 2;
    size_t        size;
    uint8_t      *data = read_file(argv[1], &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    dgx_ge_kinds_t kinds;
    CHECK(dgx_ge_kinds_init(&kinds, &pack, 8));

    /* the blob itself: chunks by tags, values back as they went, an unknown tag none, a chunk read past its end 0 and failed */
    uint8_t       buf[512];
    dgx_ge_save_t s;
    dgx_ge_save_begin(&s, buf, sizeof buf, 0xDA7A, 5000);
    dgx_ge_save_chunk(&s, "ONE_");
    dgx_ge_save_u8(&s, 7), dgx_ge_save_i16(&s, -300), dgx_ge_save_u32(&s, 0x89ABCDEF);
    dgx_ge_save_chunk_end(&s);
    dgx_ge_save_chunk(&s, "TWO_");
    dgx_ge_save_bytes(&s, "abc", 3);
    dgx_ge_save_chunk_end(&s);
    const size_t n = dgx_ge_save_end(&s);
    CHECK(n == 14 + 8 + 7 + 8 + 3);
    dgx_ge_load_t l;
    uint32_t      saved;
    CHECK(dgx_ge_load_open(&l, buf, n, 0xDA7A, &saved) && saved == 5000);
    /* of other data, of version 1 (it named no data), of a version after this one: not taken */
    CHECK(!dgx_ge_load_open(&l, buf, n, 0xDA7B, NULL));
    buf[4] = 1;
    CHECK(!dgx_ge_load_open(&l, buf, n, 0xDA7A, NULL));
    buf[4] = DGX_GE_SAVE_VERSION + 1;
    CHECK(!dgx_ge_load_open(&l, buf, n, 0xDA7A, NULL));
    buf[4] = DGX_GE_SAVE_VERSION;
    CHECK(dgx_ge_load_open(&l, buf, n, 0xDA7A, NULL));
    CHECK(dgx_ge_load_chunk(&l, "TWO_") && dgx_ge_load_left(&l) == 3);
    CHECK(dgx_ge_load_chunk(&l, "ONE_") && dgx_ge_load_u8(&l) == 7 && dgx_ge_load_i16(&l) == -300 && dgx_ge_load_u32(&l) == 0x89ABCDEF && !l.failed);
    CHECK(dgx_ge_load_u8(&l) == 0 && l.failed);
    CHECK(!dgx_ge_load_chunk(&l, "NONE"));
    CHECK(!dgx_ge_load_open(&l, buf, 13, 0xDA7A, NULL) && !dgx_ge_load_open(&l, "DGSX..........", 14, 0xDA7A, NULL));
    /* cut short: the chunk that does not fit is not there */
    CHECK(dgx_ge_load_open(&l, buf, n - 2, 0xDA7A, NULL) && dgx_ge_load_chunk(&l, "ONE_") && !dgx_ge_load_chunk(&l, "TWO_"));
    /* no room: nothing */
    dgx_ge_save_begin(&s, buf, 16, 0, 0);
    dgx_ge_save_chunk(&s, "BIG_");
    dgx_ge_save_u32(&s, 1);
    dgx_ge_save_chunk_end(&s);
    CHECK(dgx_ge_save_end(&s) == 0);

    /* a world with boxes, lives, timers and pockets, saved at 5000 and loaded 1000 later: the same, times 1000 on; params past 16 bits */
    dgx_ge_world_t *w = dgx_ge_world_create(4);
    CHECK(w && dgx_ge_world_boxes(w, &kinds, 2, 3));
    const int32_t a = dgx_ge_world_add(w, &(dgx_ge_object_t){.scene = 1, .kind = TEST_BLOB_KNIFE, .script = 2, .flip = true, .x = 10 * 256, .y = 20 * 256, .since_ms = 4000, .params = {5, -6}});
    const int32_t b = dgx_ge_world_add(w, &(dgx_ge_object_t){.scene = 2, .kind = TEST_BLOB_KNIFE, .keep = true, .count = 3, .since_ms = 4500, .params = {36010, -40000}});
    dgx_ge_object_t chest = {.scene = 1, .kind = TEST_BLOB_KNIFE, .box = (uint8_t)dgx_ge_world_new_box(w), .since_ms = 4900};
    CHECK(chest.box == 1);
    const int32_t c = dgx_ge_world_add(w, &chest);
    CHECK(a >= 0 && b >= 0 && c >= 0);
    dgx_ge_pockets_t *box = dgx_ge_world_box(w, c);
    CHECK(box && dgx_ge_pockets_put(box, &(dgx_ge_pocket_t){.kind = TEST_BLOB_KNIFE, .script = 1, .params = {9, 100000}}, 2) >= 0);
    dgx_ge_world_remove(w, a); /* a gone: its place free, the serial of the ring moved on */

    dgx_ge_cells_t cells;
    CHECK(dgx_ge_cells_init(&cells, &kinds, 3, &(dgx_ge_cells_io_t){.get = nothing_get, .set = nothing_set}));
    cells.lives[0] = (dgx_ge_life_t){.cell = 31, .kind = TEST_BLOB_KNIFE, .script = 1, .pending = 2, .pending_when = 1, .pc = 77, .due_ms = 4800, .own = {3, 40000}};
    cells.lives[1] = (dgx_ge_life_t){.cell = 32, .kind = TEST_BLOB_KNIFE, .due_ms = 5000};
    cells.count    = 2;

    dgx_ge_timers_t timers;
    CHECK(dgx_ge_timers_init(&timers, 3, fired, NULL));
    timers.timers[0] = (dgx_ge_timer_t){.name = 1, .kind = 2, .reset = 1, .event = 9, .owner_kind = 3, .map = 4, .owner = 500, .scene = 6, .held = 1, .period_ms = 1000, .limit_ms = 60000, .start_ms = 4000, .next_ms = 5000, .held_ms = 4900};
    timers.count     = 1;

    dgx_ge_pockets_t *mine = dgx_ge_pockets_create(&kinds, 4, 0);
    CHECK(mine && dgx_ge_pockets_put(mine, &(dgx_ge_pocket_t){.kind = TEST_BLOB_KNIFE, .script = 3, .params = {1, 2000010}}, 5) >= 0);

    static uint8_t blob[2048];
    dgx_ge_save_begin(&s, blob, sizeof blob, 0, 5000);
    dgx_ge_world_save(w, &s);
    dgx_ge_cells_save(&cells, &s);
    dgx_ge_timers_save(&timers, &s);
    dgx_ge_pockets_save(mine, &s, "MINE");
    const size_t bytes = dgx_ge_save_end(&s);
    CHECK(bytes > 0);

    dgx_ge_world_t *w2 = dgx_ge_world_create(4);
    CHECK(w2 && dgx_ge_world_boxes(w2, &kinds, 2, 3));
    dgx_ge_cells_t cells2;
    CHECK(dgx_ge_cells_init(&cells2, &kinds, 3, &(dgx_ge_cells_io_t){.get = nothing_get, .set = nothing_set}));
    dgx_ge_timers_t timers2;
    CHECK(dgx_ge_timers_init(&timers2, 3, fired, NULL));
    dgx_ge_pockets_t *mine2 = dgx_ge_pockets_create(&kinds, 4, 0);
    CHECK(dgx_ge_load_open(&l, blob, bytes, 0, &saved) && saved == 5000);
    const uint32_t later = 6000 - saved;
    /* checked alone: all good, nothing changed, where the load stands either */
    const dgx_ge_load_t before = l;
    CHECK(dgx_ge_world_load_check(w2, &l) && dgx_ge_cells_load_check(&cells2, &l) && dgx_ge_timers_load_check(&timers2, &l) &&
          dgx_ge_pockets_load_check(mine2, &l, "MINE") && !dgx_ge_pockets_load_check(mine2, &l, "NONE"));
    CHECK(memcmp(&before, &l, sizeof l) == 0 && dgx_ge_world_next(w2, 1, -1) == -1 && cells2.count == 0 && timers2.count == 0 &&
          dgx_ge_pockets_count(mine2, TEST_BLOB_KNIFE) == 0);
    CHECK(dgx_ge_world_load(w2, &l, later) && dgx_ge_cells_load(&cells2, &l, later) && dgx_ge_timers_load(&timers2, &l, later) && dgx_ge_pockets_load(mine2, &l, "MINE"));
    dgx_ge_object_t o;
    CHECK(!dgx_ge_world_get(w2, a, &o) && dgx_ge_world_get(w2, b, &o) && o.scene == 2 && o.keep && o.count == 3 && o.since_ms == 5500 && o.params[0] == 36010 && o.params[1] == -40000);
    CHECK(dgx_ge_world_get(w2, c, &o) && o.box == 1 && o.since_ms == 5900 && !o.flip);
    dgx_ge_pockets_t *box2 = dgx_ge_world_box(w2, c);
    CHECK(box2 && dgx_ge_pockets_count(box2, TEST_BLOB_KNIFE) == 2 && box2->places[0].params[1] == 100000);
    CHECK(dgx_ge_world_next(w2, 1, -1) == c && dgx_ge_world_next(w2, 1, c) == -1); /* the order of the ring as it was */
    const int32_t d = dgx_ge_world_add(w2, &(dgx_ge_object_t){.scene = 3});
    CHECK(d >= 0 && d == dgx_ge_world_add(w, &(dgx_ge_object_t){.scene = 3})); /* the next handle the same as the saved world would give */
    CHECK(cells2.count == 2 && cells2.lives[0].cell == 31 && cells2.lives[0].pc == 77 && cells2.lives[0].due_ms == 5800 && cells2.lives[0].own[1] == 40000 && cells2.lives[1].due_ms == 6000);
    CHECK(dgx_ge_cells_set(&cells2, 32, 0, 70000) && cells2.lives[1].own[0] == 70000);
    CHECK(timers2.count == 1 && timers2.timers[0].owner == 500 && timers2.timers[0].held && timers2.timers[0].start_ms == 5000 && timers2.timers[0].next_ms == 6000 && timers2.timers[0].held_ms == 5900 && timers2.timers[0].limit_ms == 60000);
    CHECK(dgx_ge_pockets_count(mine2, TEST_BLOB_KNIFE) == 5 && mine2->places[0].script == 3 && mine2->places[0].params[1] == 2000010);

    /* a save of version 2 (params of 16 bits), written as it was then: read as it was made */
    dgx_ge_save_begin(&s, blob, sizeof blob, 0, 5000);
    dgx_ge_save_chunk(&s, "WRLD");
    dgx_ge_save_u16(&s, 1), dgx_ge_save_u16(&s, 0), dgx_ge_save_u16(&s, 9), dgx_ge_save_u8(&s, DGX_GE_WORLD_PARAMS); /* capacity, next, serial */
    dgx_ge_save_u16(&s, 9), dgx_ge_save_u16(&s, 4), dgx_ge_save_u16(&s, TEST_BLOB_KNIFE), dgx_ge_save_u8(&s, 1), dgx_ge_save_u8(&s, 0);
    dgx_ge_save_u8(&s, 1), dgx_ge_save_u8(&s, 0), dgx_ge_save_i32(&s, 256), dgx_ge_save_i32(&s, 512), dgx_ge_save_u32(&s, 4000);
    dgx_ge_save_i16(&s, -300), dgx_ge_save_i16(&s, 32767), dgx_ge_save_u8(&s, 0); /* its params; no boxes */
    dgx_ge_save_chunk_end(&s);
    dgx_ge_save_chunk(&s, "CELL");
    dgx_ge_save_u16(&s, 1), dgx_ge_save_u8(&s, DGX_GE_CELL_OWN);
    dgx_ge_save_u16(&s, 12), dgx_ge_save_u16(&s, TEST_BLOB_KNIFE), dgx_ge_save_u8(&s, 0), dgx_ge_save_u8(&s, 0), dgx_ge_save_u8(&s, 0);
    dgx_ge_save_u16(&s, 0), dgx_ge_save_u32(&s, 4000), dgx_ge_save_i16(&s, -2), dgx_ge_save_i16(&s, 7);
    dgx_ge_save_chunk_end(&s);
    dgx_ge_save_chunk(&s, "MINE");
    dgx_ge_save_u8(&s, 4), dgx_ge_save_u8(&s, DGX_GE_POCKET_PARAMS);
    for (int i = 0; i < 4; ++i) dgx_ge_save_u16(&s, TEST_BLOB_KNIFE), dgx_ge_save_u8(&s, 0), dgx_ge_save_u8(&s, i == 0), dgx_ge_save_i16(&s, -1), dgx_ge_save_i16(&s, 5);
    dgx_ge_save_chunk_end(&s);
    const size_t old = dgx_ge_save_end(&s);
    blob[4] = 2;
    dgx_ge_world_t *w1 = dgx_ge_world_create(1);
    CHECK(old && dgx_ge_load_open(&l, blob, old, 0, NULL) && dgx_ge_world_load(w1, &l, 0) && dgx_ge_cells_load(&cells2, &l, 0) && dgx_ge_pockets_load(mine2, &l, "MINE"));
    CHECK(dgx_ge_world_next(w1, 4, -1) >= 0 && dgx_ge_world_get(w1, dgx_ge_world_next(w1, 4, -1), &o) && o.x == 256 && o.since_ms == 4000 && o.params[0] == -300 && o.params[1] == 32767);
    CHECK(cells2.count == 1 && cells2.lives[0].cell == 12 && cells2.lives[0].own[0] == -2 && cells2.lives[0].own[1] == 7);
    CHECK(dgx_ge_pockets_count(mine2, TEST_BLOB_KNIFE) == 1 && mine2->places[0].params[0] == -1 && mine2->places[0].params[1] == 5);
    /* the same bytes said to be of version 3: params of 32 bits, so many bytes are not there: refused */
    blob[4] = 3;
    CHECK(dgx_ge_load_open(&l, blob, old, 0, NULL) && !dgx_ge_world_load_check(w1, &l) && !dgx_ge_cells_load_check(&cells2, &l) && !dgx_ge_pockets_load_check(mine2, &l, "MINE"));
    dgx_ge_world_destroy(&w1);

    /* refused, nothing changed: a world of another size, pockets of another capacity, a save without the chunk */
    dgx_ge_world_t *w3 = dgx_ge_world_create(5);
    CHECK(w3 && !dgx_ge_world_load(w3, &l, 0) && dgx_ge_world_next(w3, 1, -1) == -1);
    dgx_ge_pockets_t *small = dgx_ge_pockets_create(&kinds, 2, 0);
    CHECK(small && !dgx_ge_pockets_load(small, &l, "MINE") && dgx_ge_pockets_count(small, TEST_BLOB_KNIFE) == 0);
    CHECK(!dgx_ge_pockets_load(mine2, &l, "NONE"));
    dgx_ge_save_begin(&s, blob, sizeof blob, 0, 0);
    dgx_ge_cells_save(&cells, &s);
    const size_t only_cells = dgx_ge_save_end(&s);
    CHECK(dgx_ge_load_open(&l, blob, only_cells, 0, NULL) && !dgx_ge_timers_load(&timers2, &l, 0) && timers2.count == 1);

    /* all or nothing: a world of fewer boxes takes none of the records (they come before the boxes in the chunk) */
    dgx_ge_save_begin(&s, blob, sizeof blob, 0, 5000);
    dgx_ge_world_save(w, &s);
    size_t one = dgx_ge_save_end(&s);
    dgx_ge_world_t *w4 = dgx_ge_world_create(4);
    CHECK(w4 && dgx_ge_world_boxes(w4, &kinds, 1, 3));
    const int32_t e = dgx_ge_world_add(w4, &(dgx_ge_object_t){.scene = 7});
    CHECK(dgx_ge_load_open(&l, blob, one, 0, NULL) && !dgx_ge_world_load_check(w4, &l) && !dgx_ge_world_load(w4, &l, 0));
    CHECK(dgx_ge_world_get(w4, e, &o) && o.scene == 7 && dgx_ge_world_next(w4, 1, -1) == -1);
    /* an object in a box past the world's: refused, nothing taken */
    CHECK(dgx_ge_world_add(w, &(dgx_ge_object_t){.scene = 4, .box = 3}) >= 0);
    dgx_ge_save_begin(&s, blob, sizeof blob, 0, 5000);
    dgx_ge_world_save(w, &s);
    one = dgx_ge_save_end(&s);
    dgx_ge_world_t *w5 = dgx_ge_world_create(4);
    CHECK(w5 && dgx_ge_world_boxes(w5, &kinds, 2, 3));
    CHECK(dgx_ge_load_open(&l, blob, one, 0, NULL) && !dgx_ge_world_load(w5, &l, 0) && dgx_ge_world_next(w5, 1, -1) == -1);

    /* lives: a passing waited for past the kind's three scripts, a kind no animation, a cell past the game's: refused, as they were */
    dgx_ge_cells_t few;
    CHECK(dgx_ge_cells_init(&few, &kinds, 3, &(dgx_ge_cells_io_t){.get = nothing_get, .set = nothing_set, .cells = 40}));
    few.lives[0] = (dgx_ge_life_t){.cell = 5, .kind = TEST_BLOB_KNIFE, .due_ms = 100};
    few.count    = 1;
    const dgx_ge_life_t bad[] = {
        {.cell = 31, .kind = TEST_BLOB_KNIFE, .pending = 4},
        {.cell = 31, .kind = TEST_BLOB_KNIFE, .script = 3},
        {.cell = 31, .kind = TEST_BLOB_LEVEL},
        {.cell = 40, .kind = TEST_BLOB_KNIFE},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        cells.lives[0] = bad[i];
        dgx_ge_save_begin(&s, blob, sizeof blob, 0, 0);
        dgx_ge_cells_save(&cells, &s);
        one = dgx_ge_save_end(&s);
        CHECK(dgx_ge_load_open(&l, blob, one, 0, NULL) && !dgx_ge_cells_load(&few, &l, 0) && few.count == 1 && few.lives[0].cell == 5);
    }
    cells.lives[0] = (dgx_ge_life_t){.cell = 39, .kind = TEST_BLOB_KNIFE, .pending = 3};
    dgx_ge_save_begin(&s, blob, sizeof blob, 0, 0);
    dgx_ge_cells_save(&cells, &s);
    one = dgx_ge_save_end(&s);
    CHECK(dgx_ge_load_open(&l, blob, one, 0, NULL) && dgx_ge_cells_load(&few, &l, 0) && few.count == 2 && few.lives[0].cell == 39);
    /* an event or a timer of a cell past the game's: no life begins */
    dgx_ge_cells_clear(&few);
    CHECK(!dgx_ge_cells_event(&few, 40, TEST_BLOB_KNIFE, 0, -1, 0, 0) && few.count == 0 && dgx_ge_cells_event(&few, 39, TEST_BLOB_KNIFE, 0, -1, 0, 0));

    /* timers: one that comes again with no period (it would go off for ever), a kind, a reset, an owner of none of theirs, held 2 */
    const dgx_ge_timer_t wrong[] = {
        {.kind = DGX_GE_TIMER_EVERY},
        {.kind = DGX_GE_TIMER_UNTIL, .limit_ms = 100},
        {.kind = DGX_GE_TIMER_UNTIL + 1, .period_ms = 10},
        {.kind = DGX_GE_TIMER_ONCE, .reset = DGX_GE_RESET_MAP + 1},
        {.kind = DGX_GE_TIMER_ONCE, .owner_kind = DGX_GE_OWNER_CELL + 1},
        {.kind = DGX_GE_TIMER_ONCE, .held = 2},
    };
    for (size_t i = 0; i < sizeof wrong / sizeof wrong[0]; ++i) {
        timers.timers[0] = wrong[i];
        dgx_ge_save_begin(&s, blob, sizeof blob, 0, 0);
        dgx_ge_timers_save(&timers, &s);
        one = dgx_ge_save_end(&s);
        CHECK(dgx_ge_load_open(&l, blob, one, 0, NULL) && !dgx_ge_timers_load(&timers2, &l, 0) && timers2.count == 1 && timers2.timers[0].owner == 500);
    }
    timers.timers[0] = (dgx_ge_timer_t){.kind = DGX_GE_TIMER_ONCE, .owner = 7}; /* once: no period wanted */
    dgx_ge_save_begin(&s, blob, sizeof blob, 0, 0);
    dgx_ge_timers_save(&timers, &s);
    one = dgx_ge_save_end(&s);
    CHECK(dgx_ge_load_open(&l, blob, one, 0, NULL) && dgx_ge_timers_load(&timers2, &l, 0) && timers2.count == 1 && timers2.timers[0].owner == 7);

    dgx_ge_world_destroy(&w), dgx_ge_world_destroy(&w2), dgx_ge_world_destroy(&w3), dgx_ge_world_destroy(&w4), dgx_ge_world_destroy(&w5);
    dgx_ge_cells_free(&cells), dgx_ge_cells_free(&cells2), dgx_ge_cells_free(&few);
    dgx_ge_timers_free(&timers), dgx_ge_timers_free(&timers2);
    dgx_ge_pockets_destroy(&mine), dgx_ge_pockets_destroy(&mine2), dgx_ge_pockets_destroy(&small);
    dgx_ge_kinds_free(&kinds);
    free(data);
    CHECK_DONE();
}
