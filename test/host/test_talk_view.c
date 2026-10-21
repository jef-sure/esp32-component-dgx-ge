/* A talk shown: its style from the pack (or the default one), its place, frame, text and the bar under the answer chosen; a line's time from its style. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_pockets.h"
#include "dgx_ge_talk_view.h"
#include "fonts/font0ss.h"
#include "drivers/vscreen.h"
#include "fonts/TerminusTTFMedium12.h"
#include "test_pack.h"

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*size);
    if (data && fread(data, 1, *size, f) != *size) free(data), data = NULL;
    fclose(f);
    return data;
}

/* Is every point of a rectangle among those the frame will draw anew? */
static bool covers(const dgx_ge_frame_t *f, dgx_ge_rect_t r)
{
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x) {
            bool in = false;
            for (int i = 0; i < f->dirty_count && !in; ++i)
                in = x >= f->dirty[i].x && x < f->dirty[i].x + f->dirty[i].w && y >= f->dirty[i].y && y < f->dirty[i].y + f->dirty[i].h;
            if (!in) return false;
        }
    return true;
}

int main(int argc, char **argv)
{
    size_t   size = 0;
    uint8_t *data = read_file(argc > 1 ? argv[1] : "build/test.pack", &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    size_t           n, styles_size;
    dgx_ge_strings_t strings;
    dgx_ge_dialog_t  post;
    const uint8_t   *styles = dgx_ge_pack_blob(&pack, TEST_BLOB_STYLES, &styles_size);
    const uint8_t   *texts  = dgx_ge_pack_blob(&pack, TEST_BLOB_STRINGS, &n);
    CHECK(styles && dgx_ge_strings_open(&strings, texts, n));
    const uint8_t *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_POST, &n);
    CHECK(dgx_ge_dialog_open(&post, blob, n));

    /* the styles: talk as made, alert named and never made (the default) */
    dgx_ge_style_t s, d;
    dgx_ge_style_of(styles, styles_size, TEST_STYLE_TALK, &s);
    dgx_ge_style_default(&d);
    CHECK(s.frame == 1 && s.x == 10 && s.y == -10 && s.w == 200 && s.h == 43 && s.scale_max == 2 && s.wait_ms == 4000 && s.border_width == 2);
    CHECK(s.show_name && s.appear == 1 && s.letter_ms == 20 && s.name[0] == 255 && s.align == DGX_GE_ALIGN_MIDDLE); /* one byte, both */
    CHECK(s.font == TEST_FONT_TERMINUSTTFMEDIUM12 && s.fill[2] == 80 && s.border[0] == 255 && s.bar[1] == 128 && s.pad[0] == 2);
    dgx_ge_style_of(styles, styles_size, TEST_STYLE_ALERT, &s);
    CHECK(!memcmp(&s, &d, sizeof s));
    dgx_ge_style_of(NULL, 0, 0, &s);
    CHECK(!memcmp(&s, &d, sizeof s));
    /* not a blob of styles (no "DGST", another version): the default */
    uint8_t wrong[8 + DGX_GE_STYLE_BYTES];
    memcpy(wrong, styles, sizeof wrong);
    wrong[0] = 'X';
    dgx_ge_style_of(wrong, sizeof wrong, TEST_STYLE_TALK, &s);
    CHECK(!memcmp(&s, &d, sizeof s));
    memcpy(wrong, styles, sizeof wrong);
    wrong[4] = 2;
    dgx_ge_style_of(wrong, sizeof wrong, TEST_STYLE_TALK, &s);
    CHECK(!memcmp(&s, &d, sizeof s));

    dgx_screen_t   *display = dgx_vscreen_init(320, 200, 16, DgxScreenRGB);
    dgx_ge_frame_t *frame   = dgx_ge_frame_create(display, 0, 0, 320, 200);
    dgx_ge_scene_t *scene   = dgx_ge_scene_create(frame, 2, 8, 16);
    dgx_font_t     *fonts[] = {TerminusTTFMedium12()};
    dgx_ge_talk_view_t v;
    dgx_ge_talk_view_init(&v, styles, styles_size, frame, fonts, 1);

    /* the kid has knives: two answers to choose from */
    dgx_ge_kinds_t kinds;
    CHECK(dgx_ge_kinds_init(&kinds, &pack, 16));
    dgx_ge_actor_t kid;
    dgx_ge_actor_start(&kid, dgx_ge_kind(&kinds, TEST_BLOB_CARRIER), 0, 0, 0, 0);
    kid.pockets = dgx_ge_pockets_create(&kinds, 3, 0);
    dgx_ge_pockets_put(kid.pockets, &(dgx_ge_pocket_t){.kind = TEST_BLOB_KNIFE}, 3);
    dgx_ge_actor_t *const cast[3] = {NULL, &kid, NULL};

    /* the first line of post in the style talk: its place (10 from the left, 10 from the bottom), its fields inside the padding */
    dgx_ge_talk_t t;
    dgx_ge_talk_start(&t, &post, &(dgx_ge_cast_t){.roles = cast}, TEST_STYLE_TALK, 0, NULL, NULL);
    CHECK(dgx_ge_talk_view_play(&v, &t, &strings, 0));
    CHECK(v.box.x == 10 && v.box.y == 200 - 10 - 43 && v.box.w == 200 && v.box.h == 43 && v.line.width == 200 && v.line.height == 26);
    CHECK(v.name.height == 13 && v.name_string >= 0 && !strcmp(dgx_ge_string(&strings, v.name_string), "Guard"));
    CHECK(dgx_ge_textbox_appearing(&v.line));                                 /* letter by letter */
    /* the fields change in place (the same picture, the same spot): the view marks them for the frame to draw anew */
    CHECK(covers(frame, v.box));                                               /* a new line: the whole box */
    frame->dirty_count = 0;
    dgx_ge_talk_view_play(&v, &t, &strings, 10);                               /* no letter more yet: nothing */
    CHECK(frame->dirty_count == 0);
    dgx_ge_talk_view_play(&v, &t, &strings, 20);                               /* a letter more: the line, not the name */
    CHECK(covers(frame, dgx_ge_rect(12, 149 + 13, 196, 26)) && !covers(frame, dgx_ge_rect(12, 149, 1, 1)));
    frame->dirty_count = 0;
    dgx_ge_talk_view_event(&v, &t, DGX_GE_UI_ACTIVATE, 0);                     /* a button: all of it, not on */
    dgx_ge_talk_view_play(&v, &t, &strings, 0);                                /* seen whole from now: its time runs */
    CHECK(!dgx_ge_textbox_appearing(&v.line) && t.state == DGX_GE_TALK_SAYING && !strcmp(dgx_ge_string(&strings, t.string), "Halt! Who goes there?"));
    CHECK(v.line.lines == 1 && v.line.scale == 1 && t.wait_ms == 4000); /* 252 points at 2: at 1; no time of its own: the style's */
    const uint16_t first = t.at;
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_talk_view_show(&v, &t, scene, 1);
    CHECK(scene->actor_count == 4);                                           /* the border, the fill, the name, the text */
    CHECK(scene->actors[2].image == &v.name.image && scene->actors[2].color == v.colors[4] && scene->actors[2].y == 149);
    CHECK(scene->actors[3].image == &v.line.image && scene->actors[3].y == 149 + 13);
    CHECK(scene->actors[0].flags & DGX_GE_ITEM_FILL && scene->actors[0].color == v.colors[1] && scene->actors[0].clip.w == 200);
    CHECK(scene->actors[1].clip.x == 12 && scene->actors[1].clip.w == 196 && scene->actors[1].color == v.colors[0]);
    /* in pixels of a display three times the frame: the box and the fields thrice as large, the font at thrice its scale, the overlays of the frame */
    {
        dgx_screen_t   *big3   = dgx_vscreen_init(960, 600, 16, DgxScreenRGB);
        dgx_ge_frame_t *frame3 = dgx_ge_frame_create(big3, 0, 0, 320, 200);
        dgx_ge_scene_t *scene3 = dgx_ge_scene_create(frame3, 2, 8, 16);
        CHECK(dgx_ge_frame_scale(frame3, 3));
        dgx_ge_scene_layer_pixels(scene3, 1, true);
        dgx_ge_talk_view_t v3;
        dgx_ge_talk_view_init(&v3, styles, styles_size, frame3, fonts, 1);
        dgx_ge_talk_view_pixels(&v3, true, NULL);
        dgx_ge_talk_t t3;
        dgx_ge_talk_start(&t3, &post, &(dgx_ge_cast_t){.roles = cast}, TEST_STYLE_TALK, 0, NULL, NULL);
        CHECK(dgx_ge_talk_view_play(&v3, &t3, &strings, 0));
        CHECK(v3.unit == 3 && v3.box.x == 30 && v3.box.y == 600 - 30 - 129 && v3.box.w == 600 && v3.box.h == 129);
        CHECK(v3.line.width == 592 && v3.line.height == 78 && v3.name.height == 39 && v3.text_style.scale_max == 6 && v3.text_style.scale_min == 3); /* 588 wide, made whole bytes */
        frame3->dirty_count = 0;
        dgx_ge_talk_view_play(&v3, &t3, &strings, 20);                  /* a letter more: the text's place, in points of the frame */
        CHECK(covers(frame3, dgx_ge_rect(12, 149 + 13, 196, 26)));
        dgx_ge_scene_actors_begin(scene3);
        dgx_ge_talk_view_show(&v3, &t3, scene3, 1);
        CHECK(scene3->actor_count == 0 && frame3->overlay_count == 4 && frame3->overlays[3].x == 36 && frame3->overlays[3].y == 600 - 30 - 129 + 6 + 39);
        dgx_ge_talk_view_free(&v3);
        dgx_ge_scene_destroy(&scene3);
        dgx_ge_frame_destroy(&frame3);
        dgx_screen_destroy(&big3);
    }

    /* the pointer: the view's widgets cover the box with a button; a tap on it (down, then up inside) goes on, as activate; off the box nothing */
    dgx_ge_talk_t tp;
    dgx_ge_talk_start(&tp, &post, &(dgx_ge_cast_t){.roles = cast}, TEST_STYLE_TALK, 0, NULL, NULL);
    const uint16_t tp_at = tp.at;
    CHECK(v.widgets.count == 2 && v.widget_store[1].kind == DGX_GE_WIDGET_BUTTON && v.widget_store[1].w == 200 && v.widget_store[1].y == 147);
    dgx_ge_input_t in = {0};
    dgx_ge_input_pointer(&in, 5, 5, true), dgx_ge_talk_view_pointer(&v, &tp, &in.pointer, 10); /* off the box: nothing */
    dgx_ge_input_pointer(&in, 5, 5, false);
    CHECK(!dgx_ge_talk_view_pointer(&v, &tp, &in.pointer, 10) && tp.at == tp_at && tp.state == DGX_GE_TALK_SAYING);
    dgx_ge_input_pointer(&in, 20, 170, true);
    CHECK(dgx_ge_talk_view_pointer(&v, &tp, &in.pointer, 10) && tp.at == tp_at);
    dgx_ge_input_pointer(&in, 20, 170, false);
    CHECK(dgx_ge_talk_view_pointer(&v, &tp, &in.pointer, 10) && (tp.at != tp_at || tp.state != DGX_GE_TALK_SAYING));

    /* its time from the style: on by itself 4 s after it is seen */
    CHECK(dgx_ge_talk_view_play(&v, &t, &strings, 3999) && t.at == first);
    dgx_ge_talk_view_play(&v, &t, &strings, 4000);
    CHECK(t.important && !strcmp(dgx_ge_string(&strings, t.string), "Read this."));

    /* the answers: a line each, the bar under the one chosen, moving with it */
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 5000);
    dgx_ge_talk_view_play(&v, &t, &strings, 5000);
    CHECK(t.state == DGX_GE_TALK_CHOOSING && v.answers.lines == t.shown);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_talk_view_show(&v, &t, scene, 1);
    CHECK(scene->actor_count == 5 && scene->actors[3].color == v.colors[3] && scene->actors[3].clip.y == 162 && scene->actors[4].image == &v.answers.image);
    CHECK(!strcmp(dgx_ge_string(&strings, v.name_string), "Kid")); /* the one choosing */
    /* a tap on the second answer: picked while down, taken when let go (the list over the rows, 13 points each) */
    CHECK(v.widgets.count >= 2 && v.widget_store[1].kind == DGX_GE_WIDGET_LIST && v.widget_store[1].y == 162 && v.widget_store[1].h == 13 * t.shown);
    if (t.shown > 1) {
        const uint16_t was_at = t.at;
        dgx_ge_input_pointer(&in, 20, 162 + 13 + 6, true);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &in.pointer, 5000) && t.pick == 1);
        dgx_ge_input_pointer(&in, 20, 162 + 13 + 6, false);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &in.pointer, 5000) && (t.at != was_at || t.state != DGX_GE_TALK_CHOOSING));
        dgx_ge_talk_start(&t, &post, &(dgx_ge_cast_t){.roles = cast}, TEST_STYLE_TALK, 0, NULL, NULL); /* as it was for the rest */
        dgx_ge_talk_view_play(&v, &t, &strings, 0);
        dgx_ge_talk_view_event(&v, &t, DGX_GE_UI_ACTIVATE, 0);
        dgx_ge_talk_view_play(&v, &t, &strings, 0);
        dgx_ge_talk_view_play(&v, &t, &strings, 4000);
        dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 5000);
        dgx_ge_talk_view_play(&v, &t, &strings, 5000);
        dgx_ge_scene_actors_begin(scene);
        dgx_ge_talk_view_show(&v, &t, scene, 1);
    }
    if (t.shown > 1) {
        dgx_ge_talk_event(&t, DGX_GE_UI_NEXT, 5100);
        dgx_ge_talk_view_play(&v, &t, &strings, 5100);
        dgx_ge_scene_actors_begin(scene);
        dgx_ge_talk_view_show(&v, &t, scene, 1);
        CHECK(scene->actors[3].clip.y == 162 + 13 * v.answers.scale && scene->actors[3].clip.x == 12 && scene->actors[3].clip.w == 196); /* a line lower, inside the frame */
    }
    CHECK(t.shown == 2 && t.pick == 1);
    /* six knives: three answers in a box of two lines; the third chosen, the list goes up a line, the bar on it */
    dgx_ge_pockets_put(kid.pockets, &(dgx_ge_pocket_t){.kind = TEST_BLOB_KNIFE}, 3);
    dgx_ge_talk_start(&t, &post, &(dgx_ge_cast_t){.roles = cast}, TEST_STYLE_TALK, 6000, NULL, NULL);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 6000);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 6000);
    dgx_ge_talk_view_play(&v, &t, &strings, 6000);
    CHECK(t.shown == 3 && v.answers.lines == 3 && v.answers.shown == 2 && !v.answers.cut && v.answers.top == 0);
    dgx_ge_talk_event(&t, DGX_GE_UI_NEXT, 6100);
    dgx_ge_talk_event(&t, DGX_GE_UI_NEXT, 6100);
    dgx_ge_talk_view_play(&v, &t, &strings, 6100);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_talk_view_show(&v, &t, scene, 1);
    CHECK(t.pick == 2 && v.answers.top == 1 && scene->actors[3].clip.y == 162 + 13); /* the second line seen */
    dgx_ge_talk_event(&t, DGX_GE_UI_PREV, 6200);
    dgx_ge_talk_event(&t, DGX_GE_UI_PREV, 6200);
    dgx_ge_talk_view_play(&v, &t, &strings, 6200);
    CHECK(t.pick == 0 && v.answers.top == 0);
    dgx_ge_talk_view_free(&v);
    dgx_ge_pockets_destroy(&kid.pockets);

    /* the default style: along the bottom, the frame's width less 8 */
    dgx_ge_talk_view_init(&v, NULL, 0, frame, fonts, 1);
    dgx_ge_talk_start(&t, &post, NULL, 0, 0, NULL, NULL);
    CHECK(dgx_ge_talk_view_play(&v, &t, &strings, 0) && v.box.x == 4 && v.box.w == 312 && v.box.y == 200 - 4 - 44 && !t.wait_ms);
    t.state = DGX_GE_TALK_OVER;
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_talk_view_play(&v, &t, &strings, 10);
    dgx_ge_talk_view_show(&v, &t, scene, 1);
    CHECK(scene->actor_count == 0); /* over: nothing shown */

    /* a page of settings: the pictures of controls at the right of their lines, by their values, in the colors of their sets */
    dgx_ge_dialog_t page;
    blob = dgx_ge_pack_blob(&pack, TEST_BLOB_PAGE, &n);
    CHECK(blob && dgx_ge_dialog_open(&page, blob, n));
    dgx_ge_actor_t settings;
    dgx_ge_actor_start(&settings, dgx_ge_kind(&kinds, TEST_BLOB_SETTINGS), 0, 0, 0, 0);
    static const uint8_t hero_map[4] = {0, 1, 2, 3}, wide_map[256] = {0};
    const uint8_t *const maps[2] = {hero_map, wide_map};
    dgx_ge_talk_view_pictures(&v, &pack, maps);
    dgx_ge_talk_start(&t, &page, &(dgx_ge_cast_t){.game = &settings, .kinds = &kinds}, 0, 0, NULL, NULL);
    dgx_ge_talk_view_play(&v, &t, &strings, 0);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_talk_view_show(&v, &t, scene, 1);
    CHECK(v.answers.shown == 2 && v.control_ids[0] == TEST_IMG_FOUR && v.control_ids[1] == TEST_IMG_EIGHT); /* sound on, volume 2 (loud not shown) */
    /* four answers in two lines: a bar of 3 at the right inside the frame (its border 1, a gap of 1), the knob half of it at the top */
    CHECK(scene->actor_count == 8 && dgx_ge_rect_equal(scene->actors[4].clip, dgx_ge_rect(4 + 312 - 1 - 1 - 3, 152 + 4, 3, 26)) &&
          scene->actors[4].color == v.colors[3] && dgx_ge_rect_equal(scene->actors[5].clip, dgx_ge_rect(4 + 312 - 1 - 1 - 3, 152 + 4, 3, 13)) &&
          scene->actors[5].color == v.colors[2]);
    CHECK(scene->actors[6].image == &v.control_images[0] && scene->actors[6].map == hero_map);
    CHECK(scene->actors[6].x == 4 + 312 - 6 - 3 + 0 && scene->actors[6].y == 152 + 4 + (13 - 2) / 2 + 1);     /* FOUR: 3 x 2, origin 0, 1 */
    CHECK(scene->actors[7].map == wide_map && scene->actors[7].y == 152 + 4 + 13 + (13 - 1) / 2 + 5);          /* EIGHT, the second line */
    /* the switch: its picture anew, the frame marked */
    frame->dirty_count = 0;
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 100);
    dgx_ge_talk_view_play(&v, &t, &strings, 100);
    CHECK(v.control_ids[0] == TEST_IMG_ONE && covers(frame, v.box));
    /*
     * the pointer on the controls: the slider has a range (0..3 by 1): a tap at the right end of its picture puts the
     * volume at 3, at its left end 0; its whole row swiped puts it by the way across (the row's width all of 0..3), the
     * widgets laid anew between and the swipe going on; up or down first: the list swiped instead; the switch a tap flips
     */
    {
        dgx_ge_talk_view_play(&v, &t, &strings, 100);
        int slider = -1, row = -1, sw = -1;
        for (int i = 0; i < v.widgets.count; ++i) {
            const dgx_ge_widget_t *w = &v.widget_store[i];
            if (w->kind == DGX_GE_WIDGET_SLIDER) *(w->flags & DGX_GE_WIDGET_SWIPE ? &row : &slider) = i;
            if (w->kind == DGX_GE_WIDGET_SWITCH && sw < 0) sw = i;
        }
        CHECK(slider >= 0 && row >= 0 && sw >= 0 && v.widget_store[slider].max == 3 && v.widget_store[slider].step == 1);
        const dgx_ge_widget_t *s = &v.widget_store[slider], *r = &v.widget_store[row], *w = &v.widget_store[sw], *l = &v.widget_store[1];
        /* the whole row, less 13 / 12 = 1 above and below: the gap between rows */
        CHECK(r->x == l->x && r->w == l->w && r->h == 13 - 2 * 1 && r->y == l->y + 13 + 1 && r->y == s->y && r->min_row == s->min_row);
        CHECK(s->h == r->h && s->w == 2 && s->x > r->x && s->x + s->w <= r->x + r->w); /* the picture's across (2 wide here), the row's down */
        dgx_ge_input_t pin = {0};
        dgx_ge_input_pointer(&pin, s->x + s->w - 1, s->y, true); /* the picture of the slider is 2x1 here: its row */
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && settings.params[1] == 3);
        dgx_ge_input_pointer(&pin, s->x + s->w - 1, s->y, false), dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100);
        dgx_ge_talk_view_play(&v, &t, &strings, 100); /* a frame between: volume 3 shows "Loud", the rows move, the widgets are placed anew */
        for (int i = 0; i < v.widgets.count; ++i)
            if (v.widget_store[i].kind == DGX_GE_WIDGET_SLIDER && !(v.widget_store[i].flags & DGX_GE_WIDGET_SWIPE)) s = &v.widget_store[i];
        CHECK(s->min_row == 2 && s->value == 3);
        dgx_ge_input_pointer(&pin, s->x, s->y, true);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && settings.params[1] == 0); /* the left end: 0 */
        dgx_ge_input_pointer(&pin, s->x, s->y, false), dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100);
        dgx_ge_talk_view_play(&v, &t, &strings, 100);
        /* the row swiped: down at its left, nothing; a third of it across: 1; a frame between (laid anew), on: 2; back: 0; let go */
        for (int i = 0; i < v.widgets.count; ++i)
            if (v.widget_store[i].kind == DGX_GE_WIDGET_SLIDER && (v.widget_store[i].flags & DGX_GE_WIDGET_SWIPE)) r = &v.widget_store[i];
        const int rx = r->x + 10, ry = r->y + 1, rw = r->w;
        dgx_ge_input_pointer(&pin, rx, ry, true);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && settings.params[1] == 0);
        dgx_ge_input_pointer(&pin, rx + rw / 3, ry, true);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && settings.params[1] == 1);
        dgx_ge_talk_view_play(&v, &t, &strings, 100); /* its picture anew, the widgets laid anew: the press kept */
        CHECK(v.widgets.pressed >= 0 && (v.widget_store[v.widgets.pressed].flags & DGX_GE_WIDGET_SWIPE));
        dgx_ge_input_pointer(&pin, rx + rw * 2 / 3, ry, true);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && settings.params[1] == 2);
        dgx_ge_input_pointer(&pin, rx - rw, ry, true);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && settings.params[1] == 0);
        dgx_ge_input_pointer(&pin, rx - rw, ry, false);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && settings.params[1] == 0 && t.state == DGX_GE_TALK_CHOOSING);
        dgx_ge_talk_view_play(&v, &t, &strings, 100);
        const int16_t sound = settings.params[0];
        w = NULL;
        for (int i = 0; i < v.widgets.count && !w; ++i)
            if (v.widget_store[i].kind == DGX_GE_WIDGET_SWITCH) w = &v.widget_store[i];
        dgx_ge_input_pointer(&pin, w->x + 1, w->y + 1, true), dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100);
        dgx_ge_input_pointer(&pin, w->x + 1, w->y + 1, false);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && settings.params[0] == 1 - sound);
        dgx_ge_talk_view_play(&v, &t, &strings, 100);
        /* the list swiped up: the choice down a row for each row's height, the lines along; let go: nothing taken */
        dgx_ge_talk_view_event(&v, &t, DGX_GE_UI_PREV, 100), dgx_ge_talk_view_event(&v, &t, DGX_GE_UI_PREV, 100);
        dgx_ge_talk_view_play(&v, &t, &strings, 100);
        CHECK(t.pick == 0 && v.answers.top == 0 && t.shown == 4);
        l = &v.widget_store[1];
        /* between the first two lines (the last 2 of the first, the first 2 of the second): no line taken, nothing chosen */
        dgx_ge_input_pointer(&pin, l->x + 20, l->y + 12, true);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && t.pick == 0 && v.widget_store[v.widgets.pressed].kind == DGX_GE_WIDGET_LIST);
        dgx_ge_input_pointer(&pin, l->x + 20, l->y + 12, false);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && t.pick == 0 && t.state == DGX_GE_TALK_CHOOSING);
        dgx_ge_input_pointer(&pin, l->x + 20, l->y, true), dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100);
        dgx_ge_input_pointer(&pin, l->x + 20, l->y, false);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && t.pick == 0 && t.state == DGX_GE_TALK_CHOOSING);
        /* a tap on the slider's row (its text, not its picture): that row chosen, its value as it was, nothing taken */
        for (int i = 0; i < v.widgets.count; ++i)
            if (v.widget_store[i].kind == DGX_GE_WIDGET_SLIDER && (v.widget_store[i].flags & DGX_GE_WIDGET_SWIPE)) r = &v.widget_store[i];
        const int16_t level = settings.params[1];
        dgx_ge_input_pointer(&pin, r->x + 20, r->y + 4, true), dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100);
        dgx_ge_input_pointer(&pin, r->x + 20, r->y + 4, false);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && t.pick == r->min_row && settings.params[1] == level && t.state == DGX_GE_TALK_CHOOSING);
        dgx_ge_talk_view_event(&v, &t, DGX_GE_UI_PREV, 100);
        dgx_ge_talk_view_play(&v, &t, &strings, 100);
        CHECK(t.pick == 0);
        const int lx = l->x + 20, ly = l->y + 6; /* on the first line (the second is the slider's row: the swipe across its own) */
        dgx_ge_input_pointer(&pin, lx, ly, true);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && t.pick == 0); /* the row under it, as a tap */
        dgx_ge_input_pointer(&pin, lx, ly - 5, true);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && t.pick == 0); /* less than half a row: still */
        dgx_ge_input_pointer(&pin, lx, ly - 13, true);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && t.pick == 1);
        dgx_ge_talk_view_play(&v, &t, &strings, 100);
        dgx_ge_input_pointer(&pin, lx, ly - 26, true);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && t.pick == 2);
        dgx_ge_talk_view_play(&v, &t, &strings, 100); /* the lines move along: the third answer in sight, the press kept */
        CHECK(v.answers.top == 1 && v.widgets.pressed == 1);
        dgx_ge_input_pointer(&pin, lx, ly - 39, true);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && t.pick == 3);
        dgx_ge_input_pointer(&pin, lx, ly - 39, false);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && t.pick == 3 && t.state == DGX_GE_TALK_CHOOSING); /* swiped: not taken */
        dgx_ge_talk_view_play(&v, &t, &strings, 100);
        CHECK(v.answers.top == 2);
        /* a swipe begun on the slider's row going up: the list takes it, the slider as it was */
        dgx_ge_talk_view_event(&v, &t, DGX_GE_UI_PREV, 100), dgx_ge_talk_view_event(&v, &t, DGX_GE_UI_PREV, 100);
        dgx_ge_talk_view_event(&v, &t, DGX_GE_UI_PREV, 100);
        dgx_ge_talk_view_play(&v, &t, &strings, 100);
        for (int i = 0; i < v.widgets.count; ++i)
            if (v.widget_store[i].kind == DGX_GE_WIDGET_SLIDER && (v.widget_store[i].flags & DGX_GE_WIDGET_SWIPE)) r = &v.widget_store[i];
        const int16_t volume = settings.params[1];
        const int     sx = r->x + 10, sy = r->y + 6, picked = t.pick;
        dgx_ge_input_pointer(&pin, sx, sy, true), dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100);
        dgx_ge_input_pointer(&pin, sx + 3, sy - 13, true);
        CHECK(dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100) && settings.params[1] == volume && t.pick != picked &&
              v.widget_store[v.widgets.pressed].kind == DGX_GE_WIDGET_LIST);
        dgx_ge_input_pointer(&pin, sx + 3, sy - 13, false), dgx_ge_talk_view_pointer(&v, &t, &pin.pointer, 100);
        dgx_ge_talk_view_play(&v, &t, &strings, 100);
    }
    /* in pixels of a display three times the frame: the pictures of the controls three times larger, at the right padding; the slider's widget as wide; a tap at the right end of its larger picture: at most */
    {
        dgx_screen_t   *big3   = dgx_vscreen_init(960, 600, 16, DgxScreenRGB);
        dgx_ge_frame_t *frame3 = dgx_ge_frame_create(big3, 0, 0, 320, 200);
        dgx_ge_scene_t *scene3 = dgx_ge_scene_create(frame3, 2, 8, 16);
        CHECK(dgx_ge_frame_scale(frame3, 3));
        dgx_ge_scene_layer_pixels(scene3, 1, true);
        dgx_ge_talk_view_t v3;
        dgx_ge_talk_view_init(&v3, styles, styles_size, frame3, fonts, 1);
        dgx_ge_talk_view_pixels(&v3, true, NULL);
        dgx_ge_talk_view_pictures(&v3, &pack, maps);
        settings.params[0] = 1, settings.params[1] = 2; /* sound on, volume 2: FOUR and EIGHT */
        dgx_ge_talk_t t3;
        dgx_ge_talk_start(&t3, &page, &(dgx_ge_cast_t){.game = &settings, .kinds = &kinds}, 0, 0, NULL, NULL);
        CHECK(dgx_ge_talk_view_play(&v3, &t3, &strings, 0));
        dgx_ge_scene_actors_begin(scene3);
        dgx_ge_talk_view_show(&v3, &t3, scene3, 1);
        const dgx_ge_item_t *c = NULL;
        for (int i = 0; i < frame3->overlay_count; ++i)
            if (frame3->overlays[i].image == &v3.control_images[0]) c = &frame3->overlays[i];
        const dgx_image_t  *four = &v3.control_images[0];
        const dgx_ge_rect_t cb   = c ? dgx_ge_image_box_scaled(c->image, c->x, c->y, false, c->scale) : dgx_ge_rect(0, 0, 0, 0);
        CHECK(c && c->scale == 3 && four->width == 3 && cb.w == 9 && cb.h == 6 && cb.x + cb.w == v3.box.x + v3.box.w - v3.style.pad[2]);
        /* the bar beside more answers than lines: 3 points wide, a point from the border, times 3 too */
        bool bar = false;
        for (int i = 0; i < frame3->overlay_count; ++i) {
            const dgx_ge_item_t *o = &frame3->overlays[i];
            bar |= (o->flags & DGX_GE_ITEM_FILL) && o->color == v3.colors[3] && o->clip.w == 9 && o->clip.x == v3.box.x + v3.box.w - v3.style.border_width - 3 - 9;
        }
        CHECK(bar);
        const dgx_ge_widget_t *sl = NULL;
        for (int i = 0; i < v3.widgets.count; ++i)
            if (v3.widget_store[i].kind == DGX_GE_WIDGET_SLIDER && !(v3.widget_store[i].flags & DGX_GE_WIDGET_SWIPE)) sl = &v3.widget_store[i];
        CHECK(sl && sl->w == v3.control_images[1].width * 3); /* EIGHT, 2 wide: 6 */
        dgx_ge_input_t pin3 = {0};
        dgx_ge_input_pointer(&pin3, sl->x + sl->w - 1, sl->y, true);
        CHECK(dgx_ge_talk_view_pointer(&v3, &t3, &pin3.pointer, 10) && settings.params[1] == 3);
        dgx_ge_input_pointer(&pin3, sl->x + sl->w - 1, sl->y, false), dgx_ge_talk_view_pointer(&v3, &t3, &pin3.pointer, 10);
        dgx_ge_talk_view_free(&v3);
        dgx_ge_scene_destroy(&scene3);
        dgx_ge_frame_destroy(&frame3);
        dgx_screen_destroy(&big3);
    }
    frame->dirty_count = 0;
    dgx_ge_talk_view_play(&v, &t, &strings, 200); /* nothing changed: nothing marked */
    CHECK(frame->dirty_count == 0);
    /* a frame of a picture: the fill, its corners as they are, its edges and middle repeated between them; no pictures: a fill */
    dgx_ge_dialog_t other;
    blob = dgx_ge_pack_blob(&pack, TEST_BLOB_OTHER, &n);
    CHECK(blob && dgx_ge_dialog_open(&other, blob, n));
    /* a talk started after the view's play this frame: nothing shown until played (not the fields of the talk before, in its style, at its place) */
    dgx_ge_talk_t fresh;
    dgx_ge_talk_start(&fresh, &other, NULL, TEST_STYLE_BOXED, 1000, NULL, NULL);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_talk_view_show(&v, &fresh, scene, 1);
    CHECK(scene->actor_count == 0 && v.style_number >= 0);
    dgx_ge_talk_view_free(&v);
    dgx_ge_talk_view_init(&v, styles, styles_size, frame, fonts, 1);
    dgx_ge_talk_view_pictures(&v, &pack, maps);
    dgx_ge_talk_start(&t, &other, NULL, TEST_STYLE_BOXED, 1000, NULL, NULL);
    dgx_ge_talk_view_play(&v, &t, &strings, 1000);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_talk_view_show(&v, &t, scene, 1);
    CHECK(v.frame_pieces && scene->actor_count == 11 && (scene->actors[0].flags & DGX_GE_ITEM_FILL) && dgx_ge_rect_equal(scene->actors[0].clip, v.box));
    CHECK(v.box.x == 10 && v.box.y == 20 && v.box.w == 40 && v.box.h == 30);
    const dgx_ge_item_t *pc = &scene->actors[1];
    CHECK(pc[0].image == &v.frame_pieces[0] && pc[0].x == 10 && pc[0].y == 20 && !(pc[0].flags & DGX_GE_ITEM_REPEAT) && pc[0].map == hero_map);
    CHECK((pc[1].flags & DGX_GE_ITEM_REPEAT) && pc[1].x == 11 && dgx_ge_rect_equal(pc[1].clip, dgx_ge_rect(11, 20, 38, 1)));   /* the top edge */
    CHECK(!(pc[2].flags & DGX_GE_ITEM_REPEAT) && pc[2].x == 49 && pc[2].y == 20);                                            /* top right */
    CHECK((pc[4].flags & DGX_GE_ITEM_REPEAT) && dgx_ge_rect_equal(pc[4].clip, dgx_ge_rect(11, 21, 38, 28)) && v.frame_pieces[4].width == 2);
    CHECK(pc[8].x == 49 && pc[8].y == 49 && !(pc[8].flags & DGX_GE_ITEM_REPEAT));                                            /* bottom right */
    CHECK(scene->actors[10].image == &v.line.image && scene->actors[10].x == 13 && scene->actors[10].y == 23);
    dgx_ge_talk_view_pictures(&v, NULL, NULL);
    dgx_ge_talk_view_play(&v, &t, &strings, 1100);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_talk_view_show(&v, &t, scene, 1);
    CHECK(!v.frame_pieces && scene->actor_count == 3 && scene->actors[2].image == &v.line.image); /* border, fill, the line: laid again */
    /* in pixels of a display three times the frame: the frame of a picture still pictures (not a border), its pieces three times larger at their larger places */
    {
        dgx_screen_t   *big3   = dgx_vscreen_init(960, 600, 16, DgxScreenRGB);
        dgx_ge_frame_t *frame3 = dgx_ge_frame_create(big3, 0, 0, 320, 200);
        dgx_ge_scene_t *scene3 = dgx_ge_scene_create(frame3, 2, 16, 16);
        CHECK(dgx_ge_frame_scale(frame3, 3));
        dgx_ge_scene_layer_pixels(scene3, 1, true);
        dgx_ge_talk_view_t v3;
        dgx_ge_talk_view_init(&v3, styles, styles_size, frame3, fonts, 1);
        dgx_ge_talk_view_pixels(&v3, true, NULL);
        dgx_ge_talk_view_pictures(&v3, &pack, maps);
        dgx_ge_talk_t t3;
        dgx_ge_talk_start(&t3, &other, NULL, TEST_STYLE_BOXED, 1000, NULL, NULL);
        dgx_ge_talk_view_play(&v3, &t3, &strings, 1000);
        dgx_ge_scene_actors_begin(scene3);
        dgx_ge_talk_view_show(&v3, &t3, scene3, 1);
        CHECK(v3.frame_pieces && v3.style.frame == 2 && v3.box.x == 30 && v3.box.y == 60 && v3.box.w == 120 && v3.box.h == 90);
        const dgx_ge_item_t *p3 = &frame3->overlays[1];
        CHECK(frame3->overlay_count == 11 && p3[0].image == &v3.frame_pieces[0] && p3[0].x == 30 && p3[0].y == 60 && p3[0].scale == 3);
        CHECK((p3[1].flags & DGX_GE_ITEM_REPEAT) && p3[1].x == 33 && dgx_ge_rect_equal(p3[1].clip, dgx_ge_rect(33, 60, 114, 3)) && p3[1].scale == 3);
        CHECK(p3[2].x == 147 && p3[2].y == 60 && p3[8].x == 147 && p3[8].y == 147 && dgx_ge_rect_equal(p3[4].clip, dgx_ge_rect(33, 63, 114, 84)));
        dgx_ge_talk_view_free(&v3);
        dgx_ge_scene_destroy(&scene3);
        dgx_ge_frame_destroy(&frame3);
        dgx_screen_destroy(&big3);
    }
    CHECK(v.line.canvas && v.line.lines >= 1 && v.line.text && !strcmp(v.line.text, "Thanks.")); /* laid anew in the fields of the style taken again */
    dgx_ge_talk_view_pictures(&v, &pack, maps);

    /* a portrait: of the one speaking, in the top-left corner inside the padding, cut to its room; the text beside it; none for one without */
    dgx_ge_dialog_t faces;
    blob = dgx_ge_pack_blob(&pack, TEST_BLOB_FACES, &n);
    CHECK(blob && dgx_ge_dialog_open(&faces, blob, n) && dgx_ge_dialog_role_face(&faces, 0) == TEST_IMG_FACE && dgx_ge_dialog_role_face(&faces, 1) < 0);
    dgx_ge_talk_start(&t, &faces, NULL, 0, 2000, NULL, NULL);
    dgx_ge_talk_view_play(&v, &t, &strings, 2000);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_talk_view_show(&v, &t, scene, 1);
    CHECK(v.style.portrait == 1 && v.style.portrait_w == 6 && v.line.width == 48 && v.face_id == TEST_IMG_FACE);
    CHECK(scene->actor_count == 4 && scene->actors[2].image == &v.face && scene->actors[2].map == hero_map); /* border, fill, portrait, line */
    CHECK(scene->actors[2].x == 2 && scene->actors[2].y == 2 + 1 && dgx_ge_rect_equal(scene->actors[2].clip, dgx_ge_rect(2, 2, 6, 26)));
    CHECK(scene->actors[3].image == &v.line.image && scene->actors[3].x == 2 + 6 + 2);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 2100); /* the kid chooses: no face */
    dgx_ge_talk_view_play(&v, &t, &strings, 2100);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_talk_view_show(&v, &t, scene, 1);
    CHECK(v.face_id == -1 && scene->actor_count == 4 && scene->actors[3].image == &v.answers.image && scene->actors[3].x == 10); /* border, fill, bar, answers */

    /* a soft pen: the line and the answers on one canvas of amounts of ink, drawn through 15 shades from the fill to the text */
    dgx_ge_talk_view_free(&v);
    dgx_font_t *const two_fonts[2] = {TerminusTTFMedium12(), font0ss()};
    dgx_ge_talk_view_init(&v, styles, styles_size, frame, two_fonts, 2);
    dgx_ge_talk_start(&t, &other, NULL, TEST_STYLE_INKED, 3000, NULL, NULL);
    dgx_ge_talk_view_play(&v, &t, &strings, 3000);
    CHECK(v.style.soft && v.line.bits == 8 && v.answers.image.data == v.line.image.data && v.name.canvas == NULL);
    CHECK(frame->rgb[v.ink[0]][0] == (200 * 14 + 40) / 15 && frame->rgb[v.ink[14]][0] == 40 && frame->rgb[v.ink[14]][1] == 20);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_talk_view_show(&v, &t, scene, 1);
    CHECK(scene->actors[2].image == &v.line.image && scene->actors[2].map == v.ink);
    dgx_ge_talk_start(&t, &other, NULL, TEST_STYLE_PIXELS_SOFT, 3100, NULL, NULL); /* a font of pixels: 1 bit, no shades */
    dgx_ge_talk_view_play(&v, &t, &strings, 3100);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_talk_view_show(&v, &t, scene, 1);
    CHECK(v.style.soft && v.line.bits == 1 && scene->actors[2].map == NULL);
    dgx_ge_talk_view_free(&v);
    dgx_ge_talk_view_init(&v, styles, styles_size, frame, fonts, 1);
    dgx_ge_talk_view_pictures(&v, &pack, maps);

    /* no pack given: no pictures */
    dgx_ge_talk_view_pictures(&v, NULL, NULL);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 300);
    dgx_ge_talk_view_play(&v, &t, &strings, 300);
    CHECK(v.control_ids[0] == -1 && v.control_ids[1] == -1);
    dgx_ge_talk_view_free(&v);

    dgx_ge_scene_destroy(&scene);
    dgx_ge_frame_destroy(&frame);
    display->destroy(&display);
    dgx_ge_kinds_free(&kinds);
    free(data);
    CHECK_DONE();
}
