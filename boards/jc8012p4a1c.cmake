# The Guition JC8012P4A1C (jc8012p4a1c.c), for a game's main/CMakeLists.txt: include this before its
# idf_component_register, then give it BOARD_SRCS among its SRCS, BOARD_INCLUDE_DIRS among its INCLUDE_DIRS and
# BOARD_REQUIRES among its PRIV_REQUIRES. The game's idf_component.yml names jef-sure/dgx-di (the touch panel, the pad)
# and espressif/esp_codec_dev (the ES8311); its EMBED_FILES, the touch panel's firmware jc8012p4a1c_touch.fw.
set(BOARD_SRCS ${CMAKE_CURRENT_LIST_DIR}/jc8012p4a1c.c)
set(BOARD_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
set(BOARD_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s espressif__esp_codec_dev jef-sure__dgx-di)
# its display lying as it is shown, pixels and millimetres: the game hands them to DGX once (dgx_fonts_board)
set(BOARD_PIXELS 1280 800)
set(BOARD_MM 217 136)
