#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "dgx_ge_anim.h"
#include "dgx_ge_dialog.h"
#include "dgx_ge_input.h"
#include "dgx_ge_scene.h"
#include "dgx_ge_text.h"

/*
 * Widgets: the thin layer a dialog is made of, so that a touch on the display finds what it touched. A widget is a
 * rectangle in pixels of the display with a callback; a tree of them is laid out from a root rectangle (a box
 * stacks its children as a column or a row, else children sit where they ask, from an edge of their parent), shown
 * as overlays of a layer in pixels (dgx_ge_scene_layer_pixels), and told the pointer and the buttons: the deepest
 * widget under the pointer gets the point in its own coordinates and makes of it what it is (a slider moves there,
 * or by the way the pointer goes when it is swiped, a list picks the row, a button is pressed on release inside, a
 * switch flips); the buttons move the focus (next,
 * prev) and work the focused one (more, less, activate). The look of a control is its actor (an animation of the
 * pack, its param value following) or, without one, a plain drawing of the module.
 */
typedef enum {
    DGX_GE_WIDGET_BOX,     /* holds children; COLUMN or ROW lays them, else they sit where they ask */
    DGX_GE_WIDGET_FRAME,   /* a fill with a border, children inside the padding */
    DGX_GE_WIDGET_TEXT,    /* a text box, its picture */
    DGX_GE_WIDGET_PICTURE, /* a picture of the pack, at its own size */
    DGX_GE_WIDGET_LIST,    /* rows of a text box, one picked: value (min.. from the box's top line); a bar under it; swiped up or down, it goes along */
    DGX_GE_WIDGET_SWITCH,  /* value 0 or 1 */
    DGX_GE_WIDGET_SLIDER,  /* value min..max by step */
    DGX_GE_WIDGET_BUTTON,  /* a text (or picture) pressed: activated */
} dgx_ge_widget_kind_t;

enum {
    DGX_GE_WIDGET_COLUMN  = 1u << 0, /* a box: children one under another */
    DGX_GE_WIDGET_ROW     = 1u << 1, /* side by side */
    DGX_GE_WIDGET_STRETCH = 1u << 2, /* across the stacking: as wide (a column) or tall (a row) as the box */
    DGX_GE_WIDGET_HIDDEN  = 1u << 3, /* not shown, not hit, not focused */
    DGX_GE_WIDGET_FOCUSED = 1u << 4, /* set by the tree */
    DGX_GE_WIDGET_SWIPE   = 1u << 5, /* a slider moved by the pointer's travel from where it went down (its width the whole
                                        range), not put where it is: a wide strip, easy to find, beside a small slider; a
                                        tap on it (no way gone) tells it activated, its value as it was */
};

enum { DGX_GE_WIDGET_CHANGED, DGX_GE_WIDGET_ACTIVATED, DGX_GE_WIDGET_FOCUS }; /* what a callback is told */

/* Of a row's height (a list's), its top and its bottom 1/n belong to no row: a finger between two rows takes neither. */
enum { DGX_GE_WIDGET_ROW_GAP = 12 };

typedef struct dgx_ge_widget {
    uint8_t  kind, flags;
    int8_t   parent;       /* -1 the root */
    uint8_t  pad, gap;     /* inside, in points of the style (times the unit) */
    int16_t  at_x, at_y;   /* asked: from the parent's inside edge, in points; negative: from the far edge, as styles */
    int16_t  want_w, want_h; /* asked, in points; 0: the content's (a text, a picture), or across the stacking all of the box */
    int16_t  x, y, w, h;   /* laid: in pixels of the display */
    /* the look */
    const dgx_image_t      *image;
    const uint8_t          *map;
    const dgx_ge_textbox_t *text;
    dgx_ge_actor_t         *actor; /* a control's look, its param value following; NULL: drawn by the module */
    uint8_t                 color, color2; /* fill and border, text, the bar, the knob */
    /* a control's value */
    int16_t value, min, max, step;
    int16_t min_row; /* of the one who made it: a row, a number (the widgets keep it, nothing more) */
    void (*on)(struct dgx_ge_widget *widget, int what, void *ctx);
    void *ctx;
} dgx_ge_widget_t;

typedef struct {
    dgx_ge_widget_t *widgets;
    uint8_t          count, capacity;
    int8_t           focus;   /* the widget focused, -1 none */
    int8_t           pressed; /* the widget the pointer went down on, -1 none */
    uint8_t          unit;    /* pixels of the display a point of the style (the frame's scale) */
    uint8_t          value_param; /* the param of a control's actor that follows its value */
    int16_t          press_x, press_y;     /* where the pointer went down */
    int16_t          press_value;          /* the value of the one pressed then */
    bool             dragged;              /* swiped since (a list up or down half a row, a slider a step): no tap when let go */
    bool             between;              /* a list pressed between its rows: no row taken */
} dgx_ge_widgets_t;

/* A tree in the game's array of so many widgets; a point of its measures is unit pixels. */
void dgx_ge_widgets_init(dgx_ge_widgets_t *tree, dgx_ge_widget_t *widgets, int capacity, int unit);

/* A widget added under a parent (-1: the root, the first one); its number, -1 no room. */
int dgx_ge_widgets_add(dgx_ge_widgets_t *tree, int parent, const dgx_ge_widget_t *widget);

/* All laid out: the root in its rectangle of the display (pixels), every child from it. */
void dgx_ge_widgets_layout(dgx_ge_widgets_t *tree, dgx_ge_rect_t root);

/* Shown: their items into a layer in pixels of the scene, parents before children. */
void dgx_ge_widgets_show(const dgx_ge_widgets_t *tree, dgx_ge_scene_t *scene, uint8_t layer);

/* The deepest widget under a pixel of the display that answers to a pointer; -1 none. */
int dgx_ge_widgets_hit(const dgx_ge_widgets_t *tree, int x, int y);

/* The pointer as it is now: what it is on gets it in its own coordinates; true when a widget took it. */
bool dgx_ge_widgets_pointer(dgx_ge_widgets_t *tree, const dgx_ge_pointer_t *pointer);

/* A button of the dialog's kind: the focus moves, the focused one is worked; true when a widget took it. */
bool dgx_ge_widgets_key(dgx_ge_widgets_t *tree, dgx_ge_ui_t event);

/* The focus put on a widget (-1: none); its callback told. */
void dgx_ge_widgets_focus(dgx_ge_widgets_t *tree, int widget);

#ifdef __cplusplus
}
#endif
