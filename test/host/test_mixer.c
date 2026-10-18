/*
 * The sounds of a pack and the mixer: 8 bits as given, IMA ADPCM as decoded apart from the engine (make_test_pack.py),
 * stereo as the middle and the side, a rate of its own (each sample held), a sound going round, voices summed and
 * kept within 16 bits, stop, the oldest voice giving way, the volume of all, pan, the game's output.
 */
#include <string.h>

#include "check.h"
#include "dgx_ge_mixer.h"
#include "dgx_ge_pack.h"
#include "test_pack.h"
#include "test_sounds.h"

static uint8_t file[4 << 20];

int main(int argc, char **argv)
{
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    CHECK(f);
    if (!f) return 1;
    const size_t n = fread(file, 1, sizeof(file), f);
    fclose(f);
    dgx_ge_pack_t pack;
    CHECK(dgx_ge_pack_open(&pack, file, n));
    size_t         size;
    const uint8_t *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_SOUNDS, &size);
    dgx_ge_sounds_t sounds;
    CHECK(blob && dgx_ge_sounds_open(&sounds, blob, size) && sounds.count == TEST_SOUND_COUNT);

    /* lengths and rates; a name only has none */
    uint16_t rate;
    CHECK(dgx_ge_sounds_length(&sounds, TEST_SOUND_STEP, &rate) == 0 && rate == 0);
    CHECK(dgx_ge_sounds_length(&sounds, TEST_SOUND_TONE, &rate) == 6 && rate == 8000);
    CHECK(dgx_ge_sounds_length(&sounds, TEST_SOUND_WIDE, &rate) == 64 && rate == 22050);
    CHECK(dgx_ge_sounds_length(&sounds, 99, NULL) == 0);

    /* 8 bits at the output's rate: the samples themselves, then silence and the voice free */
    dgx_ge_mixer_t m;
    int16_t        out[512];
    CHECK(dgx_ge_mixer_init(&m, &sounds, 4, 8000, 1));
    CHECK(dgx_ge_mixer_play(&m, TEST_SOUND_STEP, 255, 0) == -1); /* no samples: no voice */
    CHECK(dgx_ge_mixer_play(&m, TEST_SOUND_TONE, 255, 0) >= 0 && dgx_ge_mixer_busy(&m));
    dgx_ge_mixer_fill(&m, out, 8);
    CHECK(memcmp(out, tone_given, sizeof(tone_given)) == 0 && out[6] == 0 && out[7] == 0 && !dgx_ge_mixer_busy(&m));

    /* the output twice the sound's rate: each sample held for two */
    dgx_ge_mixer_free(&m);
    CHECK(dgx_ge_mixer_init(&m, &sounds, 4, 16000, 1));
    dgx_ge_mixer_play(&m, TEST_SOUND_TONE, 255, 0);
    dgx_ge_mixer_fill(&m, out, 12);
    bool held = true;
    for (int i = 0; i < 12; ++i) held &= out[i] == tone_given[i / 2];
    CHECK(held);
    dgx_ge_mixer_free(&m);

    /* ADPCM: as decoded apart from the engine, and near what was given */
    CHECK(dgx_ge_mixer_init(&m, &sounds, 4, 8000, 1));
    dgx_ge_mixer_play(&m, TEST_SOUND_BEEP, 255, 0);
    dgx_ge_mixer_fill(&m, out, 200);
    CHECK(memcmp(out, beep_decoded, sizeof(beep_decoded)) == 0);
    int worst = 0;
    for (int i = 0; i < 200; ++i) worst = abs(out[i] - beep_given[i]) > worst ? abs(out[i] - beep_given[i]) : worst;
    CHECK(worst < 600); /* a tone of 9000: within a few percent */

    /* going round: the four samples again and again, until stopped */
    dgx_ge_mixer_play(&m, TEST_SOUND_HUM, 255, 0);
    dgx_ge_mixer_fill(&m, out, 10);
    bool round = true;
    for (int i = 0; i < 10; ++i) round &= out[i] == hum_given[i % 4];
    CHECK(round && dgx_ge_mixer_busy(&m));
    dgx_ge_mixer_stop(&m, TEST_SOUND_HUM);
    dgx_ge_mixer_fill(&m, out, 2);
    CHECK(out[0] == 0 && !dgx_ge_mixer_busy(&m));

    /* two voices summed; past 16 bits kept at its end */
    dgx_ge_mixer_play(&m, TEST_SOUND_TONE, 255, 0);
    dgx_ge_mixer_play(&m, TEST_SOUND_HUM, 255, 0);
    dgx_ge_mixer_fill(&m, out, 5);
    CHECK(out[0] == 256 && out[1] == 512 + 256 && out[2] == 768 - 256 && out[3] == 32767 && out[4] == -32768 + 256);
    /* the same sound again: from its start, on its own voice */
    dgx_ge_mixer_stop(&m, -1);
    dgx_ge_mixer_play(&m, TEST_SOUND_TONE, 255, 0);
    dgx_ge_mixer_fill(&m, out, 2);
    CHECK(dgx_ge_mixer_play(&m, TEST_SOUND_TONE, 255, 0) == 0);
    dgx_ge_mixer_fill(&m, out, 2);
    CHECK(out[0] == 0 && out[1] == 256);
    /* the volume of all: silent, then half */
    dgx_ge_mixer_volume(&m, 0);
    dgx_ge_mixer_play(&m, TEST_SOUND_HUM, 255, 0);
    dgx_ge_mixer_fill(&m, out, 2);
    CHECK(out[0] == 0 && out[1] == 0);
    dgx_ge_mixer_volume(&m, 128);
    dgx_ge_mixer_stop(&m, -1);
    dgx_ge_mixer_play(&m, TEST_SOUND_HUM, 255, 0);
    dgx_ge_mixer_fill(&m, out, 1);
    CHECK(out[0] == 129); /* 255 is the sample itself: 128 a hair over half */
    dgx_ge_mixer_free(&m);

    /* no voice free: the oldest gives way */
    CHECK(dgx_ge_mixer_init(&m, &sounds, 2, 8000, 1));
    CHECK(dgx_ge_mixer_play(&m, TEST_SOUND_HUM, 255, 0) == 0 && dgx_ge_mixer_play(&m, TEST_SOUND_BEEP, 255, 0) == 1);
    CHECK(dgx_ge_mixer_play(&m, TEST_SOUND_TONE, 255, 0) == 0 && m.voices[0].sound == TEST_SOUND_TONE && m.voices[1].sound == TEST_SOUND_BEEP);
    dgx_ge_mixer_free(&m);

    /* stereo as the middle and the side: left and right as decoded apart; on a mono output made one */
    CHECK(dgx_ge_mixer_init(&m, &sounds, 4, 22050, 2));
    dgx_ge_mixer_play(&m, TEST_SOUND_WIDE, 255, 0);
    dgx_ge_mixer_fill(&m, out, 64);
    bool wide = true;
    for (int i = 0; i < 64; ++i) wide &= out[2 * i] == wide_decoded[i][0] && out[2 * i + 1] == wide_decoded[i][1];
    CHECK(wide);
    /* a mono sound on stereo by its pan: the middle both alike, the left side the right almost silent */
    dgx_ge_mixer_play(&m, TEST_SOUND_HUM, 255, 0);
    dgx_ge_mixer_fill(&m, out, 2);
    CHECK(out[0] == 256 && out[1] == 256);
    dgx_ge_mixer_stop(&m, -1);
    dgx_ge_mixer_play(&m, TEST_SOUND_HUM, 255, -127);
    dgx_ge_mixer_fill(&m, out, 1);
    CHECK(out[0] == 256 && out[1] == 1);
    dgx_ge_mixer_free(&m);
    CHECK(dgx_ge_mixer_init(&m, &sounds, 4, 22050, 1));
    dgx_ge_mixer_play(&m, TEST_SOUND_WIDE, 255, 0);
    dgx_ge_mixer_fill(&m, out, 64);
    bool one = true;
    for (int i = 0; i < 64; ++i) one &= out[i] == (int16_t)((wide_decoded[i][0] + wide_decoded[i][1]) / 2);
    CHECK(one);

    /* a sound made quieter in the pack (its gain): its samples so, nothing at play */
    dgx_ge_mixer_free(&m);
    CHECK(dgx_ge_mixer_init(&m, &sounds, 4, 8000, 1));
    dgx_ge_mixer_play(&m, TEST_SOUND_HUSH, 255, 0);
    dgx_ge_mixer_fill(&m, out, 4);
    CHECK(out[0] == 0 && out[1] == 256 && out[2] == 256 && out[3] == 512);

    /* the game's output: play and stop by number */
    const dgx_ge_sound_t game = dgx_ge_mixer_output(&m);
    dgx_ge_sound_stop(&game, -1);
    dgx_ge_sound_play(&game, TEST_SOUND_HUM);
    CHECK(dgx_ge_mixer_busy(&m));
    dgx_ge_sound_stop(&game, TEST_SOUND_HUM);
    CHECK(!dgx_ge_mixer_busy(&m));
    dgx_ge_mixer_free(&m);

    /* not a blob of sounds, or one cut short: refused */
    uint8_t bad[64];
    memcpy(bad, blob, sizeof(bad));
    CHECK(!dgx_ge_sounds_open(&sounds, bad, 8 + 12 * 2));
    bad[0] ^= 1;
    CHECK(!dgx_ge_sounds_open(&sounds, bad, sizeof(bad)));
    CHECK(!dgx_ge_sounds_open(&sounds, blob, size - 1) || size == 0);
    CHECK_DONE();
}
