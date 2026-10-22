#pragma once
/* DGX's stub, and what of ESP-IDF's heap a game may ask besides: the computer's memory does not run out. */
#include_next "esp_heap_caps.h"

static inline size_t heap_caps_get_free_size(unsigned caps)
{
    (void)caps;
    return (size_t)1 << 30;
}
