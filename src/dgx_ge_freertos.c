#include "dgx_ge_freertos.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

void dgx_ge_pace(int64_t t0_us, uint32_t period_us)
{
    const int64_t spent = esp_timer_get_time() - t0_us;
    vTaskDelay(spent < (int64_t)period_us ? pdMS_TO_TICKS(((int64_t)period_us - spent) / 1000) + 1 : 1);
}

/* the mixer's lock: the board's task fills while the game plays and stops */
static SemaphoreHandle_t sound_lock;

static void sound_guard(bool take)
{
    if (take) xSemaphoreTake(sound_lock, portMAX_DELAY);
    else xSemaphoreGive(sound_lock);
}

/* the board's task takes the mixer's next samples: mono, a sample a frame */
static void sound_source(void *mixer, int16_t *samples, size_t count)
{
    dgx_ge_mixer_fill(mixer, samples, (int)count);
}

dgx_ge_sound_t dgx_ge_sound_open(dgx_ge_mixer_t *mixer, dgx_ge_sounds_t *sounds, const void *blob, size_t size, int voices, uint32_t rate,
                                 uint8_t volume, dgx_ge_sound_start_t start)
{
    const dgx_ge_sound_t none = {0};
    if (!dgx_ge_sounds_open(sounds, blob, size) || !dgx_ge_mixer_init(mixer, sounds, voices, rate, 1)) return none; /* a rate of 0: none */
    if (!sound_lock) sound_lock = xSemaphoreCreateMutex();
    if (sound_lock) mixer->lock = sound_guard; /* before the task: from its first fill on */
    if (!sound_lock || !start || !start(sound_source, mixer, NULL)) {
        dgx_ge_mixer_free(mixer);
        mixer->lock = NULL; /* no task fills it */
        return none;
    }
    dgx_ge_mixer_volume(mixer, volume);
    return dgx_ge_mixer_output(mixer);
}
