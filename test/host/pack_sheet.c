/*
 * A sheet of the pictures of a pack, to look at: pack_sheet <pack> <first> <count> <set of colors> <out.ppm>
 * The pictures are drawn by the blit of the engine, each with its origin marked, in the colors of the set.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dgx_ge_image.h"
#include "dgx_ge_pack.h"
#include "drivers/vscreen.h"

#define SW 640
#define SH 480

int main(int argc, char **argv)
{
    if (argc < 6) return fprintf(stderr, "usage: %s pack first count set out.ppm\n", argv[0]), 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return perror(argv[1]), 1;
    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(size);
    if (fread(data, 1, size, f) != size) return 1;
    fclose(f);
    dgx_ge_pack_t pack;
    if (!dgx_ge_pack_open(&pack, data, size)) return fprintf(stderr, "not a pack\n"), 1;
    const int first = atoi(argv[2]), count = atoi(argv[3]), set = atoi(argv[4]);

    dgx_screen_t   *sheet  = dgx_vscreen_init(SW, SH, 8, DgxScreenRGB);
    uint8_t        *pixels = ((dgx_vscreen_t *)sheet)->v_array;
    dgx_ge_canvas_t canvas = {pixels, SW, {0, 0, SW, SH}};
    memset(pixels, 16, (size_t)SW * SH); /* a gray paper */
    uint8_t lut[256][3] = {{0}};
    const uint8_t (*rgb)[3];
    int n = dgx_ge_pack_set_colors(&pack, set, &rgb);
    uint8_t map[255];
    for (int i = 0; i < n && i < 15; ++i) memcpy(lut[1 + i], rgb[i], 3), map[i] = (uint8_t)(1 + i);
    memcpy(lut[16], (uint8_t[3]){60, 60, 70}, 3);
    memcpy(lut[17], (uint8_t[3]){255, 255, 255}, 3);
    memcpy(lut[18], (uint8_t[3]){255, 0, 255}, 3);

    int x = 4, y = 4, row_h = 0;
    for (int id = first; id < first + count && id < pack.images; ++id) {
        dgx_image_t im;
        dgx_ge_pack_image(&pack, id, &im);
        if (x + im.width + 4 > SW) x = 4, y += row_h + 6, row_h = 0;
        if (y + im.height > SH) break;
        dgx_ge_rect_t box = {(int16_t)x, (int16_t)y, (int16_t)im.width, (int16_t)im.height};
        dgx_ge_canvas_fill(&canvas, box, 0);
        dgx_image_blit(sheet, x + im.origin_x, y + im.origin_y, &im, &(dgx_image_blit_t){.map = map, .color = 17});
        pixels[(y + im.origin_y) * SW + x + im.origin_x] = 18; /* the origin */
        x += im.width + 4;
        if (im.height > row_h) row_h = im.height;
    }
    FILE *o = fopen(argv[5], "wb");
    fprintf(o, "P6 %d %d 255\n", SW, SH);
    for (int i = 0; i < SW * SH; ++i) fwrite(lut[pixels[i]], 1, 3, o);
    fclose(o);
    dgx_screen_destroy(&sheet);
    free(data);
    return 0;
}
