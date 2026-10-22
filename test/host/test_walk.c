/* Walking a grid: middle to middle at a speed, exact; a turn asked for taken where it opens; walls stop; back at once; tunnels; toward a target. */
#include <stdio.h>
#include <stdlib.h>

#include "check.h"
#include "dgx_ge_walk.h"

/* a maze of 5 x 3 cells of 8 points: '#' walls, a corridor along the middle row, a branch up at column 2, a tunnel at the row's ends */
static const char *maze[3] = {"##.##", ".....", "#####"};

static bool open_cell(const dgx_ge_walker_t *w, int col, int row, int side, void *ctx)
{
    (void)w, (void)side, (void)ctx;
    return maze[row][col] == '.';
}

static bool open_all(const dgx_ge_walker_t *w, int col, int row, int side, void *ctx)
{
    (void)w, (void)col, (void)row, (void)side, (void)ctx;
    return true;
}

static int      arrived;
static uint32_t arrived_at[16];

static void arrive(dgx_ge_walker_t *w, uint32_t at_ms, void *ctx)
{
    (void)w, (void)ctx;
    if (arrived < 16) arrived_at[arrived] = at_ms;
    ++arrived;
}

int main(void)
{
    dgx_ge_walk_grid_t g = {.cols = 5, .rows = 3, .cell_w = 8, .cell_h = 8, .wrap_x = true, .open = open_cell};
    dgx_ge_walker_t    w;
    int                x, y;
    /* 3 points a second... 80: a cell of 8 points in 100 ms */
    dgx_ge_walk_start(&w, 0, 1, 80 * 256, 1000);
    dgx_ge_walk_xy(&w, &g, &x, &y);
    CHECK(x == 4 && y == 12 && w.dir == DGX_GE_WALK_STILL);
    dgx_ge_walk_want(&w, &g, 1 /* right */, 1000, arrive, NULL);
    CHECK(w.dir == 1 && w.want == -1);
    /* called late: each middle at its moment */
    dgx_ge_walk_play(&w, &g, 1250, arrive, NULL);
    CHECK(arrived == 2 && arrived_at[0] == 1100 && arrived_at[1] == 1200 && w.col == 2);
    dgx_ge_walk_xy(&w, &g, &x, &y);
    CHECK(x == 2 * 8 + 4 + 4 && y == 12); /* half a cell past the middle of column 2 */
    /* up asked for between middles: kept, taken at the next middle where it opens (column 2 has it: passed; none at 3, 4) */
    dgx_ge_walk_want(&w, &g, 2, 1260, arrive, NULL);
    CHECK(w.want == 2 && w.dir == 1);
    /* back at once, from where it is */
    dgx_ge_walk_want(&w, &g, 0, 1270, arrive, NULL);
    CHECK(w.dir == 0 && w.col == 3 && w.want == -1);
    dgx_ge_walk_xy(&w, &g, &x, &y);
    CHECK(x == 3 * 8 + 4 - 2); /* 0.7 of the cell past column 2: back, 2.4 points short of column 3's middle (whole points: 2) */
    /* the tunnel: left past column 0 to column 4; 5.6 points to column 2's middle: 70 ms; then a cell each 100 ms */
    arrived = 0;
    dgx_ge_walk_play(&w, &g, 1650, arrive, NULL);
    CHECK(arrived == 4 && w.col == 4 && w.dir == 0);
    CHECK(arrived_at[0] == 1340 && arrived_at[1] == 1440 && arrived_at[2] == 1540 && arrived_at[3] == 1640);
    /* up at column 2 when it comes there; then a wall: it stands at the middle */
    dgx_ge_walk_want(&w, &g, 2, 1650, arrive, NULL);
    dgx_ge_walk_play(&w, &g, 2000, arrive, NULL);
    CHECK(w.col == 2 && w.row == 0 && w.dir == DGX_GE_WALK_STILL && w.pos == 0);
    /* toward a target: from column 2 of the middle row going left, the target up-left: up first (equal: up, left, down, right) */
    dgx_ge_walker_t g2;
    dgx_ge_walk_start(&g2, 2, 1, 80 * 256, 0);
    g2.dir = 0;
    CHECK(dgx_ge_walk_toward(&g2, &g, 3, 0) == 2); /* up: (2,0) is 1 from it, left (1,1) 5 */
    CHECK(dgx_ge_walk_toward(&g2, &g, 0, 0) == 0); /* left: 2, up: 4 */
    CHECK(dgx_ge_walk_toward(&g2, &g, 1, 0) == 2); /* equal (1 and 1): up first */
    g2.dir = 1;
    CHECK(dgx_ge_walk_toward(&g2, &g, 0, 1) == 2); /* not back: up is nearer than right */
    dgx_ge_walk_start(&g2, 2, 0, 80 * 256, 0);
    g2.dir = 2;
    CHECK(dgx_ge_walk_toward(&g2, &g, 0, 0) == 3); /* a dead end: back */

    /* cutting a corner, 4 points: up asked for 4 points before column 2's middle is taken at once, on the slant */
    dgx_ge_walker_t c;
    dgx_ge_walk_start(&c, 1, 1, 80 * 256, 1000);
    c.corner = 4;
    dgx_ge_walk_want(&c, &g, 1, 1000, arrive, NULL);
    arrived = 0;
    dgx_ge_walk_want(&c, &g, 2, 1050, arrive, NULL); /* 4 points along: 4 short of column 2's middle */
    CHECK(c.dir == 2 && c.col == 2 && c.row == 1 && c.want == -1 && arrived == 1 && arrived_at[0] == 1050); /* there: heard now */
    dgx_ge_walk_xy(&c, &g, &x, &y);
    CHECK(x == 2 * 8 + 4 - 4 && y == 12);
    dgx_ge_walk_play(&c, &g, 1075, arrive, NULL); /* 2 points up: 2 nearer the middle across */
    dgx_ge_walk_xy(&c, &g, &x, &y);
    CHECK(x == 2 * 8 + 4 - 2 && y == 10);
    dgx_ge_walk_play(&c, &g, 1100, arrive, NULL); /* on the middle's line: so on */
    dgx_ge_walk_xy(&c, &g, &x, &y);
    CHECK(x == 2 * 8 + 4 && y == 8 && c.off_x == 0);
    dgx_ge_walk_play(&c, &g, 1200, arrive, NULL); /* the middle of the cell above at 1150, a wall beyond: it stands */
    CHECK(c.col == 2 && c.row == 0 && c.dir == DGX_GE_WALK_STILL && arrived == 2 && arrived_at[1] == 1150);
    /* just past the middle (2 points): back to it across the new way */
    dgx_ge_walk_start(&c, 2, 1, 80 * 256, 2000);
    dgx_ge_walk_want(&c, &g, 1, 2000, arrive, NULL);
    dgx_ge_walk_want(&c, &g, 2, 2025, arrive, NULL);
    dgx_ge_walk_xy(&c, &g, &x, &y);
    CHECK(c.dir == 2 && c.col == 2 && x == 2 * 8 + 4 + 2 && y == 12);
    /* farther than 4 points, or no way that side there: kept for the next middle where it opens, as without corners */
    dgx_ge_walk_start(&c, 2, 1, 80 * 256, 3000);
    dgx_ge_walk_want(&c, &g, 1, 3000, arrive, NULL);
    dgx_ge_walk_want(&c, &g, 2, 3063, arrive, NULL); /* 5 past column 2 (more than 4), 3 short of column 3 (no way up there) */
    CHECK(c.dir == 1 && c.want == 2 && c.off_x == 0 && c.off_y == 0);
    /* no corners (0; a start keeps what it is): the same ask before the middle is kept and taken at the middle, straight */
    dgx_ge_walk_start(&c, 1, 1, 80 * 256, 4000);
    c.corner = 0;
    dgx_ge_walk_want(&c, &g, 1, 4000, arrive, NULL);
    dgx_ge_walk_want(&c, &g, 2, 4050, arrive, NULL);
    CHECK(c.dir == 1 && c.want == 2 && c.col == 1);
    dgx_ge_walk_play(&c, &g, 4125, arrive, NULL);
    dgx_ge_walk_xy(&c, &g, &x, &y);
    CHECK(c.dir == 2 && c.col == 2 && x == 2 * 8 + 4 && y == 10);

    /*
     * A turn while a corner is still being cut (off the middle): no jump to the corner, it waits for the next middle.
     * Up 4 points before column 2's middle, one point on (3 off), right asked: up still, right at the middle above.
     */
    dgx_ge_walk_grid_t open = {.cols = 9, .rows = 9, .cell_w = 8, .cell_h = 8, .wrap_x = true, .wrap_y = true, .open = open_all};
    dgx_ge_walk_start(&c, 1, 4, 80 * 256, 5000);
    c.corner = 4;
    dgx_ge_walk_want(&c, &open, 1, 5000, NULL, NULL);
    dgx_ge_walk_want(&c, &open, 2, 5050, NULL, NULL);
    dgx_ge_walk_play(&c, &open, 5062, NULL, NULL);
    int bx, by;
    dgx_ge_walk_xy(&c, &open, &bx, &by);
    dgx_ge_walk_want(&c, &open, 1, 5062, NULL, NULL);
    dgx_ge_walk_xy(&c, &open, &x, &y);
    CHECK(x == bx && y == by && c.dir == 2 && c.want == 1);
    dgx_ge_walk_play(&c, &open, 5150, NULL, NULL);
    CHECK(c.col == 2 && c.row == 3 && c.dir == 1 && c.off_x == 0 && c.off_y == 0);
    /* turns asked at random, corners of any size: never a jump, never faster than its speed (a point a step for the rounding) */
    srand(3);
    int jumps = 0;
    for (int k = 0; k < 300; ++k) {
        dgx_ge_walk_start(&c, 4, 4, (uint32_t)(40 + rand() % 120) * 256, 0);
        c.corner = (uint8_t)(1 + rand() % 12);
        dgx_ge_walk_want(&c, &open, rand() % 4, 0, NULL, NULL);
        uint32_t t = 0;
        for (int step = 0; step < 60; ++step) {
            int x0, y0, x1, y1, x2, y2;
            dgx_ge_walk_xy(&c, &open, &x0, &y0);
            const uint32_t dt = 1 + (uint32_t)(rand() % 40);
            dgx_ge_walk_play(&c, &open, t += dt, NULL, NULL);
            dgx_ge_walk_xy(&c, &open, &x1, &y1);
            const int far = (int)(c.speed / 256 * dt / 1000) + 2;
            /* across a tunnel the point goes round: the way is the shorter one */
            const int ddx = (abs(x1 - x0) + 36) % 72 - 36, ddy = (abs(y1 - y0) + 36) % 72 - 36;
            jumps += abs(ddx) > far || abs(ddy) > far;
            dgx_ge_walk_want(&c, &open, rand() % 4, t, NULL, NULL);
            dgx_ge_walk_xy(&c, &open, &x2, &y2);
            jumps += abs((abs(x2 - x1) + 36) % 72 - 36) > 1 || abs((abs(y2 - y1) + 36) % 72 - 36) > 1;
        }
    }
    CHECK(jumps == 0);
    CHECK_DONE();
}
