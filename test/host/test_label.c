/* A label: its text laid when it is another, its place marked for the frame then only; shown as an actor, nothing when empty. */
#include <string.h>

#include "check.h"
#include "dgx_ge_label.h"
#include "drivers/vscreen.h"
#include "fonts/TerminusTTFMedium12.h"

static int points(const dgx_ge_textbox_t *b)
{
    int n = 0;
    for (int i = 0; i < b->width / 8 * b->height; ++i)
        for (int k = 0; k < 8; ++k) n += b->image.data[i] >> k & 1;
    return n;
}

int main(void)
{
    dgx_screen_t   *display = dgx_vscreen_init(224, 288, 16, DgxScreenRGB);
    dgx_ge_frame_t *frame   = dgx_ge_frame_create(display, 0, 0, 224, 288);
    dgx_ge_scene_t *scene   = dgx_ge_scene_create(frame, 2, 4, 4);
    dgx_ge_label_t  score;
    CHECK(display && frame && scene);
    CHECK(dgx_ge_label_init(&score, frame, TerminusTTFMedium12(), 16, 8, 64, 13, DGX_GE_ALIGN_RIGHT, 1, 7));
    dgx_ge_frame_present(frame);
    CHECK(frame->dirty_count == 0 && points(&score.box) == 0);

    /* a number: laid, its place marked */
    dgx_ge_label_number(&score, 1230);
    CHECK(!strcmp(score.text, "1230") && points(&score.box) > 0 && frame->dirty_count == 1);
    CHECK(frame->dirty[0].x == 16 && frame->dirty[0].y == 8 && frame->dirty[0].w == 64 && frame->dirty[0].h == 13);
    dgx_ge_frame_present(frame);
    /* the same again: nothing to draw */
    dgx_ge_label_number(&score, 1230);
    dgx_ge_label_set(&score, "1230");
    CHECK(frame->dirty_count == 0);
    /* shown: one actor, its field at its place in its color */
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_label_show(&score, scene);
    CHECK(scene->actor_count == 1 && scene->actors[0].x == 16 && scene->actors[0].y == 8 && scene->actors[0].color == 7 &&
          scene->actors[0].image == &score.box.image);
    /* another color: marked; empty: nothing shown */
    dgx_ge_label_color(&score, 9);
    CHECK(frame->dirty_count == 1 && score.color == 9);
    dgx_ge_frame_present(frame);
    dgx_ge_label_set(&score, "");
    CHECK(frame->dirty_count == 1 && points(&score.box) == 0);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_label_show(&score, scene);
    CHECK(scene->actor_count == 0);
    /* too long a text: cut to the room it has, still a string */
    char long_text[100];
    memset(long_text, 'x', sizeof long_text - 1), long_text[sizeof long_text - 1] = 0;
    dgx_ge_label_set(&score, long_text);
    CHECK(strlen(score.text) == DGX_GE_LABEL_TEXT - 1);

    dgx_ge_label_free(&score);
    dgx_ge_scene_destroy(&scene);
    dgx_ge_frame_destroy(&frame);
    dgx_screen_destroy(&display);
    CHECK_DONE();
}
