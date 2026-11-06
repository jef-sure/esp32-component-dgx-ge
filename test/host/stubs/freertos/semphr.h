#pragma once
/*
 * Before DGX's stub of the same name (the Makefile puts this directory first): there a semaphore is for one thread,
 * here a mutex is a mutex of the computer, the engine's lock of the sound (dgx_ge_sound_open) as a board's task and
 * the game take it. A binary semaphore as DGX's: taking one that was not given
 * would wait for ever, a mistake of the code, which ends the program.
 */
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"

typedef struct host_semaphore {
    pthread_mutex_t mutex;
    bool            is_mutex;
    int             given; /* a binary one's */
} *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    SemaphoreHandle_t s = calloc(1, sizeof *s);
    if (s && pthread_mutex_init(&s->mutex, NULL) != 0) free(s), s = NULL;
    if (s) s->is_mutex = true;
    return s;
}

static inline SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return calloc(1, sizeof(struct host_semaphore));
}

static inline void vSemaphoreDelete(SemaphoreHandle_t s)
{
    if (s && s->is_mutex) pthread_mutex_destroy(&s->mutex);
    free(s);
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    if (s->is_mutex) return pthread_mutex_unlock(&s->mutex) == 0 ? pdTRUE : pdFALSE;
    s->given = 1;
    return pdTRUE;
}

static inline BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *woken)
{
    if (woken) *woken = pdFALSE;
    return xSemaphoreGive(s);
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, uint32_t ticks)
{
    (void)ticks;
    if (s->is_mutex) return pthread_mutex_lock(&s->mutex) == 0 ? pdTRUE : pdFALSE;
    if (!s->given) {
        fprintf(stderr, "xSemaphoreTake: the semaphore was not given, the task would wait for ever\n");
        exit(EXIT_FAILURE);
    }
    s->given = 0;
    return pdTRUE;
}
