#include <stdlib.h>
#include <string.h>

#ifndef DGX_GE_MUSIC_ALONE
#include "esp_log.h"
#endif

#include "dgx_ge_music.h"
#include "dgx_ge_synth.h"

#if !DGX_GE_SYNTH_CHIP && !DGX_GE_SYNTH_NES && !DGX_GE_SYNTH_FM
#error "no synthesizer of music in the build: DGX_GE_SYNTH_CHIP, _NES or _FM 1 (dgx_ge_synths)"
#endif

/*
 * Music by notes, as the sound chips played it: channels, each a voice of the song's synthesizer, their notes read
 * from a song (DGMU, tools/dgx_ge_music.py has its bytes) as the song's clock ticks; all in whole numbers, sample by
 * sample, at the mixer's rate.
 *
 * The clock: 48 ticks a quarter note, the tempo quarter notes a minute, so a tick every rate * 60 / (tempo * 48)
 * samples, counted exactly (a tick as the count passes rate * 60, the rest kept): a note of n ticks sounds
 * ceil(n * rate * 60 / (tempo * 48)) samples from the song's start, whatever the rate.
 *
 * The chip: a wave of a phase of 32 bits, on by the note's frequency * 2^32 / rate a sample (the noise: its register
 * shifted at 16 times the note's frequency); the square high while the phase is under its duty, the triangle up the
 * first half and down the second. The envelope: a level of 0..2^24, up by its steps in the attack, down in the decay
 * to the sustain, held, then down to 0 in the release: each stage as many samples as its ms at the rate (one at
 * least). A channel at its loudest (v15, an instrument of 15, the envelope at its top) is a quarter of the full
 * scale: four of them together reach it.
 *
 * The NES (2A03): the chip's envelope, but the volume the channel sounds at is one of 16 levels, as its DAC has them.
 * The pulse at the frequency of its timer of 11 bits (the CPU's clock / 16 / the period, the nearest to the note), a
 * duty of 12.5, 25, 50 or 75%; the triangle at the clock / 32 / the period, a staircase of 32 steps (15 down to 0, 0
 * up to 15), no volume of its own: it sounds whole while the envelope is not silent; a note lower than the timer
 * reaches sounds an octave up, or two. The noise: its 15-bit register shifted at the clock / one of its 16 periods
 * (the one nearest to 16 times the note's frequency, as the chip's), its new top bit 0 ^ 1 (long) or 0 ^ 6 (short: 93
 * steps).
 *
 * FM, after the OPL2: two operators, the modulator and the carrier, each a phase (the note's frequency * its
 * multiplier), a wave read from a quarter of a sine in flash as logarithms (LOGSIN: -log2(sin) * 256), its attenuation
 * added (its envelope, its level, the key's scale), made linear again (EXP: 2^-x, 12 bits). The modulator's output
 * goes into the carrier's phase (FM: 1024 a turn, so its loudest, 4095, is four turns) or beside it (additive); the
 * modulator's last two outputs into its own phase by its feedback ((o1 + o2) >> (9 - feedback): its 7 is 4 pi). The
 * envelope in attenuation, 0.1875 dB a unit (512: 96 dB, silence): the attack falls toward 0 as a curve (by a share of
 * where it is a sample, from silence to the top in its time), the decay and the release rise in a straight line (96
 * dB in their time), the decay to the sustain (3 dB a step; 0 is 93 dB down), held while the note sounds, or going on
 * down at the release's pace (percussive). As the chip has it, a higher note runs its envelope faster (2^(-r/4), r of
 * its octave: 0..3, or 1..15 with ksr) and, with a key scale level, sounds quieter (1.5, 3 or 6 dB an octave from
 * G0). Tremolo 1 dB at 3.7 Hz, vibrato 7 cents at 6.1 Hz, as the OPL2's shallow ones. A note restarts both phases.
 */
#define MUSIC_MAGIC 0x554D4744u /* 'DGMU' */
#define SONG_HEAD   10          /* version 1: the chip's */
#define SONG_HEAD_2 14          /* version 2: its synthesizer and its tail after the tempo */
#define NO_TAIL     0xFFFFu     /* its notes ring out to their end */
#define INSTRUMENT  10          /* the chip's, the NES's */
#define FM_INSTRUMENT 28
#define CHANNEL     8
#define NO_LOOP     0xFFFFFFFFu
#define PER_QUARTER 48
#define ENV_TOP     (1u << 24)
#define PEAK        8192
#define READS       1024 /* events read at once at most: more is a song going round with no time in it, which then ends */
#define NEVER       0xFFFFu /* an FM stage's time: it does not move */

enum { W_SQUARE_12, W_SQUARE_25, W_SQUARE_50, W_TRIANGLE, W_NOISE, W_SQUARE_75, W_NOISE_SHORT };
enum { SILENT, ATTACK, DECAY, SUSTAIN, RELEASE };
enum { EV_REST = 0x80, EV_SLUR, EV_INSTRUMENT, EV_VOLUME, EV_TEMPO, EV_GATE, EV_END = 0xFF };
enum { HOLD_NEXT = 1, HELD = 2 }; /* the next note goes on into the one after it; the note sounding goes on into the next */

/* The frequencies of the notes of the lowest octave (MIDI 0..11), Hz * 65536: an octave up, twice. */
static const uint32_t freq_low[12] = {535809, 567670, 601425, 637188, 675077, 715219, 757749, 802807, 850544, 901120, 954703, 1011473};
#if DGX_GE_SYNTH_CHIP
/* The squares' duties: where the phase stops being high. */
static const uint32_t duty[3] = {1u << 29, 1u << 30, 1u << 31};
#endif

#if DGX_GE_SYNTH_NES
#define NES_CLOCK 1789773u /* the NTSC 2A03's CPU, Hz */
/* the pulse's duties by wave: 12.5, 25, 50 (waves 0..2) and 75% (wave 5) */
static const uint32_t nes_duty[6] = {1u << 29, 1u << 30, 1u << 31, 0, 0, 3u << 30};
/* the noise's periods, CPU cycles a shift (NTSC) */
static const uint16_t nes_noise[16] = {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};
#endif

#if DGX_GE_SYNTH_FM
#define FM_QUIET   (512u << 16) /* an operator's envelope at silence (96 dB down), 1/65536 of 0.1875 dB a unit */
#define FM_E0      (16u << 16)  /* the attack's curve: it falls by a share of where it is and 3 dB more */
#define FM_ATTACK  229148u      /* ln((512 + 16) / 16) * 65536: the share of the attack a sample, by its samples */
enum { FM_PERCUSSIVE = 1, FM_KSR = 2, FM_VIBRATO = 4, FM_TREMOLO = 8 };
/* -log2(sin((i + 0.5) * pi / 512)) * 256, a quarter of a sine */
static const uint16_t LOGSIN[256] = {
    2137, 1731, 1543, 1419, 1326, 1252, 1190, 1137, 1091, 1050, 1013, 979, 949, 920, 894, 869, 846, 825, 804, 785,
    767, 749, 732, 717, 701, 687, 672, 659, 646, 633, 621, 609, 598, 587, 576, 566, 556, 546, 536, 527, 518, 509, 501,
    492, 484, 476, 468, 461, 453, 446, 439, 432, 425, 418, 411, 405, 399, 392, 386, 380, 375, 369, 363, 358, 352, 347,
    341, 336, 331, 326, 321, 316, 311, 307, 302, 297, 293, 289, 284, 280, 276, 271, 267, 263, 259, 255, 251, 248, 244,
    240, 236, 233, 229, 226, 222, 219, 215, 212, 209, 205, 202, 199, 196, 193, 190, 187, 184, 181, 178, 175, 172, 169,
    167, 164, 161, 159, 156, 153, 151, 148, 146, 143, 141, 138, 136, 134, 131, 129, 127, 125, 122, 120, 118, 116, 114,
    112, 110, 108, 106, 104, 102, 100, 98, 96, 94, 92, 91, 89, 87, 85, 83, 82, 80, 78, 77, 75, 74, 72, 70, 69, 67, 66,
    64, 63, 62, 60, 59, 57, 56, 55, 53, 52, 51, 49, 48, 47, 46, 45, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32,
    31, 30, 29, 28, 27, 26, 25, 24, 23, 23, 22, 21, 20, 20, 19, 18, 17, 17, 16, 15, 15, 14, 13, 13, 12, 12, 11, 10,
    10, 9, 9, 8, 8, 7, 7, 7, 6, 6, 5, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
};
/* 4095 * 2^(-i / 256): the fraction of a logarithm made linear, its whole part a shift */
static const uint16_t EXP[256] = {
    4095, 4084, 4073, 4062, 4051, 4040, 4029, 4018, 4007, 3996, 3986, 3975, 3964, 3953, 3943, 3932, 3921, 3911, 3900,
    3890, 3879, 3869, 3858, 3848, 3837, 3827, 3817, 3806, 3796, 3786, 3776, 3765, 3755, 3745, 3735, 3725, 3715, 3705,
    3695, 3685, 3675, 3665, 3655, 3645, 3635, 3625, 3615, 3606, 3596, 3586, 3577, 3567, 3557, 3548, 3538, 3528, 3519,
    3509, 3500, 3490, 3481, 3472, 3462, 3453, 3443, 3434, 3425, 3416, 3406, 3397, 3388, 3379, 3370, 3361, 3351, 3342,
    3333, 3324, 3315, 3306, 3297, 3289, 3280, 3271, 3262, 3253, 3244, 3236, 3227, 3218, 3209, 3201, 3192, 3183, 3175,
    3166, 3158, 3149, 3141, 3132, 3124, 3115, 3107, 3098, 3090, 3082, 3073, 3065, 3057, 3048, 3040, 3032, 3024, 3016,
    3007, 2999, 2991, 2983, 2975, 2967, 2959, 2951, 2943, 2935, 2927, 2919, 2911, 2903, 2896, 2888, 2880, 2872, 2864,
    2857, 2849, 2841, 2834, 2826, 2818, 2811, 2803, 2795, 2788, 2780, 2773, 2765, 2758, 2750, 2743, 2736, 2728, 2721,
    2713, 2706, 2699, 2691, 2684, 2677, 2670, 2662, 2655, 2648, 2641, 2634, 2627, 2620, 2612, 2605, 2598, 2591, 2584,
    2577, 2570, 2563, 2557, 2550, 2543, 2536, 2529, 2522, 2515, 2509, 2502, 2495, 2488, 2481, 2475, 2468, 2461, 2455,
    2448, 2442, 2435, 2428, 2422, 2415, 2409, 2402, 2396, 2389, 2383, 2376, 2370, 2363, 2357, 2351, 2344, 2338, 2332,
    2325, 2319, 2313, 2307, 2300, 2294, 2288, 2282, 2276, 2269, 2263, 2257, 2251, 2245, 2239, 2233, 2227, 2221, 2215,
    2209, 2203, 2197, 2191, 2185, 2179, 2173, 2167, 2161, 2156, 2150, 2144, 2138, 2132, 2127, 2121, 2115, 2109, 2104,
    2098, 2092, 2087, 2081, 2075, 2070, 2064, 2059, 2053,
};
/* 2^(-k/4) of 65536: an envelope's time at a key's rate (its quarter part; the whole part a shift) */
static const uint32_t KSR_Q[4] = {65536, 55109, 46341, 38968};

/* An operator of FM: its wave's phase and step, its envelope (attenuation) and the pace of its stage, its own attenuation. */
typedef struct {
    uint32_t phase, step;
    uint32_t env;  /* 0 (the top) .. FM_QUIET (silence) */
    uint32_t rate; /* the attack: the share it falls a sample (of 65536; 0: never); the decay, the release: what it rises */
    uint16_t base; /* its level and the key's scale, of 0.1875 dB */
    uint8_t  stage;
} op_t;
#endif

typedef struct {
    uint32_t pc;    /* where its next event is, from the song's start; 0: its events are over */
    uint16_t left;  /* ticks to its next events */
    uint16_t off;   /* ticks to the release (0: none coming) */
    uint16_t amp;   /* the loudest it is: of PEAK by its volume and its instrument's */
    uint8_t  stage, instrument, volume, gate, hold;
    union {
#if DGX_GE_SYNTH_CHIP || DGX_GE_SYNTH_NES
        struct {
            uint32_t phase; /* the noise: how far to its next shift, of 65536 */
            uint32_t step;  /* the phase a sample (the noise: shifts a sample, 16.16; the NES's pulse: 0, silent) */
            uint32_t env;   /* the envelope: 0..ENV_TOP */
            uint32_t env_step;
            uint16_t lfsr;  /* the noise's register, 15 bits */
            uint8_t  wave, sustain;
        } tone;
#endif
#if DGX_GE_SYNTH_FM
        struct {
            op_t    op[2]; /* the modulator, the carrier */
            int16_t fb[2]; /* the modulator's last two outputs */
            uint8_t key;   /* the note sounding */
        } fm;
#endif
    } u;
} channel_t;

/* A gain of 0..GAIN_TOP going to another by a step a sample. */
typedef struct {
    uint32_t now, to, step;
} ramp_t;

#define GAIN_TOP    (1u << 24)
#define NOT_WAITING (-2)

struct dgx_ge_music {
    const dgx_ge_sounds_t *sounds;
    const uint8_t         *song; /* the one playing; NULL: none */
    uint32_t               size;
    uint32_t               rate;  /* of the output */
    uint32_t               count; /* toward the next tick: + tempo * 48 a sample, a tick each rate * 60 */
    uint16_t               tempo;
    int16_t                playing; /* its number, -1 none */
    ramp_t                 level;   /* the level the game wants of it, under its bus's volume */
    ramp_t                 fade;    /* the fade of a change: out, then the one waiting in */
    uint32_t               fade_samples; /* how long the one waiting comes in */
    int16_t                waiting; /* the music to start once the one playing has faded out (-1 silence), NOT_WAITING none */
    uint8_t                volume;  /* of the music's bus: 0..255, 255 as written */
    uint8_t                channels; /* of the song playing */
    uint8_t                room;     /* channels kept */
    uint8_t                synth;    /* of the song playing (DGX_GE_SYNTH_IS_*) */
    uint8_t                head;     /* the bytes of its head */
    uint8_t                isize;    /* of each of its instruments */
    uint8_t                told;     /* the synthesizers left out whose songs were asked for: said once, a bit each */
    uint32_t               tail;     /* samples it goes on once the events of every channel are over; UINT32_MAX: to the end of its notes */
#if DGX_GE_SYNTH_FM
    uint32_t               lfo[2], lfo_step[2]; /* the tremolo's (3.7 Hz) and the vibrato's (6.1 Hz) phases */
#endif
    channel_t              ch[];
};

static uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t u16_at(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

bool dgx_ge_synth_built(int synth)
{
    return synth == DGX_GE_SYNTH_IS_CHIP ? DGX_GE_SYNTH_CHIP : synth == DGX_GE_SYNTH_IS_NES ? DGX_GE_SYNTH_NES : synth == DGX_GE_SYNTH_IS_FM ? DGX_GE_SYNTH_FM : false;
}

/* A song's synthesizer, the bytes of its head and of an instrument: version 1 is the chip's. */
static uint8_t synth_of(const uint8_t *s)
{
    return u16_at(s + 4) == 2 ? s[10] : DGX_GE_SYNTH_IS_CHIP;
}

static size_t head_of(const uint8_t *s)
{
    return u16_at(s + 4) == 2 ? SONG_HEAD_2 : SONG_HEAD;
}

static size_t isize_of(const uint8_t *s)
{
    return synth_of(s) == DGX_GE_SYNTH_IS_FM ? FM_INSTRUMENT : INSTRUMENT;
}

/* Whether a song of so many bytes holds together: its head, its instruments and channels, where its channels begin. */
static bool song_holds(const uint8_t *s, size_t bytes)
{
    if (bytes < SONG_HEAD || u32_at(s) != MUSIC_MAGIC || (u16_at(s + 4) != 1 && u16_at(s + 4) != 2)) return false;
    if (bytes < head_of(s) || synth_of(s) > DGX_GE_SYNTH_IS_FM || !s[6] || s[6] > 8 || !s[7]) return false;
    const size_t head = head_of(s) + isize_of(s) * s[7] + (size_t)CHANNEL * s[6];
    if (head > bytes) return false;
    for (int c = 0; c < s[6]; ++c) {
        const uint8_t *ce   = s + head_of(s) + isize_of(s) * s[7] + CHANNEL * c;
        const uint32_t loop = u32_at(ce + 4);
        if (u32_at(ce) < head || u32_at(ce) >= bytes || (loop != NO_LOOP && (loop < head || loop >= bytes))) return false;
    }
    return true;
}

/* Where the musics' entries are: after the sounds' (SOUNDS, version 2). */
static const uint8_t *music_entry(const dgx_ge_sounds_t *sounds, int music)
{
    if (!sounds || !sounds->blob || music < 0 || music >= sounds->musics) return NULL;
    return sounds->blob + 12 + 12 * (size_t)sounds->count + 8 * (size_t)music;
}

bool dgx_ge_music_check(const uint8_t *blob, size_t size, size_t at, uint16_t musics)
{
    if (at + 8 * (size_t)musics > size) return false;
    for (int i = 0; i < musics; ++i) {
        const uint8_t *e    = blob + at + 8 * i;
        const uint32_t from = u32_at(e), bytes = u32_at(e + 4);
        if (!bytes) continue; /* a name only: it plays nothing */
        if (from > size || bytes > size - from || !song_holds(blob + from, bytes)) return false;
    }
    return true;
}

/* A state of music for so many channels at a rate. */
static struct dgx_ge_music *music_alloc(int channels, uint32_t rate)
{
    struct dgx_ge_music *m = calloc(1, sizeof(*m) + (size_t)channels * sizeof(channel_t));
    if (!m) return NULL;
    m->rate = rate, m->room = (uint8_t)channels, m->playing = -1, m->volume = 255, m->waiting = NOT_WAITING;
    m->level.now = m->level.to = m->fade.now = m->fade.to = GAIN_TOP;
#if DGX_GE_SYNTH_FM
    m->lfo_step[0] = (uint32_t)((37ull << 32) / (10ull * rate)), m->lfo_step[1] = (uint32_t)((61ull << 32) / (10ull * rate));
#endif
    return m;
}

bool dgx_ge_music_new(const dgx_ge_sounds_t *sounds, uint32_t rate, struct dgx_ge_music **music)
{
    int most = 0;
    *music   = NULL;
    for (int i = 0; sounds && i < sounds->musics; ++i) {
        const uint8_t *e = music_entry(sounds, i);
        if (u32_at(e + 4) && sounds->blob[u32_at(e) + 6] > most) most = sounds->blob[u32_at(e) + 6];
    }
    if (!most) return true;
    struct dgx_ge_music *m = music_alloc(most, rate);
    if (!m) return false;
    m->sounds = sounds;
    *music    = m;
    return true;
}

/* Samples of so many ms at the rate, one at least. */
static uint32_t samples_of(const struct dgx_ge_music *m, uint16_t ms)
{
    const uint64_t n = (uint64_t)ms * m->rate / 1000;
    return n ? (uint32_t)n : 1;
}

static const uint8_t *instrument_of(const struct dgx_ge_music *m, int i)
{
    return m->song + m->head + m->isize * (i < m->song[7] ? i : 0);
}

/* The loudest a channel is: its volume and its instrument's, of PEAK. */
static void loudness(const struct dgx_ge_music *m, channel_t *c)
{
    c->amp = (uint16_t)((PEAK * c->volume * instrument_of(m, c->instrument)[1] + 112) / 225);
}

/* A note's frequency, Hz * 65536. */
static uint64_t freq_of(int note)
{
    return (uint64_t)freq_low[note % 12] << (note / 12);
}

#if DGX_GE_SYNTH_CHIP || DGX_GE_SYNTH_NES
/* The level of a sustain of 0..15: 15 the top. */
static uint32_t level_of(uint8_t sustain)
{
    return (uint32_t)(((uint64_t)sustain << 24) / 15);
}

static void tone_release(const struct dgx_ge_music *m, channel_t *c)
{
    if (c->stage == SILENT || c->stage == RELEASE) return;
    c->stage           = c->u.tone.env ? RELEASE : SILENT;
    const uint32_t n   = samples_of(m, u16_at(instrument_of(m, c->instrument) + 8));
    c->u.tone.env_step = c->u.tone.env / n ? c->u.tone.env / n : 1;
}

static void decay(const struct dgx_ge_music *m, channel_t *c)
{
    const uint32_t s = level_of(c->u.tone.sustain);
    if (s >= ENV_TOP) {
        c->stage = SUSTAIN;
        return;
    }
    c->stage           = DECAY;
    const uint32_t n   = samples_of(m, u16_at(instrument_of(m, c->instrument) + 6));
    c->u.tone.env_step = (ENV_TOP - s) / n ? (ENV_TOP - s) / n : 1;
}

/* The envelope of the chip and the NES on by a sample. */
static void tone_envelope(const struct dgx_ge_music *m, channel_t *c)
{
    uint32_t *env = &c->u.tone.env;
    switch (c->stage) {
    case ATTACK:
        if (ENV_TOP - *env <= c->u.tone.env_step) *env = ENV_TOP, decay(m, c);
        else *env += c->u.tone.env_step;
        break;
    case DECAY: {
        const uint32_t s = level_of(c->u.tone.sustain);
        if (*env - s <= c->u.tone.env_step) *env = s, c->stage = s ? SUSTAIN : SILENT;
        else *env -= c->u.tone.env_step;
        break;
    }
    case RELEASE:
        if (*env <= c->u.tone.env_step) *env = 0, c->stage = SILENT;
        else *env -= c->u.tone.env_step;
        break;
    default: break;
    }
}

/* An attack of the chip's envelope, unless the note goes on from the one before. */
static void tone_attack(const struct dgx_ge_music *m, channel_t *c, const uint8_t *in, bool legato)
{
    if (legato && c->stage != SILENT && c->stage != RELEASE) return;
    c->stage           = ATTACK;
    const uint32_t n   = samples_of(m, u16_at(in + 4));
    c->u.tone.env_step = ENV_TOP / n;
}
#endif

#if DGX_GE_SYNTH_CHIP
/* A note of the chip: its frequency for the wave, the instrument's wave and envelope. */
static void chip_note(const struct dgx_ge_music *m, channel_t *c, int note, bool legato)
{
    const uint8_t *in = instrument_of(m, c->instrument);
    c->u.tone.wave = in[0] <= W_NOISE ? in[0] : W_SQUARE_50, c->u.tone.sustain = in[2] > 15 ? 15 : in[2];
    loudness(m, c);
    const uint64_t f16 = freq_of(note); /* Hz * 65536 */
    if (c->u.tone.wave == W_NOISE) {
        const uint64_t s = f16 * 16 / m->rate; /* shifts a sample, 16.16 */
        c->u.tone.step   = s > (64u << 16) ? 64u << 16 : (uint32_t)s;
    } else {
        const uint64_t s = (f16 << 16) / m->rate; /* f * 2^32 / rate */
        c->u.tone.step   = s > 0x80000000u ? 0x80000000u : (uint32_t)s;
    }
    tone_attack(m, c, in, legato);
}

static int32_t chip_sample(struct dgx_ge_music *m, channel_t *c)
{
    tone_envelope(m, c);
    int32_t w;
    if (c->u.tone.wave == W_NOISE) {
        w = c->u.tone.lfsr & 1 ? 32767 : -32767;
        c->u.tone.phase += c->u.tone.step;
        for (uint32_t n = c->u.tone.phase >> 16; n; --n) c->u.tone.lfsr = (uint16_t)(c->u.tone.lfsr >> 1 | ((c->u.tone.lfsr ^ c->u.tone.lfsr >> 1) & 1) << 14);
        c->u.tone.phase &= 0xFFFF;
    } else if (c->u.tone.wave == W_TRIANGLE) {
        const int32_t x = (int32_t)(c->u.tone.phase >> 16);
        w               = x < 32768 ? 2 * x - 32768 : 98303 - 2 * x;
        c->u.tone.phase += c->u.tone.step;
    } else {
        w = c->u.tone.phase < duty[c->u.tone.wave] ? 32767 : -32767;
        c->u.tone.phase += c->u.tone.step;
    }
    return ((w * c->amp) >> 15) * (int32_t)(c->u.tone.env >> 8) >> 16;
}
#endif

#if DGX_GE_SYNTH_NES
/* The period of a timer (the CPU's cycles * per) nearest to a note, an octave up while the 11 bits cannot reach it. */
static uint32_t nes_period(uint64_t f16, uint32_t per)
{
    for (;;) {
        const uint64_t p = ((uint64_t)NES_CLOCK * 65536 * 2 / (per * f16) + 1) / 2;
        if (p <= 2048) return p ? (uint32_t)p : 1;
        f16 <<= 1;
    }
}

/* A note of the NES: the pulse's or the triangle's timer, the noise's period; the chip's envelope. */
static void nes_note(const struct dgx_ge_music *m, channel_t *c, int note, bool legato)
{
    const uint8_t *in = instrument_of(m, c->instrument);
    c->u.tone.wave = in[0] <= W_NOISE_SHORT ? in[0] : W_SQUARE_50, c->u.tone.sustain = in[2] > 15 ? 15 : in[2];
    loudness(m, c);
    const uint64_t f16 = freq_of(note);
    if (c->u.tone.wave == W_NOISE || c->u.tone.wave == W_NOISE_SHORT) {
        /* the period whose pace of shifts is nearest to 16 times the note's frequency */
        const uint64_t want = f16 * 16;
        int            best = 0;
        uint64_t       miss = UINT64_MAX;
        for (int i = 0; i < 16; ++i) {
            const uint64_t pace = (uint64_t)NES_CLOCK * 65536 / nes_noise[i], d = pace > want ? pace - want : want - pace;
            if (d < miss) miss = d, best = i;
        }
        const uint64_t s = (uint64_t)NES_CLOCK * 65536 / ((uint64_t)nes_noise[best] * m->rate); /* shifts a sample, 16.16 */
        c->u.tone.step   = s > (64u << 16) ? 64u << 16 : (uint32_t)s;
    } else if (c->u.tone.wave == W_TRIANGLE) {
        const uint64_t s = ((uint64_t)NES_CLOCK << 32) / (32ull * nes_period(f16, 32) * m->rate);
        c->u.tone.step   = s > 0x80000000u ? 0x80000000u : (uint32_t)s;
    } else {
        const uint32_t p = nes_period(f16, 16); /* t + 1: below 9 (t < 8) the pulse is silent */
        const uint64_t s = ((uint64_t)NES_CLOCK << 32) / (16ull * p * m->rate);
        c->u.tone.step   = p < 9 ? 0 : s > 0x80000000u ? 0x80000000u : (uint32_t)s;
    }
    tone_attack(m, c, in, legato);
}

static int32_t nes_sample(struct dgx_ge_music *m, channel_t *c)
{
    tone_envelope(m, c);
    int32_t  w;
    uint32_t q; /* the volume of 16 levels */
    if (c->u.tone.wave == W_NOISE || c->u.tone.wave == W_NOISE_SHORT) {
        const int tap = c->u.tone.wave == W_NOISE ? 1 : 6;
        w = c->u.tone.lfsr & 1 ? 32767 : -32767;
        c->u.tone.phase += c->u.tone.step;
        for (uint32_t n = c->u.tone.phase >> 16; n; --n) c->u.tone.lfsr = (uint16_t)(c->u.tone.lfsr >> 1 | ((c->u.tone.lfsr ^ c->u.tone.lfsr >> tap) & 1) << 14);
        c->u.tone.phase &= 0xFFFF;
    } else if (c->u.tone.wave == W_TRIANGLE) {
        const uint32_t k = c->u.tone.phase >> 27, level = k < 16 ? 15 - k : k - 16;
        c->u.tone.phase += c->u.tone.step;
        return c->stage == SILENT ? 0 : ((2 * (int32_t)level - 15) * 2184 * PEAK) >> 15; /* whole while it sounds */
    } else {
        w = !c->u.tone.step ? 0 : c->u.tone.phase < nes_duty[c->u.tone.wave] ? 32767 : -32767;
        c->u.tone.phase += c->u.tone.step;
    }
    q = (uint32_t)(((uint64_t)(c->u.tone.env >> 8) * c->amp * 15 + ((uint64_t)PEAK << 15)) / ((uint64_t)PEAK << 16));
    return (w * (int32_t)(q * PEAK / 15)) >> 15;
}
#endif

#if DGX_GE_SYNTH_FM
/* An operator's bytes in an FM instrument (the modulator 0, the carrier 1): multiplier * 2, level, key scale level,
 * flags, wave, sustain, attack, decay, release (u16, ms). */
static const uint8_t *fm_op(const struct dgx_ge_music *m, const channel_t *c, int k)
{
    return instrument_of(m, c->instrument) + 4 + 12 * k;
}

/* Samples of an operator's time at its key's rate (NEVER: 0, it does not move). */
static uint32_t fm_samples(const struct dgx_ge_music *m, const channel_t *c, const uint8_t *o, uint16_t ms)
{
    if (ms == NEVER) return 0;
    const int      key   = c->u.fm.key;
    const int      block = key < 19 ? 0 : (key - 19) / 12 > 7 ? 7 : (key - 19) / 12;
    const int      kc = 2 * block + 1, r = o[3] & FM_KSR ? kc : kc >> 2;
    const uint32_t n = (uint32_t)(((uint64_t)samples_of(m, ms) * KSR_Q[r & 3]) >> (16 + (r >> 2)));
    return n ? n : 1;
}

/* An operator's sustain as attenuation: 3 dB a step down from the top (15), 0 is 93 dB. */
static uint32_t fm_sustain(const uint8_t *o)
{
    const uint32_t sl = 15 - (o[5] > 15 ? 15 : o[5]);
    return (sl == 15 ? 496u : sl * 16) << 16;
}

/* An operator into a stage: its pace from its instrument. */
static void fm_stage(const struct dgx_ge_music *m, channel_t *c, int k, int stage)
{
    op_t          *op = &c->u.fm.op[k];
    const uint8_t *o  = fm_op(m, c, k);
    op->stage         = (uint8_t)stage;
    if (stage == ATTACK) {
        const uint32_t n = fm_samples(m, c, o, u16_at(o + 6));
        op->rate         = !n ? 0 : FM_ATTACK / n >= 65536 ? 65536 : FM_ATTACK / n;
        if (op->rate == 65536 || op->env == 0) op->env = 0, stage = DECAY, op->stage = DECAY;
        else return;
    }
    if (stage == DECAY) {
        if (fm_sustain(o) == 0) stage = SUSTAIN, op->stage = SUSTAIN;
        else {
            const uint32_t n = fm_samples(m, c, o, u16_at(o + 8));
            op->rate         = n ? FM_QUIET / n : 0;
            return;
        }
    }
    if (stage == SUSTAIN && !(o[3] & FM_PERCUSSIVE)) {
        op->rate = 0; /* held while the note sounds */
        return;
    }
    if (stage == SUSTAIN || stage == RELEASE) {
        const uint32_t n = fm_samples(m, c, o, u16_at(o + 10));
        op->rate         = n ? FM_QUIET / n : 0;
    }
}

static void fm_release(const struct dgx_ge_music *m, channel_t *c)
{
    if (c->stage == SILENT || c->stage == RELEASE) return;
    c->stage = RELEASE;
    for (int k = 0; k < 2; ++k)
        if (c->u.fm.op[k].stage != SILENT) fm_stage(m, c, k, RELEASE);
}

/* A note of FM: each operator's step and own attenuation; both from their attack and the start of their wave, unless it goes on from the one before. */
static void fm_note(const struct dgx_ge_music *m, channel_t *c, int note, bool legato)
{
    loudness(m, c);
    c->u.fm.key = (uint8_t)note;
    for (int k = 0; k < 2; ++k) {
        const uint8_t *o = fm_op(m, c, k);
        const uint64_t s = (freq_of(note) * o[0] << 15) / m->rate; /* f * multiplier * 2^32 / rate */
        static const uint8_t ksl[4] = {0, 8, 16, 32};      /* 0, 1.5, 3, 6 dB an octave */
        c->u.fm.op[k].step = s > 0x80000000u ? 0x80000000u : (uint32_t)s;
        c->u.fm.op[k].base = (uint16_t)(4 * (o[1] & 63) + (note > 19 ? ksl[o[2] & 3] * (note - 19) / 12 : 0));
    }
    if (legato && c->stage != SILENT && c->stage != RELEASE) return;
    c->stage = ATTACK;
    for (int k = 0; k < 2; ++k) c->u.fm.op[k].phase = 0, fm_stage(m, c, k, ATTACK);
}

/* An operator's envelope on by a sample. */
static void fm_envelope(const struct dgx_ge_music *m, channel_t *c, int k)
{
    op_t *op = &c->u.fm.op[k];
    switch (op->stage) {
    case ATTACK: {
        if (!op->rate) break;
        const uint32_t d = (uint32_t)(((uint64_t)(op->env + FM_E0) * op->rate) >> 16);
        if (d >= op->env) op->env = 0, fm_stage(m, c, k, DECAY);
        else op->env -= d;
        break;
    }
    case DECAY: {
        const uint32_t s = fm_sustain(fm_op(m, c, k));
        if (op->env + op->rate >= s) op->env = s, fm_stage(m, c, k, SUSTAIN);
        else op->env += op->rate;
        break;
    }
    case SUSTAIN:
        op->env = op->env + op->rate >= FM_QUIET ? FM_QUIET : op->env + op->rate; /* percussive: down at the release's pace */
        break;
    case RELEASE:
        /* silent at 96 dB, or once nothing of it is heard (78 dB with its own: its output 0 from there on) */
        if (op->env + op->rate >= FM_QUIET || ((op->env + op->rate) >> 16) + op->base >= 416) op->env = FM_QUIET, op->stage = SILENT;
        else op->env += op->rate;
        break;
    default: break;
    }
}

/* An operator's output, -4095..4095: its wave at a phase of 1024 a turn, an attenuation of 1/256 of a factor of 2. */
static int32_t fm_out(uint8_t wave, uint32_t phase, uint32_t att)
{
    phase &= 1023;
    const bool neg = phase & 512;
    if ((wave == 1 && neg) || (wave == 3 && (phase & 256))) return 0; /* the half-sine's lower half, the quarter-sine's falling quarters */
    const uint32_t q = phase & 256 && wave != 3 ? 255 - (phase & 255) : phase & 255;
    const uint32_t t = LOGSIN[q] + att;
    const int32_t  v = t >= 13 << 8 ? 0 : EXP[t & 255] >> (t >> 8);
    return neg && wave == 0 ? -v : v;
}

/* An operator's attenuation as a logarithm: its envelope, its own, the tremolo's (0..42: 1 dB). */
static uint32_t fm_att(const op_t *op, const uint8_t *o, uint32_t am)
{
    const uint32_t a = (op->env >> 16) + op->base;
    return a >= 512 ? 13 << 8 : (a << 3) + (o[3] & FM_TREMOLO ? am : 0);
}

/* An operator's phase on by a sample, by its vibrato (-32767..32767 of 7 cents) if it has one. */
static void fm_on(op_t *op, const uint8_t *o, int32_t vib)
{
    op->phase += o[3] & FM_VIBRATO ? op->step + (uint32_t)(((int64_t)op->step * vib * 265) >> 31) : op->step;
}

static int32_t fm_sample(struct dgx_ge_music *m, channel_t *c, uint32_t am, int32_t vib)
{
    const uint8_t *in = instrument_of(m, c->instrument), *mo = in + 4, *co = in + 16;
    op_t          *mod = &c->u.fm.op[0], *car = &c->u.fm.op[1];
    fm_envelope(m, c, 0), fm_envelope(m, c, 1);
    const int32_t fb  = in[2] ? (c->u.fm.fb[0] + c->u.fm.fb[1]) >> (9 - (in[2] > 7 ? 7 : in[2])) : 0;
    const int32_t mout = fm_out(mo[4], (mod->phase >> 22) + (uint32_t)fb, fm_att(mod, mo, am));
    c->u.fm.fb[1] = c->u.fm.fb[0], c->u.fm.fb[0] = (int16_t)mout;
    const int32_t cout = fm_out(co[4], (car->phase >> 22) + (uint32_t)(in[0] ? 0 : mout), fm_att(car, co, am));
    fm_on(mod, mo, vib), fm_on(car, co, vib);
    /* silent once released and its operators heard are over */
    if (c->stage == RELEASE && car->stage == SILENT && (!in[0] || mod->stage == SILENT)) c->stage = SILENT;
    return ((in[0] ? mout + cout : cout) * c->amp) >> 12;
}
#endif

/* A note on the song's synthesizer (one left out never gets here). */
static void note_on(const struct dgx_ge_music *m, channel_t *c, int note, bool legato)
{
    switch (m->synth) {
#if DGX_GE_SYNTH_CHIP
    case DGX_GE_SYNTH_IS_CHIP: chip_note(m, c, note, legato); break;
#endif
#if DGX_GE_SYNTH_NES
    case DGX_GE_SYNTH_IS_NES: nes_note(m, c, note, legato); break;
#endif
#if DGX_GE_SYNTH_FM
    case DGX_GE_SYNTH_IS_FM: fm_note(m, c, note, legato); break;
#endif
    default: break;
    }
}

static void release(const struct dgx_ge_music *m, channel_t *c)
{
#if DGX_GE_SYNTH_FM
    if (m->synth == DGX_GE_SYNTH_IS_FM) {
        fm_release(m, c);
        return;
    }
#endif
#if DGX_GE_SYNTH_CHIP || DGX_GE_SYNTH_NES
    tone_release(m, c);
#endif
}

/* A length of ticks: a byte, or two. */
static uint16_t length_at(const struct dgx_ge_music *m, channel_t *c)
{
    if (c->pc >= m->size) return 0;
    const uint8_t b = m->song[c->pc++];
    if (b < 0x80) return b;
    if (c->pc >= m->size) return 0;
    return (uint16_t)((b & 0x7F) << 8 | m->song[c->pc++]);
}

/* The events of a channel up to its next wait: a note or a rest; at its end, to its loop or over. */
static void read(struct dgx_ge_music *m, channel_t *c, int k)
{
    const uint8_t *ce = m->song + m->head + m->isize * m->song[7] + CHANNEL * k;
    for (int reads = 0; c->pc && !c->left; ++reads) {
        if (reads == READS || c->pc >= m->size) {
            c->pc = 0, release(m, c);
            return;
        }
        const uint8_t op = m->song[c->pc++];
        if (op < 0x80) {
            const uint16_t len = length_at(m, c);
            if (!len) {
                c->pc = 0, release(m, c);
                return;
            }
            const bool legato = c->hold & HELD;
            c->hold           = c->hold & HOLD_NEXT ? HELD : 0;
            note_on(m, c, op, legato);
            c->left = len;
            c->off  = c->hold & HELD ? 0 : (uint16_t)(len * c->gate / 8 ? len * c->gate / 8 : 1);
            continue;
        }
        switch (op) {
        case EV_REST: {
            const uint16_t len = length_at(m, c);
            if (!len) {
                c->pc = 0, release(m, c);
                return;
            }
            c->hold = 0, c->off = 0, c->left = len;
            release(m, c);
            break;
        }
        case EV_SLUR: c->hold |= HOLD_NEXT; break;
        case EV_INSTRUMENT:
        case EV_VOLUME:
        case EV_GATE:
            if (c->pc >= m->size) {
                c->pc = 0;
                break;
            }
            if (op == EV_INSTRUMENT) c->instrument = m->song[c->pc] < m->song[7] ? m->song[c->pc] : c->instrument;
            else if (op == EV_VOLUME) c->volume = m->song[c->pc] > 15 ? 15 : m->song[c->pc], loudness(m, c);
            else c->gate = m->song[c->pc] < 1 ? 1 : m->song[c->pc] > 8 ? 8 : m->song[c->pc];
            ++c->pc;
            break;
        case EV_TEMPO:
            if (c->pc + 2 > m->size) {
                c->pc = 0;
                break;
            }
            m->tempo = u16_at(m->song + c->pc) ? u16_at(m->song + c->pc) : 1;
            c->pc += 2;
            break;
        default: /* the end, or what is no event: to the loop, or over */
            c->pc = op == EV_END && u32_at(ce + 4) != NO_LOOP ? u32_at(ce + 4) : 0;
            if (!c->pc) c->hold = 0, release(m, c);
            break;
        }
    }
}

/* A tick of the song's clock: the releases due, the next events of the channels whose wait is over. */
static void tick(struct dgx_ge_music *m)
{
    for (int k = 0; k < m->channels; ++k) {
        channel_t *c = &m->ch[k];
        if (c->off && !--c->off) release(m, c);
        if (c->left && !--c->left) read(m, c, k);
    }
}

/* A song from its start (one that holds together, of a synthesizer of the build, of channels it has room for). */
static void begin(struct dgx_ge_music *m, const uint8_t *song, uint32_t size)
{
    memset(m->ch, 0, (size_t)m->room * sizeof(channel_t));
    m->song = song, m->size = size, m->synth = synth_of(song), m->head = (uint8_t)head_of(song), m->isize = (uint8_t)isize_of(song);
    m->channels = m->song[6], m->tempo = u16_at(m->song + 8) ? u16_at(m->song + 8) : 120, m->count = 0;
    m->tail     = m->head == SONG_HEAD_2 && u16_at(song + 12) != NO_TAIL ? (uint32_t)((uint64_t)u16_at(song + 12) * m->rate / 1000) : UINT32_MAX;
#if DGX_GE_SYNTH_FM
    m->lfo[0] = m->lfo[1] = 0;
#endif
    for (int k = 0; k < m->channels; ++k) {
        channel_t *c = &m->ch[k];
        c->pc = u32_at(m->song + m->head + m->isize * m->song[7] + CHANNEL * k), c->volume = 15, c->gate = 8;
#if DGX_GE_SYNTH_FM
        if (m->synth == DGX_GE_SYNTH_IS_FM) c->u.fm.op[0].env = c->u.fm.op[1].env = FM_QUIET;
        else
#endif
        {
#if DGX_GE_SYNTH_CHIP || DGX_GE_SYNTH_NES
            c->u.tone.lfsr = 1;
#endif
        }
        read(m, c, k);
    }
}

static void start(struct dgx_ge_music *m, int music)
{
    const uint8_t *e = music_entry(m->sounds, music);
    memset(m->ch, 0, (size_t)m->room * sizeof(channel_t));
    m->song = NULL, m->playing = -1;
    if (!e || !u32_at(e + 4) || m->sounds->blob[u32_at(e) + 6] > m->room) return;
    const uint8_t *song = m->sounds->blob + u32_at(e);
    if (!dgx_ge_synth_built(synth_of(song))) {
        /* a song of a synthesizer left out of the build: nothing plays, said once */
        if (!(m->told & 1u << synth_of(song))) {
            static const char *const names[] = {"chip", "nes", "fm"};
            m->told |= (uint8_t)(1u << synth_of(song));
#ifndef DGX_GE_MUSIC_ALONE
            ESP_LOGW("dgx-ge", "music %d: its synthesizer %s is not in this build (dgx_ge_synths): it plays nothing", music, names[synth_of(song)]);
#else
            (void)names;
#endif
        }
        return;
    }
    begin(m, song, u32_at(e + 4));
    m->playing = (int16_t)music;
}

/* A gain on its way to another: the rest of the way a sample, its last step whatever is left; true as it gets there. */
static bool ramp_on(ramp_t *r)
{
    if (r->now == r->to) return false;
    if (r->now < r->to) r->now = r->to - r->now <= r->step ? r->to : r->now + r->step;
    else r->now = r->now - r->to <= r->step ? r->to : r->now - r->step;
    return r->now == r->to;
}

/* A gain to go to another over so many samples (0: at once): there at the last of them. */
static void ramp_to(ramp_t *r, uint32_t to, uint32_t samples)
{
    const uint32_t way = to > r->now ? to - r->now : r->now - to;
    r->to = to, r->step = samples ? (uint32_t)(((uint64_t)way + samples - 1) / samples) : way;
    if (!samples) r->now = to;
}

/* The music asked for after a fade out: it starts, coming in as long as the other went out (none: silence). */
static void begin_waiting(struct dgx_ge_music *m)
{
    const int music = m->waiting;
    m->waiting      = NOT_WAITING;
    start(m, music);
    m->fade.now = m->fade_samples && m->song ? 0 : GAIN_TOP;
    ramp_to(&m->fade, GAIN_TOP, m->song ? m->fade_samples : 0);
}

int32_t dgx_ge_music_next(struct dgx_ge_music *m)
{
    if (!m->song && m->waiting != NOT_WAITING) begin_waiting(m); /* the old one ended by itself while it faded */
    if (!m->song) return 0;
    int32_t sum = 0;
    bool    on = false, events = false;
#if DGX_GE_SYNTH_FM
    uint32_t am = 0;
    int32_t  vib = 0;
    if (m->synth == DGX_GE_SYNTH_IS_FM) {
        /* the tremolo 0..42 (1 dB), the vibrato -32767..32767: triangles */
        const uint32_t a = m->lfo[0] >> 16, v = m->lfo[1] >> 16;
        am = ((a < 32768 ? a : 65535 - a) * 85) >> 16, vib = 2 * (int32_t)(v < 32768 ? v : 65535 - v) - 32767;
        m->lfo[0] += m->lfo_step[0], m->lfo[1] += m->lfo_step[1];
    }
#endif
    for (int k = 0; k < m->channels; ++k) {
        channel_t *c = &m->ch[k];
        events |= c->pc != 0;
        if (c->stage == SILENT) continue;
        on = true;
        /* the envelope on by a sample, then the wave at it */
        switch (m->synth) {
#if DGX_GE_SYNTH_CHIP
        case DGX_GE_SYNTH_IS_CHIP: sum += chip_sample(m, c); break;
#endif
#if DGX_GE_SYNTH_NES
        case DGX_GE_SYNTH_IS_NES: sum += nes_sample(m, c); break;
#endif
#if DGX_GE_SYNTH_FM
        case DGX_GE_SYNTH_IS_FM: sum += fm_sample(m, c, am, vib); break;
#endif
        default: break;
        }
    }
    /* the clock: a tick each time the count passes rate * 60 */
    m->count += (uint32_t)m->tempo * PER_QUARTER;
    while (m->count >= m->rate * 60) m->count -= m->rate * 60, tick(m);
    on |= events;
    if (!events && m->tail != UINT32_MAX) {
        if (!m->tail) on = false; /* its tail over: what still rings is cut */
        else --m->tail;
    }
    if (!on) m->song = NULL, m->playing = -1; /* every channel over and silent: the song has ended */
    /* the bus's volume, the level the game wants under it, the fade */
    ramp_on(&m->level);
    if (ramp_on(&m->fade) && !m->fade.now && m->waiting != NOT_WAITING) begin_waiting(m);
    int32_t gain = m->volume + (m->volume >> 7);
    gain         = (int32_t)(((uint32_t)gain * (m->level.now >> 8)) >> 16);
    gain         = (int32_t)(((uint32_t)gain * (m->fade.now >> 8)) >> 16);
    return sum * gain >> 8;
}

#ifndef DGX_GE_MUSIC_ALONE
/* The platform's lock round every call, as the mixer's. */
static void take(const dgx_ge_mixer_t *mixer)
{
    if (mixer->lock) mixer->lock(true);
}

static void give(const dgx_ge_mixer_t *mixer)
{
    if (mixer->lock) mixer->lock(false);
}

/* A number of none (-1, 255, beyond the musics): silence. */
static int music_of(const struct dgx_ge_music *m, int music)
{
    return music_entry(m->sounds, music) ? music : -1;
}

bool dgx_ge_mixer_music(dgx_ge_mixer_t *mixer, int music, uint32_t fade_ms)
{
    take(mixer);
    struct dgx_ge_music *m = mixer->music;
    if (m) {
        music            = music_of(m, music);
        const uint32_t n = fade_ms ? samples_of(m, fade_ms > 65535 ? 65535 : (uint16_t)fade_ms) : 0;
        if (m->waiting != NOT_WAITING && music == m->playing && m->song) {
            /* back to the one fading out: it comes in again, as fast as it would from silence */
            m->waiting = NOT_WAITING, m->fade.to = GAIN_TOP, m->fade.step = n ? (GAIN_TOP + n - 1) / n : GAIN_TOP;
        } else if (m->waiting != NOT_WAITING) {
            m->waiting = (int16_t)music; /* another after the fade out */
        } else if (music != m->playing || !m->song) {
            if (n && m->song) { /* the one playing out first, then the other in */
                m->waiting = (int16_t)music, m->fade_samples = n;
                ramp_to(&m->fade, 0, n);
            } else {
                m->waiting = (int16_t)music, m->fade_samples = n;
                begin_waiting(m);
            }
        }
    }
    const bool on = m && (m->waiting != NOT_WAITING ? m->waiting >= 0 : m->song != NULL); /* once a fade is over */
    give(mixer);
    return on;
}

void dgx_ge_mixer_music_level(dgx_ge_mixer_t *mixer, int percent, uint32_t ms)
{
    take(mixer);
    struct dgx_ge_music *m = mixer->music;
    percent                = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    if (m) ramp_to(&m->level, (uint32_t)((uint64_t)GAIN_TOP * (uint32_t)percent / 100), ms ? samples_of(m, ms > 65535 ? 65535 : (uint16_t)ms) : 0);
    give(mixer);
}

int dgx_ge_mixer_music_now(const dgx_ge_mixer_t *mixer)
{
    take(mixer);
    const int now = mixer->music ? mixer->music->playing : -1;
    give(mixer);
    return now;
}

void dgx_ge_mixer_music_volume(dgx_ge_mixer_t *mixer, uint8_t volume)
{
    take(mixer);
    if (mixer->music) mixer->music->volume = volume;
    give(mixer);
}
#endif

int dgx_ge_song_check(const uint8_t *song, size_t size)
{
    if (!song || size > UINT32_MAX || !song_holds(song, size)) return 1;
    return dgx_ge_synth_built(synth_of(song)) ? 0 : 2;
}

dgx_ge_song_player_t *dgx_ge_song_player_new(const uint8_t *song, size_t size, uint32_t rate)
{
    if (!rate || dgx_ge_song_check(song, size)) return NULL;
    struct dgx_ge_music *m = music_alloc(song[6], rate);
    if (!m) return NULL;
    begin(m, song, (uint32_t)size);
    m->playing = 0;
    return m;
}

size_t dgx_ge_song_render(dgx_ge_song_player_t *m, int16_t *out, size_t frames)
{
    size_t given = 0;
    for (size_t i = 0; i < frames; ++i) {
        if (!m || !m->song) {
            out[i] = 0;
            continue;
        }
        const int32_t s = dgx_ge_music_next(m); /* as the mixer adds it, alone, at the volume of all 255: clipped to 16 bits */
        out[i]          = (int16_t)(s < -32768 ? -32768 : s > 32767 ? 32767 : s);
        ++given;
    }
    return given;
}

void dgx_ge_song_player_free(dgx_ge_song_player_t *m)
{
    free(m);
}

size_t dgx_ge_music_render(const uint8_t *song, size_t size, uint32_t rate, int16_t *out, size_t frames)
{
    dgx_ge_song_player_t *m = dgx_ge_song_player_new(song, size, rate);
    if (!m) return 0;
    const size_t given = dgx_ge_song_render(m, out, frames);
    dgx_ge_song_player_free(m);
    return given;
}
