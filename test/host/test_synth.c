/*
 * The synthesizers of music (make_synth_pack.py), sample by sample against references worked out here: FM's sine
 * operator (its tables made again from their formulas, and near a true sine), a carrier modulated by a modulator with
 * feedback, the envelope (the attack's curve, the decay to the sustain, the release), the waves, the additive
 * connection, a percussive envelope, the key scale level; the NES's pulse of four duties, its triangle's staircase, its
 * noise of two modes, its 16 levels of volume, a note below its timer's reach; a chip song cut by its tail; the
 * project's synthesizer and a song's own; a set's instrument; a song played alone (dgx_ge_synth.h) the same as in the
 * mixer. Built with every synthesizer (as dgx_ge_synths(chip nes fm)); built with the chip alone (test_synth_chip), a
 * song of FM plays nothing and the log says so once.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "dgx_ge_mixer.h"
#include "dgx_ge_synth.h"
#include "synth_pack.h"

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*size);
    if (data && fread(data, 1, *size, f) != *size) free(data), data = NULL;
    fclose(f);
    return data;
}

static int16_t         out[200000], alone[200000];
static dgx_ge_sounds_t sounds;

/* A music played by a mixer of its own from its start: the samples into out (at most limit), how many before it ended. */
static int play(int music, uint32_t rate, int limit)
{
    dgx_ge_mixer_t m;
    CHECK(dgx_ge_mixer_init(&m, &sounds, 1, rate, 1));
    dgx_ge_mixer_music(&m, music, 0);
    int n = 0;
    while (n < limit && dgx_ge_mixer_music_now(&m) == music) dgx_ge_mixer_fill(&m, out + n, 1), ++n;
    dgx_ge_mixer_free(&m);
    return n;
}

/* A music's song where it lies in the blob, and its size. */
static const uint8_t *song_of(int music, size_t *size)
{
    const uint8_t *e  = sounds.blob + 12 + 12 * sounds.count + 8 * music;
    const uint32_t at = e[0] | e[1] << 8 | e[2] << 16 | (uint32_t)e[3] << 24;
    *size             = e[4] | e[5] << 8 | e[6] << 16 | (uint32_t)e[7] << 24;
    return sounds.blob + at;
}

/* ---- FM, made again here: the tables by their formulas, an operator's output ---- */

static uint16_t LOG[256], EXP2[256];

static void tables(void)
{
    for (int i = 0; i < 256; ++i) {
        LOG[i]  = (uint16_t)lround(-log2(sin((i + 0.5) * M_PI / 512)) * 256);
        EXP2[i] = (uint16_t)lround(4095 * pow(2, -i / 256.0));
    }
}

/* wave 0 sine, 1 half-sine, 2 abs-sine, 3 quarter-sine at a phase of 1024 a turn; att of 1/256 octave */
static int32_t op(int wave, int32_t phase, int32_t att)
{
    phase &= 1023;
    const int quarter = phase >> 8, i = phase & 255;
    if (wave == 1 && quarter >= 2) return 0;
    if (wave == 3 && (quarter == 1 || quarter == 3)) return 0;
    const int     idx = (quarter == 1 || quarter == 3) ? 255 - i : i;
    const int32_t t   = LOG[idx] + att;
    const int32_t v   = t >= 13 * 256 ? 0 : EXP2[t & 255] >> (t >> 8);
    return wave == 0 && quarter >= 2 ? -v : v;
}

/* the phase a sample of a note of so many Hz * 65536, its multiplier * 2 */
static uint32_t fm_step(uint64_t f16, int mult2, uint32_t rate)
{
    return (uint32_t)((f16 * (uint64_t)mult2 << 15) / rate);
}

#define A4    ((uint64_t)440 << 16)
#define QUIET (512u << 16)
/* the envelope's times at o4 a (MIDI 69: block 4, its rate 2 of 4 without ksr): 2^(-2/4) of theirs */
static uint32_t at_a4(uint32_t ms, uint32_t rate)
{
    return (uint32_t)(((uint64_t)(ms * rate / 1000) * 46341) >> 16);
}

int main(int argc, char **argv)
{
    char   path[512];
    size_t ssize;
    snprintf(path, sizeof path, "%s/synth.sounds", argc > 1 ? argv[1] : ".");
    uint8_t *blob = read_file(path, &ssize);
    CHECK(blob && dgx_ge_sounds_open(&sounds, blob, ssize) && sounds.musics == SY_MUSIC_COUNT);
    if (!blob) return 1;

    if (!dgx_ge_synth_built(DGX_GE_SYNTH_IS_FM)) {
        /* the chip alone: a song of FM plays nothing, is no music playing, and is said once; the chip's play */
        CHECK(dgx_ge_synth_built(DGX_GE_SYNTH_IS_CHIP) && !dgx_ge_synth_built(DGX_GE_SYNTH_IS_NES));
        /* what the mixer says goes into a file while it plays them (the checks' own words after) */
        snprintf(path, sizeof path, "%s/synth_chip.log", argc > 1 ? argv[1] : ".");
        fflush(stderr);
        const int saved = dup(2);
        FILE     *log   = fopen(path, "w");
        if (!log) return 1;
        dup2(fileno(log), 2);
        dgx_ge_mixer_t m;
        const bool     made = dgx_ge_mixer_init(&m, &sounds, 1, 8000, 1);
        const bool     none = !dgx_ge_mixer_music(&m, SY_MUSIC_FM_SINE, 0) && dgx_ge_mixer_music_now(&m) == -1;
        dgx_ge_mixer_fill(&m, out, 100);
        bool quiet = true;
        for (int i = 0; i < 100; ++i) quiet &= out[i] == 0;
        const bool again = !dgx_ge_mixer_music(&m, SY_MUSIC_FM_MOD, 0) && !dgx_ge_mixer_music(&m, SY_MUSIC_NES_PULSE, 0); /* fm again, then the NES */
        const bool chip  = dgx_ge_mixer_music(&m, SY_MUSIC_OWN_CHIP, 0) && dgx_ge_mixer_music_now(&m) == SY_MUSIC_OWN_CHIP;
        dgx_ge_mixer_fill(&m, out, 10);
        dgx_ge_mixer_free(&m);
        fflush(stderr);
        dup2(saved, 2), close(saved), fclose(log);
        CHECK(made && none && quiet && again && chip && out[0] == 8191);
        size_t lsize = 0;
        char  *said  = (char *)read_file(path, &lsize);
        int    lines = 0;
        for (size_t i = 0; said && i < lsize; ++i) lines += said[i] == '\n';
        CHECK(said && lines == 2 && strstr(said, "synthesizer fm is not in this build") && strstr(said, "synthesizer nes is not in this build"));
        size_t         n;
        const uint8_t *s = song_of(SY_MUSIC_FM_SINE, &n);
        CHECK(dgx_ge_song_check(s, n) == 2 && !dgx_ge_song_player_new(s, n, 8000) && dgx_ge_music_render(s, n, 8000, out, 10) == 0);
        free(said), free(blob);
        CHECK_DONE();
    }
    CHECK(dgx_ge_synth_built(DGX_GE_SYNTH_IS_CHIP) && dgx_ge_synth_built(DGX_GE_SYNTH_IS_NES) && !dgx_ge_synth_built(3));
    tables();

    /* FM, the carrier alone: a sine of 440 Hz at 22050 from the phase 0, at its loudest 4095 * 2: two seconds */
    {
        const uint32_t rate = 22050, step = fm_step(A4, 2, rate);
        const int      n    = play(SY_MUSIC_FM_SINE, rate, 100000);
        bool           same = true;
        int            far  = 0;
        for (int i = 0; i < 2 * (int)rate; ++i) {
            const int32_t want = op(0, (int32_t)((uint32_t)(i * step) >> 22), 0) * 8192 >> 12;
            same &= out[i] == want;
            const int d = abs(out[i] - (int)lround(8190 * sin(2 * M_PI * 440.0 * i / rate)));
            far         = d > far ? d : far;
        }
        CHECK(same && far < 40 && n == 2 * (int)rate + 2); /* its release (0 ms) a sample, a silent one: over */
        /* the same song alone (the editor's player), in pieces: the same samples */
        size_t                size;
        const uint8_t        *s = song_of(SY_MUSIC_FM_SINE, &size);
        dgx_ge_song_player_t *p = dgx_ge_song_player_new(s, size, rate);
        CHECK(p && dgx_ge_song_check(s, size) == 0 && dgx_ge_song_render(p, alone, 1000) == 1000);
        CHECK(dgx_ge_song_render(p, alone + 1000, 99000) == (size_t)n - 1000 && alone[n] == 0);
        CHECK(memcmp(out, alone, (size_t)n * sizeof(out[0])) == 0);
        dgx_ge_song_player_free(p);
        CHECK(dgx_ge_music_render(s, size, rate, alone, 2000) == 2000 && memcmp(out, alone, 2000 * sizeof(out[0])) == 0);
        CHECK(dgx_ge_song_check(s, 13) == 1 && !dgx_ge_song_player_new(s, size, 0) && dgx_ge_song_check(blob, 40) == 1);
    }

    /* the modulator at twice the note, 7.5 dB down (40 of 0.1875 dB), its feedback 5: into the carrier's phase */
    {
        const uint32_t rate = 22050, sm = fm_step(A4, 4, rate), sc = fm_step(A4, 2, rate);
        play(SY_MUSIC_FM_MOD, rate, 30000);
        int32_t  f0 = 0, f1 = 0;
        uint32_t pm = 0, pc = 0;
        bool     same = true, other = false;
        for (int i = 0; i < 30000; ++i) {
            const int32_t mo = op(0, (int32_t)(pm >> 22) + ((f0 + f1) >> 4), 40 << 3);
            f1 = f0, f0 = mo;
            const int32_t co = op(0, (int32_t)(pc >> 22) + mo, 0);
            same &= out[i] == co * 8192 >> 12;
            other |= out[i] != op(0, (int32_t)(pc >> 22), 0) * 8192 >> 12;
            pm += sm, pc += sc;
        }
        CHECK(same && other);
    }

    /* the envelope at 8000: the attack a curve from silence (its share of where it is and 3 dB a sample), the decay
       a line to 9 dB down, held; at the gate (half the note) the release, a line, silent once nothing is heard */
    {
        const uint32_t rate = 8000, step = fm_step(A4, 2, rate);
        const int      n    = play(SY_MUSIC_FM_ENV, rate, 30000);
        const uint32_t attack = 229148u / at_a4(10, rate), decay = QUIET / at_a4(100, rate), release = QUIET / at_a4(50, rate);
        const uint32_t sustain = 48u << 16;
        uint32_t       env = QUIET, phase = 0;
        int            stage = 1, top = -1, held = -1, silent = -1;
        bool           same  = true;
        for (int i = 0; i < n; ++i) {
            if (i == 4000) stage = 4; /* the release at the gate: 48 ticks of 83.3 samples */
            if (stage == 1) {
                const uint32_t d = (uint32_t)(((uint64_t)(env + (16u << 16)) * attack) >> 16);
                if (d >= env) env = 0, stage = 2, top = i;
                else env -= d;
            } else if (stage == 2) {
                if (env + decay >= sustain) env = sustain, stage = 3, held = i;
                else env += decay;
            } else if (stage == 4) {
                if (env + release >= QUIET || ((env + release) >> 16) >= 416) env = QUIET, stage = 0, silent = i;
                else env += release;
            }
            const int32_t a    = (int32_t)(env >> 16);
            const int32_t want = (a >= 512 ? 0 : op(0, (int32_t)(phase >> 22), a << 3)) * 8192 >> 12;
            same &= out[i] == want;
            phase += step;
        }
        CHECK(same);
        /* the attack 10 ms at the lowest notes: 7.1 at o4 a; the decay 9 dB of 96 in 100 ms (as long, at o4 a: 6.6 ms) */
        CHECK(top >= 54 && top <= 58 && held - top >= 50 && held - top <= 55);
        int peak = 0;
        for (int i = 2000; i < 2100; ++i) peak = abs(out[i]) > peak ? abs(out[i]) : peak;
        CHECK(peak >= 2850 && peak <= 2960); /* 9 dB down: 0.355 of 8190 */
        /* the release from 9 dB to 78 dB (nothing heard) at 96 dB a 35 ms: some 25 ms; then the song is over */
        CHECK(silent - 4000 >= 190 && silent - 4000 <= 215 && n == 8001); /* the note over at 8000 */
    }

    /* the waves: half-sine (its lower half silent), abs-sine (both halves up), quarter-sine (the rising quarters, twice) */
    {
        const uint32_t rate = 8000, step = fm_step(A4, 2, rate);
        play(SY_MUSIC_FM_WAVES, rate, 13000);
        bool same = true, half = true, up = true, quarters = true;
        for (int i = 0; i < 12000; ++i) {
            const int     wave = 1 + i / 4000;
            const int32_t ph   = (int32_t)((uint32_t)((i % 4000) * step) >> 22), want = op(wave, ph, 0) * 8192 >> 12;
            same &= out[i] == want;
            if (wave == 1) half &= (ph & 512) ? out[i] == 0 : out[i] >= 0;
            if (wave == 2) up &= out[i] >= 0;
            if (wave == 3) quarters &= (ph & 256) ? out[i] == 0 : out[i] >= 0;
        }
        CHECK(same && half && up && quarters);
    }

    /* additive: the modulator heard beside the carrier, the carrier unmodulated */
    {
        const uint32_t rate = 8000, sm = fm_step(A4, 4, rate), sc = fm_step(A4, 2, rate);
        play(SY_MUSIC_FM_ADD, rate, 16000);
        bool same = true;
        for (int i = 0; i < 16000; ++i) same &= out[i] == (op(0, (int32_t)((uint32_t)(i * sm) >> 22), 0) + op(0, (int32_t)((uint32_t)(i * sc) >> 22), 0)) * 8192 >> 12;
        CHECK(same);
    }

    /* percussive: down to 9 dB at the decay's pace, then on down at the release's (500 ms a 96 dB) while the note sounds */
    {
        const uint32_t rate = 8000, step = fm_step(A4, 2, rate), decay = QUIET / at_a4(100, rate), release = QUIET / at_a4(500, rate);
        play(SY_MUSIC_FM_PERC, rate, 16000);
        uint32_t env = 0;
        int      stage = 2;
        bool     same = true;
        for (int i = 0; i < 16000; ++i) {
            if (stage == 2) {
                if (env + decay >= 48u << 16) env = 48u << 16, stage = 3;
                else env += decay;
            } else env = env + release >= QUIET ? QUIET : env + release;
            const int32_t a = (int32_t)(env >> 16);
            same &= out[i] == (a >= 512 ? 0 : op(0, (int32_t)((uint32_t)(i * step) >> 22), a << 3)) * 8192 >> 12;
        }
        CHECK(same && abs(out[15995]) + abs(out[15996]) + abs(out[15997]) + abs(out[15998]) == 0); /* died before the note's end */
    }

    /* the key scale level, 6 dB an octave from G0: o2 a 26 semitones up (69 of 0.1875 dB), o6 a 74 (197): 24 dB apart */
    {
        const uint32_t rate = 8000;
        play(SY_MUSIC_FM_KSL, rate, 9000);
        bool same = true;
        int  low = 0, high = 0;
        for (int i = 0; i < 8000; ++i) {
            const int     o2   = i < 4000;
            const int32_t want = op(0, (int32_t)((uint32_t)((i % 4000) * fm_step(o2 ? A4 >> 2 : A4 << 2, 2, rate)) >> 22), (o2 ? 69 : 197) << 3) * 8192 >> 12;
            same &= out[i] == want;
            if (o2) low = abs(out[i]) > low ? abs(out[i]) : low;
            else high = abs(out[i]) > high ? abs(out[i]) : high;
        }
        CHECK(same && high * 16 > low * 9 / 10 && high * 16 < low * 11 / 10); /* 24 dB: a sixteenth */
    }

    /* the lowest c at 22050: every place of the tables met, each as its formula makes it */
    {
        const uint32_t rate = 22050, step = fm_step(535809u << 1, 2, rate);
        const int      n    = play(SY_MUSIC_FM_LOW, rate, 20000);
        bool           same = true;
        int            seen = 0;
        static bool    met[1024];
        for (int i = 0; i < (int)rate / 2; ++i) {
            const int32_t ph = (int32_t)((uint32_t)(i * step) >> 22);
            same &= out[i] == op(0, ph, 0) * 8192 >> 12;
            seen += !met[ph], met[ph] = true;
        }
        CHECK(same && seen == 1024 && n == (int)rate / 2 + 2);
    }

    /* the release alone at 8000: from the top at 4000, 96 dB a 35 ms (50 at the lowest notes), silent at 78 dB: over */
    {
        const uint32_t rate = 8000, step = fm_step(A4, 2, rate), release = QUIET / at_a4(50, rate);
        const int      n    = play(SY_MUSIC_FM_REL, rate, 20000);
        uint32_t       env  = 0;
        int            silent = -1;
        bool           same = true;
        for (int i = 0; i < n && silent < 0; ++i) {
            if (i >= 4000) {
                if (env + release >= QUIET || ((env + release) >> 16) >= 416) env = QUIET, silent = i;
                else env += release;
            }
            const int32_t a = (int32_t)(env >> 16);
            same &= out[i] == (a >= 512 ? 0 : op(0, (int32_t)((uint32_t)(i * step) >> 22), a << 3)) * 8192 >> 12;
        }
        CHECK(same && silent == 4000 + 229 && n == silent + 2); /* (416 << 16) / its pace: 230 samples */
    }

    /* the tremolo and the vibrato: triangles from the song's start, 0..42 of 1/256 octave (1 dB), +-7 cents */
    {
        const uint32_t rate = 22050, step = fm_step(A4, 2, rate);
        const uint32_t s0 = (uint32_t)((37ull << 32) / (10ull * rate)), s1 = (uint32_t)((61ull << 32) / (10ull * rate));
        play(SY_MUSIC_FM_LFO, rate, 2 * (int)rate);
        uint32_t l0 = 0, l1 = 0, phase = 0;
        bool     same = true;
        int      most = 0, least = 9000;
        for (int i = 0; i < 2 * (int)rate; ++i) {
            const uint32_t a = l0 >> 16, v = l1 >> 16;
            const uint32_t am  = ((a < 32768 ? a : 65535 - a) * 85) >> 16;
            const int32_t  vib = 2 * (int32_t)(v < 32768 ? v : 65535 - v) - 32767;
            same &= out[i] == op(0, (int32_t)(phase >> 22), (int32_t)am) * 8192 >> 12;
            phase += step + (uint32_t)(((int64_t)step * vib * 265) >> 31);
            l0 += s0, l1 += s1;
            if (i % 441 == 440) { /* the loudest of each 20 ms */
                int peak = 0;
                for (int k = i - 440; k <= i; ++k) peak = abs(out[k]) > peak ? abs(out[k]) : peak;
                most = peak > most ? peak : most, least = peak < least ? peak : least;
            }
        }
        CHECK(same && least * 100 > most * 87 && least * 100 < most * 92); /* 1 dB: 0.89 */
    }

    /* the NES's pulse: its timer's period nearest to 440 Hz, 254 (440.4 Hz), duties 12.5, 25, 50, 75%: a quarter each */
    {
        const uint32_t rate = 8000, step = (uint32_t)(((uint64_t)1789773 << 32) / (16ull * 254 * rate));
        static const uint32_t duty[4] = {1u << 29, 1u << 30, 1u << 31, 3u << 30};
        play(SY_MUSIC_NES_PULSE, rate, 17000);
        bool same = true;
        int  high[4] = {0};
        for (int i = 0; i < 16000; ++i) {
            const bool up = (uint32_t)(i * step) < duty[i / 4000];
            same &= out[i] == (up ? 8191 : -8192);
            high[i / 4000] += up;
        }
        CHECK(same && abs(high[0] - 500) < 12 && abs(high[1] - 1000) < 12 && abs(high[2] - 2000) < 12 && abs(high[3] - 3000) < 12);
    }

    /* its triangle: the staircase of 32 steps, 16 levels, whole whatever the volume (v8): 127 (440.4 Hz) */
    {
        const uint32_t rate = 22050, step = (uint32_t)(((uint64_t)1789773 << 32) / (32ull * 127 * rate));
        const int      n    = play(SY_MUSIC_NES_TRI, rate, 20000);
        bool           same = true;
        int            levels[16] = {0};
        for (int i = 0; i < (int)rate / 2; ++i) {
            const uint32_t k = (uint32_t)(i * step) >> 27, level = k < 16 ? 15 - k : k - 16;
            same &= out[i] == ((2 * (int32_t)level - 15) * 2184 * 8192) >> 15;
            ++levels[level];
        }
        bool all = true;
        for (int l = 0; l < 16; ++l) all &= levels[l] > 0;
        CHECK(same && all && n == (int)rate / 2 + 2 && out[n - 2] == 0 && out[0] == 8190);
    }

    /* its noise: c4 (16 * 261.6 Hz) nearest to the period 380; long (bits 0 ^ 1), then short (0 ^ 6), one register */
    {
        const uint32_t rate = 8000, shifts = (uint32_t)((uint64_t)1789773 * 65536 / (380ull * rate));
        play(SY_MUSIC_NES_NOISE, rate, 9000);
        uint32_t lfsr = 1, phase = 0;
        bool     same = true;
        for (int i = 0; i < 8000; ++i) {
            const int tap = i < 4000 ? 1 : 6;
            same &= out[i] == (lfsr & 1 ? 8191 : -8192);
            phase += shifts;
            for (uint32_t n = phase >> 16; n; --n) lfsr = lfsr >> 1 | ((lfsr ^ lfsr >> tap) & 1) << 14;
            phase &= 0xFFFF;
        }
        CHECK(same);
        /* the short mode goes round in 93 shifts (or 31) from where it is; the long one in 32767 */
        uint32_t s = lfsr, k = 0;
        do s = s >> 1 | ((s ^ s >> 6) & 1) << 14, ++k;
        while (s != lfsr && k < 40000);
        uint32_t l = 1, j = 0;
        do l = l >> 1 | ((l ^ l >> 1) & 1) << 14, ++j;
        while (l != 1 && j < 40000);
        CHECK((k == 93 || k == 31) && j == 32767);
    }

    /* its volume, 16 levels: v8 of the top is level 8; o1 c (32.7 Hz) below the pulse's lowest, an octave up: 1710 */
    {
        const uint32_t rate = 8000, a = (uint32_t)(((uint64_t)1789773 << 32) / (16ull * 254 * rate));
        const uint32_t c = (uint32_t)(((uint64_t)1789773 << 32) / (16ull * 1710 * rate));
        play(SY_MUSIC_NES_VOLUME, rate, 9000);
        const int32_t hi = 32767 * (8 * 8192 / 15) >> 15, lo = -32767 * (8 * 8192 / 15) >> 15;
        bool          same = true;
        uint32_t      phase = 0;
        for (int i = 0; i < 8000; ++i) {
            same &= out[i] == (phase < (1u << 31) ? hi : lo);
            phase += i < 4000 ? a : c;
        }
        CHECK(same && hi == 4368 && lo == -4369);
    }

    /* the chip with a tail of 100 ms: its note over at 4000, its release of a second cut at 800 more */
    {
        size_t size;
        CHECK(song_of(SY_MUSIC_CHIP_TAIL, &size)[4] == 2 && song_of(SY_MUSIC_CHIP_TAIL, &size)[10] == DGX_GE_SYNTH_IS_CHIP);
        const int n = play(SY_MUSIC_CHIP_TAIL, 8000, 20000);
        CHECK(n == 4801 && out[4800] != 0);
    }

    /* the project's synthesizer: the NES, the square 75% high three quarters; a song's own #synth: the chip (version 1) */
    {
        size_t size;
        CHECK(song_of(SY_MUSIC_PROJECT_NES, &size)[10] == DGX_GE_SYNTH_IS_NES && song_of(SY_MUSIC_OWN_CHIP, &size)[4] == 1);
        play(SY_MUSIC_PROJECT_NES, 8000, 4000);
        int high = 0;
        for (int i = 0; i < 4000; ++i) high += out[i] > 0;
        CHECK(abs(high - 3000) < 12);
        CHECK(play(SY_MUSIC_OWN_CHIP, 8000, 5000) == 4002 && out[0] == 8191 && out[4] == 8191 && out[10] == -8192);
    }

    /* a set's instrument by name: the piano of msx-music, the only one in the song; it sounds and dies away */
    {
        size_t size;
        CHECK(song_of(SY_MUSIC_SET_PIANO, &size)[7] == 1);
        const int n    = play(SY_MUSIC_SET_PIANO, 22050, 40000);
        int       peak = 0;
        for (int i = 0; i < 2000; ++i) peak = abs(out[i]) > peak ? abs(out[i]) : peak;
        CHECK(n > 11025 && peak > 2000);
    }

    /* a song whose head is wrong: its synthesizer none of the three; the blob refused */
    {
        uint8_t *bad = malloc(ssize);
        memcpy(bad, blob, ssize);
        size_t         size;
        const uint8_t *s = song_of(SY_MUSIC_CHIP_TAIL, &size); /* version 2, its instruments as the chip's */
        bad[s - blob + 10] = 3;
        CHECK(!dgx_ge_sounds_open(&(dgx_ge_sounds_t){0}, bad, ssize) && dgx_ge_song_check(bad + (s - blob), size) == 1);
        bad[s - blob + 10] = DGX_GE_SYNTH_IS_CHIP, bad[s - blob + 4] = 3;
        CHECK(!dgx_ge_sounds_open(&(dgx_ge_sounds_t){0}, bad, ssize));
        free(bad);
    }
    free(blob);
    CHECK_DONE();
}
