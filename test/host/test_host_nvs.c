/*
 * NVS on the computer (boards/host/host_nvs.c) answers as a board's NVS does, for what a game does with it: nothing
 * kept yet, a blob and its size, a string, the whole numbers each of its own type, a buffer too small, a value written
 * again, a key erased, read-only refused, and all of it there again after the "restart" (the files stay).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "nvs_flash.h"

int main(void)
{
    char dir[] = "/tmp/dgx_ge_nvs_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    setenv("DGX_GE_HOST_NVS", dir, 1);
    CHECK(nvs_flash_init() == ESP_OK);

    nvs_handle_t h;
    /* nothing kept yet: a namespace read only is not there, as on a fresh board */
    CHECK(nvs_open("game", NVS_READONLY, &h) == ESP_ERR_NVS_NOT_FOUND);
    CHECK(nvs_open("this-name-is-too-long", NVS_READWRITE, &h) == ESP_ERR_NVS_INVALID_NAME);
    CHECK(nvs_open("game", NVS_READWRITE, &h) == ESP_OK);

    /* a blob: its size alone, then into a buffer; one too small refused, its size told */
    const uint8_t save[5] = {1, 2, 3, 4, 5};
    CHECK(nvs_set_blob(h, "save", save, sizeof save) == ESP_OK);
    CHECK(nvs_commit(h) == ESP_OK);
    size_t  size = 0;
    uint8_t back[8];
    CHECK(nvs_get_blob(h, "save", NULL, &size) == ESP_OK && size == 5);
    size = sizeof back;
    CHECK(nvs_get_blob(h, "save", back, &size) == ESP_OK && size == 5 && !memcmp(back, save, 5));
    size = 3;
    CHECK(nvs_get_blob(h, "save", back, &size) == ESP_ERR_NVS_INVALID_LENGTH && size == 5);
    CHECK(nvs_get_blob(h, "none", NULL, &size) == ESP_ERR_NVS_NOT_FOUND);

    /* a string, with its 0 */
    char name[16];
    CHECK(nvs_set_str(h, "name", "kid") == ESP_OK);
    size = sizeof name;
    CHECK(nvs_get_str(h, "name", name, &size) == ESP_OK && size == 4 && !strcmp(name, "kid"));

    /* whole numbers: each read back as it was, and not as another type */
    uint32_t hops = 0;
    int8_t   small;
    CHECK(nvs_set_u32(h, "hops", 41) == ESP_OK && nvs_set_u32(h, "hops", 42) == ESP_OK); /* written again */
    CHECK(nvs_get_u32(h, "hops", &hops) == ESP_OK && hops == 42);
    CHECK(nvs_get_i8(h, "hops", &small) == ESP_ERR_NVS_TYPE_MISMATCH);
    CHECK(nvs_get_blob(h, "hops", NULL, &size) == ESP_ERR_NVS_TYPE_MISMATCH);
    int64_t far = 0;
    CHECK(nvs_set_i64(h, "far", -5000000000LL) == ESP_OK && nvs_get_i64(h, "far", &far) == ESP_OK && far == -5000000000LL);
    nvs_close(h);

    /* a "restart": the namespace is there now, read only works and keeps from writing */
    CHECK(nvs_flash_deinit() == ESP_OK && nvs_flash_init() == ESP_OK);
    CHECK(nvs_open("game", NVS_READONLY, &h) == ESP_OK);
    CHECK(nvs_get_u32(h, "hops", &hops) == ESP_OK && hops == 42);
    CHECK(nvs_set_u32(h, "hops", 1) == ESP_ERR_NVS_READ_ONLY);
    nvs_close(h);

    /* a key erased, then everything */
    CHECK(nvs_open("game", NVS_READWRITE, &h) == ESP_OK);
    CHECK(nvs_erase_key(h, "name") == ESP_OK);
    size = sizeof name;
    CHECK(nvs_get_str(h, "name", name, &size) == ESP_ERR_NVS_NOT_FOUND);
    nvs_close(h);
    CHECK(nvs_flash_erase() == ESP_OK);
    CHECK(nvs_open("game", NVS_READONLY, &h) == ESP_ERR_NVS_NOT_FOUND);

    char command[64];
    snprintf(command, sizeof command, "rm -rf '%s'", dir);
    CHECK(system(command) == 0);
    CHECK_DONE();
}
