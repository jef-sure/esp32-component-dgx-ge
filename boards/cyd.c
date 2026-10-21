/* The CYD, ESP32-2432S028R: an ESP32 with a 320x240 ILI9341 on SPI2 (MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, backlight 21). */
#include "bus/dgx_spi_esp32.h"
#include "dgx_draw.h"
#include "dgx_ge_board.h"
#include "driver/spi_common.h"
#include "drivers/ili9341.h"

/* 40 MHz: the bus divides 80 MHz by a whole number, and 80 MHz itself spoils pictures on a CYD */
#define CYD_SPI_HZ (40 * 1000 * 1000)

const uint32_t board_band = 0;              /* a strip a row: the display takes it as well as more */
const int      board_mm_w = 58, board_mm_h = 43; /* its 2.8 inches, lying (as DGX-DI's table of boards measured them) */

/* landscape (swap_xy): y of the screen along the columns of the panel, which dir_x counts */
#define CYD_DIR_X DgxScreenRightLeft
#define CYD_DIR_Y DgxScreenTopBottom

static bool turned; /* held the other way up: both axes the other way (the game's setting, board_turn) */

void board_turn(bool turn)
{
    turned = turn;
}

bool board_orient(dgx_screen_t *display, bool upside)
{
    /* the rows of the screen the other way: its y, the columns of the panel */
    dgx_orientation_t x = CYD_DIR_X, y = CYD_DIR_Y;
    if (turned) x = x == DgxScreenRightLeft ? DgxScreenLeftRight : DgxScreenRightLeft, y = y == DgxScreenTopBottom ? DgxScreenBottomTop : DgxScreenTopBottom;
    if (upside) x = x == DgxScreenRightLeft ? DgxScreenLeftRight : DgxScreenRightLeft;
    dgx_ili9341_orientation(display, x, y, true);
    return true;
}

/* no touch: on a 2.8-inch resistive panel there is nothing for a game in it (its scenes and buttons do) */
bool board_pointer_start(dgx_screen_t *display)
{
    return false;
}

bool board_pointer(uint32_t now_ms, int *x, int *y)
{
    return false;
}

struct dgx_di_device *board_touch_device(void)
{
    return NULL;
}

/* no pad: no touch, no PSRAM for its layer and mask */
const bool board_pad_able = false;

bool board_pad_show(dgx_screen_t *display, const uint8_t *layout, size_t size, uint8_t alpha)
{
    return false;
}

void board_pad_hide(dgx_screen_t *display)
{
}

uint32_t board_pad_buttons(void)
{
    return 0;
}

/* no sound: sound is I2S only, and the CYD has none (its DAC on GPIO26 is not used) */
const uint32_t board_sound_rate   = 0;
const uint8_t  board_sound_volume = 0;

bool board_sound_start(struct dgx_ge_mixer *mixer)
{
    (void)mixer;
    return false;
}

void board_sound_lock(void)
{
}

void board_sound_unlock(void)
{
}

bool board_display_init(board_t *cyd)
{
    cyd->bus = dgx_spi_init(SPI2_HOST, SPI_DMA_CH_AUTO, GPIO_NUM_13, GPIO_NUM_12, GPIO_NUM_14, GPIO_NUM_15, GPIO_NUM_2, CYD_SPI_HZ, 0);
    if (!cyd->bus) return false;
    cyd->lcd = dgx_ili9341_init(cyd->bus, GPIO_NUM_NC, GPIO_NUM_21, 16, DgxScreenRGB);
    if (!cyd->lcd) return false;
    board_orient(cyd->lcd, false);
    dgx_fill_rectangle(cyd->lcd, 0, 0, cyd->lcd->width, cyd->lcd->height, 0);
    return true;
}
