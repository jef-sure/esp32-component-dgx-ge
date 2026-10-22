/*
 * The Guition JC1060P470: an ESP32-P4 of revision 1.3 with a 7-inch 1024x600 JD9165 on two MIPI-DSI lanes of 750
 * Mbit/s (the PHY powered by LDO 3 at 2.5 V, reset 0, backlight 23), lying as it is: no turn (a frame of 320x200 three
 * times larger is 960x600 in its middle). Its touch panel a GT911, by ESP-IDF's driver as a device of DGX-DI; its sound
 * an ES8311 on I2S, as the 10"'s, its amplifier on another pin (the board's schematic, JC1060P470C_I_W_Y-V1.0).
 */
#include <stdlib.h>
#include <string.h>

#include "bus/dgx_dsi_esp32.h"
#include "dgx_di_esp_lcd_touch.h"
#include "dgx_di_keyboard.h"
#include "dgx_di_pad.h"
#include "dgx_draw.h"
#include "dgx_ge_board.h"
#include "dgx_ge_input.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "dgx_ge_mixer.h"
#include "driver/i2s_std.h"
#include "drivers/jd9165.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

enum { P7_LANES = 2, P7_LANE_MBPS = 750, P7_PHY_LDO_CHAN = 3, P7_PHY_LDO_MV = 2500, P7_RST = 0, P7_BL = 23 };

/* a band of the scaled frame under overlays: 128 KB of PSRAM, 21 rows of a 320-wide frame at x3 */
const uint32_t board_band = 128 * 1024;
const int      board_mm_w = 154, board_mm_h = 86; /* its 7 inches, lying (as DGX-DI's table of boards measured them) */

/* lying as the panel is: x of the screen along its rows, y along its columns */
#define P7_DIR_X DgxScreenLeftRight
#define P7_DIR_Y DgxScreenTopBottom

static bool turned; /* held the other way up: both axes the other way (the game's setting, board_turn) */

static dgx_di_pointer_t pointer;

static bool pad_stale; /* the pad made for the display as it stood before a turn: made anew when shown next */

void board_turn(bool turn)
{
    turned    = turn; /* the touch turns with the screen: DGX-DI takes its orientation at every read */
    pad_stale = true; /* the pad does not: it is made for the display as it stands */
}

bool board_orient(dgx_screen_t *display, bool upside)
{
    /* the rows of the screen the other way: its y, the columns of the panel (nothing swapped here) */
    dgx_orientation_t x = P7_DIR_X, y = P7_DIR_Y;
    if (turned) x = x == DgxScreenRightLeft ? DgxScreenLeftRight : DgxScreenRightLeft, y = y == DgxScreenTopBottom ? DgxScreenBottomTop : DgxScreenTopBottom;
    if (upside) y = y == DgxScreenTopBottom ? DgxScreenBottomTop : DgxScreenTopBottom;
    dgx_jd9165_orientation(display, x, y, false);
    return true;
}

bool board_display_init(board_t *p7)
{
    gpio_set_direction((gpio_num_t)P7_BL, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)P7_BL, 1);
    p7->bus = dgx_dsi_init(0, P7_LANES, P7_LANE_MBPS, P7_PHY_LDO_CHAN, P7_PHY_LDO_MV);
    if (!p7->bus) return false;
    /* two frame buffers: a frame of the game is one batch, shown whole from the next frame of the panel on */
    p7->lcd = dgx_jd9165_init(p7->bus, (gpio_num_t)P7_RST, 16, DgxScreenRGB, DgxDsiTwoFrames);
    if (!p7->lcd) return false;
    board_orient(p7->lcd, false);
    dgx_fill_rectangle(p7->lcd, 0, 0, p7->lcd->width, p7->lcd->height, 0);
    return true;
}

/* The board's I2C bus (SDA 7, SCL 8): the touch panel, the codec and more. Set up once, by whichever asks first; NULL when it cannot be. */
enum { P7_I2C_SDA = 7, P7_I2C_SCL = 8 };

static i2c_master_bus_handle_t p7_i2c(void)
{
    static i2c_master_bus_handle_t i2c;
    const i2c_master_bus_config_t  bus = {.i2c_port = I2C_NUM_0, .sda_io_num = P7_I2C_SDA, .scl_io_num = P7_I2C_SCL,
                                          .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true};
    if (!i2c && i2c_new_master_bus(&bus, &i2c) != ESP_OK) i2c = NULL;
    return i2c;
}

/*
 * Its calibration, measured on one board of this kind (another may differ a little) by DGX-DI's
 * dgx_di_calibrate_measure, 2026-10-19: 3 rounds of 9 targets, the screen lying 1024x600 as the panel gives it (flags 0:
 * the GT911 gives its points as the display lies), an affine and three points of each axis. Touches were 10.1 pixels
 * off on average (21 at worst) without it, 3.3 (7) with it. Measured before the steadying of 60 ms came: up_ms set to
 * it by hand (the GT911 does not blink). Measured anew (a game runs the measure on board_touch_device), it is printed
 * as C, ready to paste here.
 */
static const dgx_di_calibration_t touch_calibration = {.version = 1, .size = sizeof(dgx_di_calibration_t), .x_min = 0, .x_max = 1023, .y_min = 0, .y_max = 599, .width = 1024, .height = 600, .flags = 0, .knots_x = 3, .knots_y = 3, .rounds = 3, .affine = {66150, 221, -683737, 460, 66859, -1265431}, .x = {{48, 48}, {512, 512}, {975, 975}}, .y = {{49, 48}, {298, 300}, {552, 551}}, .down_ms = 0, .up_ms = 60, .blink_ms = 0, .gap_ms = 0, .off_tenths = 101, .worst_tenths = 210, .left_tenths = 33, .left_worst_tenths = 70};

/*
 * Touch: a GT911 on that bus, five fingers, each with its own number. Its reset and interrupt are not the ESP32's to
 * drive, so its address is the one it took at power-up by its interrupt's level: 0x5D (this board's), else 0x14.
 */
static dgx_di_esp_lcd_touch_t touch;

bool board_pointer_start(dgx_screen_t *display)
{
    i2c_master_bus_handle_t i2c = p7_i2c();
    if (!i2c) return false;
    static const uint8_t         address[] = {ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP};
    const esp_lcd_touch_config_t config    = {.x_max = 1024, .y_max = 600, .rst_gpio_num = GPIO_NUM_NC, .int_gpio_num = GPIO_NUM_NC};
    esp_lcd_touch_handle_t       handle    = NULL;
    for (int i = 0; i < 2 && !handle; ++i) {
        esp_lcd_panel_io_handle_t     io;
        esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        io_config.dev_addr = address[i], io_config.scl_speed_hz = 400000;
        if (esp_lcd_new_panel_io_i2c(i2c, &io_config, &io) != ESP_OK) continue;
        if (esp_lcd_touch_new_i2c_gt911(io, &config, &handle) != ESP_OK) esp_lcd_panel_io_del(io); /* off the bus: the next address */
    }
    if (!handle) {
        ESP_LOGE("touch", "no GT911");
        return false;
    }
    dgx_di_esp_lcd_touch_init(&touch, handle, 5, true);
    dgx_di_pointer_init(&pointer, &touch.device, 0, display);
    if (!dgx_di_pointer_calibrate(&pointer, &touch_calibration)) ESP_LOGE("touch", "the calibration is not this board's (the panel's range, the display)");
    return true;
}

struct dgx_di_device *board_touch_device(void)
{
    return &touch.device;
}

/* The pad (DGX-DI's: a cross under the left thumb, buttons under the right, BACK and START in the middle). */
_Static_assert((unsigned)DGX_DI_PAD_LEFT == DGX_GE_KEY_LEFT && (unsigned)DGX_DI_PAD_RIGHT == DGX_GE_KEY_RIGHT && (unsigned)DGX_DI_PAD_UP == DGX_GE_KEY_UP &&
                   (unsigned)DGX_DI_PAD_DOWN == DGX_GE_KEY_DOWN && (unsigned)DGX_DI_PAD_A == DGX_GE_KEY_A && (unsigned)DGX_DI_PAD_START == DGX_GE_KEY_START &&
                   (unsigned)DGX_DI_PAD_BACK == DGX_GE_KEY_BACK && (unsigned)DGX_DI_PAD_B == DGX_GE_KEY_B && (unsigned)DGX_DI_PAD_X == DGX_GE_KEY_X &&
                   (unsigned)DGX_DI_PAD_Y == DGX_GE_KEY_Y,
               "the pad's buttons are the engine's");
const bool board_pad_able = true;

static dgx_di_pad_t   pad;
static bool           padded;
static const uint8_t *pad_layout; /* what it was made of */

bool board_pad_show(dgx_screen_t *display, const uint8_t *layout, size_t size, uint8_t alpha)
{
    if (padded && (pad_stale || layout != pad_layout)) dgx_di_pad_free(&pad, display), padded = false; /* made anew */
    pad_stale = false;
    if (!padded) {
        if (!dgx_di_pad_open(&pad, display, board_mm_w, board_mm_h, layout, size)) return false;
        padded = true, pad_layout = layout;
    }
    if (dgx_di_pad_show(&pad, display, alpha)) return true;
    dgx_di_pad_free(&pad, display), padded = false;
    return false;
}

void board_pad_hide(dgx_screen_t *display)
{
    if (padded) dgx_di_pad_free(&pad, display), padded = false;
}

uint32_t board_pad_buttons(void)
{
    return padded ? pad.buttons : 0;
}

/*
 * The keyboard on the display (DGX-DI's), its memory only while it is open: its struct and what it typed for the game to
 * take (board_keyboard_typed). It takes its fingers in board_pointer, before the pad and the game.
 */
const bool board_keyboard_able = true;

enum { KEYBOARD_TYPED = 16 };
static struct {
    dgx_di_keyboard_t kb;
    uint32_t          typed[KEYBOARD_TYPED];
    uint8_t           count;
} *keyboard;

bool board_keyboard_open(dgx_screen_t *display, const uint8_t *layout, size_t size, dgx_font_t *font, dgx_ge_rect_t field, dgx_ge_rect_t *band)
{
    board_keyboard_close();
    if (!(keyboard = calloc(1, sizeof *keyboard))) return false;
    const dgx_di_rect_t away = {.x = (int16_t)field.x, .y = (int16_t)field.y, .w = (int16_t)field.w, .h = (int16_t)field.h};
    if (!dgx_di_keyboard_open(&keyboard->kb, display, board_mm_w, board_mm_h, layout, size, font, &away)) {
        free(keyboard), keyboard = NULL;
        return false;
    }
    *band = dgx_ge_rect(keyboard->kb.x, keyboard->kb.y, keyboard->kb.w, keyboard->kb.h);
    return true;
}

void board_keyboard_close(void)
{
    if (!keyboard) return;
    dgx_di_keyboard_close(&keyboard->kb);
    free(keyboard), keyboard = NULL;
}

int board_keyboard_typed(uint32_t *typed, int max)
{
    const int n = keyboard ? (keyboard->count < max ? keyboard->count : max) : 0;
    if (!n) return 0;
    memcpy(typed, keyboard->typed, (size_t)n * sizeof *typed);
    memmove(keyboard->typed, keyboard->typed + n, (size_t)(keyboard->count - n) * sizeof *typed);
    keyboard->count = (uint8_t)(keyboard->count - n);
    return n;
}

/* no buttons of its own: a game's buttons come from the pad on its display, if any */
const bool board_buttons_able = false;

uint32_t board_buttons(void)
{
    return 0;
}

bool board_pointer(uint32_t now_ms, int *x, int *y)
{
    static bool was; /* the first finger down at the last read */
    dgx_di_pointer_read(&pointer, now_ms);
    if (keyboard) /* the keyboard's fingers its own: never the pad's, never the first */
        keyboard->count = (uint8_t)(keyboard->count + dgx_di_keyboard_take(&keyboard->kb, &pointer, now_ms, keyboard->typed + keyboard->count,
                                                                         KEYBOARD_TYPED - keyboard->count));
    if (padded) dgx_di_pad_take(&pad, &pointer); /* the pad's fingers its own: never the first */
    const bool down = dgx_di_pointer_first(&pointer, x, y);
    if (down && !was) ESP_LOGI("touch", "down at %d, %d of the display%s", *x, *y, turned ? " (turned)" : "");
    was = down;
    return down;
}

/*
 * Sound: an ES8311 on I2S0 (MCLK 13, SCLK 12, LRCK 10, its DAC's input DSDIN 9), set up over the I2C bus (0x18), its
 * amplifier NS4150 on GPIO11 (the 10"'s on GPIO20); one channel, one speaker. A task of its own takes the mixer's next
 * samples and writes them; the writes wait for the I2S, which sets the pace.
 */
enum { P7_I2S_MCLK = 13, P7_I2S_BCLK = 12, P7_I2S_WS = 10, P7_I2S_DOUT = 9, P7_PA = 11 };
enum { P7_SOUND_FRAMES = 256 }; /* samples a write: 11.6 ms at 22050 */
enum { P7_VOLUME = 25 };        /* of the codec, 0..100: the recordings are loud (8 bits at full scale), the speaker near: a room */

const uint32_t board_sound_rate = 22050;
/* as the 10"'s, for a start: the speaker near and loud, samples at full scale loud */
const uint8_t board_sound_volume = 16;

static SemaphoreHandle_t      sound_lock;
static esp_codec_dev_handle_t speaker;

/* the mixer's lock: the task fills while the game plays and stops */
static void sound_guard(bool take)
{
    if (take) xSemaphoreTake(sound_lock, portMAX_DELAY);
    else xSemaphoreGive(sound_lock);
}

static void sound_task(void *arg)
{
    struct dgx_ge_mixer *mixer = arg;
    static int16_t       samples[P7_SOUND_FRAMES];
    bool                 told = false;
    for (;;) {
        dgx_ge_mixer_fill(mixer, samples, P7_SOUND_FRAMES);
        if (esp_codec_dev_write(speaker, samples, sizeof(samples)) == ESP_CODEC_DEV_OK) continue;
        /* not written: as long as they would have sounded, the mixer's time going on, the task never spinning */
        if (!told) {
            ESP_LOGE("sound", "the codec takes no samples");
            told = true;
        }
        vTaskDelay(pdMS_TO_TICKS(P7_SOUND_FRAMES * 1000 / board_sound_rate) + 1);
    }
}

bool board_sound_start(struct dgx_ge_mixer *mixer)
{
    static const char      *TAG = "sound";
    i2c_master_bus_handle_t i2c = p7_i2c();
    if (!i2c) {
        ESP_LOGE(TAG, "no I2C");
        return false;
    }
    i2s_chan_handle_t        tx;
    i2s_chan_config_t        chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_frame_num            = 256; /* what the driver makes of the default 240 anyway (a buffer a multiple of 64), saying so */
    const i2s_std_config_t   std  = {
           .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(board_sound_rate),
           .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
           .gpio_cfg = {.mclk = P7_I2S_MCLK, .bclk = P7_I2S_BCLK, .ws = P7_I2S_WS, .dout = P7_I2S_DOUT, .din = I2S_GPIO_UNUSED},
    };
    /* on from the start: esp_codec_dev turns it off to set the format, and off before ever on is an error in the log */
    if (i2s_new_channel(&chan, &tx, NULL) != ESP_OK || i2s_channel_init_std_mode(tx, &std) != ESP_OK || i2s_channel_enable(tx) != ESP_OK) {
        ESP_LOGE(TAG, "no I2S");
        return false;
    }
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&(audio_codec_i2s_cfg_t){.port = I2S_NUM_0, .tx_handle = tx});
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&(audio_codec_i2c_cfg_t){.port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = i2c});
    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    es8311_codec_cfg_t           es   = {.ctrl_if = ctrl, .gpio_if = gpio, .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC, .pa_pin = P7_PA, .use_mclk = true};
    es.hw_gain.pa_voltage = 5.0, es.hw_gain.codec_dac_voltage = 3.3;
    const audio_codec_if_t *codec = data && ctrl && gpio ? es8311_codec_new(&es) : NULL;
    speaker = codec ? esp_codec_dev_new(&(esp_codec_dev_cfg_t){.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec, .data_if = data}) : NULL;
    esp_codec_dev_sample_info_t format = {.sample_rate = board_sound_rate, .channel = 1, .bits_per_sample = 16};
    if (!speaker || esp_codec_dev_open(speaker, &format) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "no ES8311");
        return false;
    }
    esp_codec_dev_set_out_vol(speaker, P7_VOLUME);
    sound_lock = xSemaphoreCreateMutex();
    if (sound_lock) mixer->lock = sound_guard; /* before the task: from its first fill on */
    if (!sound_lock || xTaskCreate(sound_task, "sound", 3072, mixer, 6, NULL) != pdPASS) {
        ESP_LOGE(TAG, "no task");
        return false;
    }
    ESP_LOGI(TAG, "ES8311 at %u Hz", (unsigned)board_sound_rate);
    return true;
}
