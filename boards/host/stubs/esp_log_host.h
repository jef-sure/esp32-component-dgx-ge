#pragma once
/*
 * After DGX's esp_log.h (host.mk includes both in every file, this one second): there ESP_LOGI says nothing, here it is
 * told on stderr as the errors and the warnings are, as a board tells its log on the serial port. ESP_LOGD and
 * ESP_LOGV stay silent.
 */
#include <stdio.h>

#undef ESP_LOGI
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "I %s: " fmt "\n", tag, ##__VA_ARGS__)
