# The Guition JC8012P4A1C (jc8012p4a1c.c), for a game's main/CMakeLists.txt: include this before its
# idf_component_register, then give it BOARD_SRCS among its SRCS, BOARD_INCLUDE_DIRS among its INCLUDE_DIRS and
# BOARD_REQUIRES among its PRIV_REQUIRES. The game's idf_component.yml names jef-sure/dgx-di (the touch panel, the pad)
# and espressif/esp_codec_dev (the ES8311); its EMBED_FILES, the touch panel's firmware jc8012p4a1c_touch.fw.
# Its sdkconfig takes sdkconfig.defaults beside this and the file of its chip's revision, before the game's own. The
# first turns on DGX's MIPI-DSI bus and JD9365 (CONFIG_DGX_ENABLE_DSI, CONFIG_DGX_ENABLE_JD9365) and the virtual screens
# the engine's frame is (CONFIG_DGX_ENABLE_VSCREEN): without them their functions are missing at link; and the PSRAM
# (CONFIG_SPIRAM, CONFIG_SPIRAM_MODE_HEX), which holds the panel's two frame buffers and the band of the scaled frame:
# without it the display does not start. The board comes with a chip of either kind: one of revision 3 (mine, 3.2),
# sdkconfig.defaults.rev3; one below 3, sdkconfig.defaults.rev1, as the 7" (an ESP-IDF that knows revision 3 builds
# for one kind or the other, and a build for one does not start on the other).
# The board's file, compiled by the game's main through a file of the build that includes it: a file of the engine's
# directory in main's SRCS is a warning of ESP-IDF 6 (a source of one component built by another). The header of the
# boards comes with the engine itself (its INCLUDE_DIRS): BOARD_INCLUDE_DIRS stays, empty, for the games that name it.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
	set(BOARD_SRCS ${CMAKE_CURRENT_BINARY_DIR}/dgx_ge_board_jc8012p4a1c.c)
	file(CONFIGURE OUTPUT ${BOARD_SRCS} CONTENT "#include \"${CMAKE_CURRENT_LIST_DIR}/jc8012p4a1c.c\"\n")
endif()
set(BOARD_INCLUDE_DIRS "")
set(BOARD_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s espressif__esp_codec_dev jef-sure__dgx-di)
# its display lying as it is shown, pixels and millimetres: the game hands them to DGX once (dgx_fonts_board)
set(BOARD_PIXELS 1280 800)
set(BOARD_MM 217 136)
