#include "dgx_ge_walk.h"

static const int8_t DX[4] = {-1, 1, 0, 0}, DY[4] = {0, 0, -1, 1};

static int back_of(int side)
{
    return side ^ 1; /* left <-> right, up <-> down */
}

/* The cell beside one by a side, the grid's tunnels taken; false past an edge with none. */
static bool beside(const dgx_ge_walk_grid_t *g, int col, int row, int side, int *c, int *r)
{
    *c = col + DX[side], *r = row + DY[side];
    if (*c < 0 || *c >= g->cols) {
        if (!g->wrap_x) return false;
        *c = (*c + g->cols) % g->cols;
    }
    if (*r < 0 || *r >= g->rows) {
        if (!g->wrap_y) return false;
        *r = (*r + g->rows) % g->rows;
    }
    return true;
}

static bool open_side(const dgx_ge_walker_t *w, const dgx_ge_walk_grid_t *g, int col, int row, int side)
{
    int c, r;
    return side >= 0 && side < 4 && beside(g, col, row, side, &c, &r) && (!g->open || g->open(w, c, r, side, g->ctx));
}

/* The length of a step along a side, 1/256 of a point. */
static int32_t step_of(const dgx_ge_walk_grid_t *g, int side)
{
    return (side < 2 ? g->cell_w : g->cell_h) * 256;
}

void dgx_ge_walk_start(dgx_ge_walker_t *w, int col, int row, uint32_t speed, uint32_t now_ms)
{
    w->col = (int16_t)col, w->row = (int16_t)row, w->pos = 0, w->dir = DGX_GE_WALK_STILL, w->want = -1;
    w->speed = speed, w->at_ms = now_ms, w->rest = 0, w->off_x = w->off_y = 0;
}

/* Gone so far (1/256 of a point): off the middle across its way, as much nearer it. */
static void centre(dgx_ge_walker_t *w, int32_t gone)
{
    int32_t *off = w->off_x ? &w->off_x : &w->off_y;
    if (!*off || gone <= 0) return;
    *off = *off > 0 ? (*off > gone ? *off - gone : 0) : (-*off > gone ? *off + gone : 0);
}

/* At a middle: where now — the turn asked for if it is open, else on, else it stands. */
static void at_middle(dgx_ge_walker_t *w, const dgx_ge_walk_grid_t *g)
{
    if (w->want >= 0 && open_side(w, g, w->col, w->row, w->want)) {
        w->dir = w->want;
        w->want = -1;
    } else if (w->dir >= 0 && !open_side(w, g, w->col, w->row, w->dir)) {
        w->dir = DGX_GE_WALK_STILL;
    }
    /* off the middle along the way it goes now (it turned back in a corner cut, then off it): ahead of the middle, or behind it */
    int32_t *off = w->dir < 0 ? NULL : w->dir < 2 ? &w->off_x : &w->off_y;
    if (off && *off) w->pos = *off * (DX[w->dir] + DY[w->dir]), *off = 0;
}

void dgx_ge_walk_play(dgx_ge_walker_t *w, const dgx_ge_walk_grid_t *g, uint32_t now_ms, dgx_ge_walk_arrive_fn arrive, void *ctx)
{
    for (int guard = 0; guard < 4096 && (int32_t)(now_ms - w->at_ms) > 0; ++guard) {
        if (w->dir < 0 || !w->speed) {
            w->at_ms = now_ms; /* standing: time goes, it does not */
            return;
        }
        /* how long to the next middle (behind the middle of its cell: to that one), its way left, in thousandths of 1/256 of a point, at speed a ms */
        const int32_t  to   = w->pos < 0 ? 0 : step_of(g, w->dir);
        const uint64_t left = (uint64_t)(to - w->pos) * 1000u - w->rest;
        const uint32_t need = (uint32_t)((left + w->speed - 1) / w->speed);
        const uint32_t have = now_ms - w->at_ms;
        if (have < need) {
            const uint64_t gone = (uint64_t)w->speed * have + w->rest;
            w->pos += (int32_t)(gone / 1000u);
            centre(w, (int32_t)(gone / 1000u));
            w->rest  = (uint32_t)(gone % 1000u);
            w->at_ms = now_ms;
            return;
        }
        centre(w, to - w->pos);
        /* the next middle, at its moment: what is past it is kept */
        w->rest  = (uint32_t)((uint64_t)w->speed * need - left);
        w->at_ms += need;
        int c = w->col, r = w->row;
        if (to && !beside(g, w->col, w->row, w->dir, &c, &r)) c = w->col, r = w->row;
        w->col = (int16_t)c, w->row = (int16_t)r, w->pos = 0;
        if (to && arrive) arrive(w, w->at_ms, ctx); /* the middle of its own cell: it came there before */
        const int was = w->dir;
        at_middle(w, g);
        if (w->dir != was) w->rest = 0; /* turned or stood: from the middle */
    }
}

void dgx_ge_walk_want(dgx_ge_walker_t *w, const dgx_ge_walk_grid_t *g, int side, uint32_t now_ms, dgx_ge_walk_arrive_fn arrive, void *ctx)
{
    dgx_ge_walk_play(w, g, now_ms, arrive, ctx);
    w->want = (int8_t)side;
    if (side < 0) return;
    if (w->dir >= 0 && side == back_of(w->dir) && w->pos < 0) {
        /* behind the middle of its cell: past it the other way, so far */
        w->pos = -w->pos - (w->rest ? 1 : 0);
        w->rest = w->rest ? 1000 - w->rest : 0;
        w->dir = (int8_t)side, w->want = -1;
        return;
    }
    if (w->dir >= 0 && side == back_of(w->dir) && w->pos > 0) {
        /* back the way it came, at once: from the next cell's middle, so far the other way */
        int c, r;
        if (beside(g, w->col, w->row, w->dir, &c, &r)) {
            /* the way gone is pos and rest thousandths of 1/256 more: the way left, exactly */
            w->col = (int16_t)c, w->row = (int16_t)r;
            w->pos = step_of(g, w->dir) - w->pos - (w->rest ? 1 : 0);
            w->rest = w->rest ? 1000 - w->rest : 0;
            w->dir = (int8_t)side, w->want = -1;
        }
        return;
    }
    /*
     * A corner cut: a turn asked near a middle, before it or past it, taken at once where that side is open there. Not
     * while it is off the middle still (a corner cut just before): it waits for the next middle, where it is on it again,
     * as it is no farther off than a cell of the new way.
     */
    if (w->corner && w->dir >= 0 && side >> 1 != w->dir >> 1 && !w->off_x && !w->off_y) {
        const int32_t step = step_of(g, side), near = w->corner * 256 < step ? w->corner * 256 : step, left = step_of(g, w->dir) - w->pos;
        int           c, r;
        if (w->pos > 0 && left <= near && beside(g, w->col, w->row, w->dir, &c, &r) && open_side(w, g, c, r, side)) {
            /* before the middle of the next cell: there now, so far short of its middle along the old way */
            w->off_x = -DX[w->dir] * left, w->off_y = -DY[w->dir] * left;
            w->col = (int16_t)c, w->row = (int16_t)r, w->pos = 0, w->rest = 0, w->dir = (int8_t)side, w->want = -1;
            if (arrive) arrive(w, now_ms, ctx);
            return;
        }
        if (w->pos > 0 && w->pos <= near && open_side(w, g, w->col, w->row, side)) {
            /* just past the middle: back to it across the new way */
            w->off_x = DX[w->dir] * w->pos, w->off_y = DY[w->dir] * w->pos;
            w->pos = 0, w->rest = 0, w->dir = (int8_t)side, w->want = -1;
            return;
        }
    }
    if (w->dir < 0 || w->pos == 0) at_middle(w, g); /* standing (or at a middle): it goes now if it may */
}

void dgx_ge_walk_xy(const dgx_ge_walker_t *w, const dgx_ge_walk_grid_t *g, int *x, int *y)
{
    const int d = w->dir >= 0 ? w->dir : 0, moved = w->dir >= 0 ? w->pos / 256 : 0;
    *x = w->col * g->cell_w + g->cell_w / 2 + DX[d] * moved + w->off_x / 256;
    *y = w->row * g->cell_h + g->cell_h / 2 + DY[d] * moved + w->off_y / 256;
}

int dgx_ge_walk_toward(const dgx_ge_walker_t *w, const dgx_ge_walk_grid_t *g, int target_col, int target_row)
{
    static const int8_t order[4] = {2, 0, 3, 1}; /* up, left, down, right */
    int best = -1;
    long best_d = 0;
    for (int i = 0; i < 4; ++i) {
        const int side = order[i];
        int       c, r;
        if ((w->dir >= 0 && side == back_of(w->dir)) || !open_side(w, g, w->col, w->row, side)) continue;
        beside(g, w->col, w->row, side, &c, &r);
        const long d = (long)(c - target_col) * (c - target_col) + (long)(r - target_row) * (r - target_row);
        if (best < 0 || d < best_d) best = side, best_d = d;
    }
    if (best < 0 && w->dir >= 0 && open_side(w, g, w->col, w->row, back_of(w->dir))) best = back_of(w->dir); /* a dead end: back */
    return best;
}
