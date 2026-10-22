#pragma once
/*
 * ESP-IDF's NVS on the computer (boards/host/host_nvs.c): a namespace a directory, a key a file in it, under the
 * directory DGX_GE_HOST_NVS names (nvs/ beside where the program runs when it names none). What a game keeps on a
 * board it keeps here with the same code: blobs, strings and whole numbers.
 */
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM 0x101 /* as ESP-IDF has it; not in every stub of esp_err.h */
#endif

#define ESP_ERR_NVS_BASE              0x1100
#define ESP_ERR_NVS_NOT_INITIALIZED   (ESP_ERR_NVS_BASE + 0x01)
#define ESP_ERR_NVS_NOT_FOUND         (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_TYPE_MISMATCH     (ESP_ERR_NVS_BASE + 0x03)
#define ESP_ERR_NVS_READ_ONLY         (ESP_ERR_NVS_BASE + 0x04)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE  (ESP_ERR_NVS_BASE + 0x05)
#define ESP_ERR_NVS_INVALID_NAME      (ESP_ERR_NVS_BASE + 0x06)
#define ESP_ERR_NVS_INVALID_HANDLE    (ESP_ERR_NVS_BASE + 0x07)
#define ESP_ERR_NVS_INVALID_LENGTH    (ESP_ERR_NVS_BASE + 0x0c)
#define ESP_ERR_NVS_NO_FREE_PAGES     (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x10)

typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;

esp_err_t nvs_open(const char *name_space, nvs_open_mode_t mode, nvs_handle_t *handle);
void      nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
esp_err_t nvs_erase_all(nvs_handle_t handle);

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length);
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value, size_t *length);

/* the whole numbers, each its own type: one read as another is ESP_ERR_NVS_TYPE_MISMATCH, as on a board */
#define DGX_GE_HOST_NVS_NUMBER(name, type)                                                                             \
    esp_err_t nvs_set_##name(nvs_handle_t handle, const char *key, type value);                                       \
    esp_err_t nvs_get_##name(nvs_handle_t handle, const char *key, type *out_value);
DGX_GE_HOST_NVS_NUMBER(i8, int8_t)
DGX_GE_HOST_NVS_NUMBER(u8, uint8_t)
DGX_GE_HOST_NVS_NUMBER(i16, int16_t)
DGX_GE_HOST_NVS_NUMBER(u16, uint16_t)
DGX_GE_HOST_NVS_NUMBER(i32, int32_t)
DGX_GE_HOST_NVS_NUMBER(u32, uint32_t)
DGX_GE_HOST_NVS_NUMBER(i64, int64_t)
DGX_GE_HOST_NVS_NUMBER(u64, uint64_t)
#undef DGX_GE_HOST_NVS_NUMBER
