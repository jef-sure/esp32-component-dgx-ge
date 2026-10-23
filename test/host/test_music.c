/*
 * Music by notes in the mixer (make_music_pack.py): a square's period and duty sample by sample, the envelope's
 * shape, the noise's register, all against references worked out here; the clock at several rates, a tempo
 * changing; going round or ending; the music's own voice (no effect takes it, stopping the sounds leaves it), its
 * volume; the same music asked again goes on; a script's music and music stop reaching the game, and the script
 * waiting on the expression music for its end.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_anim.h"
#include "dgx_ge_mixer.h"
#include "dgx_ge_pack.h"
#include "music_pack.h"

#define TOP (1u << 24)

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

static int16_t out[500000], other[500000];

/* A wave of +-32767 at the loudest channel (v15, an instrument of 15: a quarter of the scale) and an envelope's level. */
static int32_t sample_of(int32_t w, uint32_t env)
{
    return ((w * 8192) >> 15) * (int32_t)(env >> 8) >> 16;
}

/* How many samples a mixer plays a music before it stops by itself (at most limit). */
static int length_of(dgx_ge_sounds_t *sounds, int music, uint32_t rate, int limit)
{
    dgx_ge_mixer_t m;
    CHECK(dgx_ge_mixer_init(&m, sounds, 1, rate, 1));
    dgx_ge_mixer_music(&m, music, 0);
    int n = 0;
    while (n < limit && dgx_ge_mixer_music_now(&m) == music) dgx_ge_mixer_fill(&m, out + n, 1), ++n;
    dgx_ge_mixer_free(&m);
    return n;
}

static dgx_ge_mixer_t *said_mixer;
static dgx_ge_sound_t  output;
static int             signals;
static uint32_t        signal_at;

static void say(dgx_ge_actor_t *actor, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    (void)actor, (void)ctx;
    if (dgx_ge_anim_said_sound(&output, what, value)) return; /* the music, its fade and level: the engine's */
    if (what == DGX_GE_ANIM_SIGNAL) ++signals, signal_at = at_ms;
}

int main(int argc, char **argv)
{
    char path[512];
    size_t ssize, psize;
    snprintf(path, sizeof path, "%s/music.sounds", argc > 1 ? argv[1] : ".");
    uint8_t *blob = read_file(path, &ssize);
    snprintf(path, sizeof path, "%s/music.pack", argc > 1 ? argv[1] : ".");
    uint8_t *file = read_file(path, &psize);
    CHECK(blob && file);
    if (!blob || !file) return 1;
    dgx_ge_sounds_t sounds;
    CHECK(dgx_ge_sounds_open(&sounds, blob, ssize) && sounds.count == MU_SOUND_COUNT && sounds.musics == MU_MUSIC_COUNT);
    CHECK(blob[4] == 2); /* version 2: with music */
    uint16_t rate;
    CHECK(dgx_ge_sounds_length(&sounds, MU_SOUND_CLICK, &rate) == 200 && rate == 8000); /* the sounds where they were */

    /* a square of 25%, 440 Hz at 8000: high while the phase (440 * 2^32 / 8000 a sample) is under a quarter */
    dgx_ge_mixer_t m;
    CHECK(dgx_ge_mixer_init(&m, &sounds, 1, 8000, 1) && m.music);
    CHECK(dgx_ge_mixer_music_now(&m) == -1);
    CHECK(dgx_ge_mixer_music(&m, MU_MUSIC_SQUARE, 0) && dgx_ge_mixer_music_now(&m) == MU_MUSIC_SQUARE);
    dgx_ge_mixer_fill(&m, out, 16002);
    const uint32_t step = (uint32_t)(((uint64_t)440 << 32) / 8000);
    bool exact = true;
    int  rises = 0, high = 0;
    for (int i = 0; i < 16000; ++i) {
        const int32_t want = (uint32_t)(i * step) < (1u << 30) ? 8191 : -8192;
        exact &= out[i] == want;
        high += out[i] > 0, rises += i && out[i] > 0 && out[i - 1] < 0;
    }
    CHECK(exact);
    CHECK(rises >= 879 && rises <= 880 && high >= 3990 && high <= 4010); /* 880 periods in 2 s, a quarter of each high */
    CHECK(out[16000] == 0 && out[16001] == 0 && dgx_ge_mixer_music_now(&m) == -1); /* a whole note at 120: 2 s, then over */
    dgx_ge_mixer_free(&m);

    /* the envelope: attack 80 samples, decay 160 to 5/15, held, at the gate (half the note) release 240; the wave a square of 50% of o0 c */
    CHECK(dgx_ge_mixer_init(&m, &sounds, 1, 8000, 1));
    dgx_ge_mixer_music(&m, MU_MUSIC_ENVELOPE, 0);
    dgx_ge_mixer_fill(&m, out, 8001);
    {
        const uint32_t s = (uint32_t)((5ull << 24) / 15), wstep = (uint32_t)(((uint64_t)(535809u << 1) << 16) / 8000);
        uint32_t env = 0, d = 0, phase = 0;
        int      stage = 1;
        bool     same = true;
        for (int i = 0; i < 8001; ++i) {
            if (i == 4000) stage = 4, d = env / 240;
            if (stage == 1) {
                if (TOP - env <= TOP / 80) env = TOP, stage = 2, d = (TOP - s) / 160;
                else env += TOP / 80;
            } else if (stage == 2) {
                if (env - s <= d) env = s, stage = 3;
                else env -= d;
            } else if (stage == 4) {
                if (env <= d) env = 0, stage = 0;
                else env -= d;
            }
            const int32_t want = stage || env ? sample_of(phase < (1u << 31) ? 32767 : -32767, env) : 0;
            same &= out[i] == want;
            phase += wstep;
        }
        CHECK(same);
    }
    /* the top a sample late: the steps of TOP / 80 fall short by their rounding */
    CHECK(abs(out[79]) < 8191 && abs(out[80]) == 8191 && abs(out[239]) > 2730 && abs(out[240]) == 2730 && abs(out[3999]) == 2730); /* the top, the sustain held */
    CHECK(abs(out[4119]) > 1300 && abs(out[4119]) < 1400 && out[4239] == 0 && out[7999] == 0); /* half way down, silent */
    CHECK(dgx_ge_mixer_music_now(&m) == -1);
    dgx_ge_mixer_free(&m);

    /* noise: the register of 15 bits (bit 0 out, bits 0 ^ 1 in at the top), shifted 16 times o4 c a second; the same every time */
    for (int round = 0; round < 2; ++round) {
        CHECK(dgx_ge_mixer_init(&m, &sounds, 1, 8000, 1));
        dgx_ge_mixer_music(&m, MU_MUSIC_NOISE, 0);
        dgx_ge_mixer_fill(&m, round ? other : out, 4000);
        dgx_ge_mixer_free(&m);
    }
    {
        const uint32_t shifts = (uint32_t)((uint64_t)(535809u << 5) * 16 / 8000);
        uint32_t lfsr = 1, phase = 0;
        bool     same = true;
        int      ups  = 0;
        for (int i = 0; i < 4000; ++i) {
            same &= out[i] == (lfsr & 1 ? 8191 : -8192);
            ups += out[i] > 0;
            phase += shifts;
            for (uint32_t n = phase >> 16; n; --n) lfsr = lfsr >> 1 | ((lfsr ^ lfsr >> 1) & 1) << 14;
            phase &= 0xFFFF;
        }
        CHECK(same && memcmp(out, other, 4000 * sizeof(out[0])) == 0 && ups > 1600 && ups < 2400); /* about as many up as down */
    }

    /* the clock: a quarter at 150 is 0.4 s at every rate, then a rest as long, then over; at 144 and 22050 9187.5: 9188 */
    static const uint32_t rates[] = {8000, 11025, 22050, 44100, 48000};
    for (int k = 0; k < 5; ++k) {
        const int n = (int)(rates[k] * 2 / 5);
        CHECK(length_of(&sounds, MU_MUSIC_CLOCK, rates[k], 100000) == 2 * n + 1);
        bool rest = true;
        for (int i = n; i < 2 * n; ++i) rest &= out[i] == 0;
        CHECK(out[n - 1] != 0 && out[0] != 0 && rest);
    }
    CHECK(length_of(&sounds, MU_MUSIC_ODD, 22050, 100000) == 9190 && out[9187] != 0 && out[9188] == 0); /* the release, then over */
    /* a tempo changing: c4 at 120 (4000), r4, then at 240 c4 (2000), r4, c4&c4 (4000), r4 (2000) */
    CHECK(length_of(&sounds, MU_MUSIC_TEMPO, 8000, 100000) == 18001);
    CHECK(out[3999] && !out[4000] && !out[7999] && out[8000] && out[9999] && !out[10000] && !out[11999] && out[12000] && out[15999] && !out[16000]);

    /* going round from its loop point: playing still after a minute; one channel of it ends, the other goes on */
    CHECK(length_of(&sounds, MU_MUSIC_ROUND, 8000, 60 * 8000) == 60 * 8000);
    bool sounding = false;
    for (int i = 59 * 8000; i < 60 * 8000; ++i) sounding |= out[i] != 0;
    CHECK(sounding);

    /* the music's own voice: effects on the only voice, the oldest giving way, then stop(-1): the music the same throughout */
    dgx_ge_mixer_t alone, click;
    CHECK(dgx_ge_mixer_init(&m, &sounds, 1, 8000, 1) && dgx_ge_mixer_init(&alone, &sounds, 1, 8000, 1) && dgx_ge_mixer_init(&click, &sounds, 1, 8000, 1));
    dgx_ge_mixer_music(&m, MU_MUSIC_SQUARE, 0), dgx_ge_mixer_music(&alone, MU_MUSIC_SQUARE, 0);
    dgx_ge_mixer_fill(&m, out, 100), dgx_ge_mixer_fill(&alone, other, 100);
    CHECK(memcmp(out, other, 100 * sizeof(out[0])) == 0);
    for (int i = 0; i < 3; ++i) CHECK(dgx_ge_mixer_play(&m, MU_SOUND_CLICK, 255, 0) == 0);
    dgx_ge_mixer_play(&click, MU_SOUND_CLICK, 255, 0);
    dgx_ge_mixer_fill(&m, out, 50), dgx_ge_mixer_fill(&alone, other, 50), dgx_ge_mixer_fill(&click, other + 50, 50);
    bool summed = true;
    for (int i = 0; i < 50; ++i) summed &= out[i] == other[i] + other[50 + i];
    CHECK(summed && dgx_ge_mixer_music_now(&m) == MU_MUSIC_SQUARE);
    dgx_ge_mixer_stop(&m, -1);
    CHECK(dgx_ge_mixer_music_now(&m) == MU_MUSIC_SQUARE && !dgx_ge_mixer_busy(&m));
    dgx_ge_mixer_fill(&m, out, 100), dgx_ge_mixer_fill(&alone, other, 100);
    CHECK(memcmp(out, other, 100 * sizeof(out[0])) == 0);
    /* its volume, apart from the sounds' and under the volume of all */
    dgx_ge_mixer_music_volume(&m, 128);
    dgx_ge_mixer_fill(&m, out, 100), dgx_ge_mixer_fill(&alone, other, 100);
    bool half = true;
    for (int i = 0; i < 100; ++i) half &= out[i] == (other[i] * 129 >> 8);
    CHECK(half);
    dgx_ge_mixer_music_volume(&m, 255), dgx_ge_mixer_volume(&m, 0);
    dgx_ge_mixer_fill(&m, out, 10), dgx_ge_mixer_fill(&alone, other, 10);
    CHECK(out[0] == 0 && out[9] == 0);
    dgx_ge_mixer_volume(&m, 255);
    /* the two buses: the effects' at 0 leaves the music alone; the music's at 0 the click alone, both at their volume */
    dgx_ge_mixer_play(&m, MU_SOUND_CLICK, 255, 0), dgx_ge_mixer_play(&click, MU_SOUND_CLICK, 255, 0);
    dgx_ge_mixer_effects_volume(&m, 0);
    dgx_ge_mixer_fill(&m, out, 20), dgx_ge_mixer_fill(&alone, other, 20), dgx_ge_mixer_fill(&click, other + 20, 20);
    CHECK(memcmp(out, other, 20 * sizeof(out[0])) == 0 && other[20] != 0);
    dgx_ge_mixer_effects_volume(&m, 128), dgx_ge_mixer_music_volume(&m, 0);
    dgx_ge_mixer_fill(&m, out, 20), dgx_ge_mixer_fill(&alone, other, 20), dgx_ge_mixer_fill(&click, other + 20, 20);
    bool bus = true;
    for (int i = 0; i < 20; ++i) bus &= out[i] == (other[20 + i] * 129 >> 8);
    CHECK(bus);
    dgx_ge_mixer_effects_volume(&m, 255), dgx_ge_mixer_music_volume(&m, 255), dgx_ge_mixer_stop(&m, -1);
    /* the same music asked again goes on; stopped, then asked, from its start */
    CHECK(dgx_ge_mixer_music(&m, MU_MUSIC_SQUARE, 0));
    dgx_ge_mixer_fill(&m, out, 100), dgx_ge_mixer_fill(&alone, other, 100);
    CHECK(memcmp(out, other, 100 * sizeof(out[0])) == 0);
    CHECK(!dgx_ge_mixer_music(&m, -1, 0) && dgx_ge_mixer_music_now(&m) == -1);
    dgx_ge_mixer_fill(&m, out, 10);
    CHECK(out[0] == 0 && out[9] == 0);
    dgx_ge_mixer_music(&m, MU_MUSIC_SQUARE, 0);
    dgx_ge_mixer_fill(&m, out, 10);
    CHECK(out[0] == 8191 && out[4] == 8191 && out[5] == -8192);
    CHECK(!dgx_ge_mixer_music(&m, 255, 0) && !dgx_ge_mixer_music(&m, MU_MUSIC_COUNT, 0) && dgx_ge_mixer_music_now(&m) == -1); /* none such: stopped */
    /* a change by a fade of 100 ms (800 samples): the square out, down by a straight line, then the noise in from silence */
    dgx_ge_mixer_music(&m, MU_MUSIC_SQUARE, 0);
    CHECK(dgx_ge_mixer_music(&m, MU_MUSIC_NOISE, 100));
    dgx_ge_mixer_fill(&m, out, 400);
    CHECK(dgx_ge_mixer_music_now(&m) == MU_MUSIC_SQUARE && abs(out[0]) > 8000 && abs(out[399]) > 3950 && abs(out[399]) < 4150);
    dgx_ge_mixer_fill(&m, out, 400);
    CHECK(abs(out[399]) < 60 && dgx_ge_mixer_music_now(&m) == MU_MUSIC_NOISE);
    dgx_ge_mixer_fill(&m, out, 800);
    CHECK(abs(out[0]) < 60 && abs(out[399]) > 3950 && abs(out[399]) < 4150 && abs(out[799]) > 8100);
    /* asked back while it fades out: it comes in again, and goes on */
    CHECK(dgx_ge_mixer_music(&m, MU_MUSIC_SQUARE, 100));
    dgx_ge_mixer_fill(&m, out, 400);
    CHECK(dgx_ge_mixer_music_now(&m) == MU_MUSIC_NOISE && abs(out[399]) < 4150);
    CHECK(dgx_ge_mixer_music(&m, MU_MUSIC_NOISE, 100));
    dgx_ge_mixer_fill(&m, out, 400);
    CHECK(dgx_ge_mixer_music_now(&m) == MU_MUSIC_NOISE && abs(out[399]) > 8100);
    /* silence by a fade: the one playing until it is out; from silence a music comes in */
    CHECK(!dgx_ge_mixer_music(&m, -1, 100));
    dgx_ge_mixer_fill(&m, out, 400);
    CHECK(dgx_ge_mixer_music_now(&m) == MU_MUSIC_NOISE);
    dgx_ge_mixer_fill(&m, out, 401);
    CHECK(dgx_ge_mixer_music_now(&m) == -1 && out[400] == 0);
    CHECK(dgx_ge_mixer_music(&m, MU_MUSIC_SQUARE, 100) && dgx_ge_mixer_music_now(&m) == MU_MUSIC_SQUARE);
    dgx_ge_mixer_fill(&m, out, 800);
    CHECK(abs(out[0]) < 60 && abs(out[399]) > 3950 && abs(out[399]) < 4150 && abs(out[799]) > 8100);
    /* the level by design: half over 100 ms, held, for the next music too; back the same way; the bus untouched */
    dgx_ge_mixer_music_level(&m, 50, 100);
    dgx_ge_mixer_fill(&m, out, 400);
    CHECK(abs(out[399]) > 6050 && abs(out[399]) < 6250);
    dgx_ge_mixer_fill(&m, out, 800);
    CHECK(abs(out[399]) > 4050 && abs(out[399]) < 4150 && abs(out[799]) > 4050 && abs(out[799]) < 4150);
    dgx_ge_mixer_music(&m, MU_MUSIC_NOISE, 0);
    dgx_ge_mixer_fill(&m, out, 10);
    CHECK(abs(out[9]) > 4050 && abs(out[9]) < 4150);
    dgx_ge_mixer_music_level(&m, 100, 0);
    dgx_ge_mixer_fill(&m, out, 10);
    CHECK(abs(out[9]) > 8100);
    /* a stereo output: the music in the middle */
    dgx_ge_mixer_t wide;
    CHECK(dgx_ge_mixer_init(&wide, &sounds, 1, 8000, 2));
    dgx_ge_mixer_music(&wide, MU_MUSIC_SQUARE, 0);
    dgx_ge_mixer_fill(&wide, out, 20);
    CHECK(out[0] == 8191 && out[1] == 8191 && out[38] == out[39] && out[38] != 0);
    dgx_ge_mixer_free(&wide), dgx_ge_mixer_free(&alone), dgx_ge_mixer_free(&click);

    /* the game's output: the music and which plays */
    output = dgx_ge_mixer_output(&m);
    dgx_ge_sound_music(&output, MU_MUSIC_THEME, 0);
    CHECK(dgx_ge_sound_music_now(&output) == MU_MUSIC_THEME);
    dgx_ge_sound_music(&output, 255, 0);
    CHECK(dgx_ge_sound_music_now(&output) == -1 && dgx_ge_sound_music_now(NULL) == -1 && dgx_ge_sound_music_now(&(dgx_ge_sound_t){0}) == -1);
    dgx_ge_sound_music(&(dgx_ge_sound_t){0}, 1, 0); /* an output without music: nothing */

    /* a script plays a music and waits for its end on the expression music, then says so; music stop stops it */
    dgx_ge_pack_t pack;
    CHECK(dgx_ge_pack_open(&pack, file, psize));
    size_t         n;
    const uint8_t *anim_blob = dgx_ge_pack_blob(&pack, MU_BLOB_PLAYER, &n);
    dgx_ge_anim_t  anim;
    CHECK(anim_blob && dgx_ge_anim_open(&anim, anim_blob, n));
    said_mixer = &m, dgx_ge_expr_sound = &output;
    dgx_ge_actor_t a;
    dgx_ge_actor_start(&a, &anim, MU_PLAYER_SCRIPT_GO, 0, 0, 0);
    int heard = -2;
    for (uint32_t t = 0; t <= 400; t += 10) {
        dgx_ge_actor_advance(&a, t, say, NULL);
        if (t == 0) heard = dgx_ge_mixer_music_now(&m);
        dgx_ge_mixer_fill(&m, out, 80); /* 10 ms at 8000 */
    }
    /* the theme: four sixteenths at 240, 250 ms; the script sees it over at its next look */
    CHECK(heard == MU_MUSIC_THEME && signals == 1 && signal_at > 250 && signal_at <= 270 && dgx_ge_mixer_music_now(&m) == -1);
    dgx_ge_mixer_music(&m, MU_MUSIC_ROUND, 0);
    dgx_ge_actor_start(&a, &anim, MU_PLAYER_SCRIPT_QUIET, 0, 0, 0);
    dgx_ge_actor_advance(&a, 0, say, NULL);
    CHECK(dgx_ge_mixer_music_now(&m) == -1);
    /* a script's change by a fade and its level: the music out, the other in; the level half */
    dgx_ge_mixer_music(&m, MU_MUSIC_SQUARE, 0);
    dgx_ge_actor_start(&a, &anim, MU_PLAYER_SCRIPT_CHANGE, 0, 0, 0);
    dgx_ge_actor_advance(&a, 0, say, NULL);
    dgx_ge_mixer_fill(&m, out, 400);
    CHECK(dgx_ge_mixer_music_now(&m) == MU_MUSIC_SQUARE && abs(out[399]) > 3950 && abs(out[399]) < 4150);
    dgx_ge_mixer_fill(&m, out, 1200);
    CHECK(dgx_ge_mixer_music_now(&m) == MU_MUSIC_NOISE && abs(out[1199]) > 8100);
    dgx_ge_actor_start(&a, &anim, MU_PLAYER_SCRIPT_DIM, 0, 0, 0);
    dgx_ge_actor_advance(&a, 0, say, NULL);
    dgx_ge_mixer_fill(&m, out, 800);
    CHECK(abs(out[799]) > 4050 && abs(out[799]) < 4150);
    CHECK(!dgx_ge_anim_said_sound(&output, DGX_GE_ANIM_SIGNAL, 1)); /* not of sound: the game's */
    dgx_ge_expr_sound = NULL;
    dgx_ge_mixer_free(&m);
    CHECK(!m.music);

    /* sounds with no music need no state; a song that does not hold together: the blob refused */
    uint8_t *bad = malloc(ssize);
    memcpy(bad, blob, ssize);
    const uint32_t song = bad[12 + 12 + 0] | bad[12 + 12 + 1] << 8 | bad[12 + 12 + 2] << 16 | (uint32_t)bad[12 + 12 + 3] << 24;
    bad[song] ^= 1;
    CHECK(!dgx_ge_sounds_open(&sounds, bad, ssize));
    bad[song] ^= 1;
    bad[song + 6] = 9; /* nine channels */
    CHECK(!dgx_ge_sounds_open(&sounds, bad, ssize));
    CHECK(!dgx_ge_sounds_open(&sounds, blob, 12 + 12 + 8 * MU_MUSIC_COUNT - 1));
    free(bad), free(blob), free(file);
    CHECK_DONE();
}
