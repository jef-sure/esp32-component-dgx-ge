#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "dgx_ge_talk_view.h"

#define SCROLL_W 5 /* points: the bar beside more answers than lines, inside the frame's right edge (an arrow at each end, 5 x 3) */
#define SCROLL_GAP 1 /* points between the bar and the border (a bar of its own, not a thicker border), its arrows and its knob */
#define SCROLL_ROOM 2 /* points between the bar and the controls' pictures, the fields, the bar under the answer chosen */
#define BAR_REACH 10  /* points past the frame's right edge a tap still finds the scroll bar */
#define BLINK_MS 500 /* the caret of a field edited comes and goes so often */
#define FIELD_PAD 2 /* points from a field's edge to its text: its border, one point, and one more */

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

/* More answers than lines: the scroll bar shown. */
static bool scrolled(const dgx_ge_talk_view_t *v)
{
    return v->answers.lines > v->answers.shown && v->answers.shown > 0;
}

/*
 * The answers are laid a line each, a long one wrapped into more: a line begins an answer when a line break is before
 * it (the first line too), else it goes on with the one above.
 */
static bool begins(const dgx_ge_textbox_t *a, int line)
{
    return line <= 0 || a->text[a->starts[line] - 1] == '\n';
}

/* The answer a line of them shows (past the last line: the last answer's). */
static int answer_of(const dgx_ge_talk_view_t *v, int line)
{
    int n = 0;
    for (int l = 1; l <= line && l < v->answers.lines; ++l) n += begins(&v->answers, l);
    return n;
}

/* The first line of an answer, and (end) the line after its last. */
static int first_line(const dgx_ge_talk_view_t *v, int answer, int *end)
{
    const dgx_ge_textbox_t *a = &v->answers;
    int                     l = 0;
    for (int n = 0; n < answer && l < a->lines;)
        if (++l < a->lines && begins(a, l)) ++n;
    int e = l < a->lines ? l + 1 : l;
    while (e < a->lines && !begins(a, e)) ++e;
    if (end) *end = e;
    return l;
}

/* The answer a row of the lines seen begins (its first line), -1 when the row goes on with one above or is past them. */
static int row_answer(const dgx_ge_talk_view_t *v, int row)
{
    const int l = v->answers.top + row;
    return row >= 0 && l < v->answers.lines && begins(&v->answers, l) ? answer_of(v, l) : -1;
}

/* The lines seen moved so the answer chosen is seen, all of it if it goes in (else from its first line). */
static void follow_pick(dgx_ge_talk_view_t *v, int pick)
{
    int       end;
    const int first = first_line(v, pick, &end), top = v->answers.top, rows = v->answers.shown;
    if (first < top) dgx_ge_textbox_scroll(&v->answers, first - top);
    else if (end > top + rows) dgx_ge_textbox_scroll(&v->answers, (end - rows < first ? end - rows : first) - top);
}

/* Where the scroll bar stands: its left edge, inside the frame's right border (points, or pixels). */
static int bar_x(const dgx_ge_talk_view_t *v)
{
    const int bw = v->style.frame ? v->style.border_width : 0;
    return v->box.x + v->box.w - bw - (SCROLL_GAP + SCROLL_W) * v->unit;
}

/* The right edge of the answers' controls and fields: the padding (a portrait at the right left of it), left of the bar when it is shown. */
static int controls_right(const dgx_ge_talk_view_t *v)
{
    const dgx_ge_style_t *s     = &v->style;
    const int             right = v->box.x + v->box.w - s->pad[2] - (s->portrait == 2 ? s->portrait_w + s->pad[0] : 0);
    const int             room  = bar_x(v) - SCROLL_ROOM * v->unit;
    return scrolled(v) && room < right ? room : right;
}

/*
 * The scroll bar's parts along its height h (points, or pixels, from the top of the lines): the arrows' cells (ah, 0
 * without arrows: too short), the knob's place and length; as show() draws them.
 */
static void bar_parts(const dgx_ge_talk_view_t *v, int h, int *ah, int *ky, int *kh)
{
    const int sw = SCROLL_W * v->unit, gap = SCROLL_GAP * v->unit;
    *ah          = v->arrows[0].data && h > 3 * (sw + gap) ? v->arrows[0].height : 0;
    const int th = h - 2 * (*ah ? *ah + gap : 0);
    *kh = th * v->answers.shown / v->answers.lines, *ky = th * v->answers.top / v->answers.lines + (*ah ? *ah + gap : 0);
}

/*
 * The arrows of the scroll bar for a unit: a triangle as wide as the bar and half as high, pointing up and pointing down,
 * 1 bit (drawn in the text's color); its rows from the point to the base, the base the whole width.
 */
static void make_arrows(dgx_ge_talk_view_t *v)
{
    for (int i = 0; i < 2; ++i) free((void *)v->arrows[i].data), v->arrows[i] = (dgx_image_t){0};
    const int w = SCROLL_W * v->unit, stride = (w + 7) / 8, th = (w + 1) / 2; /* the triangle as high as half its base */
    for (int i = 0; i < 2; ++i) {
        uint8_t *bits = calloc((size_t)stride * th, 1);
        if (!bits) return;
        for (int r = 0; r < th; ++r) {
            /* the r-th row from the point: as wide as 2r+1 (an odd base) or 2r+2 (an even one), in the middle */
            const int span = (w & 1 ? 2 * r + 1 : 2 * r + 2), x0 = (w - span) / 2, y = i == 0 ? r : th - 1 - r;
            for (int x = x0; x < x0 + span && x < w; ++x) bits[y * stride + x / 8] |= (uint8_t)(0x80 >> (x % 8));
        }
        v->arrows[i] = (dgx_image_t){.width = (uint16_t)w, .height = (uint16_t)th, .format = DGX_IMAGE_1BPP, .data = bits};
    }
}

/* The fields given back, their boxes too (another style: another size). */
static void free_fields(dgx_ge_talk_view_t *v)
{
    if (!v->fields) return;
    for (int i = 0; i < DGX_GE_TALK_VIEW_FIELDS; ++i)
        if (v->fields->boxes[i]) dgx_ge_textbox_free(v->fields->boxes[i]), free(v->fields->boxes[i]);
    free(v->fields), v->fields = NULL;
}

/* The fields made, the first time a field is seen; false when there is no memory. */
static bool make_fields(dgx_ge_talk_view_t *v)
{
    if (!v->fields && (v->fields = calloc(1, sizeof *v->fields)))
        for (int i = 0; i < DGX_GE_TALK_VIEW_FIELDS; ++i) v->fields->rows[i] = -1;
    return v->fields != NULL;
}

/* Where the field of a row of answers is: the right half of the text's width, as tall as the row (points, or pixels). */
static dgx_ge_rect_t field_box(const dgx_ge_talk_view_t *v, int row)
{
    const dgx_ge_style_t *s     = &v->style;
    const int             lh    = v->answer_style.font ? dgx_ge_text_line(v->answer_style.font, v->answers.scale) : 0;
    const int             right = controls_right(v);
    const int             w     = v->line.width / 2;
    return dgx_ge_rect(right - w, v->box.y + s->pad[1] + (v->name.canvas ? v->name.height : 0) + row * lh, w, lh);
}

/* The text of a field into its box: the end of it, as much as goes in (the caret, typing, at the end of a long text). */
static void lay_field(dgx_ge_talk_view_t *v, dgx_ge_textbox_t *box, const char *text, uint32_t now_ms)
{
    dgx_ge_textbox_set(box, text, &v->field_style, false, now_ms);
    while (box->cut && *text) {
        do ++text;
        while ((*text & 0xC0) == 0x80); /* a character at a time: past its continuation bytes */
        dgx_ge_textbox_set(box, text, &v->field_style, false, now_ms);
    }
}

/* The fields of the lines of answers seen into their boxes (made as they are first needed): the one edited as typed. */
static void take_fields(dgx_ge_talk_view_t *v, const dgx_ge_talk_t *t, uint32_t now_ms)
{
    int used = 0;
    for (int row = 0; row < v->answers.shown && used < DGX_GE_TALK_VIEW_FIELDS; ++row) {
        const int             a = row_answer(v, row);
        const dgx_ge_field_t *f = a >= 0 ? dgx_ge_talk_field(t, a) : NULL;
        if (!f || !make_fields(v)) continue;
        if (!v->fields->boxes[used]) {
            const dgx_ge_rect_t b = field_box(v, row);
            const int           tw = b.w - 2 * FIELD_PAD * v->unit;
            dgx_ge_textbox_t   *box = calloc(1, sizeof *box);
            if (!box || tw <= 0 || !dgx_ge_textbox_init_bits(box, tw, b.h, v->line.bits, 1)) {
                free(box);
                break;
            }
            v->fields->boxes[used] = box;
        }
        v->fields->rows[used] = (int8_t)row;
        lay_field(v, v->fields->boxes[used], v->editing && a == t->pick ? v->fields->edit : f->text, now_ms);
        ++used;
    }
    for (int i = used; v->fields && i < DGX_GE_TALK_VIEW_FIELDS; ++i) v->fields->rows[i] = -1;
}

/* The caret of the field edited on or off: after its text, or not. */
static void caret(dgx_ge_talk_view_t *v, bool on, uint32_t now_ms)
{
    v->fields->edit[v->fields->edit_len] = on ? '_' : 0, v->fields->edit[v->fields->edit_len + 1] = 0;
    v->fields->caret = on, v->fields->blink_ms = now_ms;
}

/* The field edited laid anew (typed, the caret came or went), and marked. */
static void relay_edit(dgx_ge_talk_view_t *v, const dgx_ge_talk_t *t, uint32_t now_ms)
{
    for (int i = 0; v->fields && i < DGX_GE_TALK_VIEW_FIELDS; ++i) {
        if (v->fields->rows[i] < 0 || !v->fields->boxes[i] || row_answer(v, v->fields->rows[i]) != t->pick) continue;
        lay_field(v, v->fields->boxes[i], v->fields->edit, now_ms);
        const dgx_ge_rect_t b = field_box(v, v->fields->rows[i]);
        if (v->pixels) dgx_ge_frame_invalidate_pixels(v->frame, b);
        else dgx_ge_frame_invalidate(v->frame, b);
    }
}

void dgx_ge_talk_view_pictures(dgx_ge_talk_view_t *v, const dgx_ge_pack_t *pack, const uint8_t *const *maps)
{
    v->pack = pack, v->maps = maps;
    v->style_number = -1; /* the style taken anew: a frame of a picture with them */
}

/* The pictures of the controls of the lines of answers seen, as their values are now, and their fields. */
static void take_controls(dgx_ge_talk_view_t *v, const dgx_ge_talk_t *t, uint32_t now_ms)
{
    take_fields(v, t, now_ms);
    for (int row = 0; row < DGX_GE_TALK_VIEW_CONTROLS; ++row) {
        int image;
        v->control_ids[row] = -1;
        const int a = row < v->answers.shown ? row_answer(v, row) : -1;
        if (!v->pack || a < 0 || !dgx_ge_talk_control(t, a, &image) || image < 0) continue;
        if (dgx_ge_pack_image(v->pack, image, &v->control_images[row])) v->control_ids[row] = (int16_t)image;
    }
    v->changes = t->changes;
}

void dgx_ge_talk_view_free(dgx_ge_talk_view_t *v)
{
    dgx_ge_textbox_free(&v->line);
    dgx_ge_textbox_free(&v->answers);
    dgx_ge_textbox_free(&v->name);
    free(v->all), v->all = NULL;
    free(v->said), v->said = NULL;
    free_fields(v);
    for (int i = 0; i < 2; ++i) free((void *)v->arrows[i].data), v->arrows[i] = (dgx_image_t){0};
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
    /* the track of the scroll bar (and the border of a field): halfway between the fill and the border, unlike the fill and the bar */
    const uint8_t rgb[6][3] = {{s->fill[0], s->fill[1], s->fill[2]}, {s->border[0], s->border[1], s->border[2]}, {s->text[0], s->text[1], s->text[2]},
                               {s->bar[0], s->bar[1], s->bar[2]}, {s->name[0], s->name[1], s->name[2]},
                               {(uint8_t)((s->fill[0] + s->border[0]) / 2), (uint8_t)((s->fill[1] + s->border[1]) / 2), (uint8_t)((s->fill[2] + s->border[2]) / 2)}};
    if (!dgx_ge_frame_add_colors(v->frame, rgb, 6, v->colors)) ESP_LOGW("dgx-ge", "talk view: no room for the colors of a style, the nearest taken");
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
    v->field_style   = (dgx_ge_text_style_t){font, v->answer_style.scale_max, v->answer_style.scale_min, 0, DGX_GE_ALIGN_LEFT};
    /* the name a line above the text, when the style shows it */
    const int nh = s->show_name && font ? dgx_ge_text_line(font, s->scale_min ? s->scale_min : 1) : 0;
    const int tw = w - s->pad[0] - s->pad[2] - (s->portrait ? s->portrait_w + s->pad[0] : 0), th = h - s->pad[1] - s->pad[3] - nh;
    dgx_ge_textbox_free(&v->line);
    dgx_ge_textbox_free(&v->answers);
    dgx_ge_textbox_free(&v->name);
    free_fields(v);
    make_arrows(v);
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

/*
 * The answers shown, a line each (an answer's own line breaks are spaces: one line an answer, wrapped when it is
 * long), into a text of the view as long as they are, laid when it is another, or a new choice (from the top; else the
 * lines seen stay where they are). False when there is no memory for it.
 */
static bool lay_answers(dgx_ge_talk_view_t *v, const dgx_ge_talk_t *t, const dgx_ge_strings_t *strings, bool fresh, uint32_t now_ms)
{
    size_t   need = 1;
    uint16_t id;
    for (int i = 0; dgx_ge_talk_answer(t, i, &id); ++i) need += strlen(dgx_ge_string(strings, id)) + 1;
    char *all = malloc(need);
    if (!all) return false;
    size_t n = 0;
    for (int i = 0; dgx_ge_talk_answer(t, i, &id); ++i) {
        if (i) all[n++] = '\n';
        for (const char *a = dgx_ge_string(strings, id); *a && n + 1 < need; ++a) all[n++] = *a == '\n' ? ' ' : *a;
    }
    all[n] = 0;
    if (!fresh && v->all && !strcmp(all, v->all)) {
        free(all); /* the same answers: as they are */
        return true;
    }
    const int top = v->answers.top;
    dgx_ge_textbox_set(&v->answers, all, &v->answer_style, true, now_ms);
    if (!fresh) dgx_ge_textbox_scroll(&v->answers, top);
    free(v->all), v->all = all;
    return true;
}

/*
 * A line of numbers made into a text of the view as long as it is: so many bytes as its longest form, a {name} 11 more
 * at most (a number of 32 bits); NULL when there is no memory for it. The text before it given back (the line laid
 * from this one at once).
 */
static const char *say_numbers(dgx_ge_talk_view_t *v, const dgx_ge_talk_t *t, const dgx_ge_strings_t *strings)
{
    int32_t numbers[DGX_GE_TALK_NUMBERS] = {t->number};
    for (int i = 1; i < t->count; ++i) numbers[i] = t->more[i - 1];
    size_t need = 1;
    for (int f = 0; f < t->forms; ++f) {
        size_t n = 1;
        for (const char *s = dgx_ge_string(strings, t->string + f); *s; ++s) n += *s == '{' ? 12 : 1;
        need = n > need ? n : need;
    }
    char *said = malloc(need);
    if (!said) return NULL;
    dgx_ge_strings_numbers(strings, t->string, t->forms, numbers, t->count ? t->count : 1, said, need);
    free(v->said), v->said = said;
    return said;
}

/* Where the line (or the answers: the same place) is on the screen: inside the padding, under the name, right of a portrait at the left. */
static dgx_ge_rect_t text_place(const dgx_ge_talk_view_t *v)
{
    const dgx_ge_rect_t b = v->box;
    const int           x = b.x + v->style.pad[0] + (v->style.portrait == 1 ? v->style.portrait_w + v->style.pad[0] : 0);
    return dgx_ge_rect(x, b.y + v->style.pad[1] + (v->name.canvas ? v->name.height : 0), v->line.width, v->line.height);
}

bool dgx_ge_talk_view_play(dgx_ge_talk_view_t *v, dgx_ge_talk_t *t, const dgx_ge_strings_t *strings, uint32_t now_ms, uint32_t world_ms)
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
    uint32_t said = (uint32_t)t->number;
    for (int i = 1; i < t->count; ++i) said = said * 31 + (uint32_t)t->more[i - 1];
    if (t->state != v->state || t->at != v->at || t->dialog != v->dialog || t->shown != v->shown || t->choices != v->choices ||
        (t->forms && said != v->said_number)) {
        v->dialog = t->dialog, v->state = t->state, v->at = t->at, v->shown = t->shown, v->choices = t->choices, v->pick = 0xFF, v->said_number = said;
        changed   = true;
        if (t->state == DGX_GE_TALK_SAYING) {
            if (!t->wait_ms) t->wait_ms = v->style.wait_ms;
            const char *text = t->forms ? say_numbers(v, t, strings) : dgx_ge_string(strings, t->string);
            if (!text) return false;
            dgx_ge_textbox_set(&v->line, text, &v->text_style, t->important || v->style.important, now_ms);
            if (!t->forms) free(v->said), v->said = NULL; /* a line of numbers no more */
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
    if (t->state == DGX_GE_TALK_CHOOSING && (t->pick != v->pick || t->changes != v->changes)) {
        /*
         * the answers, a list: all of it, never cut; laid anew for a new choice, and when a control (a field) changed a
         * value (the conditions may show others, as many); the lines shown follow the answer chosen; the pictures of the
         * controls (the texts of the fields) as their values are
         */
        if ((v->pick == 0xFF || t->changes != v->changes) && !lay_answers(v, t, strings, v->pick == 0xFF, now_ms)) return false;
        changed = true;
        v->pick = t->pick;
        follow_pick(v, t->pick);
        take_controls(v, t, now_ms);
    }
    /* a field edited from now on (activate on it): its text as it is, typed into from here; or no more */
    const bool editing = t->state == DGX_GE_TALK_CHOOSING && t->editing;
    if (editing != v->editing) {
        v->editing = editing;
        const dgx_ge_field_t *f = editing ? dgx_ge_talk_field(t, t->pick) : NULL;
        if (f && !make_fields(v)) v->editing = false, dgx_ge_talk_field_done(t, false, now_ms, world_ms); /* no memory to edit it in: as it was */
        else if (f) {
            const int most = (f->size < DGX_GE_FIELD_BYTES ? f->size : DGX_GE_FIELD_BYTES) - 1;
            int       n    = (int)strnlen(f->text, f->size);
            if (n > most)
                for (n = most; n > 0 && (f->text[n] & 0xC0) == 0x80; --n) {} /* cut between characters */
            memcpy(v->fields->edit, f->text, (size_t)n), v->fields->edit_len = (uint8_t)n;
            caret(v, true, now_ms);
        }
        if (t->state == DGX_GE_TALK_CHOOSING) take_controls(v, t, now_ms);
        changed = true;
    } else if (editing && (uint32_t)(now_ms - v->fields->blink_ms) >= BLINK_MS) {
        caret(v, !v->fields->caret, now_ms);
        relay_edit(v, t, now_ms);
    }
    if (changed) mark(v, v->box);
    else if (t->state == DGX_GE_TALK_SAYING && dgx_ge_textbox_play(&v->line, now_ms)) mark(v, text_place(v)); /* a letter more, a line up: the text alone */
    if (changed || (t->state == DGX_GE_TALK_CHOOSING && (v->widgets.count < 2 || v->widget_store[1].value != first_line(v, t->pick, NULL))))
        place_widgets(v, t);
    dgx_ge_talk_play(t, now_ms, world_ms, t->state != DGX_GE_TALK_SAYING || dgx_ge_textbox_at_end(&v->line));
    return true;
}

/* A widget of the view worked by the pointer: the line, an answer, a control: the events of the talk it means. */
static void worked(dgx_ge_widget_t *w, int what, void *ctx)
{
    dgx_ge_talk_view_t *v = ctx;
    dgx_ge_talk_t      *t = v->talk;
    if (!t || what == DGX_GE_WIDGET_FOCUS) return;
    if (w->kind == DGX_GE_WIDGET_BUTTON && w->min_row < 0) {
        /*
         * the scroll bar, tapped where it went down: by an end (its arrow, or half a line of it), a line up or down; on
         * the track above or below the knob, a page; the choice to the line past the edge seen, the lines after it
         */
        if ((what != DGX_GE_WIDGET_ACTIVATED && what != DGX_GE_WIDGET_CHANGED) || !v->answer_style.font) return;
        const int lh = dgx_ge_text_line(v->answer_style.font, v->answers.scale), rows = v->answers.shown, top = v->answers.top;
        const int y = v->widgets.press_y - (v->box.y + v->style.pad[1] + (v->name.canvas ? v->name.height : 0)), h = rows * lh;
        int       ah, ky, kh;
        bar_parts(v, h, &ah, &ky, &kh);
        const int end = ah + SCROLL_GAP * v->unit > lh / 2 ? ah + SCROLL_GAP * v->unit : lh / 2;
        int       to  = t->pick;
        if (what == DGX_GE_WIDGET_CHANGED) {
            /*
             * dragged: the knob follows the pointer, the lines seen as many lines on as the way it went is of the track
             * (the lines at the top where it went down: the button's value then); the choice to the edge they come from
             */
            const int th = h - 2 * (ah ? ah + SCROLL_GAP * v->unit : 0), dy = v->widgets.move_y - v->widgets.press_y;
            if (th <= 0) return;
            int want = v->widgets.press_value + (dy * v->answers.lines + (dy < 0 ? -th / 2 : th / 2)) / th;
            want     = want < 0 ? 0 : want > v->answers.lines - rows ? v->answers.lines - rows : want;
            if (want == top) return;
            to = want > top ? want + rows - 1 : want;
        } else if (y < end) to = top - 1;
        else if (y >= h - end) to = top + rows;
        else if (y < ky) to = top - rows;
        else if (y >= ky + kh) to = top + 2 * rows - 1;
        to = answer_of(v, to < 0 ? 0 : to); /* the line to the answer it shows */
        for (int k = 0; k < 64 && t->pick != to; ++k) dgx_ge_talk_event(t, t->pick < to ? DGX_GE_UI_NEXT : DGX_GE_UI_PREV, t->now_ms, t->world_ms);
    } else if (w->kind == DGX_GE_WIDGET_LIST) {
        /* the answer of the line under the pointer chosen: the choice moved there; taken when let go */
        const int to = answer_of(v, w->value);
        for (int k = 0; k < 16 && t->pick != to; ++k) dgx_ge_talk_event(t, t->pick < to ? DGX_GE_UI_NEXT : DGX_GE_UI_PREV, t->now_ms, t->world_ms);
        if (what == DGX_GE_WIDGET_ACTIVATED) dgx_ge_talk_view_event(v, t, DGX_GE_UI_ACTIVATE, t->now_ms, t->world_ms);
    } else if (w->kind == DGX_GE_WIDGET_SLIDER && (w->flags & DGX_GE_WIDGET_SWIPE) && what == DGX_GE_WIDGET_ACTIVATED) {
        /* a tap on a slider's row: the row chosen, as a tap on any row; its value as it is */
        for (int k = 0; k < 16 && t->pick != w->min_row; ++k) dgx_ge_talk_event(t, t->pick < w->min_row ? DGX_GE_UI_NEXT : DGX_GE_UI_PREV, t->now_ms, t->world_ms);
    } else if ((w->kind == DGX_GE_WIDGET_SLIDER || w->kind == DGX_GE_WIDGET_SWITCH) && what == DGX_GE_WIDGET_CHANGED) {
        /* a control with a range put at a place: its answer chosen, then its own events (more, less) until it is there; its value as it came out */
        for (int k = 0; k < 16 && t->pick != w->min_row; ++k) dgx_ge_talk_event(t, t->pick < w->min_row ? DGX_GE_UI_NEXT : DGX_GE_UI_PREV, t->now_ms, t->world_ms);
        int value, least, most, step;
        if (w->kind == DGX_GE_WIDGET_SWITCH) {
            /* two values: the control's own activate flips it (and takes its answer, as the button does) */
            if (dgx_ge_talk_control_range(t, t->pick, &value, &least, &most, &step) && value != w->value) dgx_ge_talk_view_event(v, t, DGX_GE_UI_ACTIVATE, t->now_ms, t->world_ms);
        } else {
            for (int k = 0; k < 64 && dgx_ge_talk_control_range(t, t->pick, &value, &least, &most, &step) && value != w->value; ++k)
                dgx_ge_talk_event(t, value < w->value ? DGX_GE_UI_MORE : DGX_GE_UI_LESS, t->now_ms, t->world_ms);
        }
        if (dgx_ge_talk_control_range(t, t->pick, &value, &least, &most, &step)) w->value = (int16_t)value;
    } else if (what == DGX_GE_WIDGET_ACTIVATED) {
        /* the line: on; a control: the choice moved to its answer, then worked as activate (the control's own) */
        if (t->state == DGX_GE_TALK_CHOOSING)
            for (int k = 0; k < 16 && t->pick != w->value; ++k) dgx_ge_talk_event(t, t->pick < w->value ? DGX_GE_UI_NEXT : DGX_GE_UI_PREV, t->now_ms, t->world_ms);
        dgx_ge_talk_view_event(v, t, DGX_GE_UI_ACTIVATE, t->now_ms, t->world_ms);
    }
}

/* Where a control of a row is drawn (its picture's box), as show() puts it; false when the row has none. */
static bool control_box(const dgx_ge_talk_view_t *v, const dgx_ge_talk_t *t, int row, int ty, int lh, dgx_ge_rect_t *box)
{
    if (row >= DGX_GE_TALK_VIEW_CONTROLS || v->control_ids[row] < 0) return false;
        const dgx_image_t    *img = &v->control_images[row];
    const int             right = controls_right(v), u = v->unit;
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
                                                                 .max = (int16_t)(v->answers.lines > 0 ? v->answers.lines - 1 : 0),
                                                                 .value = (int16_t)first_line(v, t->pick, NULL), .on = worked, .ctx = v});
        if (v->answers.lines > rows && rows > 0 && lh > 0 && v->arrows[0].data) {
            /*
             * the scroll bar, one button for a finger (its arrows alone are too small to aim at): the whole height of the
             * frame, from the controls' pictures across the frame's right margin and BAR_REACH past it (not past the
             * display); where it is tapped says what it does, dragged up or down its knob follows (worked)
             */
            const int mx = controls_right(v) - v->box.x, dw = v->pixels ? v->frame->display->width : dgx_ge_frame_width(v->frame);
            int       reach = BAR_REACH * v->unit;
            if (v->box.x + v->box.w + reach > dw) reach = dw - (v->box.x + v->box.w) > 0 ? dw - (v->box.x + v->box.w) : 0;
            dgx_ge_widgets_add(&v->widgets, root, &(dgx_ge_widget_t){.kind = DGX_GE_WIDGET_BUTTON, .flags = DGX_GE_WIDGET_DRAG, .at_x = (int16_t)mx,
                                                                     .at_y = 0, .want_w = (int16_t)(v->box.w - mx + reach), .want_h = (int16_t)v->box.h,
                                                                     .value = (int16_t)v->answers.top, .min_row = -1, .on = worked, .ctx = v});
        }
        for (int row = 0; row < rows; ++row) {
            dgx_ge_rect_t b;
            const int     a = row_answer(v, row); /* the answer that begins on the row, -1 none */
            if (a < 0) continue;
            if (dgx_ge_talk_field(t, a)) {
                /* a field: a button over it, activate (it is edited) */
                b = field_box(v, row);
                dgx_ge_widgets_add(&v->widgets, root, &(dgx_ge_widget_t){.kind = DGX_GE_WIDGET_BUTTON, .at_x = (int16_t)(b.x - v->box.x), .at_y = (int16_t)(b.y - v->box.y),
                                                                         .want_w = (int16_t)b.w, .want_h = (int16_t)b.h, .value = (int16_t)a,
                                                                         .min_row = (int16_t)a, .on = worked, .ctx = v});
                continue;
            }
            if (!control_box(v, t, row, place.y, lh, &b)) continue;
            /*
             * with a range: a switch (two values) or a slider over its picture, put at a place by the pointer, and a
             * slider's whole row too, swiped (its picture may be too small to aim at; on it, the picture's own wins:
             * the narrower); without a range: a button, worked as activate
             */
            int value, least, most, step;
            const bool    ranged = dgx_ge_talk_control_range(t, a, &value, &least, &most, &step);
            const uint8_t kind   = !ranged ? DGX_GE_WIDGET_BUTTON : most - least <= step ? DGX_GE_WIDGET_SWITCH : DGX_GE_WIDGET_SLIDER;
            /* over its picture across, the row down (less the gap between rows): a finger a little above or below it still hits */
            const int       gap = lh / DGX_GE_WIDGET_ROW_GAP;
            dgx_ge_widget_t w   = {.kind = kind, .at_x = (int16_t)(b.x - v->box.x), .at_y = (int16_t)(place.y + row * lh + gap - v->box.y),
                                   .want_w = (int16_t)b.w, .want_h = (int16_t)(lh - 2 * gap), .value = (int16_t)(ranged ? value : a), .min = (int16_t)(ranged ? least : 0),
                                 .max = (int16_t)(ranged ? most : 0), .step = (int16_t)(ranged ? step : 0),
                                 .min_row = (int16_t)a, .on = worked, .ctx = v};
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
        v->widgets.press_value = was.press_value, v->widgets.dragged = was.dragged, v->widgets.move_x = was.move_x, v->widgets.move_y = was.move_y;
        break;
    }
}

bool dgx_ge_talk_view_pointer(dgx_ge_talk_view_t *v, dgx_ge_talk_t *t, const dgx_ge_pointer_t *p, uint32_t now_ms, uint32_t world_ms)
{
    if (t->state == DGX_GE_TALK_OVER || v->style_number < 0 || !v->widgets.count) return false;
    dgx_ge_rect_t field;
    if (dgx_ge_talk_view_editing(v, t, &field)) {
        /* a field edited: the pointer is the talk's; a tap away from the field is done (the keyboard's own fingers never come here) */
        if (p->pressed && !(p->x >= field.x && p->x < field.x + field.w && p->y >= field.y && p->y < field.y + field.h))
            dgx_ge_talk_view_type(v, t, '\n', now_ms, world_ms);
        return true;
    }
    v->talk = t, t->now_ms = now_ms, t->world_ms = world_ms; /* the moments of the events the widgets tell it */
    const bool taken = dgx_ge_widgets_pointer(&v->widgets, p);
    v->talk = NULL;
    return taken;
}

void dgx_ge_talk_view_type(dgx_ge_talk_view_t *v, dgx_ge_talk_t *t, uint32_t code, uint32_t now_ms, uint32_t world_ms)
{
    const dgx_ge_field_t *f = v->editing && t->editing && v->fields ? dgx_ge_talk_field(t, t->pick) : NULL;
    if (!f) return;
    if (code == '\n' || code == 0x1B) {
        /* done: the text typed into the game's field (changed, the talk counts it); cancelled: the field as it was */
        v->fields->edit[v->fields->edit_len] = 0;
        const bool changed = code == '\n' && strcmp(f->text, v->fields->edit) != 0;
        if (changed) memcpy(f->text, v->fields->edit, (size_t)v->fields->edit_len + 1);
        v->editing = false;
        dgx_ge_talk_field_done(t, changed, now_ms, world_ms);
        if (t->state == DGX_GE_TALK_CHOOSING) take_controls(v, t, now_ms);
        mark(v, v->box);
        return;
    }
    if (code == 0x08) {
        /* the last character, its continuation bytes too */
        while (v->fields->edit_len > 0 && (v->fields->edit[--v->fields->edit_len] & 0xC0) == 0x80) {}
    } else if (code >= 0x20 && code != 0x7F && code <= 0x10FFFF && (code < 0xD800 || code > 0xDFFF)) {
        uint8_t   u[4];
        const int n = code < 0x80 ? (u[0] = (uint8_t)code, 1)
                    : code < 0x800 ? (u[0] = (uint8_t)(0xC0 | code >> 6), u[1] = (uint8_t)(0x80 | (code & 0x3F)), 2)
                    : code < 0x10000 ? (u[0] = (uint8_t)(0xE0 | code >> 12), u[1] = (uint8_t)(0x80 | (code >> 6 & 0x3F)), u[2] = (uint8_t)(0x80 | (code & 0x3F)), 3)
                                     : (u[0] = (uint8_t)(0xF0 | code >> 18), u[1] = (uint8_t)(0x80 | (code >> 12 & 0x3F)), u[2] = (uint8_t)(0x80 | (code >> 6 & 0x3F)),
                                        u[3] = (uint8_t)(0x80 | (code & 0x3F)), 4);
        const int most = (f->size < DGX_GE_FIELD_BYTES ? f->size : DGX_GE_FIELD_BYTES) - 1;
        if (v->fields->edit_len + n > most) return; /* the field is full */
        memcpy(v->fields->edit + v->fields->edit_len, u, (size_t)n), v->fields->edit_len = (uint8_t)(v->fields->edit_len + n);
    } else {
        return;
    }
    caret(v, true, now_ms); /* typing: the caret seen at once */
    relay_edit(v, t, now_ms);
}

bool dgx_ge_talk_view_editing(const dgx_ge_talk_view_t *v, const dgx_ge_talk_t *t, dgx_ge_rect_t *field)
{
    if (!v->editing || !t->editing || t->state != DGX_GE_TALK_CHOOSING || v->style_number < 0) return false;
    dgx_ge_rect_t b = field_box(v, first_line(v, t->pick, NULL) - v->answers.top);
    if (!v->pixels) {
        /* laid in points of the frame: as the frame shows them */
        const int n = v->frame->scale;
        b = dgx_ge_rect(v->frame->display_x + b.x * n, v->frame->display_y + b.y * n, b.w * n, b.h * n);
    }
    if (field) *field = b;
    return true;
}

void dgx_ge_talk_view_event(dgx_ge_talk_view_t *v, dgx_ge_talk_t *t, dgx_ge_ui_t event, uint32_t now_ms, uint32_t world_ms)
{
    /* a step not shown yet (the talk begun, or gone on, since the view last played it): nothing, none is passed unseen */
    if (v->style_number < 0 || v->dialog != t->dialog || v->state != t->state || v->at != t->at) return;
    if (v->editing && t->editing) {
        /* a field edited: activate is done, back leaves it as it was; the rest nothing */
        if (event == DGX_GE_UI_ACTIVATE || event == DGX_GE_UI_BACK) dgx_ge_talk_view_type(v, t, event == DGX_GE_UI_ACTIVATE ? '\n' : 0x1B, now_ms, world_ms);
        return;
    }
    if (t->state == DGX_GE_TALK_SAYING && (event == DGX_GE_UI_ACTIVATE || event == DGX_GE_UI_NEXT) && dgx_ge_textbox_appear_all(&v->line)) {
        mark(v, text_place(v));
        return;
    }
    dgx_ge_talk_event(t, event, now_ms, world_ms);
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
        /*
         * the bar under the answer chosen (inside the frame), then the answers over it; with more answers than lines, a
         * scroll bar at the right edge inside the frame, a column of its own: the bar under the answer ends before it
         */
        const int lh = v->answer_style.font ? dgx_ge_text_line(v->answer_style.font, v->answers.scale) : 0;
        const int bw = s->frame ? s->border_width : 0;
        const bool bar = scrolled(v) && lh > 0;
        const int  sw = SCROLL_W * v->unit, bx = bar_x(v), gap = SCROLL_GAP * v->unit; /* points: times the unit in display pixels */
        /* under the lines of the answer chosen that are seen (a long one wrapped into more) */
        int end, row = first_line(v, t->pick, &end) - v->answers.top;
        end -= v->answers.top;
        row = row < 0 ? 0 : row, end = end > v->answers.shown ? v->answers.shown : end;
        if (row < end)
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = dgx_ge_rect(b.x + bw, ty + row * lh, (bar ? bx - gap : b.x + b.w - bw) - (b.x + bw), (end - row) * lh),
                                                       .layer = layer, .flags = DGX_GE_ITEM_FILL, .color = v->colors[3]});
        dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &v->answers.image, .map = v->answers.bits == 8 ? v->ink : NULL, .x = (int16_t)tx,
                                                   .y = (int16_t)ty, .layer = layer, .color = v->colors[2]});
        if (bar) {
            /*
             * its track in a color of its own (unlike the fill and the bar), an arrow at each end (in the text's color, as
             * an ordinary scroll bar has them: a tap scrolls a line), its knob between them, as much and as far as the
             * lines seen
             */
            const int h = v->answers.shown * lh;
            int       ah, ky, kh;
            bar_parts(v, h, &ah, &ky, &kh);
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = dgx_ge_rect(bx, ty, sw, h), .layer = layer, .flags = DGX_GE_ITEM_FILL, .color = v->colors[5]});
            if (ah)
                for (int i = 0; i < 2; ++i)
                    dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &v->arrows[i], .x = (int16_t)bx, .y = (int16_t)(i ? ty + h - ah : ty), .layer = layer,
                                                               .color = v->colors[2]});
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = dgx_ge_rect(bx, ty + ky, sw, kh > 1 ? kh : 1), .layer = layer, .flags = DGX_GE_ITEM_FILL,
                                                       .color = v->colors[2]});
        }
        /* the controls: at the right of their lines (left of a portrait there), in the middle of each */
        const int right = controls_right(v);
        for (int row = 0; row < v->answers.shown && row < DGX_GE_TALK_VIEW_CONTROLS; ++row) {
            if (v->control_ids[row] < 0) continue;
            const dgx_image_t *img = &v->control_images[row];
            const int          set = dgx_ge_pack_image_set(v->pack, v->control_ids[row]), u = v->unit;
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = img, .map = v->maps && set >= 0 ? v->maps[set] : NULL,
                                                       .x = (int16_t)(right - (img->width - img->origin_x) * u),
                                                       .y = (int16_t)(ty + row * lh + (lh - img->height * u) / 2 + img->origin_y * u), .layer = layer,
                                                       .color = v->colors[2], .scale = (uint8_t)u});
        }
        /* the fields: a frame (its border the track's color, inside the fill: over the bar too), their text in it */
        for (int i = 0; v->fields && i < DGX_GE_TALK_VIEW_FIELDS; ++i) {
            if (v->fields->rows[i] < 0 || !v->fields->boxes[i]) continue;
            const dgx_ge_rect_t    f = field_box(v, v->fields->rows[i]);
            const dgx_ge_textbox_t *box = v->fields->boxes[i];
            const int              u = v->unit;
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = f, .layer = layer, .flags = DGX_GE_ITEM_FILL, .color = v->colors[5]});
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = dgx_ge_rect(f.x + u, f.y + u, f.w - 2 * u, f.h - 2 * u), .layer = layer,
                                                       .flags = DGX_GE_ITEM_FILL, .color = v->colors[0]});
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &box->image, .map = box->bits == 8 ? v->ink : NULL, .x = (int16_t)(f.x + FIELD_PAD * u),
                                                       .y = (int16_t)f.y, .layer = layer, .color = v->colors[2]});
        }
    }
}
