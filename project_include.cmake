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

# dgx_ge_now_events(<n>): how many events at once an actor's own say may give in one step, taken in turn (the queue of
# dgx_ge_actor_event, 2 bytes a place in every actor); one more is refused. The whole build (the engine and the game
# alike) gets DGX_GE_ANIM_NOW_EVENTS so many, 1 to 255; not said, the engine's header says 4. From the game's
# main/CMakeLists.txt, after its idf_component_register, beside dgx_ge_params_of_pack:
#     dgx_ge_now_events(4)
function(dgx_ge_now_events n)
    if(NOT n MATCHES "^[0-9]+$" OR n LESS 1 OR n GREATER 255)
        message(FATAL_ERROR "dgx_ge_now_events(${n}): a whole number from 1 to 255")
    endif()
    idf_build_get_property(defined COMPILE_DEFINITIONS)
    if(defined MATCHES "DGX_GE_ANIM_NOW_EVENTS=")
        message(FATAL_ERROR "dgx_ge_now_events(${n}): said already; one value for the whole build")
    endif()
    idf_build_set_property(COMPILE_DEFINITIONS "DGX_GE_ANIM_NOW_EVENTS=${n}" APPEND)
endfunction()

# dgx_ge_synths(<name>...): the synthesizers of music by notes the build carries: chip, nes, fm (one or more; those
# left out are not in the firmware). A song of one left out plays nothing, and the mixer says so once in the log. Not
# said: FM alone, the synthesizer a song has when it names none (dgx_ge_synth.h). The whole build gets DGX_GE_SYNTH_<NAME>=1 for each; a channel's state is as
# large as the largest of them needs (with fm: 64 bytes, 36 without). From the game's main/CMakeLists.txt, after its
# idf_component_register:
#     dgx_ge_synths(chip fm)
function(dgx_ge_synths)
    if(NOT ARGN)
        message(FATAL_ERROR "dgx_ge_synths(): one synthesizer at least: chip, nes, fm")
    endif()
    idf_build_get_property(defined COMPILE_DEFINITIONS)
    if(defined MATCHES "DGX_GE_SYNTH_(CHIP|NES|FM)=")
        message(FATAL_ERROR "dgx_ge_synths(${ARGN}): said already; one set for the whole build")
    endif()
    set(seen "")
    foreach(name IN LISTS ARGN)
        if(NOT name MATCHES "^(chip|nes|fm)$" OR name IN_LIST seen)
            message(FATAL_ERROR "dgx_ge_synths(${ARGN}): chip, nes or fm, each once, not ${name}")
        endif()
        list(APPEND seen ${name})
        string(TOUPPER "${name}" upper)
        idf_build_set_property(COMPILE_DEFINITIONS "DGX_GE_SYNTH_${upper}=1" APPEND)
    endforeach()
endfunction()
