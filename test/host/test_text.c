/* A field of text: the largest scale that fits, words into lines, an important text scrolls, another is cut with an ellipsis. */
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_text.h"
#include "drivers/vscreen.h"
#include "fonts/TerminusTTFMedium12.h"
#include "fonts/font0ss.h"

/* Points set in a box, row by row. */
static int points(const dgx_ge_textbox_t *b)
{
    int n = 0;
    for (int i = 0; i < b->width / 8 * b->height; ++i)
        for (int k = 0; k < 8; ++k) n += b->image.data[i] >> k & 1;
    return n;
}

/* The first and last columns with a point set (-1, -1: none). */
static void columns(const dgx_ge_textbox_t *b, int *first, int *last)
{
    *first = *last = -1;
    for (int x = 0; x < b->width; ++x)
        for (int y = 0; y < b->height; ++y)
            if (b->image.data[y * (b->width / 8) + x / 8] >> (7 - x % 8) & 1) {
                if (*first < 0) *first = x;
                *last = x;
            }
}

/* The bytes of a line of the text laid. */
static int line_is(const dgx_ge_textbox_t *b, int l, const char *s)
{
    return (int)strlen(s) == b->ends[l] - b->starts[l] && !memcmp(b->text + b->starts[l], s, strlen(s));
}

int main(void)
{
    dgx_font_t         *font  = TerminusTTFMedium12(); /* 13 a line, letters 9 above their base, 6 wide */
    dgx_ge_text_style_t style = {font, 2, 1, 500, DGX_GE_ALIGN_LEFT};
    dgx_ge_textbox_t    box;
    CHECK(dgx_ge_textbox_init(&box, 60, 26, 0) && box.width == 64 && box.image.format == 1 && box.image.width == 64 && box.grow);

    /* short: the larger scale fits */
    dgx_ge_textbox_set(&box, "Hi", &style, false, 0);
    CHECK(box.scale == 2 && box.lines == 1 && !box.cut && !box.scrolling && points(&box) > 0);
    /* a line break and spaces after it are no text: the same scale (a msgstr ends so) */
    dgx_ge_textbox_set(&box, "Hi \n", &style, false, 0);
    CHECK(box.scale == 2 && box.lines == 1 && !box.cut && !box.scrolling);
    dgx_ge_textbox_set(&box, "Hi", &style, false, 0);

    /* as DGX draws it itself: the same points */
    dgx_screen_t *same = dgx_vscreen_init(64, 26, 1, DgxScreenRGB);
    dgx_font_string_utf8_screen(same, 0, 18, "Hi", 1, DgxOutputNormal, 2, font, NULL, NULL);
    CHECK(!memcmp(((dgx_vscreen_t *)same)->v_array, box.image.data, 64 / 8 * 26));
    same->destroy(&same);

    /* Cyrillic, too wide for one line at 2: at 1, in two lines, word by word */
    dgx_ge_textbox_set(&box, "Стой! Кто идёт?", &style, false, 0);
    CHECK(box.scale == 1 && box.lines == 2 && line_is(&box, 0, "Стой! Кто") && line_is(&box, 1, "идёт?") && !box.cut);
    /* a new line where the text says */
    dgx_ge_textbox_set(&box, "a\nb", &style, false, 0);
    CHECK(box.scale == 1 && box.lines == 2 && line_is(&box, 0, "a") && line_is(&box, 1, "b"));
    /* a word wider than the line: as many characters as go, the rest on */
    dgx_ge_textbox_set(&box, "abcdefghijklmn", &style, false, 0);
    CHECK(box.lines == 2 && line_is(&box, 0, "abcdefghij") && line_is(&box, 1, "klmn"));

    /* too long, unimportant: cut, two lines shown, the second ends in an ellipsis */
    const char *longer = "one two three four five six seven eight nine ten";
    dgx_ge_textbox_set(&box, longer, &style, false, 0);
    CHECK(box.scale == 1 && box.cut && !box.scrolling && box.shown == 2 && box.lines > 2 && dgx_ge_textbox_at_end(&box));
    const int cut_points = points(&box);
    dgx_screen_t *two = dgx_vscreen_init(64, 26, 1, DgxScreenRGB);
    dgx_font_string_utf8_screen(two, 0, 9, "one two", 1, DgxOutputNormal, 1, font, NULL, NULL);
    CHECK(!memcmp(((dgx_vscreen_t *)two)->v_array, box.image.data, 64 / 8 * 13)); /* its first line as it is */
    dgx_font_string_utf8_screen(two, 0, 22, "three f...", 1, DgxOutputNormal, 1, font, NULL, NULL);
    CHECK(!memcmp(((dgx_vscreen_t *)two)->v_array, box.image.data, 64 / 8 * 26) && cut_points > 0); /* the second, cut to go with ... */
    /* cut back to a space: no space before the ellipsis ("abcdef ..." would go in; "abcdef..." is drawn) */
    dgx_ge_textbox_set(&box, "one two abcdef hi three four", &style, false, 0);
    dgx_screen_t *spaced = dgx_vscreen_init(64, 26, 1, DgxScreenRGB);
    dgx_font_string_utf8_screen(spaced, 0, 9, "one two", 1, DgxOutputNormal, 1, font, NULL, NULL);
    dgx_font_string_utf8_screen(spaced, 0, 22, "abcdef...", 1, DgxOutputNormal, 1, font, NULL, NULL);
    CHECK(box.cut && line_is(&box, 1, "abcdef hi") && !memcmp(((dgx_vscreen_t *)spaced)->v_array, box.image.data, 64 / 8 * 26));
    spaced->destroy(&spaced);
    two->destroy(&two);
    CHECK(!dgx_ge_textbox_play(&box, 10000) && !dgx_ge_textbox_scroll(&box, 1)); /* a cut text does not move */

    /* too long, important: whole, by lines going up every 500 ms; read to its end */
    dgx_ge_textbox_set(&box, longer, &style, true, 1000);
    CHECK(box.scrolling && !box.cut && box.top == 0 && !dgx_ge_textbox_at_end(&box));
    CHECK(!dgx_ge_textbox_play(&box, 1400) && box.top == 0);
    CHECK(dgx_ge_textbox_play(&box, 1500) && box.top == 1);
    for (uint32_t t = 2000; t < 10000; t += 500) dgx_ge_textbox_play(&box, t);
    CHECK(box.top == box.lines - box.shown && dgx_ge_textbox_at_end(&box) && !dgx_ge_textbox_play(&box, 20000));
    /* by hand: back up, not past the start */
    CHECK(dgx_ge_textbox_scroll(&box, -100) && box.top == 0 && !dgx_ge_textbox_scroll(&box, -1));
    CHECK(dgx_ge_textbox_scroll(&box, 1) && box.top == 1);

    /* a scale the style forbids is not taken: the least is 2, a long text is cut at 2 */
    dgx_ge_text_style_t big = {font, 2, 2, 0, DGX_GE_ALIGN_LEFT};
    dgx_ge_textbox_set(&box, longer, &big, false, 0);
    CHECK(box.scale == 2 && box.cut && box.shown == 1);
    dgx_ge_textbox_set(&box, "", &style, false, 0);
    CHECK(box.lines == 0 && points(&box) == 0);

    /* letter by letter: nothing at first, three letters after three times, the second line after the first; at its end only when all came */
    dgx_ge_textbox_set(&box, "one two three", &style, false, 1000);
    dgx_ge_textbox_appear(&box, 50, 1000);
    CHECK(dgx_ge_textbox_appearing(&box) && points(&box) == 0 && !dgx_ge_textbox_at_end(&box));
    CHECK(dgx_ge_textbox_play(&box, 1150) && box.letters == 3);
    dgx_screen_t *three = dgx_vscreen_init(64, 26, 1, DgxScreenRGB);
    dgx_font_string_utf8_screen(three, 0, 9, "one", 1, DgxOutputNormal, 1, font, NULL, NULL);
    CHECK(!memcmp(((dgx_vscreen_t *)three)->v_array, box.image.data, 64 / 8 * 26));
    CHECK(dgx_ge_textbox_play(&box, 1450) && box.letters == 9); /* "one two" (7) and "th" */
    dgx_font_string_utf8_screen(three, 0, 9, "one two", 1, DgxOutputNormal, 1, font, NULL, NULL);
    dgx_font_string_utf8_screen(three, 0, 22, "th", 1, DgxOutputNormal, 1, font, NULL, NULL);
    CHECK(!memcmp(((dgx_vscreen_t *)three)->v_array, box.image.data, 64 / 8 * 26));
    three->destroy(&three);
    CHECK(!dgx_ge_textbox_play(&box, 1460));                         /* no new letter yet */
    CHECK(dgx_ge_textbox_play(&box, 1600) && !dgx_ge_textbox_appearing(&box) && dgx_ge_textbox_at_end(&box)); /* 12 letters: all */
    dgx_ge_textbox_appear(&box, 50, 2000);
    CHECK(dgx_ge_textbox_appear_all(&box) && !dgx_ge_textbox_appearing(&box) && !dgx_ge_textbox_appear_all(&box));
    dgx_ge_textbox_appear(&box, 0, 3000); /* at once */
    CHECK(!dgx_ge_textbox_appearing(&box) && points(&box) > 0);

    /* aligned: the same line at the left, in the middle (as much room on both sides, a point either way), at the right */
    int l0, r0, l1, r1, l2, r2;
    dgx_ge_text_style_t one = {font, 1, 1, 0, DGX_GE_ALIGN_LEFT};
    dgx_ge_textbox_set(&box, "ab", &one, false, 0);
    columns(&box, &l0, &r0);
    one.align = DGX_GE_ALIGN_MIDDLE;
    dgx_ge_textbox_set(&box, "ab", &one, false, 0);
    columns(&box, &l1, &r1);
    one.align = DGX_GE_ALIGN_RIGHT;
    dgx_ge_textbox_set(&box, "ab", &one, false, 0);
    columns(&box, &l2, &r2);
    CHECK(l0 >= 0 && l0 < 3 && r1 - l1 == r0 - l0 && r2 - l2 == r0 - l0);
    CHECK(abs(l1 - (box.width - 1 - r1)) <= 2 && r2 >= box.width - 3 && l0 < l1 && l1 < l2);

    dgx_ge_textbox_free(&box);
    CHECK(!box.canvas);

    /*
     * A handwritten font: its scale is the height of a line (any size, point by
     * point), the pen writes it; words go into lines by the width of the pen's
     * drawing; appearing, the pen goes as far as the letters come.
     */
    dgx_font_t         *pen  = font0ss();
    dgx_ge_text_style_t hand = {pen, 20, 10, 0, DGX_GE_ALIGN_LEFT};
    CHECK(dgx_ge_text_line(pen, 20) == 20 && dgx_ge_text_line(font, 2) == 26);
    CHECK(dgx_ge_textbox_init(&box, 120, 40, 0));
    dgx_ge_textbox_set(&box, "hello", &hand, false, 0);
    const int whole = points(&box);
    CHECK(box.scale == 20 && box.lines == 1 && whole > 20);
    bool in_band = true; /* every point of it within its line */
    for (int y = 20; y < 40; ++y)
        for (int x = 0; x < 120; ++x) in_band = in_band && !(box.image.data[y * 15 + x / 8] >> (7 - x % 8) & 1);
    CHECK(in_band);
    dgx_ge_textbox_set(&box, "hello hello hello hello", &hand, false, 0); /* too wide at 20: two lines of 20 */
    CHECK(box.lines == 2 && box.scale == 20 && line_is(&box, 0, "hello hello hello"));
    dgx_ge_textbox_set(&box, "hello hello hello hello hello hello hello hello hello", &hand, false, 0); /* smaller, point by point, until it goes in */
    CHECK(box.scale < 20 && box.scale >= 10 && box.lines * box.scale <= 40);
    /* appearing: nothing, then part of the first letter's worth, more, all of it as set */
    dgx_ge_textbox_set(&box, "hello", &hand, false, 0);
    dgx_ge_textbox_appear(&box, 100, 0);
    CHECK(points(&box) == 0);
    dgx_ge_textbox_play(&box, 200);
    const int part = points(&box);
    dgx_ge_textbox_play(&box, 400);
    const int more = points(&box);
    CHECK(part > 0 && more > part && more < whole);
    dgx_ge_textbox_play(&box, 500);
    CHECK(!dgx_ge_textbox_appearing(&box) && points(&box) == whole);
    dgx_ge_textbox_free(&box);

    /* a soft pen: 8 bits a point, amounts of ink — shares on the edges, the whole inside; a font of pixels: none or all */
    CHECK(dgx_ge_textbox_init_bits(&box, 120, 40, 8, 0) && box.image.format == 8 && box.bits == 8);
    dgx_ge_textbox_set(&box, "hello", &hand, false, 0);
    int shares = 0, whole_ink = 0, over = 0;
    for (int i = 0; i < 120 * 40; ++i) {
        const uint8_t v = box.image.data[i];
        shares += v > 0 && v < DGX_GE_TEXT_INK, whole_ink += v == DGX_GE_TEXT_INK, over += v > DGX_GE_TEXT_INK;
    }
    CHECK(shares > 10 && whole_ink > 0 && over == 0);
    dgx_ge_textbox_set(&box, "one", &style, false, 0);
    shares = whole_ink = 0;
    for (int i = 0; i < 120 * 40; ++i) shares += box.image.data[i] > 0 && box.image.data[i] < DGX_GE_TEXT_INK, whole_ink += box.image.data[i] == DGX_GE_TEXT_INK;
    CHECK(shares == 0 && whole_ink > 0);
    /* two boxes on one canvas: the other freed first leaves it whole; the owner frees it */
    dgx_ge_textbox_t other;
    CHECK(dgx_ge_textbox_share(&other, &box, 2) && other.capacity == 2 && !other.grow);
    CHECK(other.image.data == box.image.data && other.bits == 8 && !other.owner);
    dgx_ge_textbox_set(&other, "two", &style, false, 0);
    dgx_ge_textbox_free(&other);
    CHECK(box.canvas && box.image.data[0] == box.image.data[0]);
    dgx_ge_textbox_free(&box);
    CHECK(!dgx_ge_textbox_init_bits(&box, 10, 10, 4, 0)); /* 1 or 8 */

    /* lines: a box made for so many lays no more (the text is cut there); one for any number grows with a long important text */
    dgx_ge_text_style_t plain = {font, 1, 1, 500, DGX_GE_ALIGN_LEFT};
    CHECK(dgx_ge_textbox_init(&box, 60, 26, 2) && box.capacity == 2 && !box.grow);
    dgx_ge_textbox_set(&box, longer, &plain, true, 0);
    CHECK(box.lines == 2 && box.shown == 2 && box.cut && !box.scrolling && line_is(&box, 0, "one two") && line_is(&box, 1, "three four"));
    dgx_ge_textbox_free(&box);
    char many[4000] = "";
    for (int i = 0; i < 300; ++i) strcat(many, "w\n"); /* 300 lines of one letter */
    CHECK(dgx_ge_textbox_init(&box, 60, 26, 0) && box.capacity == 4);
    dgx_ge_textbox_set(&box, many, &plain, true, 0);
    CHECK(box.lines == 300 && box.capacity >= 300 && box.scrolling && !box.cut && box.shown == 2 && line_is(&box, 299, "w"));
    CHECK(dgx_ge_textbox_scroll(&box, 1000) && box.top == 298 && dgx_ge_textbox_at_end(&box));
    dgx_ge_textbox_set(&box, many, &plain, false, 0);
    CHECK(box.lines == 300 && box.cut && !box.scrolling);
    dgx_ge_textbox_free(&box);

    /* a box nothing was laid in yet (zeroed, freed, or made and never set): a button, time, a scroll change nothing, draw nothing */
    memset(&box, 0, sizeof box);
    CHECK(!dgx_ge_textbox_appear_all(&box) && !dgx_ge_textbox_play(&box, 100) && !dgx_ge_textbox_scroll(&box, 1) && !dgx_ge_textbox_appearing(&box));
    dgx_ge_textbox_appear(&box, 20, 0);
    CHECK(!dgx_ge_textbox_appear_all(&box) && !dgx_ge_textbox_play(&box, 100));
    CHECK(dgx_ge_textbox_init(&box, 60, 26, 0));
    CHECK(!dgx_ge_textbox_appear_all(&box) && !dgx_ge_textbox_play(&box, 100) && points(&box) == 0);
    dgx_ge_textbox_free(&box);
    CHECK_DONE();
}
