# The game built for the computer: its own main.c and files, the board host (host.c) in place of a real one. A
# Makefile of the game says what the game is and includes this file last:
#
#   GAME          := mygame                                  # the program's name
#   GAME_SRCS     := main/main.c main/world.c                # the game's C files, as main/CMakeLists.txt names them
#   GAME_INCLUDES := main                                    # its directories of headers
#   GAME_EMBED    := main/assets/mygame.pack                 # as EMBED_FILES: _binary_mygame_pack_start, _end
#   GAME_PACK_H   := main/assets/mygame_pack.h               # as dgx_ge_params_of_pack: DGX_GE_ANIM_PARAMS from it
#   GAME_PACK_MAKE   := python3 tools/mygame_assets.py       # what makes the pack and its header (optional), run when
#   GAME_PACK_INPUTS := tools/mygame_assets.py $(wildcard art/*.png anim/*.anim)  # one of these is newer than the header
#   GAME_FONTS    := main/assets/mygame.fonts                # as dgx_ge_fonts (optional), with GAME_FONT_RULES
#   GAME_DEFINES  := BOARD_SCALE=4                           # as target_compile_definitions
#   HOST_DISPLAY  := 1280x800                                # the board shown: its display in pixels
#   HOST_MM       := 217x136                                 #   and in millimetres (gestures, the pad's size)
#   DGX           := managed_components/jef-sure__dgx         # DGX 0.8.0 or later (its host/dgx_host.mk)
#   DGX_DI        := managed_components/jef-sure__dgx-di
#   include managed_components/jef-sure__dgx-ge/boards/host/host.mk
#
# Then: make (the program, build-host/<GAME>), make run, make clean. HOST_SANITIZE=1 builds with AddressSanitizer and
# UBSan. Paths are the Makefile's, relative to where make runs. Needs GNU make 4.3 or later (grouped targets, &:), a C compiler (make's cc,
# or CC=), SDL2 (sdl2-config), python3 for the fonts (and what DGX's dgx_fonts.py needs for a font made: FreeType,
# fontconfig), GNU ld for the embedded files (Linux; ld64 of macOS has no -b binary). Whatever goes into the compiler's
# command (the flags, the definitions, the pack's params, the display shown, which ld) is kept in <HOST_BUILD>/config: when it
# changes, everything is built again, so make HOST_DISPLAY=320x240 over a build of another display does what it says.

HOST_MAKEFILES := $(MAKEFILE_LIST) # the game's and this: a change of either builds again
# where this file and the engine are, found from here: override, so that a DGX_GE= of the command line cannot move them
override DGX_GE_HOST := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
override DGX_GE      := $(patsubst %/,%,$(dir $(patsubst %/,%,$(dir $(DGX_GE_HOST)))))

DGX     ?= $(firstword $(wildcard managed_components/jef-sure__dgx components/jef-sure__dgx))
DGX_DI  ?= $(firstword $(wildcard managed_components/jef-sure__dgx-di components/jef-sure__dgx-di))
ifeq ($(wildcard $(DGX)/host/dgx_host.mk),)
$(error DGX: no host/dgx_host.mk under '$(DGX)': DGX 0.8.0 or later, its directory as DGX=)
endif
ifeq ($(wildcard $(DGX_DI)/include/dgx_di.h),)
$(error DGX_DI: no DGX-DI under '$(DGX_DI)': its directory as DGX_DI=)
endif
include $(DGX)/host/dgx_host.mk

# a shell with ESP-IDF activated puts ESP's linker first in PATH: the computer's programs do not link with it
ifneq ($(findstring .espressif,$(shell command -v ld)),)
$(error ld is ESP-IDF's ($(shell command -v ld)): build for the computer in a shell without ESP-IDF activated)
endif

.DEFAULT_GOAL := all

GAME         ?= game
HOST_DISPLAY ?= 1280x800
HOST_MM      ?= 217x136
HOST_BUILD   ?= build-host
GAME_FONT_SCALE ?= $(or $(patsubst BOARD_SCALE=%,%,$(filter BOARD_SCALE=%,$(GAME_DEFINES))),1)

host_w = $(word 1,$(subst x, ,$(1)))
host_h = $(word 2,$(subst x, ,$(1)))

SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)
ifeq ($(SDL_LIBS),)
$(error SDL2: no sdl2-config (libsdl2-dev, or SDL2 of the package manager))
endif

# The pack made again, if the game says how, before its header is read below: what it says (the params an actor keeps)
# goes into the command of every file, so a rule of make, run after this file is read, would come too late (the first
# make would build with the old header's params and stop at its #error). Not for make clean.
ifneq ($(and $(GAME_PACK_MAKE),$(GAME_PACK_H),$(filter-out clean,$(or $(MAKECMDGOALS),all))),)
ifneq ($(shell [ -f $(GAME_PACK_H) ] && [ -z "$$(find $(GAME_PACK_INPUTS) -newer $(GAME_PACK_H) 2>/dev/null)" ] && echo fresh),fresh)
$(info the pack: $(GAME_PACK_MAKE))
ifneq ($(shell $(GAME_PACK_MAKE) 1>&2; echo $$?),0)
$(error the pack was not made: $(GAME_PACK_MAKE) failed (it says why above))
endif
endif
endif

# the params an actor keeps, as the pack's header says (dgx_ge_params_of_pack)
ifneq ($(GAME_PACK_H),)
GAME_PARAMS := $(shell sed -n 's/^\#define [A-Z0-9_]*_ANIM_PARAMS \([0-9]*\)$$/\1/p' $(GAME_PACK_H))
endif

HOST_SRCS := $(DGX_GE_HOST)/host.c $(DGX_GE_HOST)/host_nvs.c $(wildcard $(DGX_GE)/src/*.c) \
             $(addprefix $(DGX_DI)/src/, dgx_di_pointer.c dgx_di_calibration.c dgx_di_touch.c dgx_di_rects.c \
                                         dgx_di_pad.c dgx_di_pad_layouts.c dgx_di_keyboard.c dgx_di_keyboard_layouts.c)

# the game's fonts, by DGX from its requests, for the display shown at the game's scale (dgx_fonts_board, _scale)
ifneq ($(GAME_FONTS),)
HOST_FONTS := $(HOST_BUILD)/dgx_fonts
HOST_SRCS  += $(HOST_FONTS)/dgx_fonts.c
$(HOST_FONTS)/dgx_fonts.c $(HOST_FONTS)/dgx_fonts.h &: $(GAME_FONTS) $(GAME_FONT_RULES) $(HOST_BUILD)/config $(wildcard $(dir $(GAME_FONTS))*.letters $(dir $(GAME_FONTS))*.typed)
	python3 $(DGX)/font2c/dgx_fonts.py build $(GAME_FONTS) $(HOST_FONTS) --scale $(GAME_FONT_SCALE) \
	    --pixels $(call host_w,$(HOST_DISPLAY)) $(call host_h,$(HOST_DISPLAY)) --mm $(call host_w,$(HOST_MM)) $(call host_h,$(HOST_MM)) \
	    $(if $(GAME_FONT_RULES),--rules $(GAME_FONT_RULES))
endif

# the files the firmware embeds, as ESP-IDF does: their names give the symbols (pop.pack: _binary_pop_pack_start)
HOST_EMBED_OBJS := $(addprefix $(HOST_BUILD)/embed/, $(addsuffix .o, $(notdir $(GAME_EMBED))))
define host_embed
$(HOST_BUILD)/embed/$(notdir $(1)).o: $(1) $(HOST_BUILD)/config
	@mkdir -p $$(dir $$@)
	cd $$(dir $$<) && ld -r -b binary -z noexecstack -o $$(abspath $$@) $$(notdir $$<)
endef
$(foreach f,$(GAME_EMBED),$(eval $(call host_embed,$(f))))

HOST_CPPFLAGS := -I$(DGX_GE_HOST)/stubs $(addprefix -I,$(GAME_INCLUDES)) $(if $(HOST_FONTS),-I$(HOST_FONTS)) \
                 -I$(DGX_GE)/include -I$(DGX_GE)/boards -I$(DGX_DI)/include $(DGX_HOST_CPPFLAGS) -include $(DGX_GE_HOST)/stubs/esp_log_host.h $(SDL_CFLAGS) \
                 -DHOST_DISPLAY_W=$(call host_w,$(HOST_DISPLAY)) -DHOST_DISPLAY_H=$(call host_h,$(HOST_DISPLAY)) \
                 -DHOST_MM_W=$(call host_w,$(HOST_MM)) -DHOST_MM_H=$(call host_h,$(HOST_MM)) \
                 $(if $(GAME_PARAMS),-DDGX_GE_ANIM_PARAMS=$(GAME_PARAMS)) $(addprefix -D,$(GAME_DEFINES))
HOST_CFLAGS   := $(DGX_HOST_CFLAGS) -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-format
ifeq ($(HOST_SANITIZE),1)
HOST_CFLAGS += -O1 -fsanitize=address,undefined -fno-omit-frame-pointer
endif

# what goes into the compiler's command, kept: written only when it differs, and every object depends on it (a pack
# whose params grew, another display, another compiler: all of it built again, never half old)
HOST_CONFIG     := $(HOST_BUILD)/config
# (and which ld: in a shell with ESP-IDF activated it is ESP's, whose objects a plain shell's link cannot take)
HOST_CONFIG_NOW := $(strip $(CC) $(HOST_CPPFLAGS) $(HOST_CFLAGS) fonts:$(GAME_FONT_SCALE) ld:$(shell command -v ld))
ifneq ($(HOST_CONFIG_NOW),$(strip $(file <$(HOST_CONFIG))))
$(shell mkdir -p $(HOST_BUILD))
$(file >$(HOST_CONFIG),$(HOST_CONFIG_NOW))
endif

HOST_ALL_SRCS := $(GAME_SRCS) $(HOST_SRCS) $(DGX_HOST_SRCS)
HOST_OBJS     := $(patsubst %.c,$(HOST_BUILD)/obj/%.o,$(subst /,_,$(abspath $(HOST_ALL_SRCS))))
HOST_PROGRAM  := $(HOST_BUILD)/$(GAME)

all: $(HOST_PROGRAM)

# every C file an object of its own, named by its whole path (two of one name in different places do not meet)
define host_object
$(HOST_BUILD)/obj/$(subst /,_,$(abspath $(1:.c=))).o: $(1) $(if $(HOST_FONTS),$(HOST_FONTS)/dgx_fonts.h) $(HOST_MAKEFILES) $(HOST_CONFIG)
	@mkdir -p $$(dir $$@)
	$(CC) $(HOST_CPPFLAGS) $(HOST_CFLAGS) -MMD -MP -c $$< -o $$@
endef
$(foreach f,$(HOST_ALL_SRCS),$(eval $(call host_object,$(f))))
-include $(HOST_OBJS:.o=.d)

$(HOST_PROGRAM): $(HOST_OBJS) $(HOST_EMBED_OBJS)
	$(CC) $(HOST_CFLAGS) $^ $(SDL_LIBS) $(DGX_HOST_LDLIBS) -o $@

run: $(HOST_PROGRAM)
	$(abspath $(HOST_PROGRAM))

clean:
	rm -rf $(HOST_BUILD)

.PHONY: all run clean
