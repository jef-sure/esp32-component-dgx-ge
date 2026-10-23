/*
 * hello: the smallest whole game of DGX-GE. One actor on a scene, a robot: it walks while an arrow is held and hops at
 * a tap or a button, with a sound; a label counts the hops, and the count is kept over a restart. What the robot does
 * is its script (anim/bot.anim), its pictures are art/; this file only puts them on the display and tells the robot
 * what happened.
 *
 * The same file builds for a board of the engine (idf.py, see CMakeLists.txt) and for the computer (make, see
 * Makefile): it talks to the hardware through dgx_ge_board.h only, and the board host of the engine is the computer.
 */
#include <stdio.h>

#include "esp_heap_caps.h"

#include "assets/hello_pack.h"
#include "dgx_di_pad.h"
#include "dgx_ge_anim.h"
#include "dgx_ge_board.h"
#include "dgx_ge_frame.h"
#include "dgx_ge_glue.h"
#include "dgx_ge_input.h"
#include "dgx_ge_label.h"
#include "dgx_ge_mixer.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_scene.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fonts/TerminusTTFMedium12.h"
#include "nvs_flash.h"

static const char *TAG = "hello";

/* the pack, embedded by the build (EMBED_FILES on a board, host.mk on the computer) */
extern const uint8_t hello_pack_start[] asm("_binary_hello_pack_start");
extern const uint8_t hello_pack_end[] asm("_binary_hello_pack_end");

enum { FRAME_W = 320, FRAME_H = 200 };   /* the game's own screen, points; the board shows it as large as it can */
enum { GROUND_Y = FRAME_H - 24 };        /* the top of the ground: where the robot's feet are */
enum { LAYER_BACK, LAYER_ACTORS };       /* the ground, then the robot and the label over it */
enum { SIGNAL_HOPPED = 1 };              /* what bot.anim says with signal 1 */
enum { STEP_US = 20000 };                /* a step of the loop every 20 ms at most */

static dgx_ge_pack_t   pack;
static uint8_t         maps[HELLO_SET_COLOR_COUNT]; /* the colors of the sets: indices of the frame, a byte a color */
static const uint8_t  *map_of[HELLO_SET_COUNT];     /* where the map of each set begins in maps */
static dgx_ge_anim_t   anim;
static dgx_ge_actor_t  bot;
static dgx_ge_label_t  label;
static dgx_ge_sounds_t sounds;
static dgx_ge_mixer_t  mixer;
static dgx_ge_sound_t  sound; /* the mixer as the game's output; none: nothing plays */
static uint32_t        hops;
static bool            stored; /* NVS is there: the count is kept */

/* The count kept over a restart, in NVS (on the computer: a file, boards/host/host_nvs.c). */
static void hops_load(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) e = nvs_flash_erase() == ESP_OK ? nvs_flash_init() : e;
    stored = e == ESP_OK;
    nvs_handle_t h;
    if (stored && nvs_open("hello", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u32(h, "hops", &hops) != ESP_OK) hops = 0;
        nvs_close(h);
    }
}

static void hops_keep(void)
{
    nvs_handle_t h;
    if (stored && nvs_open("hello", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "hops", hops);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* The label as the count is. */
static void hops_show(void)
{
    char text[24];
    snprintf(text, sizeof text, "hops %lu", (unsigned long)hops);
    dgx_ge_label_set(&label, text);
}

/* What the robot's script tells the game as it goes: a sound to play, a hop made. */
static void say(dgx_ge_actor_t *actor, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    if (what == DGX_GE_ANIM_SOUND) dgx_ge_sound_play(&sound, value);
    if (what == DGX_GE_ANIM_SIGNAL && value == SIGNAL_HOPPED) {
        ++hops;
        hops_show();
        hops_keep();
    }
}

/*
 * The buttons and the pointer, made the robot's events. An arrow: it turns that way if it stands, and walks, unless the
 * edge of the frame is in front of it; no arrow: it stops (after the stride it is in). A tap or A or B: a hop. The
 * events it has no rule for where it is (walk while it walks, a hop in the air) do nothing: bot.anim decides.
 */
static void control(const dgx_ge_input_t *input, uint32_t now)
{
    const int  dir   = input->held & DGX_GE_KEY_LEFT ? -1 : input->held & DGX_GE_KEY_RIGHT ? 1 : 0;
    const int  x     = dgx_ge_actor_x(&bot);
    /* it stops so far from a side that a hop on the way (12 points forward) still lands in the frame */
    const bool edge  = (dir < 0 && x <= 22) || (dir > 0 && x >= FRAME_W - 22);
    const bool stand = bot.script == HELLO_BOT_SCRIPT_STAND;
    if (dir && stand && !edge) bot.flip = dir < 0; /* its pictures face right */
    if (dir && !edge && (dir < 0) == bot.flip) dgx_ge_actor_event(&bot, HELLO_BOT_EVENT_WALK, now, say, NULL);
    else dgx_ge_actor_event(&bot, HELLO_BOT_EVENT_STOP, now, say, NULL);
    if ((input->pressed & (DGX_GE_KEY_A | DGX_GE_KEY_B)) || input->pointer.pressed) dgx_ge_actor_event(&bot, HELLO_BOT_EVENT_HOP, now, say, NULL);
}

void app_main(void)
{
    board_t board;
    if (!board_display_init(&board)) {
        ESP_LOGE(TAG, "no display");
        return;
    }
    if (!dgx_ge_pack_open(&pack, hello_pack_start, (size_t)(hello_pack_end - hello_pack_start))) {
        ESP_LOGE(TAG, "the pack does not hold together");
        return;
    }
    /* the game's screen as large as the display lets it be, in its middle: x1 on the CYD, x4 on the 10" */
    dgx_screen_t   *lcd   = board.lcd;
    dgx_ge_frame_t *frame = dgx_ge_frame_fit(lcd, FRAME_W, FRAME_H, 0, board_band);
    dgx_ge_scene_t *scene = frame ? dgx_ge_scene_create(frame, 2, 4, 4) : NULL;
    if (!scene) {
        ESP_LOGE(TAG, "no memory for the frame");
        return;
    }

    /* the colors: every set's, the sky the paper of the frame (index 0) */
    dgx_ge_frame_pack_colors(frame, &pack, maps, sizeof maps, map_of);
    const uint8_t (*sky)[3];
    if (dgx_ge_pack_set_colors(&pack, HELLO_SET_UI, &sky)) dgx_ge_frame_set_color(frame, 0, sky[0][0], sky[0][1], sky[0][2]);
    dgx_ge_scene_reset(scene, 0);

    /* the ground: the set of the scene, given once */
    static dgx_image_t ground;
    dgx_ge_item_t      item = {.x = 0, .y = GROUND_Y, .layer = LAYER_BACK};
    if (dgx_ge_item_picture(&item, &pack, map_of, HELLO_IMG_GROUND, &ground)) dgx_ge_scene_add(scene, &item);

    /* the label, and the count it shows */
    hops_load();
    dgx_ge_label_init(&label, frame, TerminusTTFMedium12(), 8, 6, 120, 14, DGX_GE_ALIGN_LEFT, LAYER_ACTORS, map_of[HELLO_SET_UI][1]);
    hops_show();

    /* the robot, standing in the middle */
    size_t         size;
    const uint8_t *blob = dgx_ge_pack_blob(&pack, HELLO_BLOB_BOT, &size);
    if (!blob || !dgx_ge_anim_open(&anim, blob, size)) {
        ESP_LOGE(TAG, "no animation of the robot");
        return;
    }
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    dgx_ge_actor_start(&bot, &anim, HELLO_BOT_SCRIPT_STAND, FRAME_W / 2, GROUND_Y - 1, now);

    /* the sound, where the board has it: the mixer at the board's rate, the board's task (on the computer, SDL) pulls it */
    const uint8_t *samples = dgx_ge_pack_blob(&pack, HELLO_BLOB_SOUNDS, &size);
    sound                  = board_sound_open(&mixer, &sounds, samples, size, 2);

    /* the pointer, where the board has one (a touch; the computer's mouse); the pad on the display only where the board has no buttons of its own */
    const bool pointing = board_pointer_start(lcd);
    const bool padded   = pointing && board_pad_able && !board_buttons_able &&
                        board_pad_show(lcd, dgx_di_pad_layout_classic, dgx_di_pad_layout_classic_size, 160);
    ESP_LOGI(TAG, "a frame of %dx%d, x%d, on a display of %dx%d; %s%s%s", FRAME_W, FRAME_H, frame->scale, lcd->width, lcd->height,
             sound.play ? "sound, " : "", pointing ? "a pointer, " : "", padded ? "a pad" : board_buttons_able ? "buttons" : "no buttons");

    ESP_LOGI(TAG, "%u bytes of RAM free, %u in one piece", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    /*
     * the cost of a frame, told once a second: of the frames that sent anything, their time and their pixels (on the
     * computer the computer's times, which say nothing of a board's)
     */
    uint32_t       logged = 0, changed = 0, pixels = 0;
    int64_t        changed_us = 0, worst_us = 0;
    dgx_ge_input_t input = {0};
    for (;;) {
        const int64_t t0 = esp_timer_get_time();
        now              = (uint32_t)(t0 / 1000);
        if (pointing) {
            int        x = 0, y = 0;
            const bool down = board_pointer(now, &x, &y);
            dgx_ge_input_pointer(&input, x, y, down);
        }
        dgx_ge_input_update(&input, board_buttons() | (padded ? board_pad_buttons() : 0));

        /* the steps whose time has come first, then the decisions on what it now is */
        dgx_ge_actor_advance(&bot, now, say, NULL);
        control(&input, now);

        /* what is seen in this frame: the robot and the label */
        dgx_ge_scene_actors_begin(scene);
        static dgx_image_t picture; /* the robot's, kept while the frame is drawn */
        dgx_ge_scene_show_actor(scene, &bot, LAYER_ACTORS, &pack, map_of, &picture);
        dgx_ge_label_show(&label, scene);
        const uint32_t before = frame->pixels_sent;
        dgx_ge_scene_render(scene);
        const int64_t took = esp_timer_get_time() - t0;
        if (frame->pixels_sent != before) {
            ++changed, changed_us += took, pixels += frame->pixels_sent - before;
            if (took > worst_us) worst_us = took;
        }
        if (now - logged >= 1000) {
            if (changed)
                ESP_LOGI(TAG, "%u pictures a second, %lld us each on average, %lld at worst, %u pixels each", (unsigned)changed,
                         (long long)(changed_us / changed), (long long)worst_us, (unsigned)(pixels / changed));
            changed = pixels = 0, changed_us = worst_us = 0, logged = now;
        }

        board_pace(t0, STEP_US); /* the rest of the step waited, a tick given up at least */
    }
}
