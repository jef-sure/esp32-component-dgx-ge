# The engine's synthesizers of music on their own: the song player (src/dgx_ge_music.c, include/dgx_ge_synth.h) and
# nothing else of the engine, no mixer, no pack, no board. Every synthesizer in.
#
#   make            build/dgx_ge_music_wav: a song (DGMU) into a WAV file, the samples the mixer plays
#   make wasm       build/dgx_ge_synth.wasm: the same C for a browser (the editor), by Emscripten's emcc
#   make clean
#
# emcc is not a package of the system here: Emscripten's SDK (https://emscripten.org/docs/getting_started/), then
#   git clone https://github.com/emscripten-core/emsdk && cd emsdk && ./emsdk install latest && ./emsdk activate latest
#   . ./emsdk_env.sh            (in the shell that runs make wasm)

ROOT    := ../..
BUILD   := build
CC      ?= cc
EMCC    ?= emcc
CFLAGS  ?= -O2
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-unused-parameter
SYNTHS  := -DDGX_GE_MUSIC_ALONE -DDGX_GE_SYNTH_CHIP=1 -DDGX_GE_SYNTH_NES=1 -DDGX_GE_SYNTH_FM=1
SRC     := $(ROOT)/src/dgx_ge_music.c
DEPS    := $(SRC) $(ROOT)/src/dgx_ge_music.h $(ROOT)/include/dgx_ge_synth.h $(ROOT)/include/dgx_ge_mixer.h $(ROOT)/include/dgx_ge_sound.h
# what JavaScript calls: the player, its check, the one call; malloc and free for the song's bytes and the samples
EXPORTS := _dgx_ge_song_check,_dgx_ge_song_player_new,_dgx_ge_song_render,_dgx_ge_song_player_free,_dgx_ge_music_render,_malloc,_free

all: $(BUILD)/dgx_ge_music_wav

$(BUILD):
	mkdir -p $@

$(BUILD)/dgx_ge_music_wav: dgx_ge_music_wav.c $(DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(SYNTHS) -I$(ROOT)/include dgx_ge_music_wav.c $(SRC) -o $@

# a module of WebAssembly alone (no JavaScript of Emscripten's), a reactor (--no-entry: _initialize, if it exports it,
# once before the rest); its memory 16 MB and no more, so a view of it made once stays good (a minute of 48 kHz mono
# is 5.6 MB); what it imports (of WASI, for libc's sake: never called by this C) may be given as empty functions
wasm: $(BUILD)/dgx_ge_synth.wasm

$(BUILD)/dgx_ge_synth.wasm: $(DEPS) | $(BUILD)
	@command -v $(EMCC) >/dev/null || { echo "make wasm: no $(EMCC): Emscripten's SDK first (this Makefile says how)"; exit 1; }
	$(EMCC) -O2 $(SYNTHS) -I$(ROOT)/include $(SRC) --no-entry -sSTANDALONE_WASM -sEXPORTED_FUNCTIONS=$(EXPORTS) \
		-sINITIAL_MEMORY=16777216 -sALLOW_MEMORY_GROWTH=0 -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all wasm clean
