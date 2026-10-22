/*
 * ESP-IDF's NVS on the computer (stubs/nvs.h): a namespace a directory, a key a file in it, under DGX_GE_HOST_NVS (nvs/
 * where the program runs, when it is not set). A file is one byte of its type, then the value: a key read as another
 * type is ESP_ERR_NVS_TYPE_MISMATCH, as on a board. A value is written whole when it is set (a file beside it, then
 * renamed over it): commit has nothing left to do. Names as long as NVS lets them be, 15 characters.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "nvs_flash.h"

enum { NAME_MAX_LEN = 15, OPEN_MAX = 16 };
enum { TYPE_BLOB = 'b', TYPE_STR = 's', TYPE_NUMBER = '0' }; /* a whole number: '0' + its size in bytes, signed or not alike */

static struct {
    char name[NAME_MAX_LEN + 1];
    bool used, writable;
} opened[OPEN_MAX];

static const char *root(void)
{
    const char *dir = getenv("DGX_GE_HOST_NVS");
    return dir && *dir ? dir : "nvs";
}

static bool valid(const char *name)
{
    const size_t n = name ? strlen(name) : 0;
    if (n == 0 || n > NAME_MAX_LEN) return false;
    for (size_t i = 0; i < n; ++i)
        if (name[i] == '/' || (name[i] == '.' && (n == 1 || (n == 2 && name[1] == '.')))) return false;
    return true;
}

esp_err_t nvs_flash_init(void)
{
    return mkdir(root(), 0755) == 0 || errno == EEXIST ? ESP_OK : ESP_FAIL;
}

esp_err_t nvs_flash_deinit(void)
{
    return ESP_OK;
}

/* Everything kept, gone: every file of every namespace (the directories are left, empty). */
esp_err_t nvs_flash_erase(void)
{
    char command[1024];
    snprintf(command, sizeof command, "rm -rf '%s'", root());
    return system(command) == 0 ? nvs_flash_init() : ESP_FAIL;
}

static bool path_of(nvs_handle_t h, const char *key, char *path, size_t size)
{
    if (h == 0 || h > OPEN_MAX || !opened[h - 1].used) return false;
    return key ? snprintf(path, size, "%s/%s/%s", root(), opened[h - 1].name, key) < (int)size
               : snprintf(path, size, "%s/%s", root(), opened[h - 1].name) < (int)size;
}

esp_err_t nvs_open(const char *name_space, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    if (!valid(name_space)) return ESP_ERR_NVS_INVALID_NAME;
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/%s", root(), name_space);
    struct stat st;
    if (mode == NVS_READONLY && stat(dir, &st) != 0) return ESP_ERR_NVS_NOT_FOUND; /* nothing kept in it yet: as a board says */
    if (mode == NVS_READWRITE && ((mkdir(root(), 0755) != 0 && errno != EEXIST) || (mkdir(dir, 0755) != 0 && errno != EEXIST))) return ESP_FAIL;
    for (int i = 0; i < OPEN_MAX; ++i)
        if (!opened[i].used) {
            opened[i].used = true, opened[i].writable = mode == NVS_READWRITE;
            strcpy(opened[i].name, name_space);
            *handle = (nvs_handle_t)(i + 1);
            return ESP_OK;
        }
    return ESP_ERR_NO_MEM;
}

void nvs_close(nvs_handle_t h)
{
    if (h >= 1 && h <= OPEN_MAX) opened[h - 1].used = false;
}

esp_err_t nvs_commit(nvs_handle_t h)
{
    return h >= 1 && h <= OPEN_MAX && opened[h - 1].used ? ESP_OK : ESP_ERR_NVS_INVALID_HANDLE;
}

static esp_err_t put(nvs_handle_t h, const char *key, char type, const void *value, size_t length)
{
    char path[1024], temp[1100];
    if (!path_of(h, NULL, path, sizeof path)) return ESP_ERR_NVS_INVALID_HANDLE;
    if (!opened[h - 1].writable) return ESP_ERR_NVS_READ_ONLY;
    if (!valid(key)) return ESP_ERR_NVS_INVALID_NAME;
    path_of(h, key, path, sizeof path);
    snprintf(temp, sizeof temp, "%s.new", path);
    FILE *f = fopen(temp, "wb");
    if (!f) return ESP_FAIL;
    const bool ok = fputc(type, f) != EOF && (length == 0 || fwrite(value, 1, length, f) == length);
    if (fclose(f) != 0 || !ok || rename(temp, path) != 0) {
        remove(temp);
        return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    }
    return ESP_OK;
}

/* The value of key, of this type: its size, and into out (if not NULL) when it fits in *length. */
static esp_err_t take(nvs_handle_t h, const char *key, char type, void *out, size_t *length)
{
    char path[1024];
    if (!path_of(h, NULL, path, sizeof path)) return ESP_ERR_NVS_INVALID_HANDLE;
    if (!valid(key)) return ESP_ERR_NVS_INVALID_NAME;
    path_of(h, key, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_ERR_NVS_NOT_FOUND;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f) - 1;
    fseek(f, 0, SEEK_SET);
    esp_err_t e = ESP_OK;
    if (size < 0 || fgetc(f) != type) e = ESP_ERR_NVS_TYPE_MISMATCH;
    else if (out && (size_t)size > *length) e = ESP_ERR_NVS_INVALID_LENGTH;
    else if (out && size > 0 && fread(out, 1, (size_t)size, f) != (size_t)size) e = ESP_FAIL;
    fclose(f);
    if (e == ESP_OK || e == ESP_ERR_NVS_INVALID_LENGTH) *length = (size_t)size;
    return e;
}

esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    char path[1024];
    if (!path_of(h, NULL, path, sizeof path)) return ESP_ERR_NVS_INVALID_HANDLE;
    if (!opened[h - 1].writable) return ESP_ERR_NVS_READ_ONLY;
    if (!valid(key)) return ESP_ERR_NVS_INVALID_NAME;
    path_of(h, key, path, sizeof path);
    return remove(path) == 0 ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_erase_all(nvs_handle_t h)
{
    char path[1024], command[1100];
    if (!path_of(h, NULL, path, sizeof path)) return ESP_ERR_NVS_INVALID_HANDLE;
    if (!opened[h - 1].writable) return ESP_ERR_NVS_READ_ONLY;
    snprintf(command, sizeof command, "rm -f '%s'/*", path);
    return system(command) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t length)
{
    return put(h, key, TYPE_BLOB, value, length);
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out_value, size_t *length)
{
    return take(h, key, TYPE_BLOB, out_value, length);
}

esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value)
{
    return put(h, key, TYPE_STR, value, strlen(value) + 1); /* with its 0, as NVS counts it */
}

esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out_value, size_t *length)
{
    return take(h, key, TYPE_STR, out_value, length);
}

#define NUMBER(name, type)                                                                                             \
    esp_err_t nvs_set_##name(nvs_handle_t h, const char *key, type value)                                             \
    {                                                                                                                  \
        return put(h, key, (char)(TYPE_NUMBER + sizeof(type)), &value, sizeof value);                                  \
    }                                                                                                                  \
    esp_err_t nvs_get_##name(nvs_handle_t h, const char *key, type *out_value)                                        \
    {                                                                                                                  \
        size_t length = sizeof *out_value;                                                                             \
        return take(h, key, (char)(TYPE_NUMBER + sizeof(type)), out_value, &length);                                   \
    }
NUMBER(i8, int8_t)
NUMBER(u8, uint8_t)
NUMBER(i16, int16_t)
NUMBER(u16, uint16_t)
NUMBER(i32, int32_t)
NUMBER(u32, uint32_t)
NUMBER(i64, int64_t)
NUMBER(u64, uint64_t)
