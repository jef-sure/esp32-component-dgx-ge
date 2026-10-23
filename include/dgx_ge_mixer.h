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
 * RAM: 28 bytes on a 32-bit chip, and the voices, some 40 bytes each.
 *
 * Music by notes (Pack.add_music, the MML of tools/dgx_ge_music.py) lies in
 * the same blob, after the sounds (SOUNDS version 2; a pack without music
 * keeps version 1): each music a song of channels, instruments and their
 * notes. The mixer plays one at a time beside the voices, never on one: it
 * synthesizes its channels on the song's synthesizer (the chip's square,
 * triangle and noise; the NES's; FM's two operators: dgx_ge_synth.h) sample
 * by sample, in whole numbers, at the output's rate, and
 * adds them to the stream; no effect takes its place and stopping the
 * sounds (dgx_ge_mixer_stop, -1 too) leaves it. A music goes round from its
 * loop point, or ends. One music gives way to another at once or by a fade
 * (the one out, then the other in, each as long as said).
 *
 * Two buses, each with the volume the game keeps (the player's settings):
 * the effects (the voices) and the music, both under the volume of all. The
 * music also has a level the game wants of it by design (a quiet scene, the
 * music lower under a talk), under its bus, going to another over a time.
 *
 * RAM: when the sounds have music, 68 bytes and 36 a channel of the most a
 * music of them has (4 channels: 212), made by dgx_ge_mixer_init; none
 * without. With FM in the build, 84 bytes and 64 a channel (4: 340). The
 * time: some 40 instructions a channel a sample on the chip and the NES,
 * some 180 on FM.
 */

typedef struct {
    const uint8_t *blob;
    size_t         size;
    uint16_t       count;
    uint16_t       musics; /* the musics after the sounds (<PREFIX>_MUSIC_<NAME>) */
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
    uint8_t                effects;  /* the bus of the effects (the voices): 0..255 */
    uint32_t               rate;     /* of the output, samples a second */
    uint32_t               ages;
    void (*lock)(bool take); /* NULL, or the platform's lock: taken (true) and given back (false) by every call */
    struct dgx_ge_music   *music; /* the music's state: NULL when the sounds have none */
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

/* The volume of the bus of the effects, the sounds on the voices (0: silent, 255: as they are; 255 at first), under the volume of all. */
void dgx_ge_mixer_effects_volume(dgx_ge_mixer_t *mixer, uint8_t volume);

/* Whether any voice sounds. */
bool dgx_ge_mixer_busy(const dgx_ge_mixer_t *mixer);

/*
 * A music of the sounds (its number, <PREFIX>_MUSIC_<NAME>) from its start in place of the one playing: at once
 * (fade_ms 0), or the one playing fades out over fade_ms and then this one comes in over as long (from silence: it
 * comes in). The same one playing goes on (stopped first, it starts anew); asked back while it fades out, it comes in
 * again. A number of none (-1; 255, as a script's music stop says it): silence, at once or after the fade out.
 * Whether a music plays once the fade is over. Up to 65535 ms.
 */
bool dgx_ge_mixer_music(dgx_ge_mixer_t *mixer, int music, uint32_t fade_ms);

/* The music sounding, -1 none: it ended (no loop point, its last note's release over) or was stopped; while one fades out for another, the one fading. */
int dgx_ge_mixer_music_now(const dgx_ge_mixer_t *mixer);

/* The volume of the bus of the music (0: silent, 255: as written; 255 at first), apart from the effects'; under the volume of all. */
void dgx_ge_mixer_music_volume(dgx_ge_mixer_t *mixer, uint8_t volume);

/*
 * The level of the music by design, 0..100 percent of its bus's volume (100 at first), reached over ms (0: at once;
 * up to 65535): lower for a quiet scene or under a talk, back to 100 the same way; the bus's volume (the player's
 * setting) stays as it is. It holds for the musics after too.
 */
void dgx_ge_mixer_music_level(dgx_ge_mixer_t *mixer, int percent, uint32_t ms);

/* The next frames of the stream: frames × channels samples into out (silence when nothing sounds). */
void dgx_ge_mixer_fill(dgx_ge_mixer_t *mixer, int16_t *out, int frames);

/* The mixer as the game's output of sounds: play at full volume in the middle, stop; the music, which plays now. */
dgx_ge_sound_t dgx_ge_mixer_output(dgx_ge_mixer_t *mixer);

#ifdef __cplusplus
}
#endif
