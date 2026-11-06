# The touch of the display as the game's (src/dgx_ge_di.c, dgx_ge_input_di.h: the board's pointer of DGX-DI, the pad
# and the keyboard on the display), for a game's main/CMakeLists.txt: include this before its idf_component_register,
# then give it DGX_GE_DI_SRCS among its SRCS. DGX-DI comes by the manifests, not by this: jef-sure/boards brings it for a
# board with touch, a game of its own hardware names jef-sure/dgx-di in its main/idf_component.yml. Not in the
# engine's library, which does not depend on DGX-DI. DGX_GE_DI_REQUIRES is empty, for the CMakeLists that still name it.
# Compiled by the game's main through a file of the build that includes it: a file of the engine's directory in main's
# SRCS is a warning of ESP-IDF 6 (a source of one component built by another).
# A board without touch (CONFIG_BOARD_TOUCH 0, of jef-sure/boards): the same functions with nothing behind them (no
# pointer: nothing shown, nothing down), and nothing of DGX-DI's pad, keyboard and pointer in the firmware, some 18 KB
# of flash.
if(DEFINED CONFIG_BOARD_TOUCH AND NOT CONFIG_BOARD_TOUCH)
	set(dgx_ge_di_none "#define DGX_GE_DI_NONE 1\n")
else()
	set(dgx_ge_di_none "")
endif()
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
	set(DGX_GE_DI_SRCS ${CMAKE_CURRENT_BINARY_DIR}/dgx_ge_di_of_game.c)
	file(CONFIGURE OUTPUT ${DGX_GE_DI_SRCS} CONTENT "${dgx_ge_di_none}#include \"${CMAKE_CURRENT_LIST_DIR}/src/dgx_ge_di.c\"\n")
endif()
set(DGX_GE_DI_REQUIRES "")
