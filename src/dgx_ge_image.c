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
    if (!(item->flags & DGX_GE_ITEM_REPEAT)) {
        dgx_image_blit(into, x, y, item->image, &how);
        return;
    }
    /* repeated: from the first picture that reaches into what is drawn, a step its size */
    const int w = item->image->width, h = item->image->height;
    if (w < 1 || h < 1) return;
    const int x0 = x + floor_div(clip.x - x, w) * w, y0 = y + floor_div(clip.y - y, h) * h;
    for (int ty = y0 > y ? y0 : y; ty < clip.y + clip.h; ty += h)
        for (int tx = x0 > x ? x0 : x; tx < clip.x + clip.w; tx += w) dgx_image_blit(into, tx, ty, item->image, &how);
}
