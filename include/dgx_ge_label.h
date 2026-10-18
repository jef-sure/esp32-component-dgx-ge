#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "dgx_ge_frame.h"
#include "dgx_ge_scene.h"
#include "dgx_ge_text.h"

/*
 * A label: a line of text that stays on the screen (a score, lives, a word
 * of the game: "READY!"), not a line of a talk. A field of text of its own
 * at a place of the frame, in a font, aligned in its field, in a color of the
 * frame. Its text is the game's, set when it changes (a number: as the
 * number); a new text is laid at once, and its place is marked for the frame
 * to draw anew (the picture of the field changes in place: the scene sees
 * the same picture). Shown, it is an actor of the scene in a layer; an empty
 * text shows nothing. Its places are those of the frame: a layer that does
 * not scroll.
 *
 * RAM: the field, a bit a point (an 8-point line 64 points wide: 64 bytes),
 * 4 bytes a line that goes into its height (one, mostly), and the text,
 * DGX_GE_LABEL_TEXT bytes.
 */
#ifndef DGX_GE_LABEL_TEXT
#define DGX_GE_LABEL_TEXT 32
#endif

typedef struct {
    dgx_ge_textbox_t    box;
    dgx_ge_text_style_t style;
    dgx_ge_frame_t     *frame;
    char                text[DGX_GE_LABEL_TEXT];
    int16_t             x, y;  /* the top-left of its field, points of the frame */
    uint8_t             layer; /* of the scene */
    uint8_t             color; /* the frame's index of its ink */
} dgx_ge_label_t;

/* A label of a field so wide and high at x, y, its text in a font (scale 1) aligned (DGX_GE_ALIGN_*); empty. False: no memory. */
bool dgx_ge_label_init(dgx_ge_label_t *label, dgx_ge_frame_t *frame, dgx_font_t *font, int x, int y, int width, int height, uint8_t align,
                       uint8_t layer, uint8_t color);
void dgx_ge_label_free(dgx_ge_label_t *label);

/* Its text (cut to DGX_GE_LABEL_TEXT - 1 bytes); laid and marked only when it is another. */
void dgx_ge_label_set(dgx_ge_label_t *label, const char *text);

/* Its text a number, in decimal. */
void dgx_ge_label_number(dgx_ge_label_t *label, long number);

/* Its ink another color of the frame: marked when it is another. */
void dgx_ge_label_color(dgx_ge_label_t *label, uint8_t color);

/* Into the scene as an actor (nothing when its text is empty). */
void dgx_ge_label_show(dgx_ge_label_t *label, dgx_ge_scene_t *scene);

#ifdef __cplusplus
}
#endif
