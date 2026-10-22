#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A saved game: the state of the world as bytes, the same on every machine
 * (little-endian, no padding), whatever keeps them (a file, the NVS of a
 * board: the platform's, the engine never writes anywhere itself).
 *
 * The blob: "DGSV", version u16, the clock of the game at the save u32, the
 * fingerprint of the game's data u32 (dgx_ge_pack_id of its pack: a save is
 * of the data it was made with, another pack does not take it), then chunks,
 * each a tag of four letters, a length u32 and that many bytes. Every
 * module writes its own chunk (dgx_ge_world_save, dgx_ge_cells_save,
 * dgx_ge_timers_save, dgx_ge_pockets_save) and the game writes what only it
 * knows; a reader takes the chunks it knows by their tags and leaves the
 * rest. Times in the chunks are of the clock of the game at the save; a
 * loader is told how much later it is now (the clock of the game at the load
 * less the one saved) and moves every time on by it: the game goes on as if
 * no time had passed.
 *
 * A module loads all or nothing: its chunk is read and checked first (the
 * numbers it can check itself: counts, indices, kinds), then taken; refused,
 * the module is as it was. dgx_ge_*_load_check reads and checks alone, so
 * that a game may check every chunk before it changes anything.
 *
 * Version 3 keeps params (of records of the world, pockets, lives of cells)
 * in 32 bits, as they are; a save of version 2 kept them in 16 and is read
 * as it was made (dgx_ge_load_param).
 */
#define DGX_GE_SAVE_VERSION 3

typedef struct dgx_ge_save {
    uint8_t *data;
    size_t   size, capacity;
    size_t   chunk; /* where the length of the chunk being written lies; 0 none */
    bool     failed; /* no room: nothing more is written, the save is no good */
} dgx_ge_save_t;

/* Begin a save into a buffer of so many bytes, of the data of that fingerprint, at this moment of the clock of the game. */
void dgx_ge_save_begin(dgx_ge_save_t *s, void *buffer, size_t capacity, uint32_t data_id, uint32_t now_ms);

/* A chunk: its tag, then its bytes, then its end. A chunk inside a chunk is an error (nothing is written). */
void dgx_ge_save_chunk(dgx_ge_save_t *s, const char tag[4]);
void dgx_ge_save_chunk_end(dgx_ge_save_t *s);
void dgx_ge_save_u8(dgx_ge_save_t *s, uint8_t v);
void dgx_ge_save_u16(dgx_ge_save_t *s, uint16_t v);
void dgx_ge_save_u32(dgx_ge_save_t *s, uint32_t v);
void dgx_ge_save_i16(dgx_ge_save_t *s, int16_t v);
void dgx_ge_save_i32(dgx_ge_save_t *s, int32_t v);
void dgx_ge_save_bytes(dgx_ge_save_t *s, const void *bytes, size_t n);

/* The save as bytes: how many; 0 when it failed (no room, a chunk left open). */
size_t dgx_ge_save_end(dgx_ge_save_t *s);

typedef struct dgx_ge_load {
    const uint8_t *data;
    size_t         size;
    size_t         at, end; /* the chunk being read: where in it, where it ends */
    bool           failed;  /* read past the end of a chunk: the values were 0 */
    uint16_t       version; /* of the save being read */
} dgx_ge_load_t;

/* Open a saved game: false when it is not one, of a version not read (before 2, after this one) or of other data (another fingerprint); the clock of the game at the save. */
bool dgx_ge_load_open(dgx_ge_load_t *l, const void *data, size_t size, uint32_t data_id, uint32_t *saved_ms);

/* Go to a chunk by its tag (the first of that tag); false when there is none. Values are read from where it begins. */
bool dgx_ge_load_chunk(dgx_ge_load_t *l, const char tag[4]);
uint8_t  dgx_ge_load_u8(dgx_ge_load_t *l);
uint16_t dgx_ge_load_u16(dgx_ge_load_t *l);
uint32_t dgx_ge_load_u32(dgx_ge_load_t *l);
int16_t  dgx_ge_load_i16(dgx_ge_load_t *l);
int32_t  dgx_ge_load_i32(dgx_ge_load_t *l);
void     dgx_ge_load_bytes(dgx_ge_load_t *l, void *bytes, size_t n);
/* A param (written dgx_ge_save_i32): 32 bits; of a save of version 2, 16. Its bytes in the save being read: */
int32_t dgx_ge_load_param(dgx_ge_load_t *l);
#define DGX_GE_LOAD_PARAM_BYTES(l) ((l)->version < 3 ? 2u : 4u)
/* Bytes left in the chunk. */
size_t dgx_ge_load_left(const dgx_ge_load_t *l);

#ifdef __cplusplus
}
#endif
