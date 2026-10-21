# The engine's part of a project's configuration: ESP-IDF includes this before the CMakeLists of any component.

# dgx_ge_fonts(<file>): the fonts of a game, the requests the engine's packer wrote for them (Pack.write_fonts: DGX's
# file of font requests, beside the pack and the letters it prints), handed to DGX, which makes them for the board
# (dgx_font_needs_file of DGX's project_include.cmake, DGX 0.7.2). The path from the CMakeLists that calls it. The game
# says the rest once: the display (dgx_fonts_board), the scale of its frame (dgx_fonts_scale), the rules of faces
# (dgx_fonts_rules).
function(dgx_ge_fonts path)
    get_filename_component(abs "${path}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
    if(NOT COMMAND dgx_font_needs_file)
        message(FATAL_ERROR "dgx_ge_fonts(${path}): DGX 0.7.2 or later is needed (its dgx_font_needs_file)")
    endif()
    dgx_font_needs_file("${abs}")
endfunction()

# dgx_ge_params_of_pack(<header>): the params an actor keeps, as the pack of the game needs them. Its header (made by
# dgx_ge_pack.py) says how many at most, <PREFIX>_ANIM_PARAMS; the whole build (the engine and the game alike) gets
# DGX_GE_ANIM_PARAMS so many. The path from the CMakeLists that calls it, after its idf_component_register:
#     dgx_ge_params_of_pack(assets/game_pack.h)
# ESP-IDF hands build definitions to every component when the build is generated, so one set here reaches the engine
# as well, though the engine's CMakeLists may have been read before.
function(dgx_ge_params_of_pack header)
    get_filename_component(path "${header}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
    file(STRINGS "${path}" line REGEX "^#define [A-Z0-9_]+_ANIM_PARAMS [0-9]+$")
    if(NOT line)
        message(FATAL_ERROR "${path}: no _ANIM_PARAMS (made by dgx_ge_pack.py with animations?)")
    endif()
    string(REGEX REPLACE "^.* ([0-9]+)$" "\\1" params "${line}")
    idf_build_set_property(COMPILE_DEFINITIONS "DGX_GE_ANIM_PARAMS=${params}" APPEND)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
endfunction()
