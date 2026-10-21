/*
 * The Guition JC8012P4A1C: an ESP32-P4 with a 10.1-inch 800x1280 JD9365 on two MIPI-DSI lanes of 1000 Mbit/s (the
 * PHY powered by LDO 3 at 2.5 V, reset 27, backlight 23), lying: 1280x800 (a frame of 320x200 four times larger fills
 * it). Its touch panel's firmware is the game's to embed: jc8012p4a1c_touch.fw, see board_pointer_start.
 */
#include "bus/dgx_dsi_esp32.h"
#include "dgx_ge_board.h"
#include "dgx_draw.h"
#include "driver/gpio.h"
#include "drivers/jd9365.h"
#include "dgx_ge_mixer.h"
#include "dgx_di_gsl3680.h"
#include "dgx_di_pad.h"
#include "dgx_ge_input.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

enum { P4_LANES = 2, P4_LANE_MBPS = 1000, P4_PHY_LDO_CHAN = 3, P4_PHY_LDO_MV = 2500, P4_RST = 27, P4_BL = 23 };

/* the turned panel takes large pieces by DMA far faster than strips: 128 KB of PSRAM (25 rows of a 320-wide frame at x4) */
const uint32_t board_band = 128 * 1024;
const int      board_mm_w = 217, board_mm_h = 136; /* its 10.1 inches, lying (measured) */

/*
 * Lying: x of the screen along the rows of the panel (swap_xy), its y along the columns, which dir_x counts; one of the
 * two counted the other way, or the picture would be mirrored.
 */
#define P4_DIR_X DgxScreenRightLeft
#define P4_DIR_Y DgxScreenTopBottom

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
    /* the rows of the screen the other way: its y, the columns of the panel; the bus places the pixels so */
    dgx_orientation_t x = P4_DIR_X, y = P4_DIR_Y;
    if (turned) x = x == DgxScreenRightLeft ? DgxScreenLeftRight : DgxScreenRightLeft, y = y == DgxScreenTopBottom ? DgxScreenBottomTop : DgxScreenTopBottom;
    if (upside) x = x == DgxScreenRightLeft ? DgxScreenLeftRight : DgxScreenRightLeft;
    dgx_jd9365_orientation(display, x, y, true);
    return true;
}

bool board_display_init(board_t *p4)
{
    gpio_set_direction((gpio_num_t)P4_BL, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)P4_BL, 1);
    p4->bus = dgx_dsi_init(0, P4_LANES, P4_LANE_MBPS, P4_PHY_LDO_CHAN, P4_PHY_LDO_MV);
    if (!p4->bus) return false;
    /* two frame buffers: a frame of the game is one batch, shown whole from the next frame of the panel on */
    p4->lcd = dgx_jd9365_init(p4->bus, (gpio_num_t)P4_RST, 16, DgxScreenRGB, DgxDsiTwoFrames);
    if (!p4->lcd) return false;
    board_orient(p4->lcd, false);
    dgx_fill_rectangle(p4->lcd, 0, 0, p4->lcd->width, p4->lcd->height, 0);
    return true;
}

/*
 * The board's one I2C bus (SDA 7, SCL 8): the codec, the touch panel, the clock, the connector CN4. Set up once, by
 * whichever asks first; NULL when it cannot be.
 */
enum { P4_I2C_SDA = 7, P4_I2C_SCL = 8 };

static i2c_master_bus_handle_t p4_i2c(void)
{
    static i2c_master_bus_handle_t i2c;
    const i2c_master_bus_config_t  bus = {.i2c_port = I2C_NUM_0, .sda_io_num = P4_I2C_SDA, .scl_io_num = P4_I2C_SCL,
                                          .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true};
    if (!i2c && i2c_new_master_bus(&bus, &i2c) != ESP_OK) i2c = NULL;
    return i2c;
}

/*
 * Touch: a GSL3680 on that bus (reset 22, int 21), a device of DGX-DI. The chip has no firmware of its own: it is
 * loaded at every start, and it is the vendor's, an array with no licence (the header esp_lcd_touch_gsl3680.h of the
 * board's package, JC8012P4A1C_I_W_Y.zip). So it is not here: the game embeds it as jc8012p4a1c_touch.fw (EMBED_FILES),
 * made from that header by DGX-DI's tools/gsl_firmware.py --range 1664x896 (the pages it writes, and the range of the
 * coordinates they give: 64 a channel of the sensor, 26 along and 14 across).
 */
enum { P4_TOUCH_RST = 22, P4_TOUCH_INT = 21 };

/*
 * Its calibration, measured on one board of this kind (another may differ a little) by DGX-DI's
 * dgx_di_calibrate_measure, 2026-10-19: 3 rounds of 9 targets, the screen lying 1280x800, the firmware's range
 * 1664x896. In the display's own orientation (800x1280), so it holds however the screen is turned. How the panel lies,
 * as the measure found it: swapped, its y the other way (flags 6); an affine for the shift, turn and stretch; three
 * points of each axis for what bends (the bottom pressed together); steadied 50/60 ms by how the panel blinked.
 * Touches were 18.1 pixels off on average (22 at worst) without it, 5.0 (11) with it. Measured anew (a game runs the
 * measure on board_touch_device), it is printed as C, ready to paste here.
 */
static const dgx_di_calibration_t touch_calibration = {.version = 1, .size = sizeof(dgx_di_calibration_t), .x_min = 0, .x_max = 1663, .y_min = 0, .y_max = 895, .width = 800, .height = 1280, .flags = 6, .knots_x = 3, .knots_y = 3, .rounds = 3, .affine = {65301, 579, 774524, -221, 66564, -691501}, .x = {{44, 48}, {407, 399}, {747, 751}}, .y = {{47, 48}, {642, 640}, {1230, 1231}}, .down_ms = 50, .up_ms = 60, .blink_ms = 22, .gap_ms = 0, .off_tenths = 181, .worst_tenths = 220, .left_tenths = 50, .left_worst_tenths = 110};

extern const uint8_t touch_fw_start[] asm("_binary_jc8012p4a1c_touch_fw_start");
extern const uint8_t touch_fw_end[] asm("_binary_jc8012p4a1c_touch_fw_end");

static dgx_di_gsl3680_t touch;

bool board_pointer_start(dgx_screen_t *display)
{
    i2c_master_bus_handle_t i2c = p4_i2c();
    if (!i2c || !dgx_di_gsl3680_init(&touch, i2c, P4_TOUCH_RST, P4_TOUCH_INT, 0, touch_fw_start, (size_t)(touch_fw_end - touch_fw_start)))
        return false;
    dgx_di_pointer_init(&pointer, &touch.device, 0, display);
    if (!dgx_di_pointer_calibrate(&pointer, &touch_calibration)) ESP_LOGE("touch", "the calibration is not this board's (the firmware's range, the display)");
    return true;
}

struct dgx_di_device *board_touch_device(void)
{
    return &touch.device;
}

/* The pad (DGX-DI's classic: a cross under the left thumb, A B X Y under the right, BACK and START in the middle). */
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

bool board_pointer(uint32_t now_ms, int *x, int *y)
{
    static bool was; /* the first finger down at the last read */
    dgx_di_pointer_read(&pointer, now_ms);
    if (padded) dgx_di_pad_take(&pad, &pointer); /* the pad's fingers its own: never the first */
    const bool down = dgx_di_pointer_first(&pointer, x, y);
    if (down && !was) ESP_LOGI("touch", "down at %d, %d of the display%s", *x, *y, turned ? " (turned)" : "");
    was = down;
    return down;
}

/*
 * Sound: an ES8311 on I2S0 (MCLK 13, BCLK 12, WS 10, DOUT 9), set up over the I2C bus, its amplifier NS4150 on GPIO20;
 * one channel, one speaker. A task of its own takes the mixer's next samples and writes them; the writes wait for the
 * I2S, which sets the pace.
 */
enum { P4_I2S_MCLK = 13, P4_I2S_BCLK = 12, P4_I2S_WS = 10, P4_I2S_DOUT = 9, P4_I2S_DIN = 11, P4_PA = 20 };
enum { P4_SOUND_FRAMES = 256 }; /* samples a write: 11.6 ms at 22050 */
enum { P4_VOLUME = 25 };        /* of the codec, 0..100: the recordings are loud (8 bits at full scale), the speaker near: a room */

const uint32_t board_sound_rate = 22050;
/* the speaker is near and loud, and samples at full scale are loud: low */
const uint8_t board_sound_volume = 16;

static SemaphoreHandle_t      sound_lock;
static esp_codec_dev_handle_t speaker;

void board_sound_lock(void)
{
    if (sound_lock) xSemaphoreTake(sound_lock, portMAX_DELAY);
}

void board_sound_unlock(void)
{
    if (sound_lock) xSemaphoreGive(sound_lock);
}

static void sound_task(void *arg)
{
    struct dgx_ge_mixer *mixer = arg;
    static int16_t       samples[P4_SOUND_FRAMES];
    for (;;) {
        board_sound_lock();
        dgx_ge_mixer_fill(mixer, samples, P4_SOUND_FRAMES);
        board_sound_unlock();
        esp_codec_dev_write(speaker, samples, sizeof(samples));
    }
}

bool board_sound_start(struct dgx_ge_mixer *mixer)
{
    static const char      *TAG = "sound";
    i2c_master_bus_handle_t i2c = p4_i2c();
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
           .gpio_cfg = {.mclk = P4_I2S_MCLK, .bclk = P4_I2S_BCLK, .ws = P4_I2S_WS, .dout = P4_I2S_DOUT, .din = I2S_GPIO_UNUSED},
    };
    /* on from the start: esp_codec_dev turns it off to set the format, and off before ever on is an error in the log */
    if (i2s_new_channel(&chan, &tx, NULL) != ESP_OK || i2s_channel_init_std_mode(tx, &std) != ESP_OK || i2s_channel_enable(tx) != ESP_OK) {
        ESP_LOGE(TAG, "no I2S");
        return false;
    }
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&(audio_codec_i2s_cfg_t){.port = I2S_NUM_0, .tx_handle = tx});
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&(audio_codec_i2c_cfg_t){.port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = i2c});
    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    es8311_codec_cfg_t           es   = {.ctrl_if = ctrl, .gpio_if = gpio, .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC, .pa_pin = P4_PA, .use_mclk = true};
    es.hw_gain.pa_voltage = 5.0, es.hw_gain.codec_dac_voltage = 3.3;
    const audio_codec_if_t *codec = data && ctrl && gpio ? es8311_codec_new(&es) : NULL;
    speaker = codec ? esp_codec_dev_new(&(esp_codec_dev_cfg_t){.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec, .data_if = data}) : NULL;
    esp_codec_dev_sample_info_t format = {.sample_rate = board_sound_rate, .channel = 1, .bits_per_sample = 16};
    if (!speaker || esp_codec_dev_open(speaker, &format) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "no ES8311");
        return false;
    }
    esp_codec_dev_set_out_vol(speaker, P4_VOLUME);
    sound_lock = xSemaphoreCreateMutex();
    if (!sound_lock || xTaskCreate(sound_task, "sound", 3072, mixer, 6, NULL) != pdPASS) {
        ESP_LOGE(TAG, "no task");
        return false;
    }
    ESP_LOGI(TAG, "ES8311 at %u Hz", (unsigned)board_sound_rate);
    return true;
}
