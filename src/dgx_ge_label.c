#include <stdio.h>
#include <string.h>

#include "dgx_ge_label.h"

static void mark(dgx_ge_label_t *l)
{
    dgx_ge_frame_invalidate(l->frame, dgx_ge_rect(l->x, l->y, l->box.width, l->box.height));
}

bool dgx_ge_label_init(dgx_ge_label_t *l, dgx_ge_frame_t *frame, dgx_font_t *font, int x, int y, int width, int height, uint8_t align,
                       uint8_t layer, uint8_t color)
{
    memset(l, 0, sizeof(*l));
    l->frame = frame, l->x = (int16_t)x, l->y = (int16_t)y, l->layer = layer, l->color = color;
    l->style = (dgx_ge_text_style_t){font, 1, 1, 0, align};
    /* as many lines as go into its height: one, mostly */
    const int line = font ? dgx_ge_text_line(font, 1) : 0, lines = line > 0 && height / line > 1 ? height / line : 1;
    if (!font || !dgx_ge_textbox_init(&l->box, width, height, lines)) return false;
    dgx_ge_textbox_set(&l->box, l->text, &l->style, false, 0);
    return true;
}

void dgx_ge_label_free(dgx_ge_label_t *l)
{
    dgx_ge_textbox_free(&l->box);
}

void dgx_ge_label_set(dgx_ge_label_t *l, const char *text)
{
    if (!text) text = "";
    /* as much as it holds, cut between letters (never inside one of several bytes); the same as it has: nothing */
    size_t n = strlen(text);
    if (n > sizeof l->text - 1)
        for (n = sizeof l->text - 1; n > 0 && ((uint8_t)text[n] & 0xC0) == 0x80; --n) {}
    if (strlen(l->text) == n && !memcmp(l->text, text, n)) return;
    memcpy(l->text, text, n), l->text[n] = 0;
    dgx_ge_textbox_set(&l->box, l->text, &l->style, false, 0);
    mark(l);
}

void dgx_ge_label_number(dgx_ge_label_t *l, long number)
{
    char s[24];
    snprintf(s, sizeof s, "%ld", number);
    dgx_ge_label_set(l, s);
}

void dgx_ge_label_color(dgx_ge_label_t *l, uint8_t color)
{
    if (l->color == color) return;
    l->color = color;
    mark(l);
}

void dgx_ge_label_show(dgx_ge_label_t *l, dgx_ge_scene_t *scene)
{
    if (!l->text[0] || !l->box.canvas) return;
    dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &l->box.image, .x = l->x, .y = l->y, .layer = l->layer, .color = l->color});
}
