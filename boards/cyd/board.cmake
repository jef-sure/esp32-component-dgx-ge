# The CYD (cyd.c), for a game's main/CMakeLists.txt: include this before its idf_component_register, then give it
# BOARD_SRCS among its SRCS, BOARD_INCLUDE_DIRS among its INCLUDE_DIRS and BOARD_REQUIRES among its PRIV_REQUIRES.
# Its sdkconfig takes sdkconfig.defaults beside this, before the game's own: the chip, the flash, DGX's SPI bus and
# ILI9341 (CONFIG_DGX_ENABLE_SPI, CONFIG_DGX_ENABLE_SPI_ILI_9341) and the virtual screens the engine's frame is
# (CONFIG_DGX_ENABLE_VSCREEN): without them their functions are missing at link.
# The board's file, compiled by the game's main through a file of the build that includes it: a file of the engine's
# directory in main's SRCS is a warning of ESP-IDF 6 (a source of one component built by another). The header of the
# boards comes with the engine itself (its INCLUDE_DIRS): BOARD_INCLUDE_DIRS stays, empty, for the games that name it.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
	set(BOARD_SRCS ${CMAKE_CURRENT_BINARY_DIR}/dgx_ge_board_cyd.c)
	file(CONFIGURE OUTPUT ${BOARD_SRCS} CONTENT "#include \"${CMAKE_CURRENT_LIST_DIR}/cyd.c\"\n")
endif()
set(BOARD_INCLUDE_DIRS "")
set(BOARD_REQUIRES esp_driver_gpio esp_driver_spi)
# its display lying as it is shown, pixels and millimetres: the game hands them to DGX once (dgx_fonts_board)
set(BOARD_PIXELS 320 240)
set(BOARD_MM 58 43)
