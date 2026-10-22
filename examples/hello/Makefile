# hello on the computer: main/ as it is, the engine's board host (SDL2) in place of a board. The display shown is
# the 10"'s (1280x800, 217x136 mm) unless HOST_DISPLAY and HOST_MM say another: make HOST_DISPLAY=320x240 HOST_MM=58x43
# shows it as the CYD does. make, then make run (or ./build-host/hello); make clean.
#
# DGX and DGX-DI: the ones ESP-IDF downloaded into managed_components/ (after an idf.py build of this example), or
# checkouts of them: make DGX=<dgx> DGX_DI=<dgx-di>. The engine: the one this example lies in, or the downloaded one.
GAME          := hello
GAME_SRCS     := main/main.c
GAME_INCLUDES := main
GAME_EMBED    := main/assets/hello.pack
GAME_PACK_H   := main/assets/hello_pack.h
# the engine's build for the computer: the engine this example lies in, or the one ESP-IDF downloaded; HOST_MK=<path>
HOST_MK       ?= $(firstword $(wildcard ../../boards/host/host.mk managed_components/jef-sure__dgx-ge/boards/host/host.mk))
ifeq ($(wildcard $(HOST_MK)),)
$(error no host.mk of the engine: an idf.py build of this example downloads it into managed_components/, or HOST_MK=<engine>/boards/host/host.mk)
endif

# the pack again when its art or its script changes (host.mk runs it before it reads the pack's header)
GAME_PACK_MAKE   := python3 tools/hello_assets.py
GAME_PACK_INPUTS := tools/hello_assets.py anim/bot.anim $(wildcard art/*.png)

include $(HOST_MK)
