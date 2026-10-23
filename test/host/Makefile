# Host tests of the core of DGX-GE, the part that is plain C.
# Run: make -C components/dgx-ge/test/host
#
# DGX is taken from the managed component of the project the engine sits in, else from a checkout next to the
# engine's (../esp32-component-dgx); DGX=<path> takes another copy.

ROOT    := ../..
DGX     ?= $(firstword $(wildcard $(ROOT)/../../managed_components/jef-sure__dgx) $(ROOT)/../esp32-component-dgx)
CC      ?= gcc
CFLAGS  ?= -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
PYTHON  ?= python3
BUILD   := build

TESTS := test_pack test_scene test_frame test_anim test_mask test_tiles test_world test_save test_widgets test_shots test_pockets test_cells test_graph test_timers test_show test_chase test_walk test_walk_bind test_dialog test_text test_label test_sound test_mixer test_music test_music_chip test_synth test_synth_chip test_synth_default test_talk_view test_crowd test_input test_layout test_host_nvs test_hits test_now test_now_2 test_glue test_board test_board_mute test_game_vars test_orient

# what of DGX the engine stands on, as DGX gives it for a computer (DGX 0.8: host/dgx_host.mk, its sources by group, the
# stubs of ESP-IDF); its fonts each test names
include $(DGX)/host/dgx_host.mk
DGX_SRCS := $(DGX_HOST_SRCS_SCREEN) $(DGX_HOST_SRCS_DRAW) $(DGX_HOST_SRCS_VSCREEN) $(DGX_HOST_SRCS_TEXT) $(DGX_HOST_SRCS_MORPH) $(DGX_HOST_SRCS_FB)
DGX_FLAGS := $(DGX_HOST_CPPFLAGS) -Wno-format
CPPFLAGS += -I$(ROOT)/include -I$(BUILD) $(DGX_FLAGS)
# the params an actor keeps, as the test pack needs them (TEST_ANIM_PARAMS; its header stops a build of fewer), as a
# game's build takes them from its pack (dgx_ge_params_of_pack)
CPPFLAGS += -DDGX_GE_ANIM_PARAMS=8
# the events at once an actor's own say may give in one step, as a game's build says them (dgx_ge_now_events): the
# engine's default said aloud; test_now_2 is built with 2
CPPFLAGS += -DDGX_GE_ANIM_NOW_EVENTS=4
# the synthesizers of music, as a game's build says them (dgx_ge_synths): all three; test_music_chip and
# test_synth_chip are built with the chip alone (dgx_ge_synths(chip)), test_synth_default with none said (FM alone)
SYNTHS   := -DDGX_GE_SYNTH_CHIP=1 -DDGX_GE_SYNTH_NES=1 -DDGX_GE_SYNTH_FM=1
CPPFLAGS += $(SYNTHS)
NONE_CPPFLAGS = $(filter-out $(SYNTHS),$(CPPFLAGS))
CHIP_CPPFLAGS = $(NONE_CPPFLAGS) -DDGX_GE_SYNTH_CHIP=1
# the display input DGX-DI, as DGX is found: the managed component of the project the engine sits in, a component
# beside the engine (named as ESP-IDF names it, or not), or a checkout next to the engine's; the join of its touch
# with the input (dgx_ge_input_di.h) is tested then, without it that test is skipped
DI      ?= $(firstword $(wildcard $(ROOT)/../../managed_components/jef-sure__dgx-di) $(wildcard $(ROOT)/../jef-sure__dgx-di) $(wildcard $(ROOT)/../dgx-di) $(wildcard $(ROOT)/../esp32-component-dgx-di))
DI_SRCS := $(if $(DI),$(DI)/src/dgx_di_touch.c)
DI_FLAGS := $(if $(DI),-I$(DI)/include)
ENGINE_SRCS := $(addprefix $(ROOT)/src/, dgx_ge_image.c dgx_ge_frame.c dgx_ge_scene.c)
HEADERS := $(wildcard $(ROOT)/include/*.h) $(wildcard $(ROOT)/src/*.h) check.h

all: $(addprefix run-, $(TESTS)) run-rules run-sprites run-scenes run-explain run-dialog-rules run-text-layout run-pack-rules run-palettes run-music-rules run-now-rules run-game-vars run-orient-rules

run-rules:
	$(PYTHON) -I test_anim_rules.py

run-test_pack: $(BUILD)/test_pack
	./$< $(BUILD)/test.pack

run-test_anim: $(BUILD)/test_anim
	./$< $(BUILD)/test.pack

run-test_shots: $(BUILD)/test_shots $(BUILD)/test.pack
	./$(BUILD)/test_shots $(BUILD)/test.pack

run-test_pockets: $(BUILD)/test_pockets $(BUILD)/test.pack
	./$(BUILD)/test_pockets $(BUILD)/test.pack

run-test_cells: $(BUILD)/test_cells $(BUILD)/test.pack
	./$(BUILD)/test_cells $(BUILD)/test.pack

run-test_graph: $(BUILD)/test_graph $(BUILD)/test.pack
	./$(BUILD)/test_graph $(BUILD)/test.pack

run-test_timers: $(BUILD)/test_timers $(BUILD)/test.pack
	./$(BUILD)/test_timers $(BUILD)/test.pack

run-test_show: $(BUILD)/test_show $(BUILD)/test.pack
	./$(BUILD)/test_show $(BUILD)/test.pack

run-test_crowd: $(BUILD)/test_crowd $(BUILD)/test.pack
	./$(BUILD)/test_crowd $(BUILD)/test.pack

run-test_orient: $(BUILD)/test_orient $(BUILD)/orient/orient.pack
	./$(BUILD)/test_orient $(BUILD)/orient/orient.pack

run-test_hits: $(BUILD)/test_hits $(BUILD)/test.pack
	./$(BUILD)/test_hits $(BUILD)/test.pack

run-test_now: $(BUILD)/test_now $(BUILD)/test.pack
	./$(BUILD)/test_now $(BUILD)/test.pack

run-test_now_2: $(BUILD)/test_now_2 $(BUILD)/test.pack
	./$(BUILD)/test_now_2 $(BUILD)/test.pack

run-test_layout: $(BUILD)/test_layout $(BUILD)/layout/layout.pack
	./$(BUILD)/test_layout $(BUILD)/layout/layout.pack

run-test_chase: $(BUILD)/test_chase $(BUILD)/test.pack
	./$(BUILD)/test_chase $(BUILD)/test.pack

run-test_walk: $(BUILD)/test_walk
	./$(BUILD)/test_walk

run-test_walk_bind: $(BUILD)/test_walk_bind $(BUILD)/test.pack
	./$(BUILD)/test_walk_bind $(BUILD)/test.pack

run-test_dialog: $(BUILD)/test_dialog $(BUILD)/test.pack
	./$(BUILD)/test_dialog $(BUILD)/test.pack

run-test_game_vars: $(BUILD)/test_game_vars $(BUILD)/test.pack
	./$(BUILD)/test_game_vars $(BUILD)/test.pack

run-test_text: $(BUILD)/test_text
	./$(BUILD)/test_text

run-test_label: $(BUILD)/test_label
	./$(BUILD)/test_label

run-test_input: $(BUILD)/test_input
	./$(BUILD)/test_input

run-test_widgets: $(BUILD)/test_widgets
	./$(BUILD)/test_widgets

run-test_sound: $(BUILD)/test_sound
	./$(BUILD)/test_sound

run-test_mixer: $(BUILD)/test_mixer
	./$< $(BUILD)/test.pack

run-test_music: $(BUILD)/test_music
	./$< $(BUILD)/music

run-test_music_chip: $(BUILD)/test_music_chip
	./$< $(BUILD)/music

run-test_synth: $(BUILD)/test_synth
	./$< $(BUILD)/synth

run-test_synth_chip: $(BUILD)/test_synth_chip
	./$< $(BUILD)/synth

run-test_talk_view: $(BUILD)/test_talk_view $(BUILD)/test.pack
	./$(BUILD)/test_talk_view $(BUILD)/test.pack

run-test_world: $(BUILD)/test_world $(BUILD)/test.pack
	./$(BUILD)/test_world $(BUILD)/test.pack

run-test_save: $(BUILD)/test_save $(BUILD)/test.pack
	./$(BUILD)/test_save $(BUILD)/test.pack

run-test_mask: $(BUILD)/test_mask $(BUILD)/test.pack
	./$(BUILD)/test_mask

run-test_tiles: $(BUILD)/test_tiles $(BUILD)/test.pack
	./$(BUILD)/test_tiles

run-test_glue: $(BUILD)/test_glue $(BUILD)/test.pack
	./$(BUILD)/test_glue $(BUILD)/test.pack

run-test_board: $(BUILD)/test_board $(BUILD)/test.pack
	./$(BUILD)/test_board $(BUILD)/test.pack

run-test_board_mute: $(BUILD)/test_board_mute $(BUILD)/test.pack
	./$(BUILD)/test_board_mute $(BUILD)/test.pack

run-%: $(BUILD)/%
	./$<

$(BUILD)/test.pack $(BUILD)/test_pack.h $(BUILD)/test_sounds.h &: make_test_pack.py test.po $(ROOT)/tools/dgx_ge_pack.py $(ROOT)/tools/dgx_ge_anim.py $(ROOT)/tools/dgx_ge_dialog.py | $(BUILD)
	$(PYTHON) -I make_test_pack.py $(BUILD)/test.pack $(BUILD)/test_pack.h

$(BUILD)/test_pack: test_pack.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_pack.c $(ROOT)/src/dgx_ge_pack.c -o $@

# what every game wrote alike between its pack and the screen: the frame for the display, the colors of the sets, actors as items
$(BUILD)/test_glue: test_glue.c $(ROOT)/src/dgx_ge_glue.c $(ROOT)/src/dgx_ge_pack.c $(ENGINE_SRCS) $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_glue.c $(ROOT)/src/dgx_ge_glue.c $(ROOT)/src/dgx_ge_pack.c $(ENGINE_SRCS) $(DGX_SRCS) -lm -o $@

# the header of the boards, on a board of the test's own (its sound, its clock, its pause): the board host's stubs before
# DGX's, as a game's build for the computer has them; once with sound, once without
BOARD_TEST_DEPS := test_board.c $(ROOT)/boards/dgx_ge_board.h $(ROOT)/src/dgx_ge_mixer.c $(ROOT)/src/dgx_ge_music.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS)
$(BUILD)/test_board: $(BOARD_TEST_DEPS) | $(BUILD)
	$(CC) -I$(ROOT)/boards/host/stubs -I$(ROOT)/boards $(CPPFLAGS) $(CFLAGS) test_board.c $(ROOT)/src/dgx_ge_mixer.c $(ROOT)/src/dgx_ge_music.c $(ROOT)/src/dgx_ge_pack.c -o $@
$(BUILD)/test_board_mute: $(BOARD_TEST_DEPS) | $(BUILD)
	$(CC) -DBOARD_MUTE -I$(ROOT)/boards/host/stubs -I$(ROOT)/boards $(CPPFLAGS) $(CFLAGS) test_board.c $(ROOT)/src/dgx_ge_mixer.c $(ROOT)/src/dgx_ge_music.c $(ROOT)/src/dgx_ge_pack.c -o $@

# NVS on the computer, the board host's (boards/host): its stubs before DGX's, its file store
$(BUILD)/test_host_nvs: test_host_nvs.c $(ROOT)/boards/host/host_nvs.c $(ROOT)/boards/host/stubs/nvs.h $(ROOT)/boards/host/stubs/nvs_flash.h check.h | $(BUILD)
	$(CC) -I$(ROOT)/boards/host/stubs $(CPPFLAGS) $(CFLAGS) test_host_nvs.c $(ROOT)/boards/host/host_nvs.c -o $@

$(BUILD)/test_scene: test_scene.c $(ENGINE_SRCS) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_scene.c $(ENGINE_SRCS) $(DGX_SRCS) -lm -o $@

$(BUILD)/test_frame: test_frame.c $(ENGINE_SRCS) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_frame.c $(ENGINE_SRCS) $(DGX_SRCS) -lm -o $@

# the scripts: what draws from pockets and lays into the world goes with them
ANIM_SRCS := $(ROOT)/src/dgx_ge_anim.c $(ROOT)/src/dgx_ge_mask.c $(ROOT)/src/dgx_ge_pockets.c $(ROOT)/src/dgx_ge_world.c $(ROOT)/src/dgx_ge_cells.c $(ROOT)/src/dgx_ge_links.c $(ROOT)/src/dgx_ge_graph.c $(ROOT)/src/dgx_ge_game.c $(ROOT)/src/dgx_ge_timers.c $(ROOT)/src/dgx_ge_save.c

$(BUILD)/test_tiles: test_tiles.c $(ENGINE_SRCS) $(ROOT)/src/dgx_ge_tiles.c $(ROOT)/src/dgx_ge_pack.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_tiles.c $(ENGINE_SRCS) $(ROOT)/src/dgx_ge_tiles.c $(ROOT)/src/dgx_ge_pack.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(DGX_SRCS) -lm -o $@

$(BUILD)/test_mask: test_mask.c $(ROOT)/src/dgx_ge_mask.c $(ROOT)/src/dgx_ge_pack.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_mask.c $(ROOT)/src/dgx_ge_mask.c $(ROOT)/src/dgx_ge_pack.c -o $@

$(BUILD)/test_anim: test_anim.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_anim.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

$(BUILD)/test_hits: test_hits.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_hits.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

# events at once from an actor's own say: with the queue the Makefile says, and with one of 2
$(BUILD)/test_now: test_now.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_now.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@
$(BUILD)/test_now_2: test_now.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) -UDGX_GE_ANIM_NOW_EVENTS -DDGX_GE_ANIM_NOW_EVENTS=2 $(CFLAGS) test_now.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

# a sheet of the pictures of a pack, to look at: build/pack_sheet <pack> <first> <count> <set> <out.ppm>
$(BUILD)/pack_sheet: pack_sheet.c $(ROOT)/src/dgx_ge_pack.c $(ROOT)/src/dgx_ge_image.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) pack_sheet.c $(ROOT)/src/dgx_ge_pack.c $(ROOT)/src/dgx_ge_image.c $(DGX_SRCS) -lm -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean run-rules run-explain run-dialog-rules run-text-layout run-pack-rules run-music-rules run-now-rules run-game-vars run-palettes run-orient-rules

# dgx_ge_now_events of project_include.cmake, by cmake -P
run-now-rules:
	$(PYTHON) -I test_now_events.py

run-pack-rules: | $(BUILD)
	CC=$(CC) $(PYTHON) -I test_pack_rules.py

# a set of a ready palette (tools/palettes/, a document's palettes): the packer brings the pictures' colors to it
run-palettes: | $(BUILD)
	$(PYTHON) -I test_palettes.py

run-text-layout: $(BUILD)/text_lay
	DGX=$(DGX) $(PYTHON) -I test_text_layout.py $(BUILD)/text_lay

run-dialog-rules:
	$(PYTHON) -I test_dialog_rules.py

# the game's variables in the tools: the packer, scripts, dialogs, the explainer
run-game-vars: | $(BUILD)
	$(PYTHON) -I test_game_vars.py

run-explain:
	$(PYTHON) -I test_explain.py
# an actor's orientation in the tools: rotate and rotation of scripts, a dialog's condition, the explainer, the packer
run-orient-rules:
	$(PYTHON) -I test_orient_rules.py

run-sprites: | $(BUILD)
	$(PYTHON) -I test_sprites.py

$(BUILD)/test_world: test_world.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_world.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

$(BUILD)/test_save: test_save.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_save.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

$(BUILD)/test_shots: test_shots.c $(ROOT)/src/dgx_ge_shots.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_shots.c $(ROOT)/src/dgx_ge_shots.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

$(BUILD)/test_pockets: test_pockets.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_pockets.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

$(BUILD)/test_cells: test_cells.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_cells.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

$(BUILD)/test_graph: test_graph.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_graph.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

$(BUILD)/test_timers: test_timers.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_timers.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

# the scenes of the editor's file: the packer's checks (test_scenes.py), and the pack the engine's test opens
$(BUILD)/layout/layout.pack $(BUILD)/layout/layout_pack.h &: test_scenes.py $(ROOT)/tools/dgx_ge_pack.py $(ROOT)/tools/dgx_ge_graph.py $(ROOT)/tools/dgx_ge_anim.py | $(BUILD)
	$(PYTHON) -I test_scenes.py

run-scenes: $(BUILD)/layout/layout.pack

$(BUILD)/test_layout: test_layout.c $(ROOT)/src/dgx_ge_layout.c $(ROOT)/src/dgx_ge_tiles.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/layout/layout_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) -I$(BUILD)/layout $(CFLAGS) test_layout.c $(ROOT)/src/dgx_ge_layout.c $(ROOT)/src/dgx_ge_tiles.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(ENGINE_SRCS) $(DGX_SRCS) -lm -o $@

$(BUILD)/test_crowd: test_crowd.c $(ROOT)/src/dgx_ge_crowd.c $(ROOT)/src/dgx_ge_tiles.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_crowd.c $(ROOT)/src/dgx_ge_crowd.c $(ROOT)/src/dgx_ge_tiles.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(ENGINE_SRCS) $(DGX_SRCS) -lm -o $@

# an actor's orientation, its mirror and its quarter turn: drawn, its masks, its scripts, what keeps it (a pack of its own)
$(BUILD)/orient/orient.pack $(BUILD)/orient/orient_pack.h &: make_orient_pack.py $(ROOT)/tools/dgx_ge_pack.py $(ROOT)/tools/dgx_ge_anim.py | $(BUILD)
	mkdir -p $(BUILD)/orient
	$(PYTHON) -I make_orient_pack.py $(BUILD)/orient/orient.pack $(BUILD)/orient/orient_pack.h
ORIENT_SRCS := $(ROOT)/src/dgx_ge_crowd.c $(ROOT)/src/dgx_ge_tiles.c $(ROOT)/src/dgx_ge_show.c $(ROOT)/src/dgx_ge_glue.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(ENGINE_SRCS)
$(BUILD)/test_orient: test_orient.c $(ORIENT_SRCS) $(BUILD)/orient/orient_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) -I$(BUILD)/orient $(CFLAGS) test_orient.c $(ORIENT_SRCS) $(DGX_SRCS) -lm -o $@

$(BUILD)/test_show: test_show.c $(ROOT)/src/dgx_ge_show.c $(ROOT)/src/dgx_ge_glue.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_show.c $(ROOT)/src/dgx_ge_show.c $(ROOT)/src/dgx_ge_glue.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(ENGINE_SRCS) $(DGX_SRCS) -lm -o $@

$(BUILD)/test_chase: test_chase.c $(ROOT)/src/dgx_ge_chase.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_chase.c $(ROOT)/src/dgx_ge_chase.c $(ROOT)/src/dgx_ge_pack.c $(ENGINE_SRCS) $(DGX_SRCS) -lm -o $@

# the walker moves an actor bound to it: the scripts go with it
$(BUILD)/test_walk: test_walk.c $(ROOT)/src/dgx_ge_walk.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_walk.c $(ROOT)/src/dgx_ge_walk.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

$(BUILD)/test_walk_bind: test_walk_bind.c $(ROOT)/src/dgx_ge_walk.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_walk_bind.c $(ROOT)/src/dgx_ge_walk.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

$(BUILD)/test_dialog: test_dialog.c $(ROOT)/src/dgx_ge_dialog.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_dialog.c $(ROOT)/src/dgx_ge_dialog.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

# the game's variables: the store, scripts, dialogs, saving
$(BUILD)/test_game_vars: test_game_vars.c $(ROOT)/src/dgx_ge_dialog.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_game_vars.c $(ROOT)/src/dgx_ge_dialog.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

$(BUILD)/test_sound: test_sound.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_sound.c -o $@

$(BUILD)/test_mixer: test_mixer.c $(ROOT)/src/dgx_ge_mixer.c $(ROOT)/src/dgx_ge_music.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(BUILD)/test_sounds.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_mixer.c $(ROOT)/src/dgx_ge_mixer.c $(ROOT)/src/dgx_ge_music.c $(ROOT)/src/dgx_ge_pack.c -o $@ -pthread

# music by notes: its pack (the songs with the sounds, apart), the mixer playing them, a script waiting on one
$(BUILD)/music/music.pack $(BUILD)/music/music_pack.h $(BUILD)/music/music.sounds &: make_music_pack.py $(ROOT)/tools/dgx_ge_pack.py $(ROOT)/tools/dgx_ge_music.py $(ROOT)/tools/dgx_ge_anim.py | $(BUILD)
	$(PYTHON) -I make_music_pack.py $(BUILD)/music

$(BUILD)/test_music: test_music.c $(ROOT)/src/dgx_ge_mixer.c $(ROOT)/src/dgx_ge_music.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/music/music_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) -I$(BUILD)/music $(CFLAGS) test_music.c $(ROOT)/src/dgx_ge_mixer.c $(ROOT)/src/dgx_ge_music.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

$(BUILD)/test_music_chip: test_music.c $(ROOT)/src/dgx_ge_mixer.c $(ROOT)/src/dgx_ge_music.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/music/music_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CHIP_CPPFLAGS) -I$(BUILD)/music $(CFLAGS) test_music.c $(ROOT)/src/dgx_ge_mixer.c $(ROOT)/src/dgx_ge_music.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

# the synthesizers: songs of each, the selection, a song alone; with the chip alone, a song of FM silent
$(BUILD)/synth/synth.pack $(BUILD)/synth/synth_pack.h $(BUILD)/synth/synth.sounds &: make_synth_pack.py $(ROOT)/tools/dgx_ge_pack.py $(ROOT)/tools/dgx_ge_music.py $(wildcard $(ROOT)/tools/music_sets/*.mml) | $(BUILD)
	$(PYTHON) -I make_synth_pack.py $(BUILD)/synth

SYNTH_DEPS := test_synth.c $(ROOT)/src/dgx_ge_mixer.c $(ROOT)/src/dgx_ge_music.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/synth/synth_pack.h $(HEADERS)
$(BUILD)/test_synth: $(SYNTH_DEPS) | $(BUILD)
	$(CC) $(CPPFLAGS) -I$(BUILD)/synth $(CFLAGS) test_synth.c $(ROOT)/src/dgx_ge_mixer.c $(ROOT)/src/dgx_ge_music.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

$(BUILD)/test_synth_chip: $(SYNTH_DEPS) | $(BUILD)
	$(CC) $(CHIP_CPPFLAGS) -I$(BUILD)/synth $(CFLAGS) test_synth.c $(ROOT)/src/dgx_ge_mixer.c $(ROOT)/src/dgx_ge_music.c $(ROOT)/src/dgx_ge_pack.c -lm -o $@

run-music-rules:
	$(PYTHON) -I test_music.py
	$(PYTHON) -I test_synths.py

$(BUILD)/test_input: test_input.c $(ROOT)/src/dgx_ge_input.c $(DI_SRCS) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(DI_FLAGS) $(CFLAGS) test_input.c $(ROOT)/src/dgx_ge_input.c $(DI_SRCS) -lm -o $@

$(BUILD)/test_label: test_label.c $(ROOT)/src/dgx_ge_label.c $(ROOT)/src/dgx_ge_text.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_label.c $(ROOT)/src/dgx_ge_label.c $(ROOT)/src/dgx_ge_text.c $(ENGINE_SRCS) $(DGX_SRCS) $(DGX)/src/fonts/TerminusTTFMedium12.c -lm -o $@

$(BUILD)/test_widgets: test_widgets.c $(ROOT)/src/dgx_ge_widgets.c $(ROOT)/src/dgx_ge_text.c $(ROOT)/src/dgx_ge_input.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_widgets.c $(ROOT)/src/dgx_ge_widgets.c $(ROOT)/src/dgx_ge_text.c $(ROOT)/src/dgx_ge_input.c $(ENGINE_SRCS) $(DGX_SRCS) $(DGX)/src/fonts/TerminusTTFMedium12.c -lm -o $@

$(BUILD)/test_text: test_text.c $(ROOT)/src/dgx_ge_text.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_text.c $(ROOT)/src/dgx_ge_text.c $(DGX_SRCS) $(DGX)/src/fonts/TerminusTTFMedium12.c $(DGX)/src/fonts/font0ss.c -lm -o $@

$(BUILD)/text_lay: text_lay.c $(ROOT)/src/dgx_ge_text.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) text_lay.c $(ROOT)/src/dgx_ge_text.c $(DGX_SRCS) $(DGX)/src/fonts/TerminusTTFMedium12.c $(DGX)/src/fonts/DroidSansRegular9.c -lm -o $@

$(BUILD)/test_talk_view: test_talk_view.c $(ROOT)/src/dgx_ge_widgets.c $(ROOT)/src/dgx_ge_input.c $(ROOT)/src/dgx_ge_talk_view.c $(ROOT)/src/dgx_ge_text.c $(ROOT)/src/dgx_ge_dialog.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(BUILD)/test_pack.h $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_talk_view.c $(ROOT)/src/dgx_ge_widgets.c $(ROOT)/src/dgx_ge_input.c $(ROOT)/src/dgx_ge_talk_view.c $(ROOT)/src/dgx_ge_text.c $(ROOT)/src/dgx_ge_dialog.c $(ANIM_SRCS) $(ROOT)/src/dgx_ge_expr.c $(ROOT)/src/dgx_ge_pack.c $(ENGINE_SRCS) $(DGX_SRCS) $(DGX)/src/fonts/TerminusTTFMedium12.c $(DGX)/src/fonts/font0ss.c -lm -o $@

# none said: FM alone, as a build of a game that does not call dgx_ge_synths
$(BUILD)/test_synth_default: test_synth_default.c $(ROOT)/include/dgx_ge_synth.h | $(BUILD)
	$(CC) $(NONE_CPPFLAGS) $(CFLAGS) test_synth_default.c -o $@
