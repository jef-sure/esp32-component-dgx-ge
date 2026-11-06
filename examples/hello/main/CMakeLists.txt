# The game and its pack. The pack is made here, as the build is configured, from game_data/, the folder the sprite
# editor keeps (hello.json, the pictures, BOT.anim; the sound's samples in sounds/), by the engine's packer
# (tools/pack.py); a change of any of them configures the build again. The packer wants a python3 with Pillow: the
# system's, not ESP-IDF's. The board is jef-sure/boards' (main/idf_component.yml), the engine CMakeLists.txt's.
idf_build_get_property(engine GAME_ENGINE)
# the touch of the display as the game's, the engine's over DGX-DI (src/dgx_ge_di.c): every board, one without touch too
include(${engine}/dgx_ge_di.cmake)
# the touch panel's firmware of the 10" is not ours to give: the game embeds its own (README; DGX-DI tools/gsl_firmware.py)
if(CONFIG_BOARD_JC8012P4A1C AND EXISTS ${CMAKE_CURRENT_LIST_DIR}/jc8012p4a1c_touch.fw)
	set(touch_fw "jc8012p4a1c_touch.fw")
endif()

set(game ${CMAKE_CURRENT_LIST_DIR}/..)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
	find_program(GAME_PYTHON NAMES python3 PATHS /usr/bin /usr/local/bin /opt/homebrew/bin NO_DEFAULT_PATH)
	if(NOT GAME_PYTHON)
		find_program(GAME_PYTHON NAMES python3 python REQUIRED)
	endif()
	file(GLOB inputs ${game}/game_data/*.json ${game}/game_data/*/*.png ${game}/game_data/*.anim ${game}/game_data/dialogs/*.dlg ${game}/game_data/sounds/*.wav ${game}/game_data/music/*.mml ${game}/tools/pack.py)
	execute_process(COMMAND ${GAME_PYTHON} ${game}/tools/pack.py ${engine} ${game} RESULT_VARIABLE packed)
	if(NOT packed EQUAL 0)
		message(FATAL_ERROR "the pack of the game could not be made (${GAME_PYTHON} tools/pack.py: it needs Pillow)")
	endif()
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${inputs})
endif()

idf_component_register(
	SRCS "main.c" ${DGX_GE_DI_SRCS}
	INCLUDE_DIRS "."
	REQUIRES jef-sure__dgx-ge jef-sure__dgx
	PRIV_REQUIRES nvs_flash esp_timer
	EMBED_FILES "assets/hello.pack" ${touch_fw}
)
if(touch_fw)
	target_compile_definitions(${COMPONENT_LIB} PRIVATE HELLO_TOUCH_FW=1)
endif()
# the params an actor keeps, as many as the pack's animations need
dgx_ge_params_of_pack(assets/hello_pack.h)
# the events at once an actor's own say may give in one step: the engine's default, said aloud
dgx_ge_now_events(4)
# the synthesizers of music the build carries: the engine's default, FM, said aloud
dgx_ge_synths(fm)
