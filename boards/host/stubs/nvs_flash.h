#pragma once
/* The partition of NVS on the computer: always there, nothing to make ready (nvs.h). */
#include "nvs.h"

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_deinit(void);
esp_err_t nvs_flash_erase(void);
