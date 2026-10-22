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

#include "assets/hello_pack.h"
#include "dgx_di_pad.h"
#include "dgx_ge_anim.h"
#include "dgx_ge_board.h"
#include "dgx_ge_frame.h"
#include "dgx_ge_input.h"
#include "dgx_ge_label.h"
#include "dgx_ge_mixer.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_scene.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fonts/TerminusTTFMedium12.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
static uint8_t         maps[HELLO_SET_COUNT][16]; /* the colors of each set: indices of the frame */
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

/* A picture of the pack as an item of the scene, in the colors of its set. */
static bool item_of(int id, dgx_image_t *image, dgx_ge_item_t *item)
{
    if (!dgx_ge_pack_image(&pack, id, image)) return false;
    item->image = image;
    item->map   = maps[dgx_ge_pack_image_set(&pack, id)];
    return true;
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
    const bool edge  = (dir < 0 && x <= 10) || (dir > 0 && x >= FRAME_W - 10);
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
    dgx_screen_t *lcd   = board.lcd;
    int           scale = lcd->width / FRAME_W < lcd->height / FRAME_H ? lcd->width / FRAME_W : lcd->height / FRAME_H;
    scale               = scale < 1 ? 1 : scale;
    dgx_ge_frame_t *frame = dgx_ge_frame_create(lcd, (lcd->width - FRAME_W * scale) / 2, (lcd->height - FRAME_H * scale) / 2, FRAME_W, FRAME_H);
    if (frame && scale > 1 && !dgx_ge_frame_scale(frame, scale)) frame = NULL;
    if (frame && board_band) dgx_ge_frame_band(frame, board_band);
    dgx_ge_scene_t *scene = frame ? dgx_ge_scene_create(frame, 2, 4, 4) : NULL;
    if (!scene) {
        ESP_LOGE(TAG, "no memory for the frame");
        return;
    }

    /* the colors: every set's, the sky the paper of the frame (index 0) */
    for (int set = 0; set < HELLO_SET_COUNT; ++set) {
        const uint8_t (*rgb)[3];
        const int n = dgx_ge_pack_set_colors(&pack, set, &rgb);
        dgx_ge_frame_add_colors(frame, rgb, n, maps[set]);
        if (set == HELLO_SET_UI) dgx_ge_frame_set_color(frame, 0, rgb[0][0], rgb[0][1], rgb[0][2]);
    }
    dgx_ge_scene_reset(scene, 0);

    /* the ground: the set of the scene, given once */
    static dgx_image_t ground;
    dgx_ge_item_t      item = {.x = 0, .y = GROUND_Y, .layer = LAYER_BACK};
    if (item_of(HELLO_IMG_GROUND, &ground, &item)) dgx_ge_scene_add(scene, &item);

    /* the label, and the count it shows */
    hops_load();
    dgx_ge_label_init(&label, frame, TerminusTTFMedium12(), 8, 6, 120, 14, DGX_GE_ALIGN_LEFT, LAYER_ACTORS, maps[HELLO_SET_UI][1]);
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
    if (board_sound_rate && samples && dgx_ge_sounds_open(&sounds, samples, size) && dgx_ge_mixer_init(&mixer, &sounds, 2, board_sound_rate, 1)) {
        if (board_sound_start(&mixer)) {
            sound = dgx_ge_mixer_output(&mixer);
            dgx_ge_mixer_volume(&mixer, board_sound_volume);
        } else {
            dgx_ge_mixer_free(&mixer);
        }
    }

    /* the pointer, where the board has one (a touch; the computer's mouse); the pad on the display only where the board has no buttons of its own */
    const bool pointing = board_pointer_start(lcd);
    const bool padded   = pointing && board_pad_able && !board_buttons_able &&
                        board_pad_show(lcd, dgx_di_pad_layout_classic, dgx_di_pad_layout_classic_size, 160);
    ESP_LOGI(TAG, "a frame of %dx%d, x%d, on a display of %dx%d; %s%s%s", FRAME_W, FRAME_H, scale, lcd->width, lcd->height,
             sound.play ? "sound, " : "", pointing ? "a pointer, " : "", padded ? "a pad" : board_buttons_able ? "buttons" : "no buttons");

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
        static dgx_image_t picture;
        dgx_ge_item_t      it = {.x = (int16_t)(dgx_ge_actor_x(&bot) + (bot.flip ? -bot.image_dx : bot.image_dx)),
                                 .y = (int16_t)(dgx_ge_actor_y(&bot) + bot.image_dy), .layer = LAYER_ACTORS,
                                 .flags = bot.flip ? DGX_GE_BLIT_FLIP_X : 0};
        if (bot.image >= 0 && item_of(bot.image, &picture, &it)) dgx_ge_scene_actor(scene, &it);
        dgx_ge_label_show(&label, scene);
        dgx_ge_scene_render(scene);

        const int64_t spent = esp_timer_get_time() - t0;
        vTaskDelay(spent < STEP_US ? pdMS_TO_TICKS((STEP_US - spent) / 1000) + 1 : 1);
    }
}
