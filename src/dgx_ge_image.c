#include <string.h>

#include "dgx_ge_image.h"
#include "drivers/vscreen.h"

void dgx_ge_canvas_fill(const dgx_ge_canvas_t *canvas, dgx_ge_rect_t r, uint8_t color)
{
    r = dgx_ge_rect_intersect(r, canvas->clip);
    if (dgx_ge_rect_empty(r)) return;
    uint8_t *row = canvas->pixels + r.y * canvas->stride + r.x;
    for (int i = 0; i < r.h; ++i, row += canvas->stride) memset(row, color, (size_t)r.w);
}

/* a / b rounded down, b > 0 */
static int floor_div(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/* A pixel of a picture: its value, 0 transparent (rows of whole bytes, the leftmost pixel in the high bits). */
static unsigned pixel_of(const dgx_image_t *im, int px, int py)
{
    const uint8_t *row = im->data + (size_t)py * (((size_t)im->width * im->format + 7) / 8);
    if (im->format == 1) return (row[px >> 3] >> (7 - (px & 7))) & 1u;
    if (im->format == 4) return (row[px >> 1] >> (px & 1 ? 0 : 4)) & 15u;
    return row[px];
}

/*
 * A picture with its origin at x, y, each of its pixels a square of n, into a linear 8-bit screen, inside clip: as
 * dgx_image_blit draws it (1 bit: the color; 4, 8 bits: the map, or the value; a silhouette: the color; mirrored round
 * its origin), only larger. DGX does not scale pictures.
 */
static void blit_scaled(dgx_screen_t *into, int x, int y, const dgx_image_t *im, const dgx_ge_item_t *item, dgx_ge_rect_t clip, int n)
{
    if (!im->data || (im->format != 1 && im->format != 4 && im->format != 8)) return;
    const bool          flip = item->flags & DGX_GE_BLIT_FLIP_X, silhouette = item->flags & DGX_GE_BLIT_SILHOUETTE;
    const dgx_ge_rect_t box  = dgx_ge_image_box_scaled(im, x, y, flip, n);
    const dgx_ge_rect_t r    = dgx_ge_rect_intersect(dgx_ge_rect_intersect(box, clip), dgx_ge_rect(0, 0, into->width, into->height));
    uint8_t            *out  = ((dgx_vscreen_t *)into)->v_array;
    for (int dy = r.y; dy < r.y + r.h; ++dy) {
        const int py  = (dy - box.y) / n;
        uint8_t  *row = out + (size_t)dy * into->width;
        for (int dx = r.x; dx < r.x + r.w; ++dx) {
            const int      bx = (dx - box.x) / n;
            const unsigned v  = pixel_of(im, flip ? im->width - 1 - bx : bx, py);
            if (v) row[dx] = silhouette || im->format == 1 ? item->color : item->map ? item->map[v - 1] : (uint8_t)v;
        }
    }
}

void dgx_ge_item_draw(dgx_screen_t *into, const dgx_ge_item_t *item, int x, int y, dgx_ge_rect_t box, dgx_ge_rect_t clip)
{
    if (dgx_ge_rect_empty(clip)) return;
    if (item->flags & (DGX_GE_ITEM_FILL | DGX_GE_ITEM_CARD)) {
        const dgx_ge_canvas_t canvas = {((dgx_vscreen_t *)into)->v_array, into->width, clip};
        dgx_ge_canvas_fill(&canvas, box, item->color);
    }
    if (!item->image || (item->flags & DGX_GE_ITEM_FILL)) return;
    const dgx_image_blit_t how = {
        .flags       = DGX_IMAGE_CLIP | (item->flags & DGX_GE_BLIT_SILHOUETTE ? DGX_IMAGE_SILHOUETTE : 0u),
        .map         = item->map,
        .color       = item->color,
        .orientation = item->flags & DGX_GE_BLIT_FLIP_X ? DgxOutputMirrorX : DgxOutputNormal,
        .clip_x      = clip.x,
        .clip_y      = clip.y,
        .clip_w      = clip.w,
        .clip_h      = clip.h,
    };
    const int n = item->scale > 1 ? item->scale : 1; /* larger: drawn by the engine (DGX does not scale pictures) */
    if (!(item->flags & DGX_GE_ITEM_REPEAT)) {
        if (n > 1) blit_scaled(into, x, y, item->image, item, clip, n);
        else dgx_image_blit(into, x, y, item->image, &how);
        return;
    }
    /* repeated: from the first picture that reaches into what is drawn, a step its size */
    const int w = item->image->width * n, h = item->image->height * n;
    if (w < 1 || h < 1) return;
    const int x0 = x + floor_div(clip.x - x, w) * w, y0 = y + floor_div(clip.y - y, h) * h;
    for (int ty = y0 > y ? y0 : y; ty < clip.y + clip.h; ty += h)
        for (int tx = x0 > x ? x0 : x; tx < clip.x + clip.w; tx += w) {
            if (n > 1) blit_scaled(into, tx, ty, item->image, item, clip, n);
            else dgx_image_blit(into, tx, ty, item->image, &how);
        }
}
