# hello for a board of the engine: idf.py build (the CYD), or another board by its directory in the engine's boards/:
#   idf.py -B build-p4 -D SDKCONFIG=build-p4/sdkconfig -D HELLO_BOARD=jc8012p4a1c build
#   idf.py -B build-p7 -D SDKCONFIG=build-p7/sdkconfig -D HELLO_BOARD=jc1060p470 build
# Its sdkconfig: the board's settings, those of its chip's revision, then the game's own (sdkconfig.defaults), a later
# file over an earlier. For the computer it is make instead (Makefile).
cmake_minimum_required(VERSION 3.22)

set(HELLO_BOARD cyd CACHE STRING "the board: cyd, jc8012p4a1c (10\"), jc1060p470 (7\")")
# the boards of the engine this example lies in; an example made from the registry has copied them beside it
# (README: ESP-IDF looks for these files before it downloads the engine into managed_components/)
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/../../boards/${HELLO_BOARD}/sdkconfig.defaults)
	set(board ../../boards/${HELLO_BOARD})
else()
	set(board boards/${HELLO_BOARD})
endif()
if(HELLO_BOARD STREQUAL "cyd")
	set(board_target esp32)
	set(board_defaults "${board}/sdkconfig.defaults")
elseif(HELLO_BOARD STREQUAL "jc8012p4a1c")
	set(board_target esp32p4)
	set(board_defaults "${board}/sdkconfig.defaults;${board}/sdkconfig.defaults.rev3")
elseif(HELLO_BOARD STREQUAL "jc1060p470")
	set(board_target esp32p4)
	set(board_defaults "${board}/sdkconfig.defaults;${board}/sdkconfig.defaults.rev1")
else()
	message(FATAL_ERROR "HELLO_BOARD ${HELLO_BOARD}: cyd, jc8012p4a1c or jc1060p470")
endif()
if(NOT SDKCONFIG_DEFAULTS)
	set(SDKCONFIG_DEFAULTS "${board_defaults};sdkconfig.defaults")
endif()
# ESP-IDF guesses the target from an sdkconfig of the project before the defaults: the board's
if(NOT IDF_TARGET AND NOT DEFINED ENV{IDF_TARGET})
	set(IDF_TARGET ${board_target})
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# a lock of the components for each chip: the CYD (esp32) and the P4 boards (esp32p4) side by side
idf_build_set_property(DEPENDENCIES_LOCK ${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.${IDF_TARGET})
# the board for main/CMakeLists.txt: a property of the build, which ESP-IDF hands on even where it reads that file early
idf_build_set_property(HELLO_BOARD ${HELLO_BOARD})
project(hello)
