/* A show: its backdrop, its actors, the cues each at its moment however late the call, the end, a skip. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_show.h"
#include "drivers/vscreen.h"
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

static int places;

static bool placed(const dgx_ge_actor_t *actor, int kind, dgx_ge_item_t *item, void *ctx)
{
    const dgx_ge_show_t *show = ctx;
    ++places;
    if (kind != TEST_BLOB_WAVE || !actor->flip) return false;
    item->image = &show->images[0]; /* any picture kept while the frame is drawn */
    item->x = 1, item->y = 2;
    return true;
}

int main(int argc, char **argv)
{
    size_t   size = 0;
    uint8_t *data = read_file(argc > 1 ? argv[1] : "build/test.pack", &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    size_t         n;
    const uint8_t *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_SHOW, &n);
    dgx_ge_show_t  show;
    CHECK(blob && dgx_ge_show_open(&show, blob, n) && show.pictures == 2 && show.actors == 2 && show.cues == 3);
    CHECK(!dgx_ge_show_open(&show, data, 12));
    CHECK(dgx_ge_show_open(&show, blob, n));

    dgx_ge_kinds_t kinds;
    CHECK(dgx_ge_kinds_init(&kinds, &pack, 16));
    CHECK(dgx_ge_show_start(&show, &kinds, 1000) && show.cast && !show.over);
    CHECK(show.cast[1].flip && dgx_ge_actor_x(&show.cast[1]) == 80);

    /* still until their cues; then waving from the cue's moment, however late the call */
    CHECK(!dgx_ge_show_play(&show, 1200, NULL, NULL) && show.cast[0].image == TEST_IMG_ONE && show.next_cue == 0);
    CHECK(!dgx_ge_show_play(&show, 1850, NULL, NULL) && show.next_cue == 2);
    CHECK(show.cast[0].script == TEST_WAVE_SCRIPT_WAVING && show.cast[0].image == TEST_IMG_EIGHT); /* from 1300: four, eight, ... at 1800 eight */
    CHECK(show.cast[1].script == TEST_WAVE_SCRIPT_WAVING && show.cast[1].image == TEST_IMG_EIGHT); /* from 1700 */

    /* drawn: the backdrop into the set, the actors into the frame (the one facing the other way flipped) */
    dgx_screen_t   *display = dgx_vscreen_init(320, 200, 16, DgxScreenRGB);
    dgx_ge_frame_t *frame   = dgx_ge_frame_create(display, 0, 0, 320, 200);
    dgx_ge_scene_t *scene   = dgx_ge_scene_create(frame, 2, 8, 8);
    dgx_ge_show_backdrop(&show, &pack, NULL, scene);
    CHECK(scene->set_count == 2 && scene->set[1].color == 3); /* one of 1 bit: no maps, its color as it is */
    CHECK(dgx_ge_show_picture(&show, 1) == TEST_IMG_ONE && dgx_ge_show_picture(&show, 2) == -1);
    /* with the maps of the sets: its color of its set (3: the third), as the frame has it */
    static const uint8_t hero_map[15] = {11, 22, 33};
    const uint8_t       *maps[TEST_SET_COUNT] = {0};
    maps[TEST_SET_HERO] = hero_map;
    dgx_ge_scene_reset(scene, 0);
    dgx_ge_show_backdrop(&show, &pack, maps, scene);
    CHECK(scene->set_count == 2 && scene->set[1].color == 33 && scene->set[0].map == hero_map);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_show_items(&show, &pack, NULL, scene, NULL, NULL);
    CHECK(scene->actor_count == 2);
    /* drawn the game's way: what it says (here: only the one facing the other way) */
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_show_items(&show, &pack, NULL, scene, placed, &show);
    CHECK(scene->actor_count == 1 && places == 2);

    /* the end at 2000; a skip ends it too */
    CHECK(dgx_ge_show_play(&show, 2000, NULL, NULL) && show.over);
    CHECK(dgx_ge_show_start(&show, &kinds, 5000) && !dgx_ge_show_play(&show, 5100, NULL, NULL));
    dgx_ge_show_skip(&show);
    CHECK(dgx_ge_show_play(&show, 5200, NULL, NULL));
    dgx_ge_show_stop(&show);
    CHECK(!show.cast && !show.images);

    /* pictures alone, no actors: it goes on until its end, not over at once */
    const uint8_t *still = dgx_ge_pack_blob(&pack, TEST_BLOB_STILL, &n);
    CHECK(still && dgx_ge_show_open(&show, still, n) && show.actors == 0 && dgx_ge_show_start(&show, &kinds, 100));
    CHECK(!dgx_ge_show_play(&show, 1500, NULL, NULL) && dgx_ge_show_play(&show, 2100, NULL, NULL));
    dgx_ge_show_stop(&show);

    dgx_ge_scene_destroy(&scene);
    dgx_ge_frame_destroy(&frame);
    display->destroy(&display);
    dgx_ge_kinds_free(&kinds);
    free(data);
    CHECK_DONE();
}
