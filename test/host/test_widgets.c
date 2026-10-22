/* Widgets: a tree laid out, shown as overlays, hit by a pointer, worked by the buttons. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_frame.h"
#include "dgx_ge_widgets.h"
#include "drivers/vscreen.h"
#include "fonts/TerminusTTFMedium12.h"

static int told_what[8], told_count;
static void told(dgx_ge_widget_t *w, int what, void *ctx)
{
    (void)w, (void)ctx;
    if (told_count < 8) told_what[told_count] = what;
    ++told_count;
}

int main(void)
{
    dgx_screen_t   *display = dgx_vscreen_init(200, 100, 16, DgxScreenRGB);
    dgx_ge_frame_t *frame   = dgx_ge_frame_create(display, 0, 0, 100, 50);
    CHECK(display && frame && dgx_ge_frame_scale(frame, 2));
    dgx_ge_scene_t *scene = dgx_ge_scene_create(frame, 2, 4, 32);
    dgx_ge_scene_layer_pixels(scene, 1, true);
    dgx_font_t *font = TerminusTTFMedium12();
    dgx_ge_textbox_t label, rows;
    CHECK(dgx_ge_textbox_init(&label, 48, 13, 1) && dgx_ge_textbox_init(&rows, 80, 39, 3));
    const dgx_ge_text_style_t style = {font, 1, 1, 0, 0};
    dgx_ge_textbox_set(&label, "Go", &style, false, 0);
    dgx_ge_textbox_set(&rows, "one\ntwo\nthree", &style, false, 0);
    CHECK(rows.shown == 3);

    /* a frame of the whole display, a column inside: a button, a slider 0..100 by 5, a switch, a list of three rows */
    dgx_ge_widget_t  store[8];
    dgx_ge_widgets_t t;
    dgx_ge_widgets_init(&t, store, 8, 2);
    const int root = dgx_ge_widgets_add(&t, -1, &(dgx_ge_widget_t){.kind = DGX_GE_WIDGET_FRAME, .pad = 2, .color = 1, .color2 = 2});
    const int col  = dgx_ge_widgets_add(&t, root, &(dgx_ge_widget_t){.kind = DGX_GE_WIDGET_BOX, .flags = DGX_GE_WIDGET_COLUMN, .gap = 1});
    const int button = dgx_ge_widgets_add(&t, col, &(dgx_ge_widget_t){.kind = DGX_GE_WIDGET_BUTTON, .text = &label, .color = 3, .color2 = 4, .on = told});
    const int slider = dgx_ge_widgets_add(&t, col, &(dgx_ge_widget_t){.kind = DGX_GE_WIDGET_SLIDER, .flags = DGX_GE_WIDGET_STRETCH, .want_h = 6, .min = 0, .max = 100, .step = 5, .value = 20, .color = 3, .color2 = 4, .on = told});
    const int sw   = dgx_ge_widgets_add(&t, col, &(dgx_ge_widget_t){.kind = DGX_GE_WIDGET_SWITCH, .want_w = 10, .want_h = 5, .min = 0, .max = 1, .color = 3, .color2 = 4, .on = told});
    const int list = dgx_ge_widgets_add(&t, col, &(dgx_ge_widget_t){.kind = DGX_GE_WIDGET_LIST, .text = &rows, .min = 0, .max = 2, .color = 3, .color2 = 4, .on = told});
    CHECK(root == 0 && col == 1 && button == 2 && slider == 3 && sw == 4 && list == 5);
    dgx_ge_widgets_layout(&t, dgx_ge_rect(0, 0, 200, 100));
    /* the column fills the frame inside its padding (2 points = 4 pixels); the button its text's size; the slider across; the list its rows */
    CHECK(store[col].x == 4 && store[col].y == 4 && store[col].w == 192 && store[col].h == 92);
    CHECK(store[button].x == 4 && store[button].y == 4 && store[button].w == 48 && store[button].h == 13);
    CHECK(store[slider].y == 4 + 13 + 2 && store[slider].w == 192 && store[slider].h == 12);
    CHECK(store[sw].y == 19 + 12 + 2 && store[sw].w == 20 && store[sw].h == 10);
    CHECK(store[list].y == 33 + 10 + 2 && store[list].w == 80 && store[list].h == 39);

    /* shown: the frame two fills, the button a fill and its text, the slider a track and a knob, the switch the same, the list a bar and its text */
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_widgets_show(&t, scene, 1);
    CHECK(frame->overlay_count == 2 + 2 + 2 + 2 + 2);
    dgx_ge_scene_render(scene);

    /* the pointer: down on the button and up inside it: activated; down and up outside: nothing */
    dgx_ge_input_t in = {0};
    told_count = 0;
    dgx_ge_input_pointer(&in, 10, 10, true);
    CHECK(dgx_ge_widgets_pointer(&t, &in.pointer) && t.focus == button && t.pressed == button);
    dgx_ge_input_pointer(&in, 12, 11, false);
    CHECK(dgx_ge_widgets_pointer(&t, &in.pointer) && told_count == 2 && told_what[0] == DGX_GE_WIDGET_FOCUS && told_what[1] == DGX_GE_WIDGET_ACTIVATED);
    told_count = 0;
    dgx_ge_input_pointer(&in, 10, 10, true), dgx_ge_widgets_pointer(&t, &in.pointer);
    dgx_ge_input_pointer(&in, 150, 90, false);
    CHECK(dgx_ge_widgets_pointer(&t, &in.pointer) && told_count == 0 && t.pressed == -1);
    /* the slider: down in its middle: 50 (a knob of 3 points = 6 pixels: the span 186); dragged to the left end: 0; let go */
    dgx_ge_input_pointer(&in, 4 + 96, 25, true);
    CHECK(dgx_ge_widgets_pointer(&t, &in.pointer) && t.focus == slider && store[slider].value == 50);
    dgx_ge_input_pointer(&in, 0, 25, true);
    CHECK(dgx_ge_widgets_pointer(&t, &in.pointer) && store[slider].value == 0);
    dgx_ge_input_pointer(&in, 0, 25, false);
    CHECK(dgx_ge_widgets_pointer(&t, &in.pointer) && t.pressed == -1 && store[slider].value == 0);
    /* a tap at 3/4: 75, by steps of 5 */
    dgx_ge_input_pointer(&in, 4 + 3 + 186 * 3 / 4, 25, true), dgx_ge_widgets_pointer(&t, &in.pointer);
    dgx_ge_input_pointer(&in, 4 + 3 + 186 * 3 / 4, 25, false), dgx_ge_widgets_pointer(&t, &in.pointer);
    CHECK(store[slider].value == 75);
    /* the switch flips on a tap; the list picks its row */
    dgx_ge_input_pointer(&in, 10, 35, true), dgx_ge_widgets_pointer(&t, &in.pointer);
    dgx_ge_input_pointer(&in, 10, 35, false), dgx_ge_widgets_pointer(&t, &in.pointer);
    CHECK(store[sw].value == 1 && t.focus == sw);
    told_count = 0;
    dgx_ge_input_pointer(&in, 20, 45 + 13 * 2 + 5, true), dgx_ge_widgets_pointer(&t, &in.pointer);
    dgx_ge_input_pointer(&in, 20, 45 + 13 * 2 + 5, false), dgx_ge_widgets_pointer(&t, &in.pointer);
    CHECK(store[list].value == 2 && told_what[told_count - 1] == DGX_GE_WIDGET_ACTIVATED);
    /* a point on nothing interactive */
    dgx_ge_input_pointer(&in, 150, 10, true);
    CHECK(!dgx_ge_widgets_pointer(&t, &in.pointer) && t.pressed == -1);
    dgx_ge_input_pointer(&in, 150, 10, false);

    /* the buttons: the focus goes round the interactive ones; more/less work the focused control; activate presses */
    dgx_ge_widgets_focus(&t, -1);
    CHECK(dgx_ge_widgets_key(&t, DGX_GE_UI_NEXT) && t.focus == button);
    CHECK(dgx_ge_widgets_key(&t, DGX_GE_UI_NEXT) && t.focus == slider);
    CHECK(dgx_ge_widgets_key(&t, DGX_GE_UI_MORE) && store[slider].value == 80 && dgx_ge_widgets_key(&t, DGX_GE_UI_LESS) && store[slider].value == 75);
    CHECK(dgx_ge_widgets_key(&t, DGX_GE_UI_NEXT) && t.focus == sw && dgx_ge_widgets_key(&t, DGX_GE_UI_ACTIVATE) && store[sw].value == 0);
    CHECK(dgx_ge_widgets_key(&t, DGX_GE_UI_NEXT) && t.focus == list && store[list].value == 2);
    CHECK(dgx_ge_widgets_key(&t, DGX_GE_UI_PREV) && store[list].value == 1 && t.focus == list); /* the rows of the list first */
    CHECK(dgx_ge_widgets_key(&t, DGX_GE_UI_PREV) && store[list].value == 0 && dgx_ge_widgets_key(&t, DGX_GE_UI_PREV) && t.focus == sw);
    CHECK(dgx_ge_widgets_key(&t, DGX_GE_UI_NEXT) && t.focus == list && dgx_ge_widgets_key(&t, DGX_GE_UI_NEXT) && dgx_ge_widgets_key(&t, DGX_GE_UI_NEXT) && store[list].value == 2);
    CHECK(dgx_ge_widgets_key(&t, DGX_GE_UI_NEXT) && t.focus == button); /* past the last row: round to the first widget */
    CHECK(!dgx_ge_widgets_key(&t, DGX_GE_UI_BACK));
    /* hidden: not hit, not focused */
    store[sw].flags |= DGX_GE_WIDGET_HIDDEN;
    CHECK(dgx_ge_widgets_hit(&t, 10, 35) == -1);
    dgx_ge_widgets_focus(&t, slider);
    CHECK(dgx_ge_widgets_key(&t, DGX_GE_UI_NEXT) && t.focus == list);

    /* spent buttons (a menu closed by them): unseen until let go, then as ever */
    dgx_ge_input_t keys = {0};
    dgx_ge_input_update(&keys, DGX_GE_KEY_UP | DGX_GE_KEY_LEFT);
    CHECK(keys.pressed == (DGX_GE_KEY_UP | DGX_GE_KEY_LEFT));
    dgx_ge_input_spend(&keys, DGX_GE_KEY_UP | DGX_GE_KEY_DOWN); /* down is not held: nothing to spend */
    CHECK(keys.held == DGX_GE_KEY_LEFT && keys.pressed == DGX_GE_KEY_LEFT && keys.spent == DGX_GE_KEY_UP);
    dgx_ge_input_update(&keys, DGX_GE_KEY_UP | DGX_GE_KEY_LEFT | DGX_GE_KEY_DOWN);
    CHECK(keys.held == (DGX_GE_KEY_LEFT | DGX_GE_KEY_DOWN) && keys.pressed == DGX_GE_KEY_DOWN && !keys.released);
    dgx_ge_input_update(&keys, DGX_GE_KEY_LEFT); /* up let go: no release seen, it was never held for them */
    CHECK(keys.held == DGX_GE_KEY_LEFT && keys.released == DGX_GE_KEY_DOWN && !keys.spent);
    dgx_ge_input_update(&keys, DGX_GE_KEY_UP | DGX_GE_KEY_LEFT);
    CHECK(keys.pressed == DGX_GE_KEY_UP && keys.held == (DGX_GE_KEY_UP | DGX_GE_KEY_LEFT));

    {
        /*
         * A button dragged (a scroll bar): a tap is a tap; moved up or down less than half its width, still a tap; past
         * that, told changed at every move with the pointer where it is, and no tap when let go
         */
        dgx_ge_widget_t  store[2];
        dgx_ge_widgets_t d;
        dgx_ge_widgets_init(&d, store, 2, 1);
        const int root = dgx_ge_widgets_add(&d, -1, &(dgx_ge_widget_t){.kind = DGX_GE_WIDGET_BOX});
        const int bar  = dgx_ge_widgets_add(&d, root, &(dgx_ge_widget_t){.kind = DGX_GE_WIDGET_BUTTON, .flags = DGX_GE_WIDGET_DRAG, .want_w = 10,
                                                                         .want_h = 60, .on = told});
        dgx_ge_widgets_layout(&d, dgx_ge_rect(100, 20, 40, 80));
        CHECK(bar == 1 && store[1].w == 10 && store[1].h == 60);
        told_count = 0;
        dgx_ge_widgets_pointer(&d, &(dgx_ge_pointer_t){.x = 105, .y = 50, .down = true, .pressed = true});
        dgx_ge_widgets_pointer(&d, &(dgx_ge_pointer_t){.x = 105, .y = 54, .down = true}); /* 4 < 5: not yet */
        dgx_ge_widgets_pointer(&d, &(dgx_ge_pointer_t){.x = 105, .y = 54, .released = true});
        CHECK(told_count >= 1 && told_what[told_count - 1] == DGX_GE_WIDGET_ACTIVATED && !d.dragged);
        told_count = 0;
        dgx_ge_widgets_pointer(&d, &(dgx_ge_pointer_t){.x = 105, .y = 50, .down = true, .pressed = true});
        dgx_ge_widgets_pointer(&d, &(dgx_ge_pointer_t){.x = 105, .y = 56, .down = true});
        dgx_ge_widgets_pointer(&d, &(dgx_ge_pointer_t){.x = 106, .y = 70, .down = true});
        CHECK(d.dragged && d.move_x == 106 && d.move_y == 70 && d.press_y == 50);
        int changed = 0;
        for (int i = 0; i < told_count && i < 8; ++i) changed += told_what[i] == DGX_GE_WIDGET_CHANGED;
        const int before = told_count;
        dgx_ge_widgets_pointer(&d, &(dgx_ge_pointer_t){.x = 106, .y = 70, .released = true});
        CHECK(changed == 2 && told_count == before); /* changed twice, nothing when let go */
    }

    dgx_ge_textbox_free(&label), dgx_ge_textbox_free(&rows);
    dgx_ge_scene_destroy(&scene);
    dgx_ge_frame_destroy(&frame);
    dgx_screen_destroy(&display);
    CHECK_DONE();
}
