/* The engine's layout of a text, for test_text_layout.py: width height scale_max scale_min important text [droid] -> scale, lines, the canvas. */
#include <stdio.h>
#include <stdlib.h>

#include "dgx_ge_text.h"
#include "fonts/DroidSansRegular9.h"
#include "fonts/TerminusTTFMedium12.h"
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 7) return 2;
    dgx_ge_text_style_t style = {argc > 7 && !strcmp(argv[7], "droid") ? DroidSansRegular9() : TerminusTTFMedium12(), (uint8_t)atoi(argv[3]), (uint8_t)atoi(argv[4]), 0, DGX_GE_ALIGN_LEFT};
    dgx_ge_textbox_t    box;
    if (!dgx_ge_textbox_init(&box, atoi(argv[1]), atoi(argv[2]), 0)) return 1;
    dgx_ge_textbox_set(&box, argv[6], &style, atoi(argv[5]) != 0, 0);
    printf("scale %d shown %d cut %d scrolling %d lines", box.scale, box.shown, box.cut, box.scrolling);
    for (int i = 0; i < box.lines; ++i) printf(" %d-%d", box.starts[i], box.ends[i]);
    printf("\ncanvas ");
    for (int i = 0; i < box.width / 8 * box.height; ++i) printf("%02x", box.image.data[i]);
    printf("\n");
    dgx_ge_textbox_free(&box);
    return 0;
}
