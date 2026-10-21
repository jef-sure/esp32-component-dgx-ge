#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "dgx_font.h"
#include "dgx_ge_dialog.h"
#include "dgx_ge_frame.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_scene.h"
#include "dgx_ge_text.h"
#include "dgx_ge_widgets.h"

/*
 * How a talk is shown: by a style, data of the pack (the blob "STYLES":
 * "DGST", version u16, count u16, then the styles; made by the packer from
 * the styles the editor makes; a style a dialog names and nobody made is the
 * default one). A style is a place on the screen, a
 * frame (a fill with a border, or a picture of nine pieces: its corners as
 * they are, its edges and middle laid again and again over the fill; the
 * packer cut them, nine pictures of the pack from picture on, row after row;
 * drawn so only with the pictures given, else as a fill), the field of text in
 * it, a font of the game's (by its number in the game's table of fonts), its
 * scales, the colors, the bar under the answer chosen, how long a line waits
 * when the dialog gives it no time, and whether lines are important unless
 * the dialog says.
 *
 * The view keeps two fields of text (the line, the answers) and lays them in
 * the style of the talk as it goes; it gives the talk its time (seen whole:
 * an important line scrolled to its end) and puts the frame, the text and
 * the bar into a layer of the scene.
 *
 * A style, 48 bytes: frame u8 (0 none, 1 fill, 2 picture), appear u8 (0 at
 * once, 1 letter by letter; bit 7: a soft pen — with a handwritten font the
 * line and the answers in a box of amounts of ink, 8 bits a point, drawn in
 * 15 shades from the fill to the text; a font of pixels stays 1 bit),
 * picture u16,
 * insets l t r b (u8 each), x y w h (i16 each; x, y below 0: from the right,
 * the bottom), padding l t r b, font u8, scale max u8, scale min u8,
 * important u8, scroll ms u16, wait ms u16, fill rgb, border width u8,
 * border rgb, letter ms u8, text rgb, portrait u8 (0 none, 1 left, 2 right),
 * bar rgb, portrait width u8, name rgb, show name u8 (bit 0: the name of
 * the one speaking above the line, in its color; bits 1-2: each line of the
 * text in its field: 0 at the left, 1 in the middle, 2 at the right). A portrait (the one speaking or
 * choosing: its role's) stands in the top corner of its side inside the
 * padding; the fields of text are narrower by its width and the left padding.
 */
#define DGX_GE_STYLE_BYTES    48
#define DGX_GE_STYLES_MAGIC   0x54534744u /* "DGST" */
#define DGX_GE_STYLES_VERSION 1u

#ifndef DGX_GE_TALK_VIEW_CONTROLS
#define DGX_GE_TALK_VIEW_CONTROLS 6 /* controls shown at once, a picture each: lines of answers seen */
#endif

typedef struct {
    uint8_t  frame;
    uint16_t picture;
    uint8_t  inset[4]; /* l t r b */
    int16_t  x, y, w, h;
    uint8_t  pad[4];
    uint8_t  font, scale_max, scale_min;
    bool     important;
    uint16_t scroll_ms, wait_ms;
    uint8_t  fill[3], border_width, border[3], text[3], bar[3], name[3];
    uint8_t  appear, letter_ms; /* 1: letter by letter, a letter every letter_ms */
    bool     soft;              /* a soft pen: the text in 8 bits a point */
    bool     show_name;
    uint8_t  align;                /* DGX_GE_ALIGN_*: each line of the text in its field */
    uint8_t  portrait, portrait_w; /* 0 none, 1 left, 2 right; the room for it */
} dgx_ge_style_t;

/* The default style: a dark box with a light border along the bottom of the screen, the first font of the game. */
void dgx_ge_style_default(dgx_ge_style_t *style);

/*
 * A style by its number (the packer's: <PREFIX>_STYLE_<name>) in the blob of
 * styles of a pack (<PREFIX>_BLOB_STYLES); the default one when there is none
 * such, or the blob is not one of styles (NULL: none at all).
 */
void dgx_ge_style_of(const uint8_t *styles, size_t size, int number, dgx_ge_style_t *style);

typedef struct {
    const uint8_t       *styles; /* the blob of styles of the pack, NULL none: the default */
    size_t               styles_size;
    dgx_ge_frame_t      *frame; /* its colors: those of a style are added to it */
    dgx_font_t *const   *fonts; /* the game's fonts, by number */
    int                  font_count;
    dgx_ge_style_t       style;
    int                  style_number; /* -1 none yet */
    uint8_t              colors[5];    /* fill, border, text, bar, name: indices of the frame */
    dgx_ge_text_style_t  text_style, answer_style;
    dgx_ge_textbox_t     line, answers, name; /* the line said, the answers (on the canvas of the line), the name of the one saying it */
    char                 said[96];            /* a line of a number as it is shown (the others are read where they lie) */
    int16_t              said_number;         /* its number: another (the same line said again) is laid anew */
    uint8_t              ink[DGX_GE_TEXT_INK]; /* a soft pen: the amounts of ink 1.. as indices of the frame */
    int                  name_string;          /* the name shown, -1 none */
    /*
     * What answers to a pointer (dgx_ge_widgets.h): the line as a button (a tap goes on), the answers as a list
     * (a tap picks and takes), a control of an answer as a button over its picture (a tap works it); hit only, the
     * view draws as it does. The talk and the moment, for the callbacks while the pointer is told.
     */
    dgx_ge_widgets_t     widgets;
    dgx_ge_widget_t      widget_store[2 * DGX_GE_TALK_VIEW_CONTROLS + 3]; /* the line, the list, a control each and a row to swipe a slider */
    dgx_ge_talk_t       *talk;
    uint32_t             now_ms;
    bool                 pixels;               /* laid in pixels of the display (its layer is one in pixels): the frame's scale its unit */
    const bool          *fonts_made;           /* by font: made for the display's scale (drawn as it is, not times the unit); NULL: none */
    uint8_t              unit;                 /* pixels of the display a point of the style, as the style was taken */
    dgx_ge_rect_t        box;          /* the frame on the screen */
    const dgx_ge_dialog_t *dialog;     /* the talk as shown: its dialog, state, step, how many answers */
    uint8_t              state;
    uint16_t             at;
    uint8_t              shown;
    uint8_t              pick;
    /* the pictures of the controls of the answers seen (by their line, -1 none); the pack and the maps of colors they are drawn by */
    const dgx_ge_pack_t  *pack;
    const uint8_t *const *maps;
    int16_t               control_ids[DGX_GE_TALK_VIEW_CONTROLS];
    dgx_image_t           control_images[DGX_GE_TALK_VIEW_CONTROLS];
    uint8_t               changes; /* the talk's, when they were taken */
    dgx_image_t          *frame_pieces; /* a frame of a picture: its nine pictures (taken with the style), NULL none */
    int16_t               face_id;      /* the portrait shown, -1 none */
    dgx_image_t           face;
} dgx_ge_talk_view_t;

/* A view of the styles of a pack (its blob STYLES, NULL none) drawing into a frame, with the game's fonts. */
void dgx_ge_talk_view_init(dgx_ge_talk_view_t *view, const uint8_t *styles, size_t styles_size, dgx_ge_frame_t *frame, dgx_font_t *const *fonts,
                           int font_count);
void dgx_ge_talk_view_free(dgx_ge_talk_view_t *view);

/* The pictures of controls: the pack and, by set of the pack, the maps of its colors into the frame (as a grid has them); none: controls are not drawn. */
void dgx_ge_talk_view_pictures(dgx_ge_talk_view_t *view, const dgx_ge_pack_t *pack, const uint8_t *const *maps);

/*
 * Shown in pixels of the display: the layer it is shown on is one in pixels (dgx_ge_scene_layer_pixels), and the
 * style is laid with the frame's scale as its unit (a point of the style so many pixels), the text drawn at the
 * display's own resolution: a soft pen as smooth as the display; a font of pixels as large as the style says in
 * points, times the unit, unless that font was made for the display's scale already (fonts_made, a flag a font of
 * the game's table: DGX made it at points x scale from the game's requests, dgx_ge_fonts) — then as it is. Pictures (a frame of nine pictures, a
 * portrait, controls) are not scaled: a frame of pictures is drawn as a border instead. Taken anew when the frame's
 * scale changes.
 */
void dgx_ge_talk_view_pixels(dgx_ge_talk_view_t *view, bool pixels, const bool *fonts_made);

/*
 * The talk moved on to now as it is shown: its line or answers laid anew
 * when they change (strings in the language of the pack), a long important
 * line scrolled, the talk given its time. False when the view could not lay
 * it (no memory).
 */
bool dgx_ge_talk_view_play(dgx_ge_talk_view_t *view, dgx_ge_talk_t *talk, const dgx_ge_strings_t *strings, uint32_t now_ms);

/*
 * An event of the interface for the talk as it is shown: a button pressed
 * while a line appears letter by letter shows all of it (the next one goes
 * on); else the talk's.
 */
void dgx_ge_talk_view_event(dgx_ge_talk_view_t *view, dgx_ge_talk_t *talk, dgx_ge_ui_t event, uint32_t now_ms);

/* The pointer as it is now (pixels of the display): a tap on the line goes on, on an answer picks and takes it, on a control works it; true when taken. */
bool dgx_ge_talk_view_pointer(dgx_ge_talk_view_t *view, dgx_ge_talk_t *talk, const dgx_ge_pointer_t *pointer, uint32_t now_ms);

/* The talk into a layer of the scene: its frame, the name of the one speaking, the text, the bar under the answer chosen, the pictures of controls at the right of their lines. Nothing when it is over. */
void dgx_ge_talk_view_show(const dgx_ge_talk_view_t *view, const dgx_ge_talk_t *talk, dgx_ge_scene_t *scene, uint8_t layer);

#ifdef __cplusplus
}
#endif
