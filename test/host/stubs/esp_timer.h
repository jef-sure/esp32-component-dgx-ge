#pragma once
#include <stdint.h>

/* The board's clock, microseconds: the test's, which says what time it is (dgx_ge_pace). */
int64_t esp_timer_get_time(void);
