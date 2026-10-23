/*
 * A walker bound to an actor: the actor where the walker is at any rate of calls (through turns, corners, stops, the
 * tunnel), smooth between them; the side told once a change, at its moment; the speed of the param from the next
 * middle; a start from the actor's place; its script's motion overwritten while bound, its own unbound.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_anim.h"
#include "dgx_ge_graph.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_walk.h"
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

/* 9 x 5 cells of 8 points: a corridor with a tunnel at its ends, another below, joined at every other column */
static const char *maze[5] = {"#########", ".........", "#.#.#.#.#", "#.......#", "#########"};

static bool open_cell(const dgx_ge_walker_t *w, int col, int row, int side, void *ctx)
{
    (void)w, (void)side, (void)ctx;
    return maze[row][col] == '.';
}

/* what was told, in order: the side (4: standing) and its moment */
typedef struct {
    int      side[512];
    uint32_t at[512];
    int      count;
} heard_t;

static void hear(heard_t *h, int side, uint32_t at_ms)
{
    if (h->count < 512) h->side[h->count] = side, h->at[h->count] = at_ms;
    ++h->count;
}

static void say(dgx_ge_actor_t *actor, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    (void)actor;
    if (what == DGX_GE_ANIM_SIGNAL && ctx) hear(ctx, value, at_ms);
}

/* What the binding should tell, worked out from a walker apart from it: the side as it changes, standing once. */
typedef struct {
    int  side;
    bool stood;
} expect_t;

static void expect(expect_t *e, heard_t *h, const dgx_ge_walker_t *w, uint32_t at_ms)
{
    if (w->dir < 0) {
        if (!e->stood) e->stood = true, hear(h, 4, at_ms);
    } else if (w->dir != e->side || e->stood) {
        e->side = w->dir, e->stood = false, hear(h, w->dir, at_ms);
    }
}

static int      arrived;
static uint32_t arrived_at[16];

static void arrive(dgx_ge_walker_t *w, uint32_t at_ms, void *ctx)
{
    (void)w, (void)ctx;
    if (arrived < 16) arrived_at[arrived] = at_ms;
    ++arrived;
}

/* the game's arrive that unbinds it at a cell (its script moves it from there) */
static dgx_ge_walk_bind_t *unbind_at;
static int                 unbind_col;

static int unbound_x, unbound_y;

static void arrive_unbind(dgx_ge_walker_t *w, uint32_t at_ms, void *ctx)
{
    (void)at_ms, (void)ctx;
    if (!unbind_at || w->col != unbind_col || !unbind_at->actor) return;
    unbound_x = unbind_at->actor->x, unbound_y = unbind_at->actor->y; /* where the game finds it */
    w->dir = DGX_GE_WALK_STILL, unbind_at->actor = NULL;            /* it stands, its script takes it on */
}

/* a gate the game opens and shuts: the cell right of column 3 of the middle row */
static bool gate_open;

static bool open_gate(const dgx_ge_walker_t *w, int col, int row, int side, void *ctx)
{
    return open_cell(w, col, row, side, ctx) && (gate_open || col != 4 || row != 1);
}

/* the game's arrive that stands it a while at a cell (eating) */
static void arrive_stand(dgx_ge_walker_t *w, uint32_t at_ms, void *ctx)
{
    (void)at_ms, (void)ctx;
    if (w->col == 3) w->at_ms += 40;
}

static dgx_ge_walk_bind_t bind_of(int way)
{
    return (dgx_ge_walk_bind_t){.dx = 5, .dy = 20, .turn = TEST_GRIDDER_EVENT_LEFT, .stop = TEST_GRIDDER_EVENT_STOP, .speed = TEST_GRIDDER_PARAM_SPEED,
                                .way = (int8_t)way};
}

/* The actor where a walker is (the binding's offsets). */
static bool at_walker(const dgx_ge_actor_t *a, const dgx_ge_walker_t *w, const dgx_ge_walk_grid_t *g)
{
    int x, y;
    dgx_ge_walk_xy(w, g, &x, &y);
    return a->x == (x + 5) * 256 && a->y == (y + 20) * 256;
}

static bool same_walker(const dgx_ge_walker_t *a, const dgx_ge_walker_t *b)
{
    return a->col == b->col && a->row == b->row && a->pos == b->pos && a->dir == b->dir && a->want == b->want && a->speed == b->speed &&
           a->at_ms == b->at_ms && a->rest == b->rest && a->off_x == b->off_x && a->off_y == b->off_y;
}

int main(int argc, char **argv)
{
    size_t   size = 0;
    uint8_t *data = read_file(argc > 1 ? argv[1] : "build/test.pack", &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    size_t         n;
    const uint8_t *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_GRIDDER, &n);
    dgx_ge_anim_t  anim;
    CHECK(blob && dgx_ge_anim_open(&anim, blob, n));
    const dgx_ge_walk_grid_t g = {.cols = 9, .rows = 5, .cell_w = 8, .cell_h = 8, .wrap_x = true, .open = open_cell};
    dgx_ge_actor_t           a;
    int                      x, y;

    /*
     * The actor where the walker is, at any rate of calls: one walker played by itself each millisecond, the bound one
     * at random moments, the same sides asked at the same moments (corners cut, walls stood at, the tunnel). At every
     * call both walkers are the same, the actor at its point, and what it was told is what the lone one did, at the
     * moments it did it.
     */
    srand(7);
    int differ = 0, astray = 0, runs = 0;
    for (int run = 0; run < 200; ++run) {
        const uint32_t speed = (uint32_t)(40 + rand() % 120) * 256;
        dgx_ge_actor_start(&a, &anim, TEST_GRIDDER_SCRIPT_STAND, 5 + 12, 20 + 12, 0);
        a.params[TEST_GRIDDER_PARAM_SPEED] = (int32_t)speed;
        dgx_ge_walk_bind_t b = bind_of(run % 2 ? TEST_GRIDDER_PARAM_WAY : -1);
        b.walker.corner      = (uint8_t)(run % 3 ? 4 : 0);
        dgx_ge_walk_bind_start(&b, &a, &g, DGX_GE_WALK_STILL, 0);
        dgx_ge_walker_t lone;
        dgx_ge_walk_start(&lone, 1, 1, speed, 0);
        lone.corner   = b.walker.corner;
        heard_t  told = {0}, done = {0};
        expect_t e    = {-1, false};
        expect(&e, &done, &lone, 0);
        uint32_t t = 0, tl = 0, next_want = (uint32_t)(rand() % 60);
        while (t < 3000) {
            uint32_t to = t + 1 + (uint32_t)(rand() % 40);
            const bool asked = next_want <= to;
            if (asked) to = next_want;
            while (tl < to) {
                dgx_ge_walk_play(&lone, &g, ++tl, NULL, NULL);
                expect(&e, &done, &lone, tl);
            }
            if (asked) {
                const int side = rand() % 4;
                dgx_ge_walk_want(&lone, &g, side, to, NULL, NULL);
                expect(&e, &done, &lone, to);
                dgx_ge_walk_bind_want(&b, &g, side, to, NULL, say, &told);
                next_want = to + 1 + (uint32_t)(rand() % 120);
            } else {
                dgx_ge_walk_bind_play(&b, &g, to, NULL, say, &told);
            }
            dgx_ge_actor_advance(&a, to, say, &told);
            t = to;
            differ += !same_walker(&b.walker, &lone);
            astray += !at_walker(&a, &lone, &g);
        }
        bool same = told.count == done.count && told.count <= 512;
        for (int i = 0; same && i < told.count; ++i) same = told.side[i] == done.side[i] && told.at[i] == done.at[i];
        runs += same && told.count > 10;
        if (run % 2) CHECK(e.side < 0 || a.params[TEST_GRIDDER_PARAM_WAY] == e.side); /* the side kept in its param */
        CHECK(b.side == e.side);
    }
    CHECK(differ == 0 && astray == 0);
    CHECK(runs == 200);

    /* smooth between the calls: on its own the actor goes on as the walker does, along a corridor and on a corner's slant */
    dgx_ge_actor_start(&a, &anim, TEST_GRIDDER_SCRIPT_STAND, 5 + 12, 20 + 12, 0);
    dgx_ge_walk_bind_t b = bind_of(-1);
    b.walker.corner      = 4;
    dgx_ge_walk_bind_start(&b, &a, &g, DGX_GE_SIDE_RIGHT, 0);
    dgx_ge_walk_bind_play(&b, &g, 20, NULL, say, NULL);
    dgx_ge_walker_t lone = b.walker;
    int32_t         ax, ay, vx, vy;
    dgx_ge_actor_state_at(&a, 45, &ax, &ay, &vx, &vy);
    dgx_ge_walk_play(&lone, &g, 45, NULL, NULL);
    dgx_ge_walk_xy(&lone, &g, &x, &y);
    CHECK(abs((ax >> 8) - (x + 5)) <= 1 && ay >> 8 == y + 20 && vx == 80 * 256 && vy == 0);
    /* down asked 4 points before column 3's middle (at 150 ms): on the slant, 2 points on each way 25 ms later */
    dgx_ge_walk_bind_want(&b, &g, DGX_GE_SIDE_DOWN, 150, NULL, say, NULL);
    CHECK(b.walker.dir == DGX_GE_SIDE_DOWN && b.walker.col == 3 && b.walker.off_x == -4 * 256 && at_walker(&a, &b.walker, &g));
    CHECK(a.vx == 80 * 256 && a.vy == 80 * 256);
    dgx_ge_actor_state_at(&a, 175, &ax, &ay, &vx, &vy);
    CHECK(ax == (3 * 8 + 4 - 2 + 5) * 256 && ay == (12 + 2 + 20) * 256);

    /* the speed of the param: changed between middles, taken at the next */
    dgx_ge_actor_start(&a, &anim, TEST_GRIDDER_SCRIPT_STAND, 5 + 12, 20 + 12, 0);
    a.params[TEST_GRIDDER_PARAM_SPEED] = 80 * 256;
    b                                  = bind_of(-1);
    dgx_ge_walk_bind_start(&b, &a, &g, DGX_GE_SIDE_RIGHT, 0);
    CHECK(b.walker.speed == 80 * 256);
    dgx_ge_walk_bind_play(&b, &g, 30, arrive, say, NULL);
    a.params[TEST_GRIDDER_PARAM_SPEED] = 160 * 256;
    arrived                            = 0;
    dgx_ge_walk_bind_play(&b, &g, 99, arrive, say, NULL);
    CHECK(b.walker.speed == 80 * 256 && arrived == 0);
    dgx_ge_walk_bind_play(&b, &g, 160, arrive, say, NULL);
    CHECK(arrived == 2 && arrived_at[0] == 100 && arrived_at[1] == 150 && b.walker.speed == 160 * 256);
    /* none (-1): the game's speed stays */
    b.speed        = -1;
    b.walker.speed = 40 * 256;
    dgx_ge_walk_bind_play(&b, &g, 400, arrive, say, NULL);
    CHECK(b.walker.speed == 40 * 256);

    /* a start from the actor's place: on the edge of columns 3 and 4, a point below the middle's line */
    dgx_ge_actor_start(&a, &anim, TEST_GRIDDER_SCRIPT_STAND, 5 + 32, 20 + 13, 0);
    b = bind_of(-1);
    dgx_ge_walk_bind_start(&b, &a, &g, DGX_GE_SIDE_LEFT, 0);
    CHECK(b.walker.col == 4 && b.walker.row == 1 && b.walker.pos == 4 * 256 && b.walker.off_y == 256);
    CHECK(a.x == (5 + 32) * 256 && a.y == (20 + 13) * 256 && a.vx == -80 * 256 && a.vy == -80 * 256);
    arrived = 0;
    dgx_ge_walk_bind_play(&b, &g, 60, arrive, say, NULL);
    CHECK(arrived == 1 && arrived_at[0] == 50 && b.walker.col == 3 && b.walker.off_y == 0 && at_walker(&a, &b.walker, &g));
    /* going right from there: short of column 4's middle, past column 3's (it hears it comes to 4) */
    dgx_ge_actor_start(&a, &anim, TEST_GRIDDER_SCRIPT_STAND, 5 + 32, 20 + 12, 0);
    dgx_ge_walk_bind_start(&b, &a, &g, DGX_GE_SIDE_RIGHT, 0);
    CHECK(b.walker.col == 3 && b.walker.pos == 4 * 256 && a.x == (5 + 32) * 256);
    arrived = 0;
    dgx_ge_walk_bind_play(&b, &g, 60, arrive, say, NULL);
    CHECK(arrived == 1 && arrived_at[0] == 50 && b.walker.col == 4);
    /* past the left edge, in the tunnel: the cell there (-1), no jump; the cell of the other side as it goes on */
    dgx_ge_actor_start(&a, &anim, TEST_GRIDDER_SCRIPT_STAND, 5 - 6, 20 + 12, 0);
    dgx_ge_walk_bind_start(&b, &a, &g, DGX_GE_SIDE_LEFT, 0);
    CHECK(b.walker.col == -1 && b.walker.pos == 2 * 256 && a.x == (5 - 6) * 256);
    arrived = 0;
    dgx_ge_walk_bind_play(&b, &g, 80, arrive, say, NULL);
    CHECK(arrived == 1 && arrived_at[0] == 75 && b.walker.col == 7);
    /* short of the middle of the cell past the edge: past column 0's */
    dgx_ge_actor_start(&a, &anim, TEST_GRIDDER_SCRIPT_STAND, 5 - 2, 20 + 12, 0);
    dgx_ge_walk_bind_start(&b, &a, &g, DGX_GE_SIDE_LEFT, 0);
    CHECK(b.walker.col == 0 && b.walker.pos == 6 * 256 && a.x == (5 - 2) * 256);
    /* standing off the middle: kept there, and from there as it goes */
    dgx_ge_actor_start(&a, &anim, TEST_GRIDDER_SCRIPT_STAND, 5 + 14, 20 + 12, 0);
    dgx_ge_walk_bind_start(&b, &a, &g, DGX_GE_WALK_STILL, 0);
    CHECK(b.walker.dir == DGX_GE_WALK_STILL && a.x == (5 + 14) * 256 && a.vx == 0);
    dgx_ge_walk_bind_want(&b, &g, DGX_GE_SIDE_LEFT, 10, NULL, say, NULL);
    CHECK(b.walker.dir == DGX_GE_SIDE_LEFT && a.x == (5 + 14) * 256 && a.vx == -80 * 256);

    /* its script moving it while bound: the binding's motion again at the next play */
    dgx_ge_actor_start(&a, &anim, TEST_GRIDDER_SCRIPT_STAND, 5 + 12, 20 + 12, 0);
    b = bind_of(-1);
    dgx_ge_walk_bind_start(&b, &a, &g, DGX_GE_SIDE_RIGHT, 0);
    dgx_ge_walk_bind_play(&b, &g, 20, NULL, say, NULL);
    dgx_ge_actor_event(&a, TEST_GRIDDER_EVENT_DRIFT, 20, say, NULL);
    CHECK(a.vy == 50 * 256);
    dgx_ge_walk_bind_play(&b, &g, 40, NULL, say, NULL);
    CHECK(a.vx == 80 * 256 && a.vy == 0 && at_walker(&a, &b.walker, &g));
    /* unbound: its own (the walker walks alone) */
    b.actor = NULL;
    dgx_ge_actor_event(&a, TEST_GRIDDER_EVENT_DRIFT, 40, say, NULL);
    const int32_t was_x = a.x;
    dgx_ge_walk_bind_play(&b, &g, 60, NULL, say, NULL);
    dgx_ge_actor_advance(&a, 60, say, NULL);
    CHECK(a.x == was_x && a.y > (20 + 12) * 256 && b.walker.pos > 0);
    /*
     * Unbound by the game as it comes to a middle (stood there, its script to take it on): the game finds it at the
     * middle at that moment (from 2 points below the line it went to it on the slant), nothing told after (no stop).
     */
    dgx_ge_actor_start(&a, &anim, TEST_GRIDDER_SCRIPT_STAND, 5 + 12, 20 + 14, 0);
    b = bind_of(-1);
    dgx_ge_walk_bind_start(&b, &a, &g, DGX_GE_SIDE_RIGHT, 0);
    unbind_at = &b, unbind_col = 2;
    heard_t heard = {0};
    dgx_ge_walk_bind_play(&b, &g, 130, arrive_unbind, say, &heard);
    CHECK(unbound_x == (5 + 20) * 256 && unbound_y == (20 + 12) * 256);
    CHECK(!b.actor && b.walker.col == 2 && b.walker.dir == DGX_GE_WALK_STILL && a.x == (5 + 20) * 256 && a.moved_ms == 100);
    CHECK(heard.count == 1 && heard.side[0] == DGX_GE_SIDE_RIGHT);

    /* stood at a gate, told so; the gate opened, on the same way: told it again */
    const dgx_ge_walk_grid_t gated = {.cols = 9, .rows = 5, .cell_w = 8, .cell_h = 8, .wrap_x = true, .open = open_gate};
    dgx_ge_actor_start(&a, &anim, TEST_GRIDDER_SCRIPT_STAND, 5 + 12, 20 + 12, 0);
    b = bind_of(-1);
    dgx_ge_walk_bind_start(&b, &a, &gated, DGX_GE_SIDE_RIGHT, 0);
    heard = (heard_t){0};
    dgx_ge_walk_bind_play(&b, &gated, 300, NULL, say, &heard); /* column 3 at 200: the gate shut */
    gate_open = true;
    dgx_ge_walk_bind_want(&b, &gated, DGX_GE_SIDE_RIGHT, 310, NULL, say, &heard);
    CHECK(heard.count == 3 && heard.side[0] == DGX_GE_SIDE_RIGHT && heard.side[1] == 4 && heard.at[1] == 200 && heard.side[2] == DGX_GE_SIDE_RIGHT &&
          heard.at[2] == 310);

    /* stood a while by the game at a middle (its arrive): the actor stands there as long, then goes on */
    dgx_ge_actor_start(&a, &anim, TEST_GRIDDER_SCRIPT_STAND, 5 + 12, 20 + 12, 0);
    b = bind_of(-1);
    dgx_ge_walk_bind_start(&b, &a, &g, DGX_GE_SIDE_RIGHT, 0);
    dgx_ge_walk_bind_play(&b, &g, 220, arrive_stand, say, NULL); /* column 3 at 200: stands till 240 */
    dgx_ge_actor_advance(&a, 220, say, NULL);
    CHECK(a.x == (5 + 28) * 256 && b.walker.at_ms == 240);
    dgx_ge_walk_bind_play(&b, &g, 265, arrive_stand, say, NULL);
    CHECK(a.x == (5 + 30) * 256 && at_walker(&a, &b.walker, &g));

    free(data);
    CHECK_DONE();
}
