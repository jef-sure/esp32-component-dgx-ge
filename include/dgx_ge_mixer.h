#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dgx_ge_sound.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The sounds of a pack and a mixer of them.
 *
 * A pack keeps its sounds in one blob, SOUNDS (Pack.add_sound): their samples
 * at their own rate, mono or stereo, as IMA ADPCM of 4 bits or as 8 bits
 * unsigned, some going round until stopped; read where they lie (flash), not
 * copied. A stereo sound is left and right, or (smaller) the middle at its
 * rate and the side at a 2^n-th of it: left = middle + side, right = middle -
 * side. A sound with no samples plays nothing.
 *
 * The mixer plays them on voices, as many as the game says: each voice a
 * place in a sound, its step (the sound's rate to the output's), its volume
 * and pan. It sums them into a stream of signed 16-bit samples, mono or
 * stereo (interleaved, left first) as the platform asks, at the platform's
 * rate: a mono sound on stereo stands by its pan, a stereo one on mono is
 * made one. Play takes a free voice; the same sound playing starts anew; no
 * voice free: the oldest one gives way. The game knows sounds only by number
 * through dgx_ge_sound_t (dgx_ge_mixer_output).
 *
 * The game plays and stops; the platform fills its output, often from a task
 * of its own. Such a platform gives the mixer its lock (lock, before its task
 * starts), and every call of the mixer takes it: the game calls the mixer as
 * it likes, its output too, with no lock of its own (a second take of a lock
 * that is not recursive would never return).
 * RAM: 24 bytes on a 32-bit chip, and the voices, some 40 bytes each.
 */

typedef struct {
    const uint8_t *blob;
    size_t         size;
    uint16_t       count;
} dgx_ge_sounds_t;

/* The sounds of a blob SOUNDS; false when it is no such blob or does not hold together. */
bool dgx_ge_sounds_open(dgx_ge_sounds_t *sounds, const void *blob, size_t size);

/* How many samples (a channel) a sound has, and its rate; 0 when it has none or there is no such sound. */
uint32_t dgx_ge_sounds_length(const dgx_ge_sounds_t *sounds, int sound, uint16_t *rate);

typedef struct {
    const uint8_t *data;      /* the sound's data; NULL: the voice is free */
    const uint8_t *stream[2]; /* the samples of each stream (a channel, or the middle and the side) */
    uint32_t       frames;    /* samples of the sound */
    uint32_t       frame;     /* the one sounding now */
    uint32_t       taken[2];  /* samples of each stream taken so far */
    uint32_t       frac;      /* how far past the frame, of 65536 */
    uint32_t       step;      /* the sound's rate to the output's, 16.16 */
    uint32_t       age;       /* when it began: the oldest gives way */
    int16_t        now[2];    /* the sample of each stream now */
    int16_t        pred[2];   /* ADPCM: the predictor and step index of each stream */
    uint8_t        index[2];
    int16_t        sound;
    uint8_t        codec;     /* 1 8 bits unsigned, 2 IMA ADPCM */
    uint8_t        channels;  /* 1 or 2 */
    bool           mid_side;  /* stereo as the middle and the side */
    uint8_t        shift;     /* the side: a sample every 2^shift frames */
    bool           loop;
    uint8_t        volume;    /* 0..255 (255: as recorded) */
    int8_t         pan;       /* -127 left .. 0 the middle .. 127 right (on a stereo output) */
} dgx_ge_voice_t;

typedef struct dgx_ge_mixer {
    const dgx_ge_sounds_t *sounds;
    dgx_ge_voice_t        *voices;
    uint8_t                voice_count;
    uint8_t                channels; /* of the output: 1 or 2 */
    uint8_t                volume;   /* of all: 0..255 */
    uint32_t               rate;     /* of the output, samples a second */
    uint32_t               ages;
    void (*lock)(bool take); /* NULL, or the platform's lock: taken (true) and given back (false) by every call */
} dgx_ge_mixer_t;

/* A mixer of so many voices (1..255) for an output of rate and channels (1 or 2), no lock; false when there is no room. */
bool dgx_ge_mixer_init(dgx_ge_mixer_t *mixer, const dgx_ge_sounds_t *sounds, int voices, uint32_t rate, int channels);
/* Its voices given back; its lock and channels kept: a task still filling gets silence. */
void dgx_ge_mixer_free(dgx_ge_mixer_t *mixer);

/* A sound from its start at a volume (0..255) and pan (-127..127); its voice, -1 when it has no samples. */
int dgx_ge_mixer_play(dgx_ge_mixer_t *mixer, int sound, uint8_t volume, int8_t pan);

/* A sound stopped (every voice of it); -1: all of them. */
void dgx_ge_mixer_stop(dgx_ge_mixer_t *mixer, int sound);

/* The volume of all (0: silent, 255: as the sounds are): the game's setting of sound. */
void dgx_ge_mixer_volume(dgx_ge_mixer_t *mixer, uint8_t volume);

/* Whether any voice sounds. */
bool dgx_ge_mixer_busy(const dgx_ge_mixer_t *mixer);

/* The next frames of the stream: frames × channels samples into out (silence when nothing sounds). */
void dgx_ge_mixer_fill(dgx_ge_mixer_t *mixer, int16_t *out, int frames);

/* The mixer as the game's output of sounds: play at full volume in the middle, stop. */
dgx_ge_sound_t dgx_ge_mixer_output(dgx_ge_mixer_t *mixer);

#ifdef __cplusplus
}
#endif
