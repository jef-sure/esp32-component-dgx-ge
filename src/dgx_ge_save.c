#include <string.h>

#include "dgx_ge_save.h"

#define HEADER_BYTES 14u /* "DGSV", version, the clock at the save, the fingerprint of the data */

static void put(dgx_ge_save_t *s, const void *bytes, size_t n)
{
    if (s->failed) return;
    if (s->size + n > s->capacity) {
        s->failed = true;
        return;
    }
    memcpy(s->data + s->size, bytes, n);
    s->size += n;
}

void dgx_ge_save_u8(dgx_ge_save_t *s, uint8_t v)
{
    put(s, &v, 1);
}

void dgx_ge_save_u16(dgx_ge_save_t *s, uint16_t v)
{
    const uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    put(s, b, 2);
}

void dgx_ge_save_u32(dgx_ge_save_t *s, uint32_t v)
{
    const uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(s, b, 4);
}

void dgx_ge_save_i16(dgx_ge_save_t *s, int16_t v)
{
    dgx_ge_save_u16(s, (uint16_t)v);
}

void dgx_ge_save_i32(dgx_ge_save_t *s, int32_t v)
{
    dgx_ge_save_u32(s, (uint32_t)v);
}

void dgx_ge_save_bytes(dgx_ge_save_t *s, const void *bytes, size_t n)
{
    put(s, bytes, n);
}

void dgx_ge_save_begin(dgx_ge_save_t *s, void *buffer, size_t capacity, uint32_t data_id, uint32_t now_ms)
{
    memset(s, 0, sizeof(*s));
    s->data     = buffer;
    s->capacity = capacity;
    put(s, "DGSV", 4);
    dgx_ge_save_u16(s, DGX_GE_SAVE_VERSION);
    dgx_ge_save_u32(s, now_ms);
    dgx_ge_save_u32(s, data_id);
}

void dgx_ge_save_chunk(dgx_ge_save_t *s, const char tag[4])
{
    if (s->chunk) {
        s->failed = true;
        return;
    }
    put(s, tag, 4);
    s->chunk = s->size;
    dgx_ge_save_u32(s, 0);
    if (s->failed) s->chunk = 0;
}

void dgx_ge_save_chunk_end(dgx_ge_save_t *s)
{
    if (!s->chunk) {
        s->failed = true;
        return;
    }
    if (!s->failed) {
        const uint32_t n = (uint32_t)(s->size - s->chunk - 4);
        s->data[s->chunk] = (uint8_t)n, s->data[s->chunk + 1] = (uint8_t)(n >> 8), s->data[s->chunk + 2] = (uint8_t)(n >> 16), s->data[s->chunk + 3] = (uint8_t)(n >> 24);
    }
    s->chunk = 0;
}

size_t dgx_ge_save_end(dgx_ge_save_t *s)
{
    if (s->chunk) s->failed = true;
    return s->failed ? 0 : s->size;
}

static uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool dgx_ge_load_open(dgx_ge_load_t *l, const void *data, size_t size, uint32_t data_id, uint32_t *saved_ms)
{
    memset(l, 0, sizeof(*l));
    const uint8_t *d = data;
    if (!d || size < HEADER_BYTES || memcmp(d, "DGSV", 4) != 0) return false;
    const unsigned version = d[4] | d[5] << 8;
    if (version != DGX_GE_SAVE_VERSION || u32_at(d + 10) != data_id) return false; /* version 1 named no data: whose it is, nobody knows */
    l->data = d, l->size = size;
    if (saved_ms) *saved_ms = u32_at(d + 6);
    return true;
}

bool dgx_ge_load_chunk(dgx_ge_load_t *l, const char tag[4])
{
    l->failed = false;
    for (size_t at = HEADER_BYTES; at + 8 <= l->size;) {
        const size_t n = u32_at(l->data + at + 4);
        if (n > l->size - at - 8) break; /* a chunk cut short: the save ends here */
        if (memcmp(l->data + at, tag, 4) == 0) {
            l->at = at + 8, l->end = at + 8 + n;
            return true;
        }
        at += 8 + n;
    }
    l->at = l->end = 0;
    return false;
}

static bool take(dgx_ge_load_t *l, void *bytes, size_t n)
{
    if (l->at + n > l->end) {
        l->failed = true;
        memset(bytes, 0, n);
        return false;
    }
    memcpy(bytes, l->data + l->at, n);
    l->at += n;
    return true;
}

uint8_t dgx_ge_load_u8(dgx_ge_load_t *l)
{
    uint8_t v;
    take(l, &v, 1);
    return v;
}

uint16_t dgx_ge_load_u16(dgx_ge_load_t *l)
{
    uint8_t b[2];
    take(l, b, 2);
    return (uint16_t)(b[0] | b[1] << 8);
}

uint32_t dgx_ge_load_u32(dgx_ge_load_t *l)
{
    uint8_t b[4];
    take(l, b, 4);
    return u32_at(b);
}

int16_t dgx_ge_load_i16(dgx_ge_load_t *l)
{
    return (int16_t)dgx_ge_load_u16(l);
}

int32_t dgx_ge_load_i32(dgx_ge_load_t *l)
{
    return (int32_t)dgx_ge_load_u32(l);
}

void dgx_ge_load_bytes(dgx_ge_load_t *l, void *bytes, size_t n)
{
    take(l, bytes, n);
}

size_t dgx_ge_load_left(const dgx_ge_load_t *l)
{
    return l->end > l->at ? l->end - l->at : 0;
}
