/*
 * The computer as a board: the game's own app_main on a PC, in a window of SDL2. It shows a board's display rather
 * than being one, so it is the size of the board the game is built for: HOST_DISPLAY_W x HOST_DISPLAY_H pixels,
 * HOST_MM_W x HOST_MM_H millimetres (host.mk sets them from HOST_DISPLAY and HOST_MM). The display a virtual screen of
 * DGX of 16 bits, shown in the window, which may be stretched; the mouse a finger of DGX-DI, so its pad and keyboard
 * are pressed with it as on a touch screen; the keys the board's own buttons, and typing for its keyboard; SDL audio
 * pulls the mixer; NVS on files (host_nvs.c). vTaskDelay, which no loop of a firmware goes without, is where the
 * window is shown and its events read: the game's loop is the program's. The window closed, the program ends.
 *
 * Variables of the environment for runs without anyone at the computer (a check, a picture for the docs):
 * DGX_GE_HOST_QUIT_MS, the program ends so many ms after it began; DGX_GE_HOST_SHOT, the display as it is then written
 * to that file (a PPM); DGX_GE_HOST_INPUT, what a hand would do, at ms from the start, the moments in order:
 *     "500 right+, 1500 right-, 2000 tap 640 400, 2600 a+, 2700 a-"
 * a button held (+) or let go (-), by the names of DGX_GE_KEY_* (left right up down a b x y start select), or a tap
 * of the mouse at x, y of the display (down for 60 ms). With SDL_VIDEODRIVER=dummy and SDL_AUDIODRIVER=dummy no window
 * opens and no sound plays.
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dgx_di.h"
#include "dgx_di_keyboard.h"
#include "dgx_di_pad.h"
#include "dgx_draw.h"
#include "dgx_ge_board.h"
#include "dgx_ge_input.h"
#include "dgx_ge_mixer.h"
#include "drivers/vscreen.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

#ifndef HOST_DISPLAY_W
#define HOST_DISPLAY_W 1280
#define HOST_DISPLAY_H 800
#endif
#ifndef HOST_MM_W
#define HOST_MM_W 217
#define HOST_MM_H 136
#endif

static const char *TAG = "host";

const uint32_t board_band = 0; /* the display is memory: the frame goes to it a strip a row, as fast as anything */
const int      board_mm_w = HOST_MM_W, board_mm_h = HOST_MM_H;

static SDL_Window   *window;
static SDL_Renderer *renderer;
static SDL_Texture  *texture;
static uint32_t     *argb;    /* the display as the window takes it */
static dgx_screen_t *display; /* the board's display: what the game draws on, what the window shows */
static uint32_t      shown_ms, quit_ms, start_ms;
static int           exit_code; /* 1 once app_main has given up */

int64_t esp_timer_get_time(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* The display as it is, into a PPM: for a run with no window (DGX_GE_HOST_SHOT). */
static void shoot(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f || !dgx_vscreen_to_argb8888(display, 0, 0, display->width, display->height, NULL, argb, display->width)) {
        ESP_LOGE(TAG, "no picture to %s", path);
        if (f) fclose(f);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", display->width, display->height);
    for (int i = 0; i < display->width * display->height; ++i) {
        const uint8_t rgb[3] = {(uint8_t)(argb[i] >> 16), (uint8_t)(argb[i] >> 8), (uint8_t)argb[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    ESP_LOGI(TAG, "the display written to %s", path);
}

static void quit(int code)
{
    const char *shot = getenv("DGX_GE_HOST_SHOT");
    if (shot && *shot && display) shoot(shot);
    SDL_Quit();
    exit(code);
}

/* ---- the display ---- */

bool board_display_init(board_t *board)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { /* the sound apart (board_sound_start): no audio is no reason for no display */
        ESP_LOGE(TAG, "SDL: %s", SDL_GetError());
        return false;
    }
    start_ms      = now_ms();
    const char *q = getenv("DGX_GE_HOST_QUIT_MS");
    if (q && *q) quit_ms = now_ms() + (uint32_t)atol(q);
    /* the window as large as the screen of the computer lets it be, the display a whole number of times in it */
    int           zoom = 1;
    SDL_Rect      usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0)
        while (HOST_DISPLAY_W * (zoom + 1) <= usable.w * 9 / 10 && HOST_DISPLAY_H * (zoom + 1) <= usable.h * 9 / 10) ++zoom;
    window   = SDL_CreateWindow("dgx-ge", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, HOST_DISPLAY_W * zoom, HOST_DISPLAY_H * zoom,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!renderer && window) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, HOST_DISPLAY_W, HOST_DISPLAY_H) : NULL;
    if (!texture) {
        ESP_LOGE(TAG, "SDL: %s", SDL_GetError());
        return false;
    }
    /* the display stretched with the window, its proportions kept; the mouse told in pixels of the display */
    SDL_RenderSetLogicalSize(renderer, HOST_DISPLAY_W, HOST_DISPLAY_H);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    argb    = malloc((size_t)HOST_DISPLAY_W * HOST_DISPLAY_H * sizeof *argb);
    display = argb ? dgx_vscreen_init(HOST_DISPLAY_W, HOST_DISPLAY_H, 16, DgxScreenRGB) : NULL;
    if (!display) {
        ESP_LOGE(TAG, "no display of %dx%d", HOST_DISPLAY_W, HOST_DISPLAY_H);
        return false;
    }
    dgx_fill_rectangle(display, 0, 0, display->width, display->height, 0);
    board->bus = NULL; /* a virtual screen has none */
    board->lcd = display;
    ESP_LOGI(TAG, "a display of %dx%d pixels, %dx%d mm, in a window of %dx%d", HOST_DISPLAY_W, HOST_DISPLAY_H, HOST_MM_W, HOST_MM_H,
             HOST_DISPLAY_W * zoom, HOST_DISPLAY_H * zoom);
    return true;
}

/* The window lies on a desk, not in a hand: never turned. A frame upside down sends its rows the other way itself. */
bool board_orient(dgx_screen_t *display, bool upside)
{
    (void)display;
    return !upside;
}

void board_turn(bool turned)
{
    (void)turned;
}

static void show(void)
{
    if (!dgx_vscreen_to_argb8888(display, 0, 0, display->width, display->height, NULL, argb, display->width)) return;
    SDL_UpdateTexture(texture, NULL, argb, display->width * (int)sizeof *argb);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

/* ---- the mouse, a finger ---- */

static struct {
    dgx_di_device_t device;
    int             x, y;
    bool            down;
} mouse;

static int mouse_read(dgx_di_device_t *device, dgx_di_point_t *points, int max)
{
    (void)device;
    if (!mouse.down || max < 1) return 0;
    points[0] = (dgx_di_point_t){.x = (int16_t)mouse.x, .y = (int16_t)mouse.y, .size = 0, .id = 0};
    return 1;
}

static dgx_di_pointer_t pointer;
static bool             pointing;

bool board_pointer_start(dgx_screen_t *display)
{
    /* one finger, its range the display's: no calibration, the mouse lands where it points */
    mouse.device = (dgx_di_device_t){.read = mouse_read, .fingers = 1, .x_min = 0, .x_max = (int16_t)(display->width - 1), .y_min = 0,
                                     .y_max = (int16_t)(display->height - 1)};
    dgx_di_pointer_init(&pointer, &mouse.device, 0, display);
    pointing = true;
    return true;
}

struct dgx_di_device *board_touch_device(void)
{
    return pointing ? &mouse.device : NULL;
}

/* ---- the pad and the keyboard of DGX-DI on the display, pressed with the mouse ---- */

const bool            board_pad_able = true;
static dgx_di_pad_t   pad;
static bool           padded;
static const uint8_t *pad_layout;

bool board_pad_show(dgx_screen_t *display, const uint8_t *layout, size_t size, uint8_t alpha)
{
    if (padded && layout != pad_layout) dgx_di_pad_free(&pad, display), padded = false;
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

const bool board_keyboard_able = true;
enum { KEYBOARD_TYPED = 32 };
static struct {
    dgx_di_keyboard_t kb;
    uint32_t          typed[KEYBOARD_TYPED];
    uint8_t           count;
} *keyboard;

static void typed_one(uint32_t c)
{
    if (keyboard && keyboard->count < KEYBOARD_TYPED) keyboard->typed[keyboard->count++] = c;
}

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
    SDL_StartTextInput(); /* the computer's own keyboard types too */
    return true;
}

void board_keyboard_close(void)
{
    if (!keyboard) return;
    SDL_StopTextInput();
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

bool board_pointer(uint32_t now, int *x, int *y)
{
    dgx_di_pointer_read(&pointer, now);
    if (keyboard)
        keyboard->count = (uint8_t)(keyboard->count + dgx_di_keyboard_take(&keyboard->kb, &pointer, now, keyboard->typed + keyboard->count,
                                                                         KEYBOARD_TYPED - keyboard->count));
    if (padded) dgx_di_pad_take(&pad, &pointer);
    return dgx_di_pointer_first(&pointer, x, y);
}

/* ---- the keys, the board's own buttons ---- */

const bool board_buttons_able = true;

static uint32_t scripted; /* the buttons DGX_GE_HOST_INPUT holds */

/* DGX_GE_HOST_INPUT: what is due by now, done (the buttons it holds, a tap of the mouse). */
static void script_input(uint32_t now)
{
    static const char *next;
    static bool        begun;
    static uint32_t    tap_up; /* the scripted tap lets go then */
    if (!begun) next = getenv("DGX_GE_HOST_INPUT"), begun = true;
    if (tap_up && (int32_t)(now - tap_up) >= 0) mouse.down = false, tap_up = 0;
    static const char *const names[] = {"left", "right", "up", "down", "a", "start", "select", "b", "x", "y"};
    static const uint32_t    bits[]  = {DGX_GE_KEY_LEFT, DGX_GE_KEY_RIGHT, DGX_GE_KEY_UP,  DGX_GE_KEY_DOWN, DGX_GE_KEY_A,
                                        DGX_GE_KEY_START, DGX_GE_KEY_SELECT, DGX_GE_KEY_B, DGX_GE_KEY_X,    DGX_GE_KEY_Y};
    while (next && *next) {
        unsigned at;
        char     word[16];
        int      used = 0;
        if (sscanf(next, " %u %15[a-z+-]%n", &at, word, &used) != 2) {
            ESP_LOGE(TAG, "DGX_GE_HOST_INPUT: not understood at \"%s\"", next);
            next = NULL;
            return;
        }
        if ((int32_t)(now - start_ms - at) < 0) return; /* not yet */
        const char *rest = next + used;
        if (!strcmp(word, "tap")) {
            int x, y, more = 0;
            if (sscanf(rest, " %d %d%n", &x, &y, &more) == 2) mouse.x = x, mouse.y = y, mouse.down = true, tap_up = now + 60, rest += more;
        } else {
            const size_t n    = strlen(word);
            const char   sign = n > 1 ? word[n - 1] : 0;
            size_t       i    = 0;
            if (sign == '+' || sign == '-') word[n - 1] = 0;
            while (i < sizeof names / sizeof names[0] && strcmp(word, names[i])) ++i;
            if ((sign != '+' && sign != '-') || i == sizeof names / sizeof names[0]) {
                ESP_LOGE(TAG, "DGX_GE_HOST_INPUT: \"%s\" is no button held (+) or let go (-), nor a tap, at \"%s\"", word, next);
                next = NULL;
                return;
            }
            scripted = sign == '+' ? scripted | bits[i] : scripted & ~bits[i];
        }
        while (*rest == ' ') ++rest;
        next = *rest == ',' ? rest + 1 : rest;
    }
}

uint32_t board_buttons(void)
{
    if (keyboard) return 0; /* the keys type now */
    static const struct {
        SDL_Scancode key;
        uint32_t     button;
    } keys[] = {
        {SDL_SCANCODE_LEFT, DGX_GE_KEY_LEFT}, {SDL_SCANCODE_RIGHT, DGX_GE_KEY_RIGHT}, {SDL_SCANCODE_UP, DGX_GE_KEY_UP},
        {SDL_SCANCODE_DOWN, DGX_GE_KEY_DOWN}, {SDL_SCANCODE_LSHIFT, DGX_GE_KEY_A},    {SDL_SCANCODE_RSHIFT, DGX_GE_KEY_A},
        {SDL_SCANCODE_Z, DGX_GE_KEY_B},       {SDL_SCANCODE_X, DGX_GE_KEY_X},         {SDL_SCANCODE_C, DGX_GE_KEY_Y},
        {SDL_SCANCODE_RETURN, DGX_GE_KEY_START}, {SDL_SCANCODE_SPACE, DGX_GE_KEY_SELECT},
    };
    const uint8_t *k    = SDL_GetKeyboardState(NULL);
    uint32_t       held = scripted;
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; ++i)
        if (k[keys[i].key]) held |= keys[i].button;
    return held;
}

/* ---- the sound: SDL's audio thread pulls the mixer ---- */

const uint32_t board_sound_rate   = 22050;
const uint8_t  board_sound_volume = 32; /* the computer's speakers: a little louder than the 10"'s near one */

static SDL_mutex *sound_lock;

static void sound_guard(bool take)
{
    if (take) SDL_LockMutex(sound_lock);
    else SDL_UnlockMutex(sound_lock);
}

static void sound_fill(void *mixer, Uint8 *stream, int bytes)
{
    dgx_ge_mixer_fill(mixer, (int16_t *)stream, bytes / (int)sizeof(int16_t));
}

bool board_sound_start(struct dgx_ge_mixer *mixer)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        ESP_LOGW(TAG, "no sound: %s", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want = {.freq = (int)board_sound_rate, .format = AUDIO_S16SYS, .channels = 1, .samples = 512, .callback = sound_fill,
                          .userdata = mixer},
                  got;
    sound_lock = SDL_CreateMutex();
    if (!sound_lock) return false;
    mixer->lock                 = sound_guard; /* before the audio thread: from its first fill on */
    const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (!dev) {
        ESP_LOGW(TAG, "no sound: %s", SDL_GetError());
        mixer->lock = NULL;
        return false;
    }
    SDL_PauseAudioDevice(dev, 0);
    return true;
}

/* ---- the loop: the window shown, its events read ---- */

static void events(void)
{
    for (SDL_Event e; SDL_PollEvent(&e);) {
        switch (e.type) {
        case SDL_QUIT: quit(exit_code); break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) mouse.down = e.type == SDL_MOUSEBUTTONDOWN, mouse.x = e.button.x, mouse.y = e.button.y;
            break;
        case SDL_MOUSEMOTION: mouse.x = e.motion.x, mouse.y = e.motion.y; break;
        case SDL_TEXTINPUT:
            /* the code points of what was typed, from UTF-8 */
            for (const unsigned char *s = (const unsigned char *)e.text.text; *s;) {
                uint32_t c = *s++;
                int      more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
                c &= more == 3 ? 0x07 : more == 2 ? 0x0f : more == 1 ? 0x1f : 0x7f;
                while (more-- && (*s & 0xc0) == 0x80) c = c << 6 | (*s++ & 0x3f);
                typed_one(c);
            }
            break;
        case SDL_KEYDOWN:
            if (keyboard && e.key.keysym.sym == SDLK_BACKSPACE) typed_one(0x08);
            if (keyboard && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER)) typed_one(0x0a);
            if (keyboard && e.key.keysym.sym == SDLK_ESCAPE) typed_one(0x1b);
            break;
        }
    }
    /* in the margins round the display (a window of other proportions): at its edge */
    mouse.x = mouse.x < 0 ? 0 : mouse.x >= HOST_DISPLAY_W ? HOST_DISPLAY_W - 1 : mouse.x;
    mouse.y = mouse.y < 0 ? 0 : mouse.y >= HOST_DISPLAY_H ? HOST_DISPLAY_H - 1 : mouse.y;
}

void vTaskDelay(TickType_t ticks)
{
    events();
    const uint32_t now = now_ms();
    script_input(now);
    if (quit_ms && (int32_t)(now - quit_ms) >= 0) quit(exit_code);
    if (display && now - shown_ms >= 15) show(), shown_ms = now; /* at most as often as the screen of the computer */
    SDL_Delay(ticks * portTICK_PERIOD_MS);
}

void app_main(void);

int main(void)
{
    app_main();
    /* the game gave up (it said why): at once when the display was never set up, or the run has an end (QUIT_MS); else
       the window stays until it is closed */
    ESP_LOGE(TAG, "app_main returned");
    exit_code = 1;
    if (!display || quit_ms) quit(exit_code);
    for (;;) vTaskDelay(pdMS_TO_TICKS(20));
}
