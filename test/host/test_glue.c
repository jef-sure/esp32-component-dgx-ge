/*
 * What every game wrote alike between its pack and the screen, now the engine's: the frame made for the display
 * (dgx_ge_frame_fit) against the games' own lines, at every display and frame size; the colors of all the sets at once
 * (dgx_ge_frame_pack_colors) against the colors of the pack, too small a room, too few indices of the frame; a
 * picture and an actor as items (dgx_ge_item_picture, _actor, dgx_ge_scene_show_actor) against the games' show_actor.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_glue.h"
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

/* a display of so many pixels: the frame only reads its size */
static dgx_screen_t display_of(int w, int h)
{
    dgx_screen_t d;
    memset(&d, 0, sizeof d);
    d.width = w, d.height = h;
    return d;
}

/* ---- the frame for the display ---- */

static void test_fit(void)
{
    /* the boards' displays, a stood-up one, one smaller than the frame, the frames of the games (PoP 320x200, a maze 224x288) */
    static const int displays[][2] = {{320, 240}, {240, 320}, {1280, 800}, {1024, 600}, {800, 1280}, {200, 150}, {64, 64}, {2560, 1600}};
    static const int frames[][2]   = {{320, 200}, {224, 288}, {320, 240}, {160, 100}, {16, 16}, {7, 3}};
    for (unsigned d = 0; d < sizeof displays / sizeof displays[0]; ++d)
        for (unsigned f = 0; f < sizeof frames / sizeof frames[0]; ++f) {
            dgx_screen_t lcd = display_of(displays[d][0], displays[d][1]);
            const int    w = frames[f][0], h = frames[f][1];
            /* as every game wrote it (bounded by the frame's 8) */
            int scale = lcd.width / w < lcd.height / h ? lcd.width / w : lcd.height / h;
            scale     = scale < 1 ? 1 : scale > 8 ? 8 : scale;
            dgx_ge_frame_t *frame = dgx_ge_frame_fit(&lcd, w, h, 0, 0);
            CHECK(frame && frame->scale == scale && frame->display == &lcd);
            if (!frame) continue;
            CHECK(frame->display_x == (lcd.width - w * scale) / 2 && frame->display_y == (lcd.height - h * scale) / 2);
            CHECK(dgx_ge_frame_width(frame) == w && dgx_ge_frame_height(frame) == h);
            /* at least as large as the display holds, and no larger: all of it seen unless even 1 is too large */
            if (scale > 1) CHECK(w * scale <= lcd.width && h * scale <= lcd.height);
            if (scale < 8) CHECK(w * (scale + 1) > lcd.width || h * (scale + 1) > lcd.height);
            CHECK(scale == 1 ? !frame->scaled : frame->scaled && frame->scaled->height == scale && frame->band == 0); /* a strip */
            dgx_ge_frame_destroy(&frame);
        }

    /* the boards' sizes as they are: the CYD x1 under a strip of 20, the 10" x4 whole, the 7" x3 with 32 each side */
    dgx_screen_t cyd = display_of(320, 240), ten = display_of(1280, 800), seven = display_of(1024, 600);
    dgx_ge_frame_t *f = dgx_ge_frame_fit(&cyd, 320, 200, 0, 0);
    CHECK(f && f->scale == 1 && f->display_x == 0 && f->display_y == 20);
    dgx_ge_frame_destroy(&f);
    f = dgx_ge_frame_fit(&ten, 320, 200, 0, 0);
    CHECK(f && f->scale == 4 && f->display_x == 0 && f->display_y == 0);
    dgx_ge_frame_destroy(&f);
    f = dgx_ge_frame_fit(&seven, 320, 200, 0, 0);
    CHECK(f && f->scale == 3 && f->display_x == 32 && f->display_y == 0);
    dgx_ge_frame_destroy(&f);

    /* a scale given (the game's fonts made for it): taken as it is, in the middle, even past the display's edges */
    f = dgx_ge_frame_fit(&ten, 320, 200, 2, 0);
    CHECK(f && f->scale == 2 && f->display_x == 320 && f->display_y == 200);
    dgx_ge_frame_destroy(&f);
    f = dgx_ge_frame_fit(&cyd, 320, 200, 2, 0);
    CHECK(f && f->scale == 2 && f->display_x == -160 && f->display_y == -80);
    dgx_ge_frame_destroy(&f);
    CHECK(!dgx_ge_frame_fit(&ten, 16, 16, 9, 0)); /* above the frame's 8 */
    CHECK(!dgx_ge_frame_fit(&ten, 0, 200, 0, 0) && !dgx_ge_frame_fit(&ten, 320, 0, 0, 0) && !dgx_ge_frame_fit(NULL, 320, 200, 0, 0));

    /* a band the board asks for: rows of the frame as many as it holds, after the scale; at 1 only kept for later */
    f = dgx_ge_frame_fit(&ten, 320, 200, 0, 1280 * 4 * 10);
    CHECK(f && f->scale == 4 && f->band == 1280 * 4 * 10 && f->scaled && f->scaled->height == 10 * 4);
    dgx_ge_frame_destroy(&f);
    f = dgx_ge_frame_fit(&cyd, 320, 200, 0, 4096);
    CHECK(f && f->scale == 1 && f->band == 4096 && !f->scaled);
    dgx_ge_frame_destroy(&f);
}

/* ---- the colors of the pack's sets ---- */

/* every map points at frame's indices of its set's colors, one after another from the start of maps */
static void check_maps(const dgx_ge_pack_t *pack, const dgx_ge_frame_t *frame, const uint8_t *maps, const uint8_t **map_of, int sets)
{
    size_t at = 0;
    for (int set = 0; set < sets; ++set) {
        const uint8_t (*rgb)[3];
        const int n = dgx_ge_pack_set_colors(pack, set, &rgb);
        CHECK(map_of[set] == maps + at);
        for (int i = 0; i < n && map_of[set]; ++i) CHECK(!memcmp(frame->rgb[map_of[set][i]], rgb[i], 3) && map_of[set][i] >= 1);
        at += (size_t)n;
    }
}

static void test_colors(const dgx_ge_pack_t *pack)
{
    dgx_screen_t    lcd   = display_of(320, 240);
    dgx_ge_frame_t *frame = dgx_ge_frame_fit(&lcd, 64, 48, 0, 0);
    CHECK(frame && pack->sets == TEST_SET_COUNT);
    if (!frame) return;

    /* the header's count is the colors of all the sets, and the maps take that many bytes */
    size_t total = 0;
    for (int set = 0; set < pack->sets; ++set) {
        const uint8_t (*rgb)[3];
        total += (size_t)dgx_ge_pack_set_colors(pack, set, &rgb);
    }
    CHECK(total == TEST_SET_COLOR_COUNT && total > 16); /* a set wider than 16 colors among them */

    static uint8_t        maps[TEST_SET_COLOR_COUNT + 8];
    static const uint8_t *map_of[TEST_SET_COUNT];
    memset(maps, 0xEE, sizeof maps);
    CHECK(dgx_ge_frame_pack_colors(frame, pack, maps, TEST_SET_COLOR_COUNT, map_of));
    check_maps(pack, frame, maps, map_of, TEST_SET_COUNT);
    CHECK(maps[TEST_SET_COLOR_COUNT] == 0xEE); /* nothing past its size */
    /* as dgx_ge_frame_add_colors set by set gives them (equal colors one index): the same maps, the same palette */
    dgx_ge_frame_t *by_hand = dgx_ge_frame_fit(&lcd, 64, 48, 0, 0);
    uint8_t         one[TEST_SET_COLOR_COUNT];
    size_t          at = 0;
    for (int set = 0; set < TEST_SET_COUNT; ++set) {
        const uint8_t (*rgb)[3];
        const int n = dgx_ge_pack_set_colors(pack, set, &rgb);
        if (n) dgx_ge_frame_add_colors(by_hand, rgb, n, one + at);
        at += (size_t)n;
    }
    CHECK(by_hand && !memcmp(one, maps, sizeof one) && by_hand->colors_used == frame->colors_used &&
          !memcmp(by_hand->rgb, frame->rgb, sizeof frame->rgb));
    dgx_ge_frame_destroy(&by_hand);

    /* again (a new level after dgx_ge_frame_forget_colors): the same indices */
    dgx_ge_frame_forget_colors(frame);
    uint8_t again[TEST_SET_COLOR_COUNT];
    CHECK(dgx_ge_frame_pack_colors(frame, pack, again, sizeof again, map_of) && !memcmp(again, maps, sizeof again));

    /* a byte short: the last set with colors does not fit, it and those after it NULL, nothing of theirs given */
    int last = -1;
    for (int set = 0; set < TEST_SET_COUNT; ++set) {
        const uint8_t (*rgb)[3];
        if (dgx_ge_pack_set_colors(pack, set, &rgb)) last = set;
    }
    dgx_ge_frame_forget_colors(frame);
    memset(maps, 0xEE, sizeof maps);
    CHECK(last >= 0 && !dgx_ge_frame_pack_colors(frame, pack, maps, TEST_SET_COLOR_COUNT - 1, map_of));
    check_maps(pack, frame, maps, map_of, last);
    for (int set = last; set < TEST_SET_COUNT; ++set) CHECK(!map_of[set]);
    CHECK(maps[TEST_SET_COLOR_COUNT - 1] == 0xEE);
    /* room for the first and the last but not the wide one between: the last is not taken past it */
    dgx_ge_frame_forget_colors(frame);
    memset(maps, 0xEE, sizeof maps);
    const uint8_t (*rgb)[3];
    const int hero_n = dgx_ge_pack_set_colors(pack, TEST_SET_HERO, &rgb), tiny_n = dgx_ge_pack_set_colors(pack, TEST_SET_TINY, &rgb);
    CHECK(TEST_SET_HERO < TEST_SET_WIDE && TEST_SET_WIDE < TEST_SET_TINY && tiny_n > 0);
    CHECK(!dgx_ge_frame_pack_colors(frame, pack, maps, (size_t)(hero_n + tiny_n), map_of));
    CHECK(map_of[TEST_SET_HERO] == maps && !map_of[TEST_SET_WIDE] && !map_of[TEST_SET_TINY] && maps[hero_n] == 0xEE);
    /* no room at all */
    CHECK(!dgx_ge_frame_pack_colors(frame, pack, NULL, 0, map_of));

    /* too few indices: 250 taken already, false, every set still mapped (to the nearest colors) */
    dgx_ge_frame_forget_colors(frame);
    static uint8_t gray[250][3], gray_map[250];
    for (int i = 0; i < 250; ++i) gray[i][0] = (uint8_t)i, gray[i][1] = 7, gray[i][2] = (uint8_t)(255 - i);
    CHECK(dgx_ge_frame_add_colors(frame, gray, 250, gray_map) && frame->colors_used == 250);
    CHECK(!dgx_ge_frame_pack_colors(frame, pack, maps, TEST_SET_COLOR_COUNT, map_of));
    size_t from = 0;
    for (int set = 0; set < TEST_SET_COUNT; ++set) {
        const uint8_t (*rgb)[3];
        CHECK(map_of[set] == maps + from);
        from += (size_t)dgx_ge_pack_set_colors(pack, set, &rgb);
    }
    CHECK(frame->colors_used <= 255);
    dgx_ge_frame_destroy(&frame);
}

/* ---- an actor as an item ---- */

/* the games' own (snake, tetris, hello: item_of and show_actor), for 4- and 8-bit pictures */
static bool games_item(const dgx_ge_pack_t *pack, const uint8_t *const *map_of, const dgx_ge_actor_t *a, dgx_image_t *image, int layer, dgx_ge_item_t *it)
{
    *it = (dgx_ge_item_t){.x     = (int16_t)(dgx_ge_actor_x(a) + (a->flip ? -a->image_dx : a->image_dx)),
                          .y     = (int16_t)(dgx_ge_actor_y(a) + a->image_dy),
                          .layer = (uint8_t)layer,
                          .flags = a->flip ? DGX_GE_BLIT_FLIP_X : 0};
    if (a->image < 0 || !dgx_ge_pack_image(pack, a->image, image)) return false;
    it->image = image;
    it->map   = map_of[dgx_ge_pack_image_set(pack, a->image)];
    return true;
}

static void test_items(const dgx_ge_pack_t *pack)
{
    dgx_screen_t    lcd   = display_of(320, 240);
    dgx_ge_frame_t *frame = dgx_ge_frame_fit(&lcd, 96, 64, 0, 0);
    static uint8_t        maps[TEST_SET_COLOR_COUNT];
    static const uint8_t *map_of[TEST_SET_COUNT];
    CHECK(frame && dgx_ge_frame_pack_colors(frame, pack, maps, sizeof maps, map_of));
    dgx_ge_scene_t *scene = frame ? dgx_ge_scene_create(frame, 3, 4, 2) : NULL;
    CHECK(scene);
    if (!scene) return;

    /* every picture of 4 and 8 bits, every way round, with offsets forward and back, up and down */
    static const int8_t offsets[][2] = {{0, 0}, {3, -2}, {-5, 4}, {127, -128}};
    int                 compared     = 0;
    for (int id = -1; id < pack->images; ++id)
        for (int flip = 0; flip < 2; ++flip)
            for (unsigned o = 0; o < sizeof offsets / sizeof offsets[0]; ++o) {
                dgx_image_t probe;
                if (id >= 0 && (!dgx_ge_pack_image(pack, id, &probe) || probe.format == 1)) continue;
                dgx_ge_actor_t a;
                memset(&a, 0, sizeof a);
                a.x = (40 + id) << 8, a.y = 30 << 8, a.image = (int16_t)id, a.flip = flip, a.image_dx = offsets[o][0], a.image_dy = offsets[o][1];
                dgx_image_t   mine, theirs;
                dgx_ge_item_t expected, got = {.layer = 2};
                const bool    shown = games_item(pack, map_of, &a, &theirs, 2, &expected);
                CHECK(dgx_ge_item_actor(&got, &a, pack, map_of, &mine) == shown);
                if (!shown) continue;
                ++compared;
                CHECK(got.image == &mine && mine.width == theirs.width && mine.height == theirs.height && mine.origin_x == theirs.origin_x &&
                      mine.origin_y == theirs.origin_y && mine.format == theirs.format && mine.data == theirs.data);
                got.image = expected.image; /* the same picture, read into another place */
                CHECK(dgx_ge_item_same(&got, &expected));
                /* as an actor of the scene: the item the scene keeps is that one */
                dgx_ge_scene_actors_begin(scene);
                CHECK(dgx_ge_scene_show_actor(scene, &a, 2, pack, map_of, &mine) && scene->actor_count == 1);
                if (scene->actor_count == 1) {
                    dgx_ge_item_t kept = scene->actors[0];
                    CHECK(kept.image == &mine);
                    kept.image = expected.image;
                    CHECK(dgx_ge_item_same(&kept, &expected));
                }
            }
    CHECK(compared >= 2 * 4 * 3); /* FOUR, EIGHT, BOX at least */

    /* the rest of the item stays the game's: its key, its clip, its other flags; its flip follows the actor's either way */
    dgx_ge_actor_t a;
    memset(&a, 0, sizeof a);
    a.x = 10 << 8, a.y = 12 << 8, a.image = TEST_IMG_FOUR, a.image_dx = 2, a.image_dy = 1;
    dgx_image_t   image;
    dgx_ge_item_t it = {.layer = 1, .key = 9, .clip = dgx_ge_rect(1, 2, 3, 4), .flags = DGX_GE_ITEM_OPEN_SPACE | DGX_GE_BLIT_FLIP_X};
    CHECK(dgx_ge_item_actor(&it, &a, pack, map_of, &image) && it.x == 12 && it.y == 13 && it.layer == 1 && it.key == 9 &&
          dgx_ge_rect_equal(it.clip, dgx_ge_rect(1, 2, 3, 4)) && it.flags == DGX_GE_ITEM_OPEN_SPACE && it.map == map_of[TEST_SET_HERO]);
    a.flip = true;
    CHECK(dgx_ge_item_actor(&it, &a, pack, map_of, &image) && it.x == 8 && it.flags == (DGX_GE_ITEM_OPEN_SPACE | DGX_GE_BLIT_FLIP_X));

    /* a picture of 1 bit: no map, its color the frame's of that color of its set */
    dgx_ge_item_t one = {.color = 2};
    CHECK(dgx_ge_item_picture(&one, pack, map_of, TEST_IMG_ONE, &image) && one.image == &image && !one.map && one.color == map_of[TEST_SET_HERO][1]);
    dgx_ge_item_t plain = {.color = 0};
    CHECK(dgx_ge_item_picture(&plain, pack, map_of, TEST_IMG_ONE, &image) && plain.color == 0);
    /* no maps: the values themselves */
    dgx_ge_item_t raw = {.color = 2};
    CHECK(dgx_ge_item_picture(&raw, pack, NULL, TEST_IMG_FOUR, &image) && !raw.map);
    CHECK(dgx_ge_item_picture(&raw, pack, NULL, TEST_IMG_ONE, &image) && raw.color == 2);
    /* no such picture */
    CHECK(!dgx_ge_item_picture(&raw, pack, map_of, -1, &image) && !dgx_ge_item_picture(&raw, pack, map_of, pack->images, &image));

    /* no room in the scene: false (2 actors at most) */
    dgx_ge_scene_actors_begin(scene);
    a.flip = false;
    dgx_image_t images[3];
    CHECK(dgx_ge_scene_show_actor(scene, &a, 0, pack, map_of, &images[0]) && dgx_ge_scene_show_actor(scene, &a, 0, pack, map_of, &images[1]));
    CHECK(!dgx_ge_scene_show_actor(scene, &a, 0, pack, map_of, &images[2]) && scene->actor_count == 2);
    a.image = -1;
    dgx_ge_scene_actors_begin(scene);
    CHECK(!dgx_ge_scene_show_actor(scene, &a, 0, pack, map_of, &images[0]) && scene->actor_count == 0); /* nothing to show */
    dgx_ge_scene_destroy(&scene);
    dgx_ge_frame_destroy(&frame);
}

int main(int argc, char **argv)
{
    size_t        size = 0;
    uint8_t      *data = read_file(argc > 1 ? argv[1] : "build/test.pack", &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    if (!data) CHECK_DONE();
    test_fit();
    test_colors(&pack);
    test_items(&pack);
    free(data);
    CHECK_DONE();
}
