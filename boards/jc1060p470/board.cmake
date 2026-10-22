# The Guition JC1060P470 (jc1060p470.c), for a game's main/CMakeLists.txt: include this before its
# idf_component_register, then give it BOARD_SRCS among its SRCS, BOARD_INCLUDE_DIRS among its INCLUDE_DIRS and
# BOARD_REQUIRES among its PRIV_REQUIRES. The game's idf_component.yml names jef-sure/dgx-di (the pointer, the pad),
# espressif/esp_lcd_touch_gt911 (its touch panel) and espressif/esp_codec_dev (the ES8311). Its sdkconfig takes
# sdkconfig.defaults beside this and the file of its chip's revision, before the game's own. The first turns on DGX's
# MIPI-DSI bus and JD9165 (CONFIG_DGX_ENABLE_DSI, CONFIG_DGX_ENABLE_JD9165) and the virtual screens the engine's frame
# is (CONFIG_DGX_ENABLE_VSCREEN): without them their functions are missing at link; and the PSRAM (CONFIG_SPIRAM,
# CONFIG_SPIRAM_MODE_HEX), which holds the panel's two frame buffers and the band of the scaled frame: without it the
# display does not start. Its chip (mine) is of revision 1.3, sdkconfig.defaults.rev1: an ESP-IDF that knows revision 3
# builds for revision 3.1 and later by default, which does not start on it.
# The board's file, compiled by the game's main through a file of the build that includes it: a file of the engine's
# directory in main's SRCS is a warning of ESP-IDF 6 (a source of one component built by another). The header of the
# boards comes with the engine itself (its INCLUDE_DIRS): BOARD_INCLUDE_DIRS stays, empty, for the games that name it.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
	set(BOARD_SRCS ${CMAKE_CURRENT_BINARY_DIR}/dgx_ge_board_jc1060p470.c)
	file(CONFIGURE OUTPUT ${BOARD_SRCS} CONTENT "#include \"${CMAKE_CURRENT_LIST_DIR}/jc1060p470.c\"\n")
endif()
set(BOARD_INCLUDE_DIRS "")
set(BOARD_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_lcd espressif__esp_lcd_touch_gt911 espressif__esp_codec_dev jef-sure__dgx-di)
# its display lying as it is shown, pixels and millimetres: the game hands them to DGX once (dgx_fonts_board)
set(BOARD_PIXELS 1024 600)
set(BOARD_MM 154 86)
