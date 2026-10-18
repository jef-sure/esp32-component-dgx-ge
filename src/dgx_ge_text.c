#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dgx_ge_text.h"
#include "dgx_hw_font.h"
#include "drivers/vscreen.h"

#define LINE_BYTES 160 /* a line of text drawn at once, bytes */
static const char ELLIPSIS[] = "...";

/* Room for so many lines (0: any number, from a few on); false when there is no memory. */
static bool lines_room(dgx_ge_textbox_t *box, int lines)
{
    box->grow     = lines <= 0;
    box->capacity = (uint16_t)(lines <= 0 ? 4 : lines > 0xFFFF ? 0xFFFF : lines);
    box->starts   = malloc(2 * (size_t)box->capacity * sizeof *box->starts);
    box->ends     = box->starts ? box->starts + box->capacity : NULL;
    return box->starts != NULL;
}

/* Another line to lay: room for it; false when the box has no more (or no memory to grow). */
static bool line_room(dgx_ge_textbox_t *box, int n)
{
    if (n < box->capacity) return true;
    if (!box->grow || box->capacity == 0xFFFF) return false;
    const unsigned more = box->capacity * 2u > 0xFFFFu ? 0xFFFFu : box->capacity * 2u;
    uint16_t *tables = malloc(2 * (size_t)more * sizeof *tables);
    if (!tables) return false;
    memcpy(tables, box->starts, (size_t)n * sizeof *tables);
    memcpy(tables + more, box->ends, (size_t)n * sizeof *tables);
    free(box->starts);
    box->starts = tables, box->ends = tables + more, box->capacity = (uint16_t)more;
    return true;
}

bool dgx_ge_textbox_init_bits(dgx_ge_textbox_t *box, int width, int height, int bits, int lines)
{
    memset(box, 0, sizeof(*box));
    width = (width + 7) & ~7;
    if (width <= 0 || height <= 0 || (bits != 1 && bits != 8)) return false;
    if (!lines_room(box, lines)) return false;
    box->canvas = dgx_vscreen_init(width, height, (uint8_t)bits, DgxScreenRGB);
    if (!box->canvas) {
        free(box->starts);
        box->starts = box->ends = NULL;
        return false;
    }
    box->bits   = (uint8_t)bits;
    box->owner  = true;
    box->width  = width;
    box->height = height;
    box->image  = (dgx_image_t){.width  = (uint16_t)width, .height = (uint16_t)height, .format = (uint8_t)bits,
                                .data   = ((dgx_vscreen_t *)box->canvas)->v_array};
    return true;
}

bool dgx_ge_textbox_init(dgx_ge_textbox_t *box, int width, int height, int lines)
{
    return dgx_ge_textbox_init_bits(box, width, height, 1, lines);
}

bool dgx_ge_textbox_share(dgx_ge_textbox_t *box, const dgx_ge_textbox_t *with, int lines)
{
    memset(box, 0, sizeof(*box));
    box->canvas = with->canvas, box->bits = with->bits, box->width = with->width, box->height = with->height, box->image = with->image;
    return lines_room(box, lines);
}

void dgx_ge_textbox_free(dgx_ge_textbox_t *box)
{
    if (box->owner && box->canvas && box->canvas->destroy) box->canvas->destroy(&box->canvas);
    free(box->starts);
    memset(box, 0, sizeof(*box));
}

/* The ink of a point drawn whole: 1 in a box of bits, all of it in a box of amounts. */
static uint32_t ink_of(const dgx_ge_textbox_t *box)
{
    return box->bits == 8 ? DGX_GE_TEXT_INK : 1;
}

static bool handwritten(const dgx_font_t *font)
{
    return font->f_type == DGX_FONT_HW;
}

int dgx_ge_text_line(const dgx_font_t *font, int scale)
{
    return handwritten(font) ? scale : font->yAdvance * scale;
}

/* A handwritten font: points a cell of its grid when a line is so many points high; the pen, points thick. */
static float cell_of(const dgx_font_t *font, int scale)
{
    return (float)scale / (float)font->yAdvance;
}

/* as thick as the line is high, a sixteenth of it (1 below 24, 2 at least from 24: a thin line stays readable), however large the display */
static int pen_of(int scale)
{
    return scale < 24 ? 1 : scale / 16 > 2 ? scale / 16 : 2;
}

/* Where a handwritten line begins, so the leftmost of its pen is at 0: points; and how wide it is. */
static int hw_left(const char *line, dgx_font_t *font, int scale, int *width)
{
    int l, t, r, b;
    const float cell = cell_of(font, scale);
    if (!dgx_hw_text_box(font, line, &l, &t, &r, &b)) {
        if (width) *width = 0;
        return 0;
    }
    const int from = l < 0 ? l : 0, pen = pen_of(scale);
    if (width) *width = (int)ceilf((float)(r - from + 1) * cell) + pen;
    return (int)ceilf((float)-from * cell) + pen / 2;
}

/* How wide so many bytes of a text are in a font at a scale. */
static int width_of(const char *s, int n, dgx_font_t *font, int scale)
{
    char line[LINE_BYTES + 1];
    if (n > LINE_BYTES) n = LINE_BYTES;
    memcpy(line, s, (size_t)n);
    line[n] = 0;
    if (!n) return 0;
    if (handwritten(font)) {
        int width;
        hw_left(line, font, scale, &width);
        return width;
    }
    int16_t corner, height;
    return dgx_font_string_bounds(line, font, &corner, &height) * scale;
}

/* A line of a handwritten font written at a line k of the box, from x on: all of it, or (appearing) as far as so many of its letters of so many. */
static void hw_line(dgx_ge_textbox_t *box, const char *s, int k, int letters, int all, int from)
{
    dgx_font_t *font = box->style->font;
    const float cell = cell_of(font, box->scale);
    const int   x = from + hw_left(s, font, box->scale, NULL), y = k * box->scale + (int)lroundf((float)-font->yOffsetLowest * cell);
    const int pen = pen_of(box->scale) | (box->bits == 8 ? DGX_PEN_SOFT : 0); /* soft where the box holds amounts of ink */
    if (letters >= all) {
        dgx_hw_draw_text(box->canvas, x, y, font, s, cell, pen, ink_of(box), true);
        return;
    }
    /* the pen as far as the letters come: their share of the effort of the line */
    dgx_hw_writing_t writing;
    dgx_hw_writing_begin(&writing, box->canvas, x, y, font, s, cell, pen, ink_of(box), true, NULL);
    dgx_hw_writing_draw_to(&writing, dgx_hw_text_effort(font, s, true, NULL) * (float)letters / (float)(all > 0 ? all : 1));
}

/* The next character of UTF-8 from byte i: the byte after it. */
static int next_char(const char *s, int i)
{
    ++i;
    while ((s[i] & 0xC0) == 0x80) ++i;
    return i;
}

/* A text into lines as wide as the box at a scale: word by word, a word wider than a line by characters. How many lines; more than the box has: as many as it has. */
static int lay(dgx_ge_textbox_t *box, const char *text, dgx_font_t *font, int scale)
{
    int n = 0, i = 0;
    const int len = (int)strlen(text);
    while (i < len && line_room(box, n)) {
        const int start = i;
        int       end   = i; /* the end of the last word that goes in */
        for (;;) {
            /* the next word: up to a space, a new line, or the end */
            int w = end;
            while (w < len && text[w] == ' ') ++w;
            int e = w;
            while (e < len && text[e] != ' ' && text[e] != '\n') ++e;
            if (e - start > LINE_BYTES || width_of(text + start, e - start, font, scale) > box->width) {
                if (end == start) {
                    /* one word wider than the line: as many of its characters as go */
                    int c = start;
                    while (c < e && width_of(text + start, next_char(text, c) - start, font, scale) <= box->width && next_char(text, c) - start <= LINE_BYTES)
                        c = next_char(text, c);
                    end = c > start ? c : next_char(text, start);
                }
                break;
            }
            end = e;
            if (e >= len || text[e] == '\n') break;
        }
        box->starts[n] = (uint16_t)start, box->ends[n] = (uint16_t)end;
        ++n;
        i = end;
        while (i < len && text[i] == ' ') ++i;
        if (i < len && text[i] == '\n') ++i;
    }
    return n;
}

/* The lines shown, drawn anew; the last one shown ends in an ellipsis when the text is cut. */
static void draw(dgx_ge_textbox_t *box)
{
    dgx_font_t *font  = box->style->font;
    const int   line  = dgx_ge_text_line(font, box->scale), above = -font->yOffsetLowest * box->scale;
    memset((uint8_t *)box->image.data, 0, (size_t)box->width * box->bits / 8 * box->height);
    int letters = box->letters; /* those before the top line count too: they appeared before it went up */
    for (int l = 0; l < box->top && l < box->lines && letters != 0xFFFF; ++l)
        for (int i = box->starts[l]; i < box->ends[l]; i = next_char(box->text, i)) --letters;
    for (int k = 0; k < box->shown && box->top + k < box->lines; ++k) {
        const int l = box->top + k;
        char      s[LINE_BYTES + sizeof ELLIPSIS];
        int       n = box->ends[l] - box->starts[l];
        if (n > LINE_BYTES) n = LINE_BYTES;
        memcpy(s, box->text + box->starts[l], (size_t)n);
        s[n] = 0;
        if (box->cut && k == box->shown - 1) {
            /* the last line seen of a text cut: as much of it as goes with the ellipsis */
            while (n > 0) {
                while (n > 0 && s[n - 1] == ' ') --n; /* no space before it */
                memcpy(s + n, ELLIPSIS, sizeof ELLIPSIS);
                if (width_of(s, (int)strlen(s), font, box->scale) <= box->width) break;
                while (n > 0 && (s[--n] & 0xC0) == 0x80) {}
                s[n] = 0;
            }
            if (n == 0) memcpy(s, ELLIPSIS, sizeof ELLIPSIS);
        }
        /* where it begins: as the style aligns the whole line (appearing, its letters where they will stand) */
        int from = 0;
        if (box->style->align != DGX_GE_ALIGN_LEFT) {
            const int room = box->width - width_of(s, (int)strlen(s), font, box->scale);
            from           = room <= 0 ? 0 : box->style->align == DGX_GE_ALIGN_MIDDLE ? room / 2 : room;
        }
        if (handwritten(font)) {
            /* written by the pen: appearing, as far as its letters have come */
            int all = 0;
            for (int i = 0; s[i]; i = next_char(s, i)) ++all;
            const int come = box->letters == 0xFFFF ? all : letters < all ? letters : all;
            hw_line(box, s, k, come, all, from);
            if (box->letters != 0xFFFF && (letters -= all) <= 0) break;
            continue;
        }
        if (box->letters != 0xFFFF) {
            /* appearing: as many letters of it as have come */
            int i = 0;
            while (s[i] && letters > 0) i = next_char(s, i), --letters;
            if (letters <= 0) s[i] = 0;
        }
        dgx_font_string_utf8_screen(box->canvas, (int16_t)from, (int16_t)(k * line + above), s, ink_of(box), DgxOutputNormal, box->scale, font, NULL, NULL);
        if (box->letters != 0xFFFF && letters <= 0) break;
    }
}

/* What is left of the text after n lines laid; spaces and line breaks after the last line laid are no text. */
static const char *rest_of(const dgx_ge_textbox_t *box, int n)
{
    const char *rest = n ? box->text + box->ends[n - 1] : "";
    while (*rest == ' ' || *rest == '\n') ++rest;
    return rest;
}

void dgx_ge_textbox_set(dgx_ge_textbox_t *box, const char *text, const dgx_ge_text_style_t *style, bool important, uint32_t now_ms)
{
    box->text = text ? text : "", box->style = style, box->top = 0, box->since_ms = now_ms, box->letters = 0xFFFF, box->letter_ms = 0;
    box->scrolling = box->cut = false;
    const int min = style->scale_min ? style->scale_min : 1, max = style->scale_max > min ? style->scale_max : min;
    /* the largest scale it goes in at */
    int scale = max, n = 0, fits = 0;
    for (; scale >= min; --scale) {
        n    = lay(box, box->text, style->font, scale);
        fits = box->height / dgx_ge_text_line(style->font, scale);
        if (n == 0 || (n <= fits && !*rest_of(box, n))) break;
    }
    if (scale < min) scale = min, n = lay(box, box->text, style->font, scale), fits = box->height / dgx_ge_text_line(style->font, scale);
    box->scale = (uint8_t)scale, box->lines = (uint16_t)n, box->shown = (uint16_t)(fits < n ? fits : n);
    /* the box had no lines for the rest: cut, whatever its importance (the box was made so); else past the box: whole or cut */
    const bool unlaid = *rest_of(box, n) != 0;
    if (unlaid || (n > fits && !important)) box->cut = true;
    else if (n > fits) box->scrolling = true;
    if (!box->shown && n) box->shown = 1; /* a box lower than a line still shows one, cut */
    draw(box);
}

bool dgx_ge_textbox_scroll(dgx_ge_textbox_t *box, int lines)
{
    if (!box->scrolling) return false;
    int top = box->top + lines;
    const int last = box->lines - box->shown;
    if (top > last) top = last;
    if (top < 0) top = 0;
    if (top == box->top) return false;
    box->top = (uint16_t)top;
    draw(box);
    return true;
}

void dgx_ge_textbox_appear(dgx_ge_textbox_t *box, uint8_t letter_ms, uint32_t now_ms)
{
    box->letter_ms = letter_ms, box->shown_ms = now_ms;
    box->letters   = letter_ms ? 0 : 0xFFFF;
    draw(box);
}

bool dgx_ge_textbox_appear_all(dgx_ge_textbox_t *box)
{
    if (box->letters == 0xFFFF) return false;
    box->letters = 0xFFFF;
    draw(box);
    return true;
}

/* Letters in the lines shown, from the first (those above it already appeared). */
static int letters_shown(const dgx_ge_textbox_t *box)
{
    int n = 0;
    for (int l = 0; l < box->top + box->shown && l < box->lines; ++l)
        for (int i = box->starts[l]; i < box->ends[l]; i = next_char(box->text, i)) ++n;
    return n;
}

bool dgx_ge_textbox_appearing(const dgx_ge_textbox_t *box)
{
    return box->letters != 0xFFFF;
}

bool dgx_ge_textbox_play(dgx_ge_textbox_t *box, uint32_t now_ms)
{
    if (box->letters != 0xFFFF) {
        /* appearing: a letter every letter_ms; all of the lines shown come, it has appeared */
        const uint32_t n = (uint32_t)(now_ms - box->shown_ms) / box->letter_ms;
        const int      all = letters_shown(box);
        const uint16_t was = box->letters;
        box->letters       = n >= (uint32_t)all ? 0xFFFF : (uint16_t)n;
        if (box->letters == 0xFFFF) box->since_ms = now_ms; /* its scroll (if it scrolls) starts now */
        if (box->letters != was) draw(box);
        return box->letters != was;
    }
    if (!box->scrolling || !box->style->scroll_ms || (int32_t)(now_ms - box->since_ms) < box->style->scroll_ms) return false;
    box->since_ms = now_ms;
    return dgx_ge_textbox_scroll(box, 1);
}

bool dgx_ge_textbox_at_end(const dgx_ge_textbox_t *box)
{
    return box->letters == 0xFFFF && (!box->scrolling || box->top + box->shown >= box->lines);
}
