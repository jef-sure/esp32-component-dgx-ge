/*
 * A song of the engine (DGMU: tools/dgx_ge_music.py song.mml song.dgmu) into a WAV file, by the engine's own
 * synthesizers, the same samples the mixer plays (dgx_ge_synth.h):
 *
 *     dgx_ge_music_wav song.dgmu song.wav [rate] [seconds]
 *
 * The rate 22050 unless said; a song that goes round is cut at the seconds (60 unless said), one that ends at its end.
 */
#include <stdio.h>
#include <stdlib.h>

#include "dgx_ge_synth.h"

static void u32(FILE *f, uint32_t v)
{
    const uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, f);
}

static void u16(FILE *f, uint16_t v)
{
    const uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    fwrite(b, 1, 2, f);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "dgx_ge_music_wav song.dgmu song.wav [rate] [seconds]\n");
        return 2;
    }
    const uint32_t rate = argc > 3 ? (uint32_t)atoi(argv[3]) : 22050, seconds = argc > 4 ? (uint32_t)atoi(argv[4]) : 60;
    FILE *in = fopen(argv[1], "rb");
    if (!in) {
        perror(argv[1]);
        return 1;
    }
    fseek(in, 0, SEEK_END);
    const long size = ftell(in);
    fseek(in, 0, SEEK_SET);
    uint8_t *song = malloc((size_t)size);
    if (!song || fread(song, 1, (size_t)size, in) != (size_t)size) return 1;
    fclose(in);
    const int wrong = dgx_ge_song_check(song, (size_t)size);
    if (wrong) {
        fprintf(stderr, "%s: %s\n", argv[1], wrong == 2 ? "its synthesizer is not in this build" : "no song of the engine");
        return 1;
    }
    const size_t most = (size_t)rate * seconds;
    int16_t     *out  = malloc(most * sizeof(int16_t));
    const size_t n    = out ? dgx_ge_music_render(song, (size_t)size, rate, out, most) : 0;
    FILE        *f    = fopen(argv[2], "wb");
    if (!f) {
        perror(argv[2]);
        return 1;
    }
    fwrite("RIFF", 1, 4, f), u32(f, (uint32_t)(36 + 2 * n)), fwrite("WAVEfmt ", 1, 8, f);
    u32(f, 16), u16(f, 1), u16(f, 1), u32(f, rate), u32(f, 2 * rate), u16(f, 2), u16(f, 16);
    fwrite("data", 1, 4, f), u32(f, (uint32_t)(2 * n));
    for (size_t i = 0; i < n; ++i) u16(f, (uint16_t)out[i]);
    fclose(f);
    printf("%s: %zu samples, %.2f s\n", argv[2], n, (double)n / rate);
    free(out), free(song);
    return 0;
}
