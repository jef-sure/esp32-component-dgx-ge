# The Guition JC1060P470 (jc1060p470.c), for a game's main/CMakeLists.txt: include this before its
# idf_component_register, then give it BOARD_SRCS among its SRCS, BOARD_INCLUDE_DIRS among its INCLUDE_DIRS and
# BOARD_REQUIRES among its PRIV_REQUIRES. The game's idf_component.yml names jef-sure/dgx-di (the pointer, the pad),
# espressif/esp_lcd_touch_gt911 (its touch panel) and espressif/esp_codec_dev (the ES8311); its sdkconfig is one for an
# ESP32-P4 below revision 3 (CONFIG_ESP32P4_SELECTS_REV_LESS_V3) with DGX's JD9165.
set(BOARD_SRCS ${CMAKE_CURRENT_LIST_DIR}/jc1060p470.c)
set(BOARD_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
set(BOARD_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_lcd espressif__esp_lcd_touch_gt911 espressif__esp_codec_dev jef-sure__dgx-di)
# its display lying as it is shown, pixels and millimetres: the game hands them to DGX once (dgx_fonts_board)
set(BOARD_PIXELS 1024 600)
set(BOARD_MM 154 86)
