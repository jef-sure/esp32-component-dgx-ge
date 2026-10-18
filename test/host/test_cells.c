/* Living cells: a life begins with an event, plays over its cell (its modifier, its tile), signals, rests; dice and whole numbers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_cells.h"
#include "dgx_ge_links.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_timers.h"
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

/* the game's cells: a tile and a modifier each; what was set, what was signalled */
static int      tiles[4] = {5, 5, 5, 5}, mods[4];
static int      sets, signals, signal_cell, signal_value;
static uint32_t signal_at;

static void get(uint16_t cell, int *tile, int *mod, void *ctx)
{
    (void)ctx;
    *tile = tiles[cell], *mod = mods[cell];
}

static void set(uint16_t cell, int tile, int mod, void *ctx)
{
    (void)ctx;
    tiles[cell] = tile, mods[cell] = mod, ++sets;
}

static int sound_cell = -1, sound_number = -1;
static uint32_t sound_at;

static void sound_heard(uint16_t cell, int sound, uint32_t at_ms, void *ctx)
{
    (void)ctx;
    sound_cell = cell, sound_number = sound, sound_at = at_ms;
}

static void signal_heard(uint16_t cell, int value, uint32_t at_ms, void *ctx)
{
    (void)ctx;
    ++signals, signal_cell = cell, signal_value = value, signal_at = at_ms;
}

static uint16_t latch_life(uint16_t cell, void *ctx)
{
    (void)ctx;
    return cell < 4 ? TEST_BLOB_LATCH : 0xFFFF;
}

/* the timers of the cells: one gone off goes back to the life of its cell (a button) */
static dgx_ge_cells_t *the_cells;
static int             fired;

static uint16_t button_life(uint16_t cell, void *ctx)
{
    (void)ctx, (void)cell;
    return TEST_BLOB_BUTTON;
}

static void timer_fired(const dgx_ge_timer_t *timer, uint32_t at_ms, void *ctx)
{
    (void)ctx;
    fired += dgx_ge_cells_timer(the_cells, timer, at_ms, button_life, NULL);
}

int main(int argc, char **argv)
{
    size_t   size = 0;
    uint8_t *data = read_file(argc > 1 ? argv[1] : "build/test.pack", &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    dgx_ge_kinds_t kinds;
    CHECK(dgx_ge_kinds_init(&kinds, &pack, 16));
    dgx_ge_cells_t cells;
    CHECK(dgx_ge_cells_init(&cells, &kinds, 2, &(dgx_ge_cells_io_t){get, set, signal_heard, NULL, 0}));

    /* told to open: a life at rest, into opening; a step of 4 every 100 ms, the top signalled when it is reached */
    CHECK(!dgx_ge_cells_living(&cells, 1));
    CHECK(dgx_ge_cells_event(&cells, 1, TEST_BLOB_LATCH, TEST_EVENT_OPEN, -1, 0, 1000));
    CHECK(dgx_ge_cells_living(&cells, 1) && mods[1] == 0 && sets == 0); /* the first step waits */
    dgx_ge_cells_play(&cells, 1099);
    CHECK(mods[1] == 0);
    dgx_ge_cells_play(&cells, 1100);
    CHECK(mods[1] == 4 && sets == 1);
    dgx_ge_cells_play(&cells, 1300);
    CHECK(mods[1] == 12 && signals == 1 && signal_cell == 1 && signal_value == 7 && signal_at == 1300 && cells.lives[0].own[1] == 1);
    /* up for 300 ms, then down a step every 100 ms; down, it rests: the life ends, the cell stays as it is */
    dgx_ge_cells_play(&cells, 1600);
    CHECK(mods[1] == 12);
    dgx_ge_cells_play(&cells, 1700);
    CHECK(mods[1] == 8);
    dgx_ge_cells_play(&cells, 1900);
    CHECK(mods[1] == 0 && !dgx_ge_cells_living(&cells, 1) && cells.count == 0);

    /* how long it stays up given by the event (a link): 100 ms */
    CHECK(dgx_ge_cells_event(&cells, 2, TEST_BLOB_LATCH, TEST_EVENT_OPEN, 0, 100, 2000));
    dgx_ge_cells_play(&cells, 2300);
    CHECK(mods[2] == 12);
    dgx_ge_cells_play(&cells, 2500);
    CHECK(mods[2] == 8); /* 2400: the wait over, 2500: a step down */

    /* broken while up: another tile, the low bits of the modifier with bit 7 (whole numbers: & 0x0F | 0x80) */
    mods[3] = 0x3B;
    CHECK(dgx_ge_cells_event(&cells, 3, TEST_BLOB_LATCH, TEST_EVENT_BREAK, -1, 0, 3000));
    CHECK(tiles[3] == 0 && mods[3] == 0x8B && !dgx_ge_cells_living(&cells, 3)); /* broken holds: over at once */
    /* an event that means nothing at rest: no life stays */
    const int before = cells.count;
    CHECK(dgx_ge_cells_event(&cells, 0, TEST_BLOB_LATCH, TEST_EVENT_PICKED, -1, 0, 3000) && cells.count == before);

    /* room for two: a third is refused; the game ends one; no kind: none */
    CHECK(cells.count == 1 && cells.lives[0].cell == 2);
    CHECK(dgx_ge_cells_event(&cells, 0, TEST_BLOB_LATCH, TEST_EVENT_OPEN, -1, 0, 3000));
    CHECK(!dgx_ge_cells_event(&cells, 3, TEST_BLOB_LATCH, TEST_EVENT_OPEN, -1, 0, 3000));
    CHECK(cells.lives[0].cell == 2 && cells.lives[1].cell == 0); /* as they began */
    dgx_ge_cells_stop(&cells, 2);
    CHECK(cells.count == 1 && cells.lives[0].cell == 0 && !dgx_ge_cells_living(&cells, 2));
    CHECK(!dgx_ge_cells_event(&cells, 3, 9999, TEST_EVENT_OPEN, -1, 0, 3000));
    dgx_ge_cells_clear(&cells);
    CHECK(cells.count == 0);

    /* the dice of the game: rnd(8) the next number of its seed, 0..8, as the game would cast it; % of whole numbers */
    uint32_t seed = 12345, mine = 12345;
    dgx_ge_dice   = &seed;
    CHECK(dgx_ge_cells_event(&cells, 1, TEST_BLOB_FLICKER, TEST_EVENT_LIGHT, -1, 0, 4000));
    CHECK(cells.lives[0].own[0] == 3); /* 23 % 5 */
    bool same = true;
    for (int k = 1; k <= 5; ++k) {
        dgx_ge_cells_play(&cells, 4000 + 100 * (uint32_t)k);
        mine = mine * 214013u + 2531011u;
        same = same && mods[1] == (int)((mine >> 16) % 9);
    }
    CHECK(same && seed == mine);
    dgx_ge_dice = NULL;
    dgx_ge_cells_play(&cells, 4600); /* no dice: 0 */
    CHECK(mods[1] == 0);

    /* links: a source fires from its first to its last, each target's life told its own event or the one fired, a value first */
    dgx_ge_cells_clear(&cells);
    dgx_ge_cells_free(&cells);
    CHECK(dgx_ge_cells_init(&cells, &kinds, 4, &(dgx_ge_cells_io_t){get, set, signal_heard, NULL, 0}));
    size_t         n;
    const uint8_t *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_LINKS, &n);
    dgx_ge_links_t links;
    CHECK(blob && dgx_ge_links_open(&links, blob, n) && links.count == 5);
    for (int i = 0; i < 4; ++i) tiles[i] = 5, mods[i] = 0;
    mods[2] = 0;
    CHECK(dgx_ge_links_fire(&links, 0, TEST_EVENT_OPEN, &cells, latch_life, NULL, 5000) == 3);
    CHECK(dgx_ge_cells_living(&cells, 1) && !dgx_ge_cells_living(&cells, 2) && tiles[2] == 0 && dgx_ge_cells_living(&cells, 3));
    CHECK(cells.lives[1].cell == 3 && cells.lives[1].own[0] == 100 && cells.lives[0].own[0] == 300); /* the value given; none: as it starts */
    CHECK(dgx_ge_links_fire(&links, 3, TEST_EVENT_OPEN, &cells, latch_life, NULL, 5000) == 1 && dgx_ge_cells_living(&cells, 0));
    CHECK(dgx_ge_links_fire(&links, 9, TEST_EVENT_OPEN, &cells, latch_life, NULL, 5000) == 0);
    CHECK(!dgx_ge_links_open(&links, data, 4));

    /* a life starts a timer of its cell: a button down, 300 ms; pressed again meanwhile, anew; gone off, back to its life, up */
    dgx_ge_timers_t timers;
    CHECK(dgx_ge_timers_init(&timers, 2, timer_fired, NULL));
    blob = dgx_ge_pack_blob(&pack, TEST_BLOB_TIMERS, &n);
    CHECK(blob && dgx_ge_timers_define(&timers, blob, n));
    dgx_ge_cells_clear(&cells); /* the latches above still live: room for the button */
    the_cells = &cells, cells.timers = &timers, cells.map = 3;
    tiles[1] = 6, mods[1] = 0;
    cells.io.sound = sound_heard; /* its sound to the game, by its cell and moment */
    CHECK(dgx_ge_cells_event(&cells, 1, TEST_BLOB_BUTTON, TEST_EVENT_PRESS, -1, 0, 6000) && tiles[1] == 7 && !dgx_ge_cells_living(&cells, 1));
    CHECK(sound_cell == 1 && sound_number == 1 && sound_at == 6000);
    CHECK(timers.count == 1 && timers.timers[0].name == TEST_TIMER_DOWN && timers.timers[0].owner_kind == DGX_GE_OWNER_CELL);
    CHECK(timers.timers[0].owner == 1 && timers.timers[0].map == 3 && timers.timers[0].start_ms == 6000);
    CHECK(dgx_ge_cells_event(&cells, 1, TEST_BLOB_BUTTON, TEST_EVENT_PRESS, -1, 0, 6200)); /* down still: the same tile, the timer anew */
    CHECK(timers.count == 1 && timers.timers[0].start_ms == 6200 && tiles[1] == 7);
    dgx_ge_timers_play(&timers, 6499);
    CHECK(fired == 0 && tiles[1] == 7);
    dgx_ge_timers_play(&timers, 7000); /* late: it goes off at 6500 */
    CHECK(fired == 1 && tiles[1] == 6 && timers.count == 0 && !dgx_ge_cells_living(&cells, 1));
    /* the map left: the timers of its cells go (their cells are no more) */
    CHECK(dgx_ge_cells_event(&cells, 1, TEST_BLOB_BUTTON, TEST_EVENT_PRESS, -1, 0, 8000) && timers.count == 1);
    dgx_ge_timers_leave(&timers, 3, -1);
    CHECK(timers.count == 0);
    /* not of a cell, or no event: not given */
    dgx_ge_timer_t other = {.owner_kind = DGX_GE_OWNER_GAME, .event = TEST_EVENT_RELEASE};
    CHECK(!dgx_ge_cells_timer(&cells, &other, 9000, button_life, NULL));
    other.owner_kind = DGX_GE_OWNER_CELL, other.event = 0xFF;
    CHECK(!dgx_ge_cells_timer(&cells, &other, 9000, button_life, NULL));
    /* said by a script: stop, hold, let go; none of a timer: false */
    CHECK(dgx_ge_timers_said(&timers, DGX_GE_ANIM_TIMER_START, TEST_TIMER_DOWN, DGX_GE_OWNER_CELL, 2, 3, 0, 9000) && timers.count == 1);
    CHECK(dgx_ge_timers_said(&timers, DGX_GE_ANIM_TIMER_HOLD, TEST_TIMER_DOWN, DGX_GE_OWNER_CELL, 2, 3, 0, 9100) && timers.timers[0].held);
    CHECK(dgx_ge_timers_said(&timers, DGX_GE_ANIM_TIMER_GO, TEST_TIMER_DOWN, DGX_GE_OWNER_CELL, 2, 3, 0, 9300) && timers.timers[0].next_ms == 9500);
    CHECK(dgx_ge_timers_said(&timers, DGX_GE_ANIM_TIMER_STOP, TEST_TIMER_DOWN, DGX_GE_OWNER_CELL, 2, 3, 0, 9300) && timers.count == 0);
    CHECK(!dgx_ge_timers_said(&timers, DGX_GE_ANIM_SIGNAL, 0, DGX_GE_OWNER_CELL, 2, 3, 0, 9300) && !dgx_ge_timers_said(NULL, DGX_GE_ANIM_TIMER_START, 0, 0, 0, 0, 0, 0));
    dgx_ge_timers_free(&timers);

    dgx_ge_cells_free(&cells);
    dgx_ge_kinds_free(&kinds);
    free(data);
    CHECK_DONE();
}
