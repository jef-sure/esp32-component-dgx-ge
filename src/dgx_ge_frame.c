#include <stdlib.h>
#include <string.h>

#include "dgx_draw.h"
#include "dgx_ge_frame.h"
#include "dgx_lut.h"
#include "drivers/vscreen.h"

dgx_ge_frame_t *dgx_ge_frame_create(dgx_screen_t *display, int x, int y, int width, int height)
{
    dgx_ge_frame_t *frame = calloc(1, sizeof(*frame));
    if (!frame) return NULL;
    frame->buffer = dgx_vscreen_init(width, height, 8, DgxScreenRGB);
    if (!frame->buffer) {
        free(frame);
        return NULL;
    }
    frame->canvas.pixels = ((dgx_vscreen_t *)frame->buffer)->v_array;
    frame->canvas.stride = width;
    frame->canvas.clip   = dgx_ge_rect(0, 0, width, height);
    memset(frame->canvas.pixels, 0, (size_t)width * height);
    frame->display   = display;
    frame->display_x = x;
    frame->display_y = y;
    frame->scale     = 1;
    dgx_ge_frame_invalidate_all(frame);
    return frame;
}

void dgx_ge_frame_destroy(dgx_ge_frame_t **frame)
{
    if (!*frame) return;
    dgx_screen_destroy(&(*frame)->buffer);
    if ((*frame)->scaled) dgx_screen_destroy(&(*frame)->scaled);
    free((*frame)->overlays);
    free((*frame)->shown_overlays);
    free((*frame)->shown_boxes);
    free((*frame)->between);
    free(*frame);
    *frame = NULL;
}

/* The buffer a scale sends through: a band of rows of the frame, scaled, as many as band bytes hold (a strip of one at least). */
static dgx_screen_t *scaled_buffer(const dgx_ge_frame_t *frame, int scale, uint32_t bytes)
{
    const int w = dgx_ge_frame_width(frame) * scale, h = dgx_ge_frame_height(frame), rows = (int)(bytes / ((uint32_t)w * scale));
    return dgx_vscreen_init(w, (rows < 1 ? 1 : rows > h ? h : rows) * scale, 8, DgxScreenRGB);
}

bool dgx_ge_frame_band(dgx_ge_frame_t *frame, uint32_t bytes)
{
    if (frame->scale > 1) {
        dgx_screen_t *scaled = scaled_buffer(frame, frame->scale, bytes);
        if (!scaled) return false;
        if (frame->scaled) dgx_screen_destroy(&frame->scaled);
        frame->scaled = scaled;
    }
    frame->band = bytes;
    return true;
}

bool dgx_ge_frame_scale(dgx_ge_frame_t *frame, int scale)
{
    if (scale < 1 || scale > 8) return false;
    if (scale == frame->scale) return true;
    dgx_screen_t *scaled = NULL;
    if (scale > 1) {
        /* the band the board asked for; no room for it: a strip */
        scaled = scaled_buffer(frame, scale, frame->band);
        if (!scaled && frame->band) scaled = scaled_buffer(frame, scale, 0);
        if (!scaled) return false;
    }
    if (frame->scaled) dgx_screen_destroy(&frame->scaled);
    frame->scaled = scaled;
    frame->scale  = (uint8_t)scale;
    free(frame->between);
    frame->between = NULL;
    if (frame->smooth && !dgx_ge_frame_smooth(frame, true)) frame->smooth = false;
    dgx_ge_frame_invalidate_all(frame);
    return true;
}

bool dgx_ge_frame_smooth(dgx_ge_frame_t *frame, bool smooth)
{
    free(frame->between);
    frame->between = NULL;
    frame->smooth  = smooth;
    if (smooth && frame->scale == 4) {
        frame->between = malloc((size_t)6 * 2 * (dgx_ge_frame_width(frame) + 2));
        if (!frame->between) return frame->smooth = false;
    }
    dgx_ge_frame_invalidate_all(frame);
    return true;
}

/*
 * Scale2x of one row: every point P with its neighbours above A, left C, right B and below D becomes four; a corner
 * where two equal neighbours meet and the other two differ takes their color (a step of a slanted edge filled in).
 * The row and those above and below it are given whole (width points; beyond its ends a row goes on at its edge);
 * the points x0 .. x0 + w - 1 of it are scaled into the two rows out, 2 * w wide. A loop over plain rows, not a call
 * a point: a whole display of 1280x800 is made by it a frame (at x4, twice).
 */
static inline void scale2x_point(uint8_t P, uint8_t A, uint8_t B, uint8_t C, uint8_t D, uint8_t *top, uint8_t *bottom)
{
    top[0]    = C == A && C != D && A != B ? A : P;
    top[1]    = A == B && A != C && B != D ? B : P;
    bottom[0] = D == C && D != B && C != A ? C : P;
    bottom[1] = B == D && B != A && D != C ? D : P;
}

static void scale2x_row(const uint8_t *up, const uint8_t *row, const uint8_t *down, int width, int x0, int w, uint8_t *top, uint8_t *bottom)
{
    const int last = width - 1;
    /* the points whose neighbours are all inside the row go without a care for its ends; those at the ends, with it */
    const int from = 1 - x0 > 0 ? (1 - x0 < w ? 1 - x0 : w) : 0, to = last - x0 < w ? (last - x0 > from ? last - x0 : from) : w;
    for (int i = 0; i < w; ++i) {
        if (i == from) i = to; /* the middle below */
        if (i >= w) break;
        const int x = x0 + i, at = x < 0 ? 0 : x > last ? last : x;
        const int l = x - 1 < 0 ? 0 : x - 1 > last ? last : x - 1, r = x + 1 < 0 ? 0 : x + 1 > last ? last : x + 1;
        scale2x_point(row[at], up[at], row[r], row[l], down[at], top + 2 * i, bottom + 2 * i);
    }
    for (int i = from; i < to; ++i) {
        const int x = x0 + i;
        scale2x_point(row[x], up[x], row[x + 1], row[x - 1], down[x], top + 2 * i, bottom + 2 * i);
    }
}

/* A row of the frame by its number; above the first the first, below the last the last (the edges go on). */
static const uint8_t *frame_row(const dgx_ge_frame_t *f, int y)
{
    const int h = dgx_ge_frame_height(f);
    y = y < 0 ? 0 : y >= h ? h - 1 : y;
    return f->canvas.pixels + (size_t)y * f->canvas.stride;
}

/*
 * The strip of a row of the frame (sx.., y, w points) scaled smooth into rows: 2 or 4 rows of w * n points. next: the
 * row below the one before, of the same part (its rows scaled by 2 are there already: moved up, one row more made).
 */
static void smooth_row(dgx_ge_frame_t *f, int sx, int y, int w, bool next, uint8_t *rows, size_t stride)
{
    const int fw = dgx_ge_frame_width(f);
    if (f->scale == 2) {
        scale2x_row(frame_row(f, y - 1), frame_row(f, y), frame_row(f, y + 1), fw, sx, w, rows, rows + stride);
        return;
    }
    /* 4: the three rows about y scaled by 2 (six rows, a point wider each side for their neighbours), the middle two scaled again */
    const int bw = 2 * (w + 2); /* the part and a point each side, scaled */
    uint8_t  *b  = f->between;
    if (next) memmove(b, b + (size_t)2 * bw, (size_t)4 * bw);
    for (int r = next ? 2 : 0; r < 3; ++r)
        scale2x_row(frame_row(f, y - 2 + r), frame_row(f, y - 1 + r), frame_row(f, y + r), fw, sx - 1, w + 2, b + (size_t)(2 * r) * bw,
                    b + (size_t)(2 * r + 1) * bw);
    scale2x_row(b + (size_t)1 * bw, b + (size_t)2 * bw, b + (size_t)3 * bw, bw, 2, 2 * w, rows, rows + stride);
    scale2x_row(b + (size_t)2 * bw, b + (size_t)3 * bw, b + (size_t)4 * bw, bw, 2, 2 * w, rows + 2 * stride, rows + 3 * stride);
}

/* The box of an overlay on the display: its clip for a fill or a repeated picture, else its picture inside its clip. */
static dgx_ge_rect_t overlay_box(const dgx_ge_item_t *item)
{
    const bool    fill = item->flags & DGX_GE_ITEM_FILL, over_clip = fill || (item->flags & DGX_GE_ITEM_REPEAT);
    if (!item->image && !fill) return dgx_ge_rect(0, 0, 0, 0);
    dgx_ge_rect_t box = over_clip ? item->clip : dgx_ge_image_box(item->image, item->x, item->y, item->flags & DGX_GE_BLIT_FLIP_X);
    return dgx_ge_rect_empty(item->clip) ? box : dgx_ge_rect_intersect(box, item->clip);
}

static int floor_div(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

void dgx_ge_frame_invalidate_pixels(dgx_ge_frame_t *f, dgx_ge_rect_t d)
{
    if (dgx_ge_rect_empty(d)) return;
    const int n = f->scale, h = dgx_ge_frame_height(f);
    const int x0 = floor_div(d.x - f->display_x, n), x1 = floor_div(d.x + d.w - 1 - f->display_x, n);
    int       y0 = floor_div(d.y - f->display_y, n), y1 = floor_div(d.y + d.h - 1 - f->display_y, n);
    if (f->upside) { /* the rows the other way round: as the display shows them */
        const int top = f->orient ? f->display->height - f->display_y - h * n : f->display_y;
        y0 = floor_div(d.y - top, n), y1 = floor_div(d.y + d.h - 1 - top, n);
        if (!f->orient) { const int a = h - 1 - y1, b = h - 1 - y0; y0 = a, y1 = b; }
    }
    dgx_ge_frame_invalidate(f, dgx_ge_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1));
}

bool dgx_ge_frame_scroll(dgx_ge_frame_t *f, int dx, int dy)
{
    const int w = dgx_ge_frame_width(f), h = dgx_ge_frame_height(f), n = f->scale;
    if (!dx && !dy) return true;
    if (dx <= -w || dx >= w || dy <= -h || dy >= h || f->upside || f->wiping) {
        dgx_ge_frame_invalidate_all(f);
        return false;
    }
    /* the pixels of the frame moved: the rows in the order that does not tread on what is still to move */
    const int ax = dx < 0 ? -dx : dx;
    for (int i = 0; i < h; ++i) {
        const int y = dy > 0 ? h - 1 - i : i, from = y - dy;
        if (from < 0 || from >= h) continue;
        memmove(f->canvas.pixels + (size_t)y * f->canvas.stride + (dx > 0 ? dx : 0), f->canvas.pixels + (size_t)from * f->canvas.stride + (dx < 0 ? -dx : 0),
                (size_t)(w - ax));
    }
    /* what was marked goes with it; the strips that came in are new */
    int kept = 0;
    for (int i = 0; i < f->dirty_count; ++i) {
        const dgx_ge_rect_t r = dgx_ge_rect_intersect(dgx_ge_rect(f->dirty[i].x + dx, f->dirty[i].y + dy, f->dirty[i].w, f->dirty[i].h), f->canvas.clip);
        if (!dgx_ge_rect_empty(r)) f->dirty[kept++] = r;
    }
    f->dirty_count = kept;
    const bool moved = dgx_scroll_no_fill(f->display, f->display_x, f->display_y, w * n, h * n, dx * n, dy * n) == ESP_OK;
    if (!moved) {
        dgx_ge_frame_invalidate_all(f);
        return false;
    }
    if (dx) dgx_ge_frame_invalidate(f, dgx_ge_rect(dx > 0 ? 0 : w + dx, 0, ax, h));
    if (dy) dgx_ge_frame_invalidate(f, dgx_ge_rect(0, dy > 0 ? 0 : h + dy, w, dy < 0 ? -dy : dy));
    for (int i = 0; i < f->shown_overlay_count; ++i) dgx_ge_frame_invalidate_pixels(f, f->shown_boxes[i]); /* the display under them moved */
    return true;
}

void dgx_ge_frame_overlays_begin(dgx_ge_frame_t *f)
{
    f->overlay_count = 0;
}

bool dgx_ge_frame_overlay(dgx_ge_frame_t *f, const dgx_ge_item_t *item)
{
    if (!f->overlays) {
        f->overlays       = calloc(DGX_GE_FRAME_OVERLAYS, sizeof(dgx_ge_item_t));
        f->shown_overlays = calloc(DGX_GE_FRAME_OVERLAYS, sizeof(dgx_ge_item_t));
        f->shown_boxes    = calloc(DGX_GE_FRAME_OVERLAYS, sizeof(dgx_ge_rect_t));
        if (!f->overlays || !f->shown_overlays || !f->shown_boxes) {
            /* all or none: the next one tries again */
            free(f->overlays), free(f->shown_overlays), free(f->shown_boxes);
            f->overlays = f->shown_overlays = NULL, f->shown_boxes = NULL;
            return false;
        }
        f->overlay_capacity = DGX_GE_FRAME_OVERLAYS;
    }
    if (f->scale == 1 && !f->scaled) {
        /* overlays go into the strip: one row of it at the scale of 1 */
        f->scaled = dgx_vscreen_init(dgx_ge_frame_width(f), 1, 8, DgxScreenRGB);
        if (!f->scaled) return false;
    }
    if (f->overlay_count >= f->overlay_capacity) return false;
    f->overlays[f->overlay_count++] = *item;
    return true;
}

void dgx_ge_frame_overlays_end(dgx_ge_frame_t *f)
{
    if (!f->overlays) return;
    const int both = f->overlay_count > f->shown_overlay_count ? f->overlay_count : f->shown_overlay_count;
    for (int i = 0; i < both; ++i) {
        const bool          was = i < f->shown_overlay_count, is = i < f->overlay_count;
        const dgx_ge_rect_t box = is ? overlay_box(&f->overlays[i]) : dgx_ge_rect(0, 0, 0, 0);
        /* the same item over the same box: as it is (what changed inside it, its owner marks); the box shown is kept, not worked out again from a picture that may have changed since */
        if (was && is && memcmp(&f->shown_overlays[i], &f->overlays[i], sizeof(dgx_ge_item_t)) == 0 && dgx_ge_rect_equal(box, f->shown_boxes[i])) continue;
        if (was) dgx_ge_frame_invalidate_pixels(f, f->shown_boxes[i]);
        if (is) dgx_ge_frame_invalidate_pixels(f, box), f->shown_boxes[i] = box;
    }
    memcpy(f->shown_overlays, f->overlays, (size_t)f->overlay_count * sizeof(dgx_ge_item_t));
    f->shown_overlay_count = f->overlay_count;
}

/* The overlays over a strip of the display (dx, dy, w, h pixels), drawn into the strip's rows. */
static void overlay_strip(dgx_ge_frame_t *f, int dx, int dy, int w, int h)
{
    const dgx_ge_rect_t strip = dgx_ge_rect(dx, dy, w, h);
    for (int i = 0; i < f->overlay_count; ++i) {
        const dgx_ge_item_t *item = &f->overlays[i];
        const dgx_ge_rect_t  box  = overlay_box(item);
        if (!dgx_ge_rect_overlap(box, strip)) continue;
        dgx_ge_rect_t clip = dgx_ge_rect_empty(item->clip) ? strip : dgx_ge_rect_intersect(item->clip, strip);
        clip.x -= dx, clip.y -= dy;
        dgx_ge_item_draw(f->scaled, item, item->x - dx, item->y - dy, dgx_ge_rect(box.x - dx, box.y - dy, box.w, box.h), clip);
    }
}

/* Whether an overlay lies on a part of the display. */
static bool overlaid(const dgx_ge_frame_t *f, dgx_ge_rect_t part)
{
    for (int i = 0; i < f->overlay_count; ++i)
        if (dgx_ge_rect_overlap(overlay_box(&f->overlays[i]), part)) return true;
    return false;
}

/*
 * A part of the frame to the display at a pixel of it, through the palette; at a scale, every point a square of
 * pixels. Blocks with nothing over them: the display repeats the points itself (DGX_LUT_SCALE: through the palette,
 * in its own order of pixels). Else a band of rows at a time: each row of points n times over, n times down (or
 * smoothed), the overlays drawn into the band where they lie, the band sent at once. Without a scale and overlays,
 * straight from the frame.
 */
static void send(dgx_ge_frame_t *f, int sx, int sy, int w, int h, int dx, int dy)
{
    const int n = f->scale;
    if ((n == 1 && !f->overlay_count) || !f->scaled) {
        dgx_vscreen8_region_to_screen16(f->display, dx, dy, f->buffer, sx, sy, w, h, f->lut, DGX_LUT_EXACT);
        return;
    }
    /* nothing over the part: DGX repeats the points itself, smoothed or not (the same Scale2x, on the whole frame's indices) */
    if (n > 1 && !overlaid(f, dgx_ge_rect(dx, dy, w * n, h * n))) {
        dgx_vscreen8_region_to_screen16(f->display, dx, dy, f->buffer, sx, sy, w, h, f->lut,
                                        DGX_LUT_EXACT | DGX_LUT_SCALE(n) | (f->smooth ? DGX_LUT_SMOOTH : 0));
        return;
    }
    uint8_t     *rows   = ((dgx_vscreen_t *)f->scaled)->v_array;
    const size_t stride = (size_t)f->scaled->width;
    const int    band   = f->scaled->height / n; /* rows of the frame a band */
    for (int y0 = 0; y0 < h; y0 += band) {
        const int k = h - y0 < band ? h - y0 : band;
        for (int y = y0; y < y0 + k; ++y) {
            const uint8_t *from  = f->canvas.pixels + (size_t)(sy + y) * f->canvas.stride + sx;
            uint8_t       *strip = rows + (size_t)(y - y0) * n * stride, *to = strip;
            if (f->smooth && (n == 2 || n == 4)) {
                smooth_row(f, sx, sy + y, w, y > 0, strip, stride);
            } else {
                if (n == 1) memcpy(to, from, (size_t)w);
                else
                    for (int x = 0; x < w; ++x)
                        for (int j = 0; j < n; ++j) *to++ = from[x];
                for (int j = 1; j < n; ++j) memcpy(strip + j * stride, strip, (size_t)w * n);
            }
        }
        if (f->overlay_count) overlay_strip(f, dx, dy + y0 * n, w * n, k * n);
        dgx_vscreen8_region_to_screen16(f->display, dx, dy + y0 * n, f->scaled, 0, 0, w * n, k * n, f->lut, DGX_LUT_EXACT);
    }
}

void dgx_ge_frame_set_color(dgx_ge_frame_t *frame, uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    dgx_ge_frame_set_palette(frame, index, 1, (const uint8_t[][3]){{r, g, b}});
}

/* The colors first.. of the frame into the palette of the display, with the tint of the frame. */
static void to_lut(dgx_ge_frame_t *frame, int first, int count)
{
    if (count <= 0) return;
    if (frame->tint) dgx_lut_mix_color(frame->lut, first, count, (const uint8_t (*)[3])frame->rgb[first], frame->tint_rgb, frame->tint);
    else dgx_lut_from_rgb(frame->lut, first, count, (const uint8_t (*)[3])frame->rgb[first]);
}

void dgx_ge_frame_set_palette(dgx_ge_frame_t *frame, int first, int count, const uint8_t (*rgb)[3])
{
    if (first < 0 || first > 255) return;
    if (count > 256 - first) count = 256 - first;
    memcpy(frame->rgb[first], rgb, (size_t)count * 3);
    to_lut(frame, first, count);
    dgx_ge_frame_invalidate_all(frame);
}

void dgx_ge_frame_tint(dgx_ge_frame_t *frame, uint32_t rgb, uint8_t amount)
{
    if (frame->tint == amount && (!amount || frame->tint_rgb == rgb)) return;
    frame->tint_rgb = rgb;
    frame->tint     = amount;
    to_lut(frame, 0, 256);
    dgx_ge_frame_invalidate_all(frame);
}

/* The index of the color of the frame nearest to rgb (0 when it has none). */
static uint8_t nearest(const dgx_ge_frame_t *frame, const uint8_t rgb[3])
{
    int best = 0, least = 0x7FFFFFFF;
    for (int at = 1; at <= frame->colors_used; ++at) {
        const int dr = frame->rgb[at][0] - rgb[0], dg = frame->rgb[at][1] - rgb[1], db = frame->rgb[at][2] - rgb[2];
        const int d = dr * dr + dg * dg + db * db;
        if (d < least) least = d, best = at;
    }
    return (uint8_t)best;
}

bool dgx_ge_frame_add_colors(dgx_ge_frame_t *frame, const uint8_t (*rgb)[3], int count, uint8_t *map)
{
    int used = frame->colors_used;
    for (int i = 0; i < count; ++i) {
        int at = 1;
        while (at <= used && memcmp(frame->rgb[at], rgb[i], 3)) ++at;
        if (at > used) {
            if (used == 255) {
                /* no room: what was taken here is given back (used is not stored), every color the nearest the frame has */
                for (int k = 0; k < count; ++k) map[k] = nearest(frame, rgb[k]);
                return false;
            }
            memcpy(frame->rgb[at], rgb[i], 3);
            used = at;
        }
        map[i] = (uint8_t)at;
    }
    to_lut(frame, frame->colors_used + 1, used - frame->colors_used);
    frame->colors_used = (uint16_t)used;
    return true;
}

void dgx_ge_frame_forget_colors(dgx_ge_frame_t *frame)
{
    frame->colors_used = 0;
}

void dgx_ge_frame_invalidate_all(dgx_ge_frame_t *frame)
{
    frame->dirty[0]    = frame->canvas.clip;
    frame->dirty_count = 1;
}

/* What it costs to send two rectangles as one: the pixels sent besides theirs. */
static int merge_waste(dgx_ge_rect_t a, dgx_ge_rect_t b)
{
    return dgx_ge_rect_area(dgx_ge_rect_union(a, b)) - dgx_ge_rect_area(a) - dgx_ge_rect_area(b) +
           dgx_ge_rect_area(dgx_ge_rect_intersect(a, b));
}


void dgx_ge_frame_invalidate(dgx_ge_frame_t *frame, dgx_ge_rect_t r)
{
    r = dgx_ge_rect_intersect(r, frame->canvas.clip);
    if (dgx_ge_rect_empty(r)) return;
    /* merge with every rectangle it overlaps or nearly does, again and again while it grows */
    for (int i = 0; i < frame->dirty_count;) {
        if (merge_waste(r, frame->dirty[i]) <= DGX_GE_FRAME_WINDOW_PIXELS) {
            r                  = dgx_ge_rect_union(r, frame->dirty[i]);
            frame->dirty[i]    = frame->dirty[--frame->dirty_count];
            i                  = 0;
        } else {
            ++i;
        }
    }
    if (frame->dirty_count < DGX_GE_FRAME_MAX_DIRTY) {
        frame->dirty[frame->dirty_count++] = r;
        return;
    }
    /* no place left: it goes together with the one it costs least with */
    int best = 0, best_waste = merge_waste(r, frame->dirty[0]);
    for (int i = 1; i < frame->dirty_count; ++i) {
        int waste = merge_waste(r, frame->dirty[i]);
        if (waste < best_waste) best = i, best_waste = waste;
    }
    r = dgx_ge_rect_union(r, frame->dirty[best]);
    frame->dirty[best] = frame->dirty[--frame->dirty_count];
    dgx_ge_frame_invalidate(frame, r);
}

void dgx_ge_frame_upside_down(dgx_ge_frame_t *frame, bool upside)
{
    if (frame->upside == upside) return;
    frame->upside = upside;
    if (frame->orient && !frame->orient(frame->display, upside)) frame->orient = NULL; /* it could not: rows one by one */
    dgx_ge_frame_invalidate_all(frame);
}

void dgx_ge_frame_wipe(dgx_ge_frame_t *frame, int x)
{
    frame->wiping = x >= 0;
    frame->wipe_x = (int16_t)(x < 0 ? 0 : x > dgx_ge_frame_width(frame) ? dgx_ge_frame_width(frame) : x);
}

void dgx_ge_frame_present(dgx_ge_frame_t *frame)
{
    if (!frame->dirty_count) return;
    dgx_screen_progress_up(frame->display);
    const int h = dgx_ge_frame_height(frame);
    int       waiting = 0; /* wiping: the parts right of the wipe, kept at the front of the list */
    for (int i = 0; i < frame->dirty_count; ++i) {
        dgx_ge_rect_t r = frame->dirty[i];
        if (frame->wiping && r.x + r.w > frame->wipe_x) {
            const int left = frame->wipe_x - r.x;
            frame->dirty[waiting++] = left > 0 ? dgx_ge_rect(frame->wipe_x, r.y, r.w - left, r.h) : r;
            if (left <= 0) continue;
            r.w = left;
        }
        const int n = frame->scale;
        if (!frame->upside) {
            send(frame, r.x, r.y, r.w, r.h, frame->display_x + r.x * n, frame->display_y + r.y * n);
        } else if (frame->orient) {
            /* the display turns its rows itself (about its own height): the rows go where they come out mirrored in the frame */
            const int top = frame->display->height - frame->display_y - h * n;
            send(frame, r.x, r.y, r.w, r.h, frame->display_x + r.x * n, top + r.y * n);
        } else {
            for (int y = r.y; y < r.y + r.h; ++y) /* a row at a time, to its mirrored place */
                send(frame, r.x, y, r.w, 1, frame->display_x + r.x * n, frame->display_y + (h - 1 - y) * n);
        }
        frame->pixels_sent += (uint32_t)r.w * r.h * n * n;
    }
    frame->dirty_count = waiting;
    dgx_screen_progress_down(frame->display);
}
