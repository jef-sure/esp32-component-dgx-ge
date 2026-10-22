#pragma once
/*
 * Before DGX's stub of the same name (boards/host/host.mk puts this directory first): there vTaskDelay does nothing,
 * here it is where the board shows the display in its window, reads the mouse and the keys, and sleeps.
 */
#include "freertos/FreeRTOS.h"

void vTaskDelay(TickType_t ticks);
