# The CYD (cyd.c), for a game's main/CMakeLists.txt: include this before its idf_component_register, then give it
# BOARD_SRCS among its SRCS, BOARD_INCLUDE_DIRS among its INCLUDE_DIRS and BOARD_REQUIRES among its PRIV_REQUIRES.
set(BOARD_SRCS ${CMAKE_CURRENT_LIST_DIR}/cyd.c)
set(BOARD_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
set(BOARD_REQUIRES esp_driver_gpio esp_driver_spi)
# its display lying as it is shown, pixels and millimetres: the game hands them to DGX once (dgx_fonts_board)
set(BOARD_PIXELS 320 240)
set(BOARD_MM 58 43)
