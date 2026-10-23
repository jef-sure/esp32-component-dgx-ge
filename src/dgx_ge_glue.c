#include "dgx_ge_glue.h"

bool dgx_ge_frame_pack_colors(dgx_ge_frame_t *frame, const dgx_ge_pack_t *pack, uint8_t *maps, size_t size, const uint8_t **map_of)
{
    bool   ok = true, room = true;
    size_t at = 0;
    for (int set = 0; set < pack->sets; ++set) {
        const uint8_t (*rgb)[3];
        const int n = dgx_ge_pack_set_colors(pack, set, &rgb);
        room        = room && at + (size_t)n <= size;
        if (!room) {
            map_of[set] = NULL, ok = false;
            continue;
        }
        if (n && !dgx_ge_frame_add_colors(frame, rgb, n, maps + at)) ok = false;
        map_of[set] = maps ? maps + at : NULL;
        at += (size_t)n;
    }
    return ok;
}

bool dgx_ge_item_picture(dgx_ge_item_t *item, const dgx_ge_pack_t *pack, const uint8_t *const *maps, int id, dgx_image_t *image)
{
    if (id < 0 || !dgx_ge_pack_image(pack, id, image)) return false;
    const int set = dgx_ge_pack_image_set(pack, id);
    item->image   = image;
    item->map     = image->format == 1 || !maps || set < 0 ? NULL : maps[set];
    /* one of 1 bit: in a color of its set, as the frame has it */
    if (image->format == 1 && item->color >= 1 && item->color <= 15 && maps && set >= 0 && maps[set]) item->color = maps[set][item->color - 1];
    return true;
}

bool dgx_ge_item_actor(dgx_ge_item_t *item, const dgx_ge_actor_t *actor, const dgx_ge_pack_t *pack, const uint8_t *const *maps, dgx_image_t *image)
{
    int x, y;
    dgx_ge_actor_picture_at(actor, &x, &y);
    item->x     = (int16_t)x;
    item->y     = (int16_t)y;
    item->flags = (uint8_t)((item->flags & ~DGX_GE_BLIT_FLIP_X) | (actor->flip ? DGX_GE_BLIT_FLIP_X : 0));
    item->turn  = actor->turn & 3;
    return dgx_ge_item_picture(item, pack, maps, actor->image, image);
}

bool dgx_ge_scene_show_actor(dgx_ge_scene_t *scene, const dgx_ge_actor_t *actor, int layer, const dgx_ge_pack_t *pack, const uint8_t *const *maps,
                             dgx_image_t *image)
{
    dgx_ge_item_t item = {.layer = (uint8_t)layer};
    return dgx_ge_item_actor(&item, actor, pack, maps, image) && dgx_ge_scene_actor(scene, &item);
}
