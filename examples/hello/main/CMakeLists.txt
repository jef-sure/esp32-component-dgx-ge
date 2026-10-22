# The board (CMakeLists.txt of the project), the engine's: the one this example lies in, or the one ESP-IDF downloaded
idf_build_get_property(HELLO_BOARD HELLO_BOARD)
set(engine ${CMAKE_CURRENT_LIST_DIR}/../../..)
if(NOT EXISTS ${engine}/boards/dgx_ge_board.h)
	set(engine ${CMAKE_CURRENT_LIST_DIR}/../managed_components/jef-sure__dgx-ge)
endif()
include(${engine}/boards/${HELLO_BOARD}/board.cmake)
# the touch panel's firmware of the 10" is not ours to give: the game embeds its own (DGX-DI tools/gsl_firmware.py)
if(HELLO_BOARD STREQUAL "jc8012p4a1c")
	set(board_embed "jc8012p4a1c_touch.fw")
endif()

idf_component_register(
	SRCS "main.c" ${BOARD_SRCS}
	INCLUDE_DIRS "." ${BOARD_INCLUDE_DIRS}
	REQUIRES jef-sure__dgx-ge jef-sure__dgx
	PRIV_REQUIRES nvs_flash esp_timer jef-sure__dgx-di ${BOARD_REQUIRES}
	EMBED_FILES "assets/hello.pack" ${board_embed}
)
# the params an actor keeps, as many as the pack's animations need
dgx_ge_params_of_pack(assets/hello_pack.h)
