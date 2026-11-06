/*
 * What the engine gives every game alike on FreeRTOS (dgx_ge_freertos.h), over a board of its own here: the sound
 * started from a blob SOUNDS (dgx_ge_sound_open: the mixer at the board's rate and volume, under a lock, the source of
 * the board's task; none when the board has no sound, the blob is none or not SOUNDS, the voices do not fit, its task
 * does not start) and the end of a pass of the loop (dgx_ge_pace) against the line every game wrote, with the clock
 * and vTaskDelay the board's. Built twice: a board with sound, and one without (BOARD_MUTE). The mutex of FreeRTOS is
 * the stub's, stubs/freertos/semphr.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_freertos.h"
#include "dgx_ge_pack.h"
#include "freertos/task.h"
#include "test_pack.h"

#ifdef BOARD_MUTE
const uint32_t board_sound_rate = 0;
#else
const uint32_t board_sound_rate = 22050;
#endif
const uint8_t board_sound_volume = 77;

/* the board's task: started or not, where it takes its samples */
static bool                  starts = true;
static int                   started;
static dgx_ge_sound_source_t source;
static void                 *given;

static bool board_sound_start(dgx_ge_sound_source_t from, void *ctx, uint32_t *rate_out)
{
    ++started, source = from, given = ctx;
    CHECK(!rate_out); /* the engine was told the rate */
    return starts;
}

/* the board's clock and its pause: so many microseconds now, the ticks the loop gave */
static int64_t  now_us;
static int      delays;
static uint32_t delayed;

int64_t esp_timer_get_time(void)
{
    return now_us;
}

void vTaskDelay(TickType_t ticks)
{
    ++delays, delayed = ticks;
}

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

static void test_sound(const dgx_ge_pack_t *pack)
{
    size_t          size;
    const uint8_t  *blob = dgx_ge_pack_blob(pack, TEST_BLOB_SOUNDS, &size);
    dgx_ge_mixer_t  mixer;
    dgx_ge_sounds_t sounds;
    CHECK(blob);
    memset(&mixer, 0, sizeof mixer);
    dgx_ge_sound_t sound = dgx_ge_sound_open(&mixer, &sounds, blob, size, 3, board_sound_rate, board_sound_volume, board_sound_start);
#ifdef BOARD_MUTE
    /* no sound on the board: nothing opened, nothing started, nothing kept */
    CHECK(!sound.play && !sound.stop && !started && !mixer.voices);
#else
    CHECK(sound.play && sound.ctx == &mixer && started == 1 && given == &mixer);
    CHECK(sounds.count == TEST_SOUND_COUNT && mixer.sounds == &sounds && mixer.voice_count == 3 && mixer.rate == 22050 && mixer.channels == 1);
    CHECK(mixer.volume == board_sound_volume && mixer.lock);
    /* it plays through the mixer: a voice taken */
    dgx_ge_sound_play(&sound, TEST_SOUND_TONE);
    int busy = 0;
    for (int i = 0; i < mixer.voice_count; ++i) busy += mixer.voices[i].data != NULL;
    CHECK(busy == 1);
    /* the board's task takes the mixer's samples through the source: the tone sounds */
    int16_t samples[64];
    memset(samples, 0, sizeof samples);
    CHECK(source);
    source(given, samples, 64);
    int loud = 0;
    for (int i = 0; i < 64; ++i) loud += samples[i] != 0;
    CHECK(loud > 0);
    dgx_ge_mixer_free(&mixer);

    /* its task does not start: the mixer given back, no output */
    starts = false, started = 0;
    memset(&mixer, 0, sizeof mixer);
    sound = dgx_ge_sound_open(&mixer, &sounds, blob, size, 3, board_sound_rate, board_sound_volume, board_sound_start);
    CHECK(!sound.play && started == 1 && !mixer.voices && !mixer.lock);
    starts = true, started = 0;
#endif
    /* no blob, one that is not SOUNDS, no voices: none, the board's task never started */
    memset(&mixer, 0, sizeof mixer);
    CHECK(!dgx_ge_sound_open(&mixer, &sounds, NULL, 0, 3, board_sound_rate, board_sound_volume, board_sound_start).play && !started);
    const uint8_t *other = dgx_ge_pack_blob(pack, TEST_BLOB_SHOW, &size);
    CHECK(other && !dgx_ge_sound_open(&mixer, &sounds, other, size, 3, board_sound_rate, board_sound_volume, board_sound_start).play && !started);
    CHECK(!dgx_ge_sound_open(&mixer, &sounds, blob, 1, 3, board_sound_rate, board_sound_volume, board_sound_start).play && !started);
    blob = dgx_ge_pack_blob(pack, TEST_BLOB_SOUNDS, &size);
    CHECK(!dgx_ge_sound_open(&mixer, &sounds, blob, size, 0, board_sound_rate, board_sound_volume, board_sound_start).play && !started && !mixer.voices);
}

static void test_pace(void)
{
    /* as every game wrote it, for periods of 16 and 20 ms and every pass from none to well past the period */
    static const uint32_t periods[] = {16000, 20000, 1000, 999};
    for (unsigned p = 0; p < sizeof periods / sizeof periods[0]; ++p)
        for (int64_t spent = 0; spent <= 3 * (int64_t)periods[p]; spent += 37) {
            const int64_t t0 = 1000000007LL; /* a board's clock does not begin at 0 */
            now_us = t0 + spent, delays = 0;
            dgx_ge_pace(t0, periods[p]);
            const int64_t STEP_US = periods[p];
            const uint32_t games  = spent < STEP_US ? pdMS_TO_TICKS((STEP_US - spent) / 1000) + 1 : 1;
            CHECK(delays == 1 && delayed == games && delayed >= 1);
            if (spent >= STEP_US) CHECK(delayed == 1); /* late: one tick, nothing made up */
        }
    /* a pass of none at 16 ms: 16 ms and a tick; of 15.5 ms: a tick only */
    now_us = 5000, dgx_ge_pace(5000, 16000);
    CHECK(delayed == 17);
    now_us = 5000 + 15500, dgx_ge_pace(5000, 16000);
    CHECK(delayed == 1);
}

int main(int argc, char **argv)
{
    size_t        size = 0;
    uint8_t      *data = read_file(argc > 1 ? argv[1] : "build/test.pack", &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    if (!data) CHECK_DONE();
    test_sound(&pack);
    test_pace();
    free(data);
    CHECK_DONE();
}
