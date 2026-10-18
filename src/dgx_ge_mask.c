#include <stdlib.h>
#include <string.h>

#include "dgx_ge_mask.h"

#define GRID 16 /* tiles across and down the overlap at most */

/* The rectangle a mask takes in the scene, by the origin of its picture, mirrored or not. */
dgx_ge_rect_t dgx_ge_mask_place(const dgx_ge_mask_t *m, int x, int y, bool flip)
{
    const int left = flip ? x - (m->x + m->width - 1) : x + m->x;
    return dgx_ge_rect(left, y + m->y, m->width, m->height);
}

bool dgx_ge_mask_has(const dgx_ge_mask_t *m, dgx_ge_rect_t r, bool flip, int sx, int sy)
{
    const int c = flip ? r.x + r.w - 1 - sx : sx - r.x, row = sy - r.y;
    return m->bits[row * ((m->width + 7) / 8) + c / 8] >> (7 - c % 8) & 1;
}

#define place_of dgx_ge_mask_place
#define has      dgx_ge_mask_has

static int floor_div(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

int dgx_ge_masks_overlap(const dgx_ge_mask_t *a, int ax, int ay, bool a_flip, const dgx_ge_mask_t *b, int bx, int by, bool b_flip, int tile,
                         dgx_ge_area_t *areas)
{
    const dgx_ge_rect_t ra = place_of(a, ax, ay, a_flip), rb = place_of(b, bx, by, b_flip), both = dgx_ge_rect_intersect(ra, rb);
    if (dgx_ge_rect_empty(both)) return 0;
    if (tile < 1) tile = 1;
    /* a grid of tiles of the scene over the overlap, coarser if it would be too wide */
    while ((floor_div(both.x + both.w - 1, tile) - floor_div(both.x, tile) + 1) > GRID || (floor_div(both.y + both.h - 1, tile) - floor_div(both.y, tile) + 1) > GRID)
        tile *= 2;
    const int tx0 = floor_div(both.x, tile), ty0 = floor_div(both.y, tile);
    const int cols = floor_div(both.x + both.w - 1, tile) - tx0 + 1, rows = floor_div(both.y + both.h - 1, tile) - ty0 + 1;
    uint16_t  count[GRID * GRID];
    memset(count, 0, sizeof(count));
    bool any = false;
    for (int sy = both.y; sy < both.y + both.h; ++sy)
        for (int sx = both.x; sx < both.x + both.w; ++sx)
            if (has(a, ra, a_flip, sx, sy) && has(b, rb, b_flip, sx, sy)) {
                ++count[(floor_div(sy, tile) - ty0) * GRID + floor_div(sx, tile) - tx0];
                any = true;
            }
    if (!any) return 0;

    /* neighbouring tiles where they meet make one area */
    uint8_t seen[GRID * GRID], stack[GRID * GRID];
    memset(seen, 0, sizeof(seen));
    int n = 0;
    for (int cell = 0; cell < GRID * GRID; ++cell) {
        if (!count[cell] || seen[cell]) continue;
        /* too many areas: the rest is joined to the last */
        const bool    join  = n == DGX_GE_AREAS_MAX;
        const int     slot  = join ? n - 1 : n++;
        dgx_ge_area_t area  = join ? areas[slot] : (dgx_ge_area_t){0};
        int32_t       sum_x = (int32_t)area.cx * area.points, sum_y = (int32_t)area.cy * area.points;
        int           top   = 0;
        stack[top++]        = (uint8_t)cell;
        seen[cell]          = 1;
        while (top) {
            const int c = stack[--top], cx = c % GRID, cy = c / GRID;
            /* the tile cut to the overlap */
            const dgx_ge_rect_t t = dgx_ge_rect_intersect(dgx_ge_rect((tx0 + cx) * tile, (ty0 + cy) * tile, tile, tile), both);
            area.box              = area.points ? dgx_ge_rect_union(area.box, t) : t;
            area.points += count[c];
            sum_x += (int32_t)(t.x * 2 + t.w - 1) * count[c] / 2;
            sum_y += (int32_t)(t.y * 2 + t.h - 1) * count[c] / 2;
            static const int8_t step[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (int k = 0; k < 4; ++k) {
                const int nx = cx + step[k][0], ny = cy + step[k][1], next = ny * GRID + nx;
                if (nx < 0 || ny < 0 || nx >= cols || ny >= rows || seen[next] || !count[next]) continue;
                seen[next]   = 1;
                stack[top++] = (uint8_t)next;
            }
        }
        area.cx     = (int16_t)(sum_x / area.points);
        area.cy     = (int16_t)(sum_y / area.points);
        areas[slot] = area;
    }
    return n;
}

/* As dgx_ge_bodies_overlap, only the pairs of masks rows lets meet (row by the index of a, a bit by that of b); NULL: all. */
static int overlap_by(const dgx_ge_body_t *a, const dgx_ge_body_t *b, const uint16_t *rows, int tile, dgx_ge_overlap_fn fn, void *ctx)
{
    dgx_ge_mask_t ma[16], mb[16];
    int           na = dgx_ge_pack_masks(a->pack, a->image, ma, 16), nb = dgx_ge_pack_masks(b->pack, b->image, mb, 16), met = 0;
    if (na > 16) na = 16;
    if (nb > 16) nb = 16;
    for (int i = 0; i < na; ++i) {
        if (!(a->active >> ma[i].index & 1)) continue;
        const uint16_t with = b->active & (rows ? rows[ma[i].index] : 0xFFFFu);
        for (int j = 0; j < nb; ++j) {
            if (!(with >> mb[j].index & 1)) continue;
            dgx_ge_area_t areas[DGX_GE_AREAS_MAX];
            const int     n = dgx_ge_masks_overlap(&ma[i], a->x, a->y, a->flip, &mb[j], b->x, b->y, b->flip, tile, areas);
            if (!n) continue;
            ++met;
            if (fn) fn(ma[i].index, mb[j].index, areas, n, ctx);
        }
    }
    return met;
}

int dgx_ge_bodies_overlap(const dgx_ge_body_t *a, const dgx_ge_body_t *b, int tile, dgx_ge_overlap_fn fn, void *ctx)
{
    return overlap_by(a, b, NULL, tile, fn, ctx);
}

/* ---- what a mask touches ---- */

/* The points touching on one side, gathered: their box, the sum of them (for the middle), how many. */
typedef struct {
    int32_t  sx, sy;
    int      x0, y0, x1, y1;
    uint32_t n;
} gather_t;

static void gather(gather_t *g, int x, int y)
{
    if (!g->n) g->x0 = g->x1 = x, g->y0 = g->y1 = y;
    if (x < g->x0) g->x0 = x;
    if (x > g->x1) g->x1 = x;
    if (y < g->y0) g->y0 = y;
    if (y > g->y1) g->y1 = y;
    g->sx += x, g->sy += y, ++g->n;
}

static unsigned gathered(const gather_t *g, dgx_ge_area_t areas[DGX_GE_TOUCH_SIDES])
{
    unsigned bits = 0;
    for (int s = 0; s < DGX_GE_TOUCH_SIDES; ++s) {
        if (!g[s].n) {
            areas[s] = (dgx_ge_area_t){0};
            continue;
        }
        areas[s] = (dgx_ge_area_t){.box    = dgx_ge_rect(g[s].x0, g[s].y0, g[s].x1 - g[s].x0 + 1, g[s].y1 - g[s].y0 + 1),
                                   .cx     = (int16_t)(g[s].sx / (int32_t)g[s].n),
                                   .cy     = (int16_t)(g[s].sy / (int32_t)g[s].n),
                                   .points = (uint16_t)(g[s].n > 0xFFFF ? 0xFFFF : g[s].n)};
        bits |= 1u << s;
    }
    return bits;
}

static inline bool within(dgx_ge_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static bool within_any(const dgx_ge_rect_t *solids, int count, int x, int y)
{
    for (int i = 0; i < count; ++i)
        if (within(solids[i], x, y)) return true;
    return false;
}

unsigned dgx_ge_mask_touch_rect_among(const dgx_ge_mask_t *m, dgx_ge_rect_t place, bool flip, dgx_ge_rect_t solid, int reach, const dgx_ge_rect_t *solids,
                                      int count, dgx_ge_area_t areas[DGX_GE_TOUCH_SIDES])
{
    gather_t g[DGX_GE_TOUCH_SIDES] = {{0}};
    if (reach < 0) reach = 0;
    const dgx_ge_rect_t near = dgx_ge_rect(solid.x - reach, solid.y - reach, solid.w + 2 * reach, solid.h + 2 * reach);
    const dgx_ge_rect_t both = dgx_ge_rect_intersect(place, near);
    for (int y = both.y; y < both.y + both.h; ++y)
        for (int x = both.x; x < both.x + both.w; ++x) {
            if (!has(m, place, flip, x, y)) continue;
            const bool in_x = x >= solid.x && x < solid.x + solid.w, in_y = y >= solid.y && y < solid.y + solid.h;
            if (in_x && in_y) gather(&g[0], x, y);
            else if (!in_x && !in_y) continue;
            else if (within_any(solids, count, x, y)) continue;        /* inside another solid: not beside this one */
            else if (in_x) gather(&g[y < solid.y ? 1 : 2], x, y); /* the solid below the point, or above it */
            else gather(&g[x < solid.x ? 4 : 3], x, y);          /* the solid to the right of the point, or to its left */
        }
    return gathered(g, areas);
}

unsigned dgx_ge_mask_touch_rect(const dgx_ge_mask_t *m, dgx_ge_rect_t place, bool flip, dgx_ge_rect_t solid, int reach, dgx_ge_area_t areas[DGX_GE_TOUCH_SIDES])
{
    return dgx_ge_mask_touch_rect_among(m, place, flip, solid, reach, NULL, 0, areas);
}

unsigned dgx_ge_mask_touch_mask(const dgx_ge_mask_t *a, dgx_ge_rect_t pa, bool fa, const dgx_ge_mask_t *b, dgx_ge_rect_t pb, bool fb, int reach,
                                dgx_ge_area_t areas[DGX_GE_TOUCH_SIDES])
{
    gather_t g[DGX_GE_TOUCH_SIDES] = {{0}};
    if (reach < 0) reach = 0;
    const dgx_ge_rect_t near = dgx_ge_rect(pb.x - reach, pb.y - reach, pb.w + 2 * reach, pb.h + 2 * reach), both = dgx_ge_rect_intersect(pa, near);
    for (int y = both.y; y < both.y + both.h; ++y)
        for (int x = both.x; x < both.x + both.w; ++x) {
            if (!has(a, pa, fa, x, y)) continue;
            if (within(pb, x, y) && has(b, pb, fb, x, y)) {
                gather(&g[0], x, y);
                continue;
            }
            static const int8_t step[4][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}}; /* the solid below, above, on the left, on the right */
            for (int s = 0; s < 4; ++s)
                for (int d = 1; d <= reach; ++d) {
                    const int nx = x + step[s][0] * d, ny = y + step[s][1] * d;
                    if (within(pb, nx, ny) && has(b, pb, fb, nx, ny)) {
                        gather(&g[1 + s], x, y);
                        break;
                    }
                }
        }
    return gathered(g, areas);
}

/* ---- bodies looked at by what has changed ---- */

typedef struct {
    dgx_ge_body_t body;
    dgx_ge_rect_t bounds; /* of its active masks in the scene; empty: it meets nothing */
    uint16_t      owner, side;
    bool          used, changed;
} collider_t;

struct dgx_ge_collisions {
    collider_t    *bodies;
    const struct dgx_ge_actor **actors; /* the actor of each body, NULL none; NULL until one is given */
    uint16_t      *order; /* used bodies by the left of their bounds */
    int            capacity, count, tile;
    dgx_ge_meets_t meets;
};

static void meet_all(dgx_ge_meets_t *m)
{
    for (int i = 0; i < 16; ++i) m->own[i] = 0, m->foreign[i] = 0xFFFFu;
}

dgx_ge_collisions_t *dgx_ge_collisions_create(int capacity, int tile)
{
    if (capacity < 1) return NULL;
    dgx_ge_collisions_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->bodies   = calloc((size_t)capacity, sizeof(collider_t));
    c->order    = calloc((size_t)capacity, sizeof(uint16_t));
    c->capacity = capacity;
    c->tile     = tile;
    meet_all(&c->meets);
    if (!c->bodies || !c->order) dgx_ge_collisions_destroy(&c);
    return c;
}

void dgx_ge_collisions_destroy(dgx_ge_collisions_t **c)
{
    if (!c || !*c) return;
    free((*c)->bodies);
    free((*c)->actors);
    free((*c)->order);
    free(*c);
    *c = NULL;
}

static bool same_body(const dgx_ge_body_t *a, const dgx_ge_body_t *b)
{
    return a->pack == b->pack && a->image == b->image && a->x == b->x && a->y == b->y && a->flip == b->flip && a->active == b->active;
}

static dgx_ge_rect_t bounds_of(const dgx_ge_body_t *b)
{
    dgx_ge_mask_t m[16];
    int           n   = dgx_ge_pack_masks(b->pack, b->image, m, 16);
    dgx_ge_rect_t box = dgx_ge_rect(0, 0, 0, 0);
    if (n > 16) n = 16;
    for (int i = 0; i < n; ++i) {
        if (!(b->active >> m[i].index & 1)) continue;
        const dgx_ge_rect_t r = place_of(&m[i], b->x, b->y, b->flip);
        box                   = dgx_ge_rect_empty(box) ? r : dgx_ge_rect_union(box, r);
    }
    return box;
}

void dgx_ge_collisions_meets(dgx_ge_collisions_t *c, const dgx_ge_meets_t *meets)
{
    if (meets) c->meets = *meets;
    else meet_all(&c->meets);
    for (int i = 0; i < c->capacity; ++i) c->bodies[i].changed = c->bodies[i].used; /* pairs looked at no more or anew */
}

void dgx_ge_collisions_set(dgx_ge_collisions_t *c, int handle, const dgx_ge_body_t *body, uint16_t owner, uint16_t side)
{
    if (handle < 0 || handle >= c->capacity) return;
    collider_t *k = &c->bodies[handle];
    if (k->used && k->owner == owner && k->side == side && same_body(&k->body, body)) return; /* nothing has changed */
    if (!k->used) c->order[c->count++] = (uint16_t)handle;
    k->body    = *body;
    k->owner   = owner;
    k->side    = side;
    k->bounds  = bounds_of(body);
    k->used    = true;
    k->changed = true;
}

void dgx_ge_collisions_remove(dgx_ge_collisions_t *c, int handle)
{
    if (handle < 0 || handle >= c->capacity || !c->bodies[handle].used) return;
    c->bodies[handle].used = false;
    if (c->actors) c->actors[handle] = NULL;
    int kept = 0;
    for (int i = 0; i < c->count; ++i)
        if (c->order[i] != handle) c->order[kept++] = c->order[i];
    c->count = kept;
}

void dgx_ge_collisions_shift(dgx_ge_collisions_t *c, int dx, int dy)
{
    for (int i = 0; i < c->capacity; ++i) {
        collider_t *b = &c->bodies[i];
        if (!b->used) continue;
        b->body.x -= dx, b->body.y -= dy;
        if (!dgx_ge_rect_empty(b->bounds)) b->bounds.x = (int16_t)(b->bounds.x - dx), b->bounds.y = (int16_t)(b->bounds.y - dy);
    }
}

typedef struct {
    dgx_ge_meet_fn fn;
    void          *ctx;
    int            a, b, met;
} pair_t;

static void pair_met(int index_a, int index_b, const dgx_ge_area_t *areas, int count, void *ctx)
{
    pair_t *p = ctx;
    ++p->met;
    if (p->fn) p->fn(p->a, index_a, p->b, index_b, areas, count, p->ctx);
}

int dgx_ge_collisions_look(dgx_ge_collisions_t *c, dgx_ge_meet_fn fn, void *ctx)
{
    /* by the left of the bounds: from one look to the next the order hardly changes, so insertion sorts it at once */
    for (int i = 1; i < c->count; ++i) {
        const uint16_t h = c->order[i];
        int            j = i;
        while (j > 0 && c->bodies[c->order[j - 1]].bounds.x > c->bodies[h].bounds.x) {
            c->order[j] = c->order[j - 1];
            --j;
        }
        c->order[j] = h;
    }
    pair_t pair = {fn, ctx, 0, 0, 0};
    for (int i = 0; i < c->count; ++i) {
        const collider_t *a = &c->bodies[c->order[i]];
        if (dgx_ge_rect_empty(a->bounds)) continue;
        /* the bodies starting before a ends: only they may reach it */
        for (int j = i + 1; j < c->count; ++j) {
            const collider_t *b = &c->bodies[c->order[j]];
            if (b->bounds.x >= a->bounds.x + a->bounds.w) break;
            if (dgx_ge_rect_empty(b->bounds) || !(a->changed || b->changed)) continue;
            if (a->owner && a->owner == b->owner) continue;
            if (!dgx_ge_rect_overlap(a->bounds, b->bounds)) continue;
            pair.a = c->order[i], pair.b = c->order[j];
            overlap_by(&a->body, &b->body, a->side && a->side == b->side ? c->meets.own : c->meets.foreign, c->tile, pair_met, &pair);
        }
    }
    for (int i = 0; i < c->count; ++i) c->bodies[c->order[i]].changed = false;
    return pair.met;
}

bool dgx_ge_collisions_ray(const dgx_ge_collisions_t *c, int x0, int y0, int x1, int y1, int index, uint16_t owner, uint16_t side,
                           dgx_ge_ray_hit_t *hit)
{
    const int     dx = x1 - x0, dy = y1 - y0, steps = (abs(dx) > abs(dy) ? abs(dx) : abs(dy));
    const int     left = x0 < x1 ? x0 : x1, top = y0 < y1 ? y0 : y1;
    dgx_ge_rect_t span = dgx_ge_rect(left, top, abs(dx) + 1, abs(dy) + 1);
    int           best = steps + 1;
    for (int i = 0; i < c->count; ++i) {
        const collider_t *b = &c->bodies[c->order[i]];
        if (dgx_ge_rect_empty(b->bounds) || !dgx_ge_rect_overlap(b->bounds, span)) continue;
        if (owner && b->owner == owner) continue;
        const uint16_t row = index >= 0 && index < 16 ? (side && b->side == side ? c->meets.own[index] : c->meets.foreign[index]) : 0;
        const uint16_t with = b->body.active & row;
        if (!with) continue;
        dgx_ge_mask_t m[16];
        int           n = dgx_ge_pack_masks(b->body.pack, b->body.image, m, 16);
        if (n > 16) n = 16;
        /* along the path, point by point, up to the best found so far */
        for (int k = 0; k <= steps && k < best; ++k) {
            const int px = steps ? x0 + (dx * k + (dx >= 0 ? steps / 2 : -steps / 2)) / steps : x0;
            const int py = steps ? y0 + (dy * k + (dy >= 0 ? steps / 2 : -steps / 2)) / steps : y0;
            for (int j = 0; j < n; ++j) {
                if (!(with >> m[j].index & 1)) continue;
                const dgx_ge_rect_t r = place_of(&m[j], b->body.x, b->body.y, b->body.flip);
                if (px < r.x || py < r.y || px >= r.x + r.w || py >= r.y + r.h || !has(&m[j], r, b->body.flip, px, py)) continue;
                best = k;
                *hit = (dgx_ge_ray_hit_t){.handle = c->order[i], .index = m[j].index, .x = px, .y = py, .along = steps ? (float)k / (float)steps : 0};
                break;
            }
        }
    }
    return best <= steps;
}

int dgx_ge_collisions_touching(const dgx_ge_collisions_t *c, const dgx_ge_body_t *body, uint16_t owner, uint16_t side, int mask, int reach,
                               dgx_ge_touch_fn fn, void *ctx)
{
    dgx_ge_mask_t ma[16];
    int           na = dgx_ge_pack_masks(body->pack, body->image, ma, 16), found = 0;
    if (na > 16) na = 16;
    for (int h = 0; h < c->capacity; ++h) {
        const collider_t *o = &c->bodies[h];
        if (!o->used || (owner && o->owner == owner)) continue;
        const uint16_t *rows = side && o->side == side ? c->meets.own : c->meets.foreign;
        dgx_ge_mask_t   mb[16];
        int             nb = dgx_ge_pack_masks(o->body.pack, o->body.image, mb, 16);
        if (nb > 16) nb = 16;
        for (int i = 0; i < na; ++i) {
            if (!(body->active >> ma[i].index & 1) || (mask >= 0 && ma[i].index != mask)) continue;
            const uint16_t with = o->body.active & rows[ma[i].index];
            for (int j = 0; j < nb; ++j) {
                if (!(with >> mb[j].index & 1)) continue;
                dgx_ge_area_t  areas[DGX_GE_TOUCH_SIDES];
                const unsigned bits = dgx_ge_mask_touch_mask(&ma[i], place_of(&ma[i], body->x, body->y, body->flip), body->flip, &mb[j],
                                                             place_of(&mb[j], o->body.x, o->body.y, o->body.flip), o->body.flip, reach, areas);
                for (int s = 0; s < DGX_GE_TOUCH_SIDES; ++s) {
                    if (!(bits >> s & 1)) continue;
                    ++found;
                    if (fn)
                        fn(&(dgx_ge_touch_t){.mask = ma[i].index, .from = (uint8_t)(1u << s), .area = areas[s], .handle = h, .index = mb[j].index},
                           ctx);
                }
            }
        }
    }
    return found;
}

static void touch_bits_of(const dgx_ge_touch_t *t, void *ctx)
{
    *(unsigned *)ctx |= t->from;
}

unsigned dgx_ge_collisions_touch_sides(const dgx_ge_collisions_t *c, const dgx_ge_body_t *body, uint16_t owner, uint16_t side, int mask, int reach)
{
    unsigned bits = 0;
    dgx_ge_collisions_touching(c, body, owner, side, mask, reach, touch_bits_of, &bits);
    return bits;
}

void dgx_ge_collisions_actor(dgx_ge_collisions_t *c, int handle, const struct dgx_ge_actor *actor)
{
    if (handle < 0 || handle >= c->capacity) return;
    if (!c->actors) {
        if (!actor) return;
        c->actors = calloc((size_t)c->capacity, sizeof(*c->actors));
        if (!c->actors) return;
    }
    c->actors[handle] = actor;
}

const struct dgx_ge_actor *dgx_ge_collisions_actor_of(const dgx_ge_collisions_t *c, int handle)
{
    return c->actors && handle >= 0 && handle < c->capacity ? c->actors[handle] : NULL;
}

int dgx_ge_collisions_nearest(const dgx_ge_collisions_t *c, int x, int y, int radius, uint16_t owner, dgx_ge_near_fn fn, void *ctx, int *dx,
                              int *dy)
{
    int     best = -1;
    int64_t best_d2 = (int64_t)radius * radius;
    for (int i = 0; i < c->count; ++i) {
        const int         h = c->order[i];
        const collider_t *b = &c->bodies[h];
        if (owner && b->owner == owner) continue;
        const int     ox = b->body.x - x, oy = b->body.y - y;
        const int64_t d2 = (int64_t)ox * ox + (int64_t)oy * oy;
        if (d2 > best_d2 || (d2 == best_d2 && best >= 0 && h > best)) continue;
        if (fn && !fn(h, c->actors ? c->actors[h] : NULL, ctx)) continue;
        best = h, best_d2 = d2;
        if (dx) *dx = ox;
        if (dy) *dy = oy;
    }
    return best;
}
