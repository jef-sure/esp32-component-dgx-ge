#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "dgx_ge_talk_view.h"

#define SCROLL_W 3 /* points: the bar beside more answers than lines, inside the frame's right edge */
#define SCROLL_GAP 1 /* points between the bar and the border: a bar of its own, not a thicker border */

static inline uint16_t u16_at(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

void dgx_ge_style_default(dgx_ge_style_t *s)
{
    *s = (dgx_ge_style_t){.frame = 1, .picture = 0xFFFF, .x = 4, .y = -4, .w = -8, .h = 44, .pad = {6, 4, 6, 4}, .font = 0, .scale_max = 1,
                          .scale_min = 1, .scroll_ms = 1200, .fill = {16, 16, 48}, .border_width = 1, .border = {200, 200, 220},
                          .text = {240, 240, 240}, .bar = {64, 64, 140}, .name = {255, 220, 120}};
}

static inline uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void dgx_ge_style_of(const uint8_t *blob, size_t size, int number, dgx_ge_style_t *s)
{
    dgx_ge_style_default(s);
    if (!blob || size < 8 || u32_at(blob) != DGX_GE_STYLES_MAGIC || u16_at(blob + 4) != DGX_GE_STYLES_VERSION) return;
    if (number < 0 || number >= u16_at(blob + 6) || 8u + (size_t)(number + 1) * DGX_GE_STYLE_BYTES > size) return;
    const uint8_t *p = blob + 8 + number * DGX_GE_STYLE_BYTES;
    if (p[0] == 0xFF) return; /* named, never made: the default */
    s->frame   = p[0];
    s->appear  = p[1] & 0x7F, s->soft = (p[1] & 0x80) != 0;
    s->picture = u16_at(p + 2);
    memcpy(s->inset, p + 4, 4);
    s->x = (int16_t)u16_at(p + 8), s->y = (int16_t)u16_at(p + 10), s->w = (int16_t)u16_at(p + 12), s->h = (int16_t)u16_at(p + 14);
    memcpy(s->pad, p + 16, 4);
    s->font = p[20], s->scale_max = p[21], s->scale_min = p[22], s->important = p[23] != 0;
    s->scroll_ms = u16_at(p + 24), s->wait_ms = u16_at(p + 26);
    memcpy(s->fill, p + 28, 3), s->border_width = p[31];
    memcpy(s->border, p + 32, 3), memcpy(s->text, p + 36, 3), memcpy(s->bar, p + 40, 3), memcpy(s->name, p + 44, 3);
    s->letter_ms = p[35], s->show_name = (p[47] & 1) != 0, s->align = p[47] >> 1 & 3;
    s->portrait = p[39] <= 2 ? p[39] : 0, s->portrait_w = p[43];
}

void dgx_ge_talk_view_init(dgx_ge_talk_view_t *v, const uint8_t *styles, size_t styles_size, dgx_ge_frame_t *frame, dgx_font_t *const *fonts,
                           int font_count)
{
    memset(v, 0, sizeof(*v));
    v->styles = styles, v->styles_size = styles_size, v->frame = frame, v->fonts = fonts, v->font_count = font_count;
    v->style_number = -1;
    v->state        = DGX_GE_TALK_OVER;
    v->face_id      = -1;
    for (int i = 0; i < DGX_GE_TALK_VIEW_CONTROLS; ++i) v->control_ids[i] = -1;
}

void dgx_ge_talk_view_pictures(dgx_ge_talk_view_t *v, const dgx_ge_pack_t *pack, const uint8_t *const *maps)
{
    v->pack = pack, v->maps = maps;
    v->style_number = -1; /* the style taken anew: a frame of a picture with them */
}

/* The pictures of the controls of the lines of answers seen, as their values are now. */
static void take_controls(dgx_ge_talk_view_t *v, const dgx_ge_talk_t *t)
{
    for (int row = 0; row < DGX_GE_TALK_VIEW_CONTROLS; ++row) {
        int image;
        v->control_ids[row] = -1;
        if (!v->pack || row >= v->answers.shown || !dgx_ge_talk_control(t, v->answers.top + row, &image) || image < 0) continue;
        if (dgx_ge_pack_image(v->pack, image, &v->control_images[row])) v->control_ids[row] = (int16_t)image;
    }
    v->changes = t->changes;
}

void dgx_ge_talk_view_free(dgx_ge_talk_view_t *v)
{
    dgx_ge_textbox_free(&v->line);
    dgx_ge_textbox_free(&v->answers);
    dgx_ge_textbox_free(&v->name);
    free(v->frame_pieces);
    v->frame_pieces = NULL;
    v->style_number = -1;
}

void dgx_ge_talk_view_pixels(dgx_ge_talk_view_t *v, bool pixels, const bool *fonts_made)
{
    v->pixels       = pixels;
    v->fonts_made   = fonts_made;
    v->style_number = -1; /* laid anew */
}

static void place_widgets(dgx_ge_talk_view_t *v, const dgx_ge_talk_t *t);

/* What the view marks of the frame: a rectangle of its own (points, or pixels of the display when it is laid in them). */
static void mark(const dgx_ge_talk_view_t *v, dgx_ge_rect_t r)
{
    if (v->pixels) dgx_ge_frame_invalidate_pixels(v->frame, r);
    else dgx_ge_frame_invalidate(v->frame, r);
}

/*
 * A style taken: its place on the screen, its colors added to the frame, its fields of text made as large as it says;
 * laid in pixels of the display, every point of it is the frame's scale of pixels (the style's numbers stay points).
 */
static bool take_style(dgx_ge_talk_view_t *v, int number)
{
    dgx_ge_style_of(v->styles, v->styles_size, number, &v->style);
    dgx_ge_style_t *s = &v->style;
    const int u = v->pixels ? v->frame->scale : 1;
    v->unit     = (uint8_t)u;
    if (u > 1) {
        s->x *= u, s->y *= u, s->w *= u, s->h *= u;
        for (int i = 0; i < 4; ++i) s->pad[i] = (uint8_t)(s->pad[i] * u);
        s->border_width = (uint8_t)(s->border_width * u);
        s->portrait_w   = (uint8_t)(s->portrait_w * u); /* its pictures (frame, portrait, controls) drawn u times larger */
    }
    const int fw = dgx_ge_frame_width(v->frame) * u, fh = dgx_ge_frame_height(v->frame) * u;
    const int w = s->w > 0 ? s->w : fw + s->w, h = s->h > 0 ? s->h : fh + s->h;
    const int x = s->x >= 0 ? s->x : fw + s->x - w, y = s->y >= 0 ? s->y : fh + s->y - h;
    v->box = dgx_ge_rect(x + (v->pixels ? v->frame->display_x : 0), y + (v->pixels ? v->frame->display_y : 0), w, h);
    const uint8_t rgb[5][3] = {{s->fill[0], s->fill[1], s->fill[2]}, {s->border[0], s->border[1], s->border[2]}, {s->text[0], s->text[1], s->text[2]},
                               {s->bar[0], s->bar[1], s->bar[2]}, {s->name[0], s->name[1], s->name[2]}};
    if (!dgx_ge_frame_add_colors(v->frame, rgb, 5, v->colors)) ESP_LOGW("dgx-ge", "talk view: no room for the colors of a style, the nearest taken");
    dgx_font_t *font = v->font_count > 0 ? v->fonts[s->font < v->font_count ? s->font : 0] : NULL;
    /* the scale of the text in pixels: a hand pen (its scale is its height) times the unit; a font of pixels too, unless made for the display */
    const bool made = v->fonts_made && v->font_count > 0 && v->fonts_made[s->font < v->font_count ? s->font : 0];
    if (u > 1 && (!made || (font && font->f_type == DGX_FONT_HW)))
        s->scale_max = (uint8_t)(s->scale_max * u), s->scale_min = (uint8_t)(s->scale_min * u);
    if (s->soft && font && font->f_type == DGX_FONT_HW) {
        /* the shades of the ink: from the fill (none) to the text (all of it) */
        uint8_t shades[DGX_GE_TEXT_INK][3];
        for (int i = 0; i < DGX_GE_TEXT_INK; ++i)
            for (int c = 0; c < 3; ++c) shades[i][c] = (uint8_t)((s->fill[c] * (DGX_GE_TEXT_INK - 1 - i) + s->text[c] * (i + 1)) / DGX_GE_TEXT_INK);
        if (!dgx_ge_frame_add_colors(v->frame, shades, DGX_GE_TEXT_INK, v->ink)) ESP_LOGW("dgx-ge", "talk view: no room for the shades of a soft pen, the nearest taken");
    }
    v->text_style    = (dgx_ge_text_style_t){font, s->scale_max, s->scale_min, s->scroll_ms, s->align};
    v->answer_style  = (dgx_ge_text_style_t){font, s->scale_min ? s->scale_min : 1, s->scale_min ? s->scale_min : 1, 0, s->align};
    /* the name a line above the text, when the style shows it */
    const int nh = s->show_name && font ? dgx_ge_text_line(font, s->scale_min ? s->scale_min : 1) : 0;
    const int tw = w - s->pad[0] - s->pad[2] - (s->portrait ? s->portrait_w + s->pad[0] : 0), th = h - s->pad[1] - s->pad[3] - nh;
    dgx_ge_textbox_free(&v->line);
    dgx_ge_textbox_free(&v->answers);
    dgx_ge_textbox_free(&v->name);
    /* a frame of a picture: its nine pictures, while the style is taken */
    free(v->frame_pieces);
    v->frame_pieces = NULL;
    if (s->frame == 2 && v->pack && (v->frame_pieces = malloc(9 * sizeof(dgx_image_t))))
        for (int i = 0; i < 9 && v->frame_pieces; ++i)
            if (!dgx_ge_pack_image(v->pack, s->picture + i, &v->frame_pieces[i])) free(v->frame_pieces), v->frame_pieces = NULL;
    v->style_number = number, v->name_string = -1;
    v->state        = 0xFF; /* the fields are new: laid anew whatever is shown */
    /* the line and the answers are never shown at once: one canvas */
    /* a font of pixels: 1 bit a point; a soft pen (a handwritten font): amounts of ink, 8 bits */
    const bool soft = s->soft && font && font->f_type == DGX_FONT_HW;
    /* the line and the answers of any number of lines (an important line is whole, however long); the name one */
    if (!font || tw <= 0 || th <= 0 || !dgx_ge_textbox_init_bits(&v->line, tw, th, soft ? 8 : 1, 0)) return false;
    if (!dgx_ge_textbox_share(&v->answers, &v->line, 0)) return false;
    return !nh || dgx_ge_textbox_init(&v->name, tw, nh, 1);
}

/* Where the line (or the answers: the same place) is on the screen: inside the padding, under the name. */
static dgx_ge_rect_t text_place(const dgx_ge_talk_view_t *v)
{
    const dgx_ge_rect_t b = v->box;
    return dgx_ge_rect(b.x + v->style.pad[0], b.y + v->style.pad[1] + (v->name.canvas ? v->name.height : 0), v->line.width, v->line.height);
}

bool dgx_ge_talk_view_play(dgx_ge_talk_view_t *v, dgx_ge_talk_t *t, const dgx_ge_strings_t *strings, uint32_t now_ms)
{
    if (t->state == DGX_GE_TALK_OVER) {
        v->state = DGX_GE_TALK_OVER;
        return true;
    }
    if (t->style != v->style_number && !take_style(v, t->style)) return false;
    /* the pictures of the fields change in place: what changed is marked for the frame to draw anew (the scene sees only places and pictures) */
    bool changed = false;
    /* a new line or a new choice: laid anew; a line without a time of its own waits as long as its style says */
    /* the numbers of a line, as one (the same line said again with others is laid anew) */
    int16_t said = t->number;
    for (int i = 1; i < t->count; ++i) said = (int16_t)(said * 31 + t->more[i - 1]);
    if (t->state != v->state || t->at != v->at || t->dialog != v->dialog || t->shown != v->shown || (t->forms && said != v->said_number)) {
        v->dialog = t->dialog, v->state = t->state, v->at = t->at, v->shown = t->shown, v->pick = 0xFF, v->said_number = said;
        changed   = true;
        if (t->state == DGX_GE_TALK_SAYING) {
            if (!t->wait_ms) t->wait_ms = v->style.wait_ms;
            int16_t numbers[DGX_GE_TALK_NUMBERS] = {t->number};
            for (int i = 1; i < t->count; ++i) numbers[i] = t->more[i - 1];
            const char *text = t->forms ? dgx_ge_strings_numbers(strings, t->string, t->forms, numbers, t->count ? t->count : 1, v->said, sizeof v->said)
                                        : dgx_ge_string(strings, t->string);
            dgx_ge_textbox_set(&v->line, text, &v->text_style, t->important || v->style.important, now_ms);
            dgx_ge_textbox_appear(&v->line, v->style.appear == 1 ? v->style.letter_ms : 0, now_ms);
        }
        /* the portrait of the one speaking, or choosing, if the style shows one */
        const int face = v->style.portrait && v->pack ? dgx_ge_dialog_role_face(t->dialog, t->role) : -1;
        v->face_id     = (int16_t)(face >= 0 && dgx_ge_pack_image(v->pack, face, &v->face) ? face : -1);
        /* the name of the one speaking, or choosing */
        const int name = v->name.canvas ? dgx_ge_dialog_role_name(t->dialog, t->role) : -1;
        if (name != v->name_string) {
            v->name_string = name;
            dgx_ge_textbox_set(&v->name, name >= 0 ? dgx_ge_string(strings, name) : "", &v->answer_style, false, now_ms);
        }
    }
    if (t->state == DGX_GE_TALK_CHOOSING && t->pick != v->pick) {
        /* the answers, a line each; laid again only when they change */
        static char all[512];
        int         n = 0;
        uint16_t    id;
        all[0] = 0;
        for (int i = 0; dgx_ge_talk_answer(t, i, &id); ++i) {
            const char *a = dgx_ge_string(strings, id);
            if (n + (int)strlen(a) + 2 >= (int)sizeof all) break;
            if (i) all[n++] = '\n';
            strcpy(all + n, a);
            n += (int)strlen(a);
        }
        /* a list: all of it, never cut; the lines shown follow the answer chosen */
        if (v->pick == 0xFF) dgx_ge_textbox_set(&v->answers, all, &v->answer_style, true, now_ms);
        changed = true;
        v->pick = t->pick;
        if (t->pick < v->answers.top) dgx_ge_textbox_scroll(&v->answers, t->pick - v->answers.top);
        if (t->pick >= v->answers.top + v->answers.shown) dgx_ge_textbox_scroll(&v->answers, t->pick - (v->answers.top + v->answers.shown) + 1);
        take_controls(v, t);
    } else if (t->state == DGX_GE_TALK_CHOOSING && t->changes != v->changes) {
        /* a control changed a value: its picture */
        take_controls(v, t);
        changed = true;
    }
    if (changed) mark(v, v->box);
    else if (t->state == DGX_GE_TALK_SAYING && dgx_ge_textbox_play(&v->line, now_ms)) mark(v, text_place(v)); /* a letter more, a line up: the text alone */
    if (changed || (t->state == DGX_GE_TALK_CHOOSING && (v->widgets.count < 2 || v->widget_store[1].value != t->pick))) place_widgets(v, t);
    dgx_ge_talk_play(t, now_ms, t->state != DGX_GE_TALK_SAYING || dgx_ge_textbox_at_end(&v->line));
    return true;
}

/* A widget of the view worked by the pointer: the line, an answer, a control: the events of the talk it means. */
static void worked(dgx_ge_widget_t *w, int what, void *ctx)
{
    dgx_ge_talk_view_t *v = ctx;
    dgx_ge_talk_t      *t = v->talk;
    if (!t || what == DGX_GE_WIDGET_FOCUS) return;
    if (w->kind == DGX_GE_WIDGET_LIST) {
        /* the row under the pointer chosen: the choice moved there; taken when let go */
        for (int k = 0; k < 16 && t->pick != w->value; ++k) dgx_ge_talk_event(t, t->pick < w->value ? DGX_GE_UI_NEXT : DGX_GE_UI_PREV, v->now_ms);
        if (what == DGX_GE_WIDGET_ACTIVATED) dgx_ge_talk_view_event(v, t, DGX_GE_UI_ACTIVATE, v->now_ms);
    } else if (w->kind == DGX_GE_WIDGET_SLIDER && (w->flags & DGX_GE_WIDGET_SWIPE) && what == DGX_GE_WIDGET_ACTIVATED) {
        /* a tap on a slider's row: the row chosen, as a tap on any row; its value as it is */
        for (int k = 0; k < 16 && t->pick != w->min_row; ++k) dgx_ge_talk_event(t, t->pick < w->min_row ? DGX_GE_UI_NEXT : DGX_GE_UI_PREV, v->now_ms);
    } else if ((w->kind == DGX_GE_WIDGET_SLIDER || w->kind == DGX_GE_WIDGET_SWITCH) && what == DGX_GE_WIDGET_CHANGED) {
        /* a control with a range put at a place: its answer chosen, then its own events (more, less) until it is there; its value as it came out */
        for (int k = 0; k < 16 && t->pick != w->min_row; ++k) dgx_ge_talk_event(t, t->pick < w->min_row ? DGX_GE_UI_NEXT : DGX_GE_UI_PREV, v->now_ms);
        int value, least, most, step;
        if (w->kind == DGX_GE_WIDGET_SWITCH) {
            /* two values: the control's own activate flips it (and takes its answer, as the button does) */
            if (dgx_ge_talk_control_range(t, t->pick, &value, &least, &most, &step) && value != w->value) dgx_ge_talk_view_event(v, t, DGX_GE_UI_ACTIVATE, v->now_ms);
        } else {
            for (int k = 0; k < 64 && dgx_ge_talk_control_range(t, t->pick, &value, &least, &most, &step) && value != w->value; ++k)
                dgx_ge_talk_event(t, value < w->value ? DGX_GE_UI_MORE : DGX_GE_UI_LESS, v->now_ms);
        }
        if (dgx_ge_talk_control_range(t, t->pick, &value, &least, &most, &step)) w->value = (int16_t)value;
    } else if (what == DGX_GE_WIDGET_ACTIVATED) {
        /* the line: on; a control: the choice moved to its answer, then worked as activate (the control's own) */
        if (t->state == DGX_GE_TALK_CHOOSING)
            for (int k = 0; k < 16 && t->pick != w->value; ++k) dgx_ge_talk_event(t, t->pick < w->value ? DGX_GE_UI_NEXT : DGX_GE_UI_PREV, v->now_ms);
        dgx_ge_talk_view_event(v, t, DGX_GE_UI_ACTIVATE, v->now_ms);
    }
}

/* Where a control of a row is drawn (its picture's box), as show() puts it; false when the row has none. */
static bool control_box(const dgx_ge_talk_view_t *v, const dgx_ge_talk_t *t, int row, int ty, int lh, dgx_ge_rect_t *box)
{
    if (row >= DGX_GE_TALK_VIEW_CONTROLS || v->control_ids[row] < 0) return false;
    const dgx_ge_style_t *s   = &v->style;
    const dgx_image_t    *img = &v->control_images[row];
    const int             right = v->box.x + v->box.w - s->pad[2] - (s->portrait == 2 ? s->portrait_w + s->pad[0] : 0), u = v->unit;
    *box = dgx_ge_rect(right - img->width * u, ty + row * lh + (lh - img->height * u) / 2, img->width * u, img->height * u);
    (void)t;
    return true;
}

/* The widgets anew: what answers to a pointer where the fields are now (hit only; the view draws). */
static void place_widgets(dgx_ge_talk_view_t *v, const dgx_ge_talk_t *t)
{
    /* the pointer down on one of them (a swipe going on): kept on the same one laid anew, with where it went down */
    const dgx_ge_widgets_t was     = v->widgets;
    const bool             held    = was.widgets && was.pressed >= 0 && was.pressed < was.count;
    const dgx_ge_widget_t  pressed = held ? v->widget_store[was.pressed] : (dgx_ge_widget_t){0};
    dgx_ge_widgets_init(&v->widgets, v->widget_store, (int)(sizeof v->widget_store / sizeof v->widget_store[0]), 1);
    const int root = dgx_ge_widgets_add(&v->widgets, -1, &(dgx_ge_widget_t){.kind = DGX_GE_WIDGET_BOX});
    const dgx_ge_rect_t place = text_place(v);
    if (t->state == DGX_GE_TALK_SAYING) {
        dgx_ge_widgets_add(&v->widgets, root, &(dgx_ge_widget_t){.kind = DGX_GE_WIDGET_BUTTON, .at_x = 0, .at_y = 0, .want_w = (int16_t)v->box.w,
                                                                 .want_h = (int16_t)v->box.h, .on = worked, .ctx = v});
    } else if (t->state == DGX_GE_TALK_CHOOSING) {
        const int lh = v->answer_style.font ? dgx_ge_text_line(v->answer_style.font, v->answers.scale) : 0;
        const int rows = v->answers.shown, list_h = rows > 0 && lh > 0 ? rows * lh : place.h;
        /* all the answers, the rows seen counted from the top line of their text: swiped up or down, the choice goes along */
        dgx_ge_widgets_add(&v->widgets, root, &(dgx_ge_widget_t){.kind = DGX_GE_WIDGET_LIST, .at_x = (int16_t)(place.x - v->box.x), .at_y = (int16_t)(place.y - v->box.y),
                                                                 .want_w = (int16_t)place.w, .want_h = (int16_t)list_h, .text = &v->answers, .min = 0,
                                                                 .max = (int16_t)(t->shown > 0 ? t->shown - 1 : 0), .value = (int16_t)t->pick, .on = worked, .ctx = v});
        for (int row = 0; row < rows; ++row) {
            dgx_ge_rect_t b;
            if (!control_box(v, t, row, place.y, lh, &b)) continue;
            /*
             * with a range: a switch (two values) or a slider over its picture, put at a place by the pointer, and a
             * slider's whole row too, swiped (its picture may be too small to aim at; on it, the picture's own wins:
             * the narrower); without a range: a button, worked as activate
             */
            int value, least, most, step;
            const bool    ranged = dgx_ge_talk_control_range(t, v->answers.top + row, &value, &least, &most, &step);
            const uint8_t kind   = !ranged ? DGX_GE_WIDGET_BUTTON : most - least <= step ? DGX_GE_WIDGET_SWITCH : DGX_GE_WIDGET_SLIDER;
            /* over its picture across, the row down (less the gap between rows): a finger a little above or below it still hits */
            const int       gap = lh / DGX_GE_WIDGET_ROW_GAP;
            dgx_ge_widget_t w   = {.kind = kind, .at_x = (int16_t)(b.x - v->box.x), .at_y = (int16_t)(place.y + row * lh + gap - v->box.y),
                                   .want_w = (int16_t)b.w, .want_h = (int16_t)(lh - 2 * gap), .value = (int16_t)(ranged ? value : v->answers.top + row), .min = (int16_t)(ranged ? least : 0),
                                 .max = (int16_t)(ranged ? most : 0), .step = (int16_t)(ranged ? step : 0),
                                 .min_row = (int16_t)(v->answers.top + row), .on = worked, .ctx = v};
            dgx_ge_widgets_add(&v->widgets, root, &w);
            if (kind == DGX_GE_WIDGET_SLIDER) {
                w.flags = DGX_GE_WIDGET_SWIPE, w.at_x = (int16_t)(place.x - v->box.x), w.want_w = (int16_t)place.w;
                dgx_ge_widgets_add(&v->widgets, root, &w);
            }
        }
    }
    dgx_ge_widgets_layout(&v->widgets, v->box);
    for (int i = 0; held && i < v->widgets.count; ++i) {
        const dgx_ge_widget_t *w = &v->widget_store[i];
        if (w->kind != pressed.kind || w->flags != (pressed.flags & ~DGX_GE_WIDGET_FOCUSED) || w->min_row != pressed.min_row) continue;
        v->widgets.pressed = (int8_t)i, v->widgets.press_x = was.press_x, v->widgets.press_y = was.press_y;
        v->widgets.press_value = was.press_value, v->widgets.dragged = was.dragged;
        break;
    }
}

bool dgx_ge_talk_view_pointer(dgx_ge_talk_view_t *v, dgx_ge_talk_t *t, const dgx_ge_pointer_t *p, uint32_t now_ms)
{
    if (t->state == DGX_GE_TALK_OVER || v->style_number < 0 || !v->widgets.count) return false;
    v->talk = t, v->now_ms = now_ms;
    const bool taken = dgx_ge_widgets_pointer(&v->widgets, p);
    v->talk = NULL;
    return taken;
}

void dgx_ge_talk_view_event(dgx_ge_talk_view_t *v, dgx_ge_talk_t *t, dgx_ge_ui_t event, uint32_t now_ms)
{
    if (t->state == DGX_GE_TALK_SAYING && (event == DGX_GE_UI_ACTIVATE || event == DGX_GE_UI_NEXT) && dgx_ge_textbox_appear_all(&v->line)) {
        mark(v, text_place(v));
        return;
    }
    dgx_ge_talk_event(t, event, now_ms);
}

void dgx_ge_talk_view_show(const dgx_ge_talk_view_t *v, const dgx_ge_talk_t *t, dgx_ge_scene_t *scene, uint8_t layer)
{
    /* only a talk the view has played: a talk started after the view's play this frame waits (its style and text are not laid yet) */
    if (t->state == DGX_GE_TALK_OVER || v->state == DGX_GE_TALK_OVER || v->dialog != t->dialog || v->style_number < 0) return;
    const dgx_ge_style_t *s = &v->style;
    const dgx_ge_rect_t   b = v->box;
    if (s->frame == 2 && v->frame_pieces) {
        /* a frame of a picture: the fill under it, its corners where they are, its edges and its middle laid again and again between them */
        const dgx_image_t *p = v->frame_pieces;
        const int          u = v->unit, l = p[0].width * u, t = p[0].height * u, r = p[2].width * u, bt = p[6].height * u;
        const int          xs[3] = {b.x, b.x + l, b.x + b.w - r}, ws[3] = {l, b.w - l - r, r};
        const int          ys[3] = {b.y, b.y + t, b.y + b.h - bt}, hs[3] = {t, b.h - t - bt, bt};
        dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = b, .layer = layer, .flags = DGX_GE_ITEM_FILL, .color = v->colors[0]});
        for (int i = 0; i < 9; ++i) {
            const int set = dgx_ge_pack_image_set(v->pack, s->picture + i);
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &p[i], .map = v->maps && set >= 0 ? v->maps[set] : NULL, .x = (int16_t)xs[i % 3],
                                                       .y = (int16_t)ys[i / 3], .clip = dgx_ge_rect(xs[i % 3], ys[i / 3], ws[i % 3], hs[i / 3]),
                                                       .layer = layer, .flags = i == 0 || i == 2 || i == 6 || i == 8 ? 0 : DGX_GE_ITEM_REPEAT,
                                                       .color = v->colors[1], .scale = (uint8_t)u});
        }
    } else if (s->frame == 1 || s->frame == 2) {
        /* the frame: its border, then its fill inside it (a frame of a picture without its pictures: as this) */
        const int bw = s->border_width;
        if (bw) dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = b, .layer = layer, .flags = DGX_GE_ITEM_FILL, .color = v->colors[1]});
        dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = dgx_ge_rect(b.x + bw, b.y + bw, b.w - 2 * bw, b.h - 2 * bw), .layer = layer,
                                                   .flags = DGX_GE_ITEM_FILL, .color = v->colors[0]});
    }
    int tx = b.x + s->pad[0], ty = b.y + s->pad[1];
    if (s->portrait) {
        /* the portrait in the top corner of its side, cut to its room; the text beside it */
        const int px = s->portrait == 1 ? tx : b.x + b.w - s->pad[2] - s->portrait_w;
        if (v->face_id >= 0) {
            const int set = dgx_ge_pack_image_set(v->pack, v->face_id);
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &v->face, .map = v->maps && set >= 0 ? v->maps[set] : NULL,
                                                       .x = (int16_t)(px + v->face.origin_x * v->unit), .y = (int16_t)(ty + v->face.origin_y * v->unit),
                                                       .clip = dgx_ge_rect(px, ty, s->portrait_w, b.h - s->pad[1] - s->pad[3]), .layer = layer,
                                                       .color = v->colors[2], .scale = v->unit});
        }
        if (s->portrait == 1) tx += s->portrait_w + s->pad[0];
    }
    if (v->name.canvas) {
        if (v->name_string >= 0)
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &v->name.image, .x = (int16_t)tx, .y = (int16_t)ty, .layer = layer, .color = v->colors[4]});
        ty += v->name.height;
    }
    if (t->state == DGX_GE_TALK_SAYING) {
        dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &v->line.image, .map = v->line.bits == 8 ? v->ink : NULL, .x = (int16_t)tx, .y = (int16_t)ty,
                                                   .layer = layer, .color = v->colors[2]});
    } else {
        /* the bar under the answer chosen (inside the frame), then the answers over it */
        const int lh = v->answer_style.font ? dgx_ge_text_line(v->answer_style.font, v->answers.scale) : 0, row = t->pick - v->answers.top;
        const int bw = s->frame ? s->border_width : 0;
        if (row >= 0 && row < v->answers.shown)
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = dgx_ge_rect(b.x + bw, ty + row * lh, b.w - 2 * bw, lh), .layer = layer,
                                                       .flags = DGX_GE_ITEM_FILL, .color = v->colors[3]});
        dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &v->answers.image, .map = v->answers.bits == 8 ? v->ink : NULL, .x = (int16_t)tx,
                                                   .y = (int16_t)ty, .layer = layer, .color = v->colors[2]});
        if (v->answers.lines > v->answers.shown && v->answers.shown > 0 && lh > 0) {
            /* more answers than lines: a bar at the right edge inside the frame, its knob as much and as far as the lines seen */
            const int h = v->answers.shown * lh, kh = h * v->answers.shown / v->answers.lines, ky = h * v->answers.top / v->answers.lines;
            const int sw = SCROLL_W * v->unit, bx = b.x + b.w - bw - SCROLL_GAP * v->unit - sw; /* points: times the unit in display pixels */
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = dgx_ge_rect(bx, ty, sw, h), .layer = layer, .flags = DGX_GE_ITEM_FILL, .color = v->colors[3]});
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = dgx_ge_rect(bx, ty + ky, sw, kh > 1 ? kh : 1), .layer = layer, .flags = DGX_GE_ITEM_FILL,
                                                       .color = v->colors[2]});
        }
        /* the controls: at the right of their lines (left of a portrait there), in the middle of each */
        const int right = b.x + b.w - s->pad[2] - (s->portrait == 2 ? s->portrait_w + s->pad[0] : 0);
        for (int row = 0; row < v->answers.shown && row < DGX_GE_TALK_VIEW_CONTROLS; ++row) {
            if (v->control_ids[row] < 0) continue;
            const dgx_image_t *img = &v->control_images[row];
            const int          set = dgx_ge_pack_image_set(v->pack, v->control_ids[row]), u = v->unit;
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = img, .map = v->maps && set >= 0 ? v->maps[set] : NULL,
                                                       .x = (int16_t)(right - (img->width - img->origin_x) * u),
                                                       .y = (int16_t)(ty + row * lh + (lh - img->height * u) / 2 + img->origin_y * u), .layer = layer,
                                                       .color = v->colors[2], .scale = (uint8_t)u});
        }
    }
}
