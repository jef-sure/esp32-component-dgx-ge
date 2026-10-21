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
