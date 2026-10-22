#include <stdlib.h>
#include <string.h>

#include "dgx_ge_mixer.h"

/*
 * SOUNDS: 'DGSN', version, count; 12 bytes a sound: offset, samples, rate, codec (bit 5 middle and side, bit 6
 * stereo, bit 7 round), the side's shift; the data: a stream a channel one after the other (ADPCM: the states of the
 * streams first, each stream's nibbles from a byte of its own)
 */
#define SOUNDS_MAGIC 0x4E534744u
#define HEAD         8
#define ENTRY        12
enum { CODEC_PCM8 = 1, CODEC_ADPCM = 2 };

static const int16_t ima_steps[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,    31,    34,    37,
    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,
    230,   253,   279,   307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,   1060,  1166,
    1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,
    6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767};
static const int8_t ima_index[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t u16_at(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* Samples of a stream: the sound's, or the side's (one every 2^shift, the last part counting). */
static uint32_t stream_samples(uint32_t frames, int stream, bool mid_side, uint8_t shift)
{
    return stream && mid_side ? (frames + (1u << shift) - 1) >> shift : frames;
}

/* The bytes a stream takes (ADPCM: its nibbles, from a byte of its own). */
static size_t stream_size(uint8_t codec, uint32_t samples)
{
    return codec == CODEC_ADPCM ? ((size_t)samples + 1) / 2 : samples;
}

/* The bytes a sound's data takes: the ADPCM states, the streams. */
static size_t data_size(uint8_t codec, uint8_t channels, bool mid_side, uint8_t shift, uint32_t frames)
{
    size_t n = codec == CODEC_ADPCM ? 4u * channels : 0;
    for (int c = 0; c < channels; ++c) n += stream_size(codec, stream_samples(frames, c, mid_side, shift));
    return n;
}

bool dgx_ge_sounds_open(dgx_ge_sounds_t *sounds, const void *blob, size_t size)
{
    const uint8_t *b = blob;
    memset(sounds, 0, sizeof(*sounds));
    if (!b || size < HEAD || u32_at(b) != SOUNDS_MAGIC || u16_at(b + 4) != 1) return false;
    const uint16_t count = u16_at(b + 6);
    if (size < HEAD + (size_t)ENTRY * count) return false;
    for (int i = 0; i < count; ++i) {
        const uint8_t *e      = b + HEAD + ENTRY * i;
        const uint32_t frames = u32_at(e + 4);
        const uint8_t  codec = e[10] & 0x0F, channels = e[10] & 0x40 ? 2 : 1, shift = e[11];
        const bool     mid_side = channels == 2 && (e[10] & 0x20);
        if (!frames) continue;
        if ((codec != CODEC_PCM8 && codec != CODEC_ADPCM) || !u16_at(e + 8) || shift > 3) return false;
        if (u32_at(e) > size || data_size(codec, channels, mid_side, shift, frames) > size - u32_at(e)) return false;
    }
    sounds->blob = b, sounds->size = size, sounds->count = count;
    return true;
}

static const uint8_t *entry(const dgx_ge_sounds_t *sounds, int sound)
{
    if (!sounds || !sounds->blob || sound < 0 || sound >= sounds->count) return NULL;
    const uint8_t *e = sounds->blob + HEAD + ENTRY * sound;
    return u32_at(e + 4) ? e : NULL;
}

uint32_t dgx_ge_sounds_length(const dgx_ge_sounds_t *sounds, int sound, uint16_t *rate)
{
    const uint8_t *e = entry(sounds, sound);
    if (rate) *rate = e ? u16_at(e + 8) : 0;
    return e ? u32_at(e + 4) : 0;
}

bool dgx_ge_mixer_init(dgx_ge_mixer_t *mixer, const dgx_ge_sounds_t *sounds, int voices, uint32_t rate, int channels)
{
    memset(mixer, 0, sizeof(*mixer));
    if (voices < 1 || voices > 255 || !rate || (channels != 1 && channels != 2)) return false;
    mixer->voices = calloc((size_t)voices, sizeof(dgx_ge_voice_t));
    if (!mixer->voices) return false;
    mixer->sounds = sounds, mixer->voice_count = (uint8_t)voices, mixer->rate = rate, mixer->channels = (uint8_t)channels;
    mixer->volume = 255;
    return true;
}

/* The platform's lock round every call: its task fills while the game plays. */
static void take(const dgx_ge_mixer_t *mixer)
{
    if (mixer->lock) mixer->lock(true);
}

static void give(const dgx_ge_mixer_t *mixer)
{
    if (mixer->lock) mixer->lock(false);
}

void dgx_ge_mixer_free(dgx_ge_mixer_t *mixer)
{
    /* the lock and the channels kept: a task still filling gets silence, as many samples as before */
    take(mixer);
    void (*lock)(bool)     = mixer->lock;
    const uint8_t channels = mixer->channels;
    free(mixer->voices);
    memset(mixer, 0, sizeof(*mixer));
    mixer->lock = lock, mixer->channels = channels;
    give(mixer);
}

/* The next sample of a stream of an ADPCM sound: its nibble number n. */
static int16_t adpcm_next(dgx_ge_voice_t *v, int channel, uint32_t n)
{
    const uint8_t *p    = v->stream[channel];
    const uint8_t  code = n & 1 ? p[n / 2] >> 4 : p[n / 2] & 0x0F;
    const int     step = ima_steps[v->index[channel]];
    int           delta = step >> 3;
    if (code & 4) delta += step;
    if (code & 2) delta += step >> 1;
    if (code & 1) delta += step >> 2;
    int pred = v->pred[channel] + (code & 8 ? -delta : delta);
    pred     = pred < -32768 ? -32768 : pred > 32767 ? 32767 : pred;
    int idx  = v->index[channel] + ima_index[code & 7];
    v->index[channel] = (uint8_t)(idx < 0 ? 0 : idx > 88 ? 88 : idx);
    return v->pred[channel] = (int16_t)pred;
}

/* The samples of each stream at the frame the voice is at, taken in turn (ADPCM: only ever forward); the side every 2^shift frames. */
static void take_frame(dgx_ge_voice_t *v)
{
    for (int c = 0; c < v->channels; ++c) {
        const uint32_t want = c && v->mid_side ? v->frame >> v->shift : v->frame;
        while (v->taken[c] <= want) {
            const uint32_t n = v->taken[c]++;
            v->now[c]        = v->codec == CODEC_ADPCM ? adpcm_next(v, c, n) : (int16_t)((v->stream[c][n] - 128) * 256);
        }
    }
}

/* A voice at the start of its sound: the streams found, the ADPCM states from the head of its data, the first frame taken. */
static void restart(dgx_ge_voice_t *v, const uint8_t *start)
{
    v->frame = 0, v->frac = 0, v->data = start;
    const uint8_t *at = start + (v->codec == CODEC_ADPCM ? 4 * v->channels : 0);
    for (int c = 0; c < v->channels; ++c) {
        if (v->codec == CODEC_ADPCM) v->pred[c] = (int16_t)u16_at(start + 4 * c), v->index[c] = start[4 * c + 2] > 88 ? 88 : start[4 * c + 2];
        v->stream[c] = at, v->taken[c] = 0;
        at += stream_size(v->codec, stream_samples(v->frames, c, v->mid_side, v->shift));
    }
    take_frame(v);
}

static int play(dgx_ge_mixer_t *mixer, int sound, uint8_t volume, int8_t pan)
{
    const uint8_t *e = entry(mixer->sounds, sound);
    if (!e || !mixer->voices) return -1;
    int at = -1;
    for (int i = 0; i < mixer->voice_count && at < 0; ++i)
        if (mixer->voices[i].data && mixer->voices[i].sound == sound) at = i; /* the same sound playing: anew */
    for (int i = 0; i < mixer->voice_count && at < 0; ++i)
        if (!mixer->voices[i].data) at = i;
    if (at < 0) { /* none free: the oldest gives way */
        at = 0;
        for (int i = 1; i < mixer->voice_count; ++i)
            if (mixer->voices[i].age - mixer->voices[at].age > 0x80000000u) at = i;
    }
    dgx_ge_voice_t *v = &mixer->voices[at];
    memset(v, 0, sizeof(*v));
    v->sound = (int16_t)sound, v->frames = u32_at(e + 4), v->codec = e[10] & 0x0F, v->channels = e[10] & 0x40 ? 2 : 1;
    v->mid_side = v->channels == 2 && (e[10] & 0x20), v->shift = e[11];
    v->loop = e[10] & 0x80, v->volume = volume, v->pan = pan < -127 ? -127 : pan, v->age = mixer->ages++;
    v->step = (uint32_t)(((uint64_t)u16_at(e + 8) << 16) / mixer->rate);
    restart(v, mixer->sounds->blob + u32_at(e));
    return at;
}

int dgx_ge_mixer_play(dgx_ge_mixer_t *mixer, int sound, uint8_t volume, int8_t pan)
{
    take(mixer);
    const int at = play(mixer, sound, volume, pan);
    give(mixer);
    return at;
}

void dgx_ge_mixer_stop(dgx_ge_mixer_t *mixer, int sound)
{
    take(mixer);
    for (int i = 0; mixer->voices && i < mixer->voice_count; ++i)
        if (sound < 0 || mixer->voices[i].sound == sound) mixer->voices[i].data = NULL;
    give(mixer);
}

void dgx_ge_mixer_volume(dgx_ge_mixer_t *mixer, uint8_t volume)
{
    take(mixer);
    mixer->volume = volume;
    give(mixer);
}

bool dgx_ge_mixer_busy(const dgx_ge_mixer_t *mixer)
{
    bool busy = false;
    take(mixer);
    for (int i = 0; mixer->voices && i < mixer->voice_count && !busy; ++i) busy = mixer->voices[i].data != NULL;
    give(mixer);
    return busy;
}

/* A sample by a volume of 0..255, 255 the sample itself. */
static int32_t scaled(int32_t s, int volume)
{
    return s * (volume + (volume >> 7)) >> 8;
}

void dgx_ge_mixer_fill(dgx_ge_mixer_t *mixer, int16_t *out, int frames)
{
    take(mixer);
    const int ch = mixer->channels;
    for (int f = 0; f < frames; ++f) {
        int32_t sum[2] = {0, 0};
        for (int i = 0; mixer->voices && i < mixer->voice_count; ++i) {
            dgx_ge_voice_t *v = &mixer->voices[i];
            if (!v->data) continue;
            /* left and right: as kept, or the middle plus and minus the side */
            int32_t l = v->now[0], r = v->channels == 2 ? v->now[1] : l;
            if (v->mid_side) l = v->now[0] + v->now[1], r = v->now[0] - v->now[1];
            l = scaled(l, v->volume), r = scaled(r, v->volume);
            if (ch == 1) {
                sum[0] += v->channels == 2 ? (l + r) / 2 : l;
            } else if (v->channels == 2) {
                sum[0] += l, sum[1] += r;
            } else {
                /* a mono sound by its pan: the middle both at full, a side the other fading */
                sum[0] += scaled(l, v->pan > 0 ? 255 - 2 * v->pan : 255);
                sum[1] += scaled(l, v->pan < 0 ? 255 + 2 * v->pan : 255);
            }
            /* on by the step: every whole frame passed taken in turn (ADPCM goes only forward) */
            v->frac += v->step;
            while (v->data && v->frac >= 65536) {
                v->frac -= 65536;
                if (++v->frame >= v->frames) {
                    if (!v->loop) {
                        v->data = NULL;
                        break;
                    }
                    const uint8_t *e = entry(mixer->sounds, v->sound);
                    const uint32_t frac = v->frac;
                    restart(v, mixer->sounds->blob + u32_at(e));
                    v->frac = frac;
                    continue;
                }
                take_frame(v);
            }
        }
        for (int c = 0; c < ch; ++c) {
            const int32_t s = scaled(sum[c], mixer->volume);
            out[f * ch + c] = (int16_t)(s < -32768 ? -32768 : s > 32767 ? 32767 : s);
        }
    }
    give(mixer);
}

static void output_play(void *ctx, int sound)
{
    dgx_ge_mixer_play(ctx, sound, 255, 0);
}

static void output_stop(void *ctx, int sound)
{
    dgx_ge_mixer_stop(ctx, sound);
}

dgx_ge_sound_t dgx_ge_mixer_output(dgx_ge_mixer_t *mixer)
{
    return (dgx_ge_sound_t){output_play, output_stop, mixer};
}
