#pragma once
#include <stdint.h>

/* The computer's monotonic clock, microseconds (boards/host/host.c): as a board's, it does not begin at 0. */
int64_t esp_timer_get_time(void);
