#pragma once
/*
 * Before DGX's stub of the same name (the Makefile puts this directory first): there vTaskDelay does nothing, here it
 * is a function, the test's, which counts the ticks the loop gave (dgx_ge_pace).
 */
#include "freertos/FreeRTOS.h"

void vTaskDelay(TickType_t ticks);
