# The engine is plain C on top of DGX screens and builds on a host as well, see test/host. No board in the library: a
# game sets up its own hardware (its display, its touch, its sound) and gives the engine a screen of DGX, or takes one
# of boards/ into its own build (boards/<board>/board.cmake), which this does not build.
idf_component_register(
	SRCS "src/dgx_ge_image.c" "src/dgx_ge_frame.c" "src/dgx_ge_scene.c" "src/dgx_ge_pack.c" "src/dgx_ge_anim.c" "src/dgx_ge_input.c" "src/dgx_ge_mask.c" "src/dgx_ge_tiles.c" "src/dgx_ge_expr.c" "src/dgx_ge_world.c" "src/dgx_ge_shots.c" "src/dgx_ge_pockets.c" "src/dgx_ge_dialog.c" "src/dgx_ge_text.c" "src/dgx_ge_talk_view.c" "src/dgx_ge_cells.c" "src/dgx_ge_links.c" "src/dgx_ge_graph.c" "src/dgx_ge_game.c" "src/dgx_ge_timers.c" "src/dgx_ge_show.c" "src/dgx_ge_chase.c" "src/dgx_ge_walk.c" "src/dgx_ge_label.c" "src/dgx_ge_save.c" "src/dgx_ge_widgets.c" "src/dgx_ge_crowd.c" "src/dgx_ge_mixer.c" "src/dgx_ge_layout.c"
	# boards/: the header of the boards (dgx_ge_board.h), for the game that takes one of them; the engine's own
	# directory, so ESP-IDF 6 does not take main's include of it for a directory of another component
	INCLUDE_DIRS "include" "boards"
	REQUIRES jef-sure__dgx
)
# The expression machine picks a step by one switch on dense codes: a table of jumps, not a tree of comparisons.
# ESP-IDF turns tables of jumps off for everything (a table in flash breaks code that must run from IRAM while the
# cache is off); this code lives in flash anyway and never runs then, so its table may live there too. The second flag
# is GCC's own, as ESP-IDF gives its opposite only to GCC.
if(CMAKE_C_COMPILER_ID MATCHES "GNU")
	set_source_files_properties(src/dgx_ge_expr.c PROPERTIES COMPILE_OPTIONS "-fjump-tables;-ftree-switch-conversion")
else()
	set_source_files_properties(src/dgx_ge_expr.c PROPERTIES COMPILE_OPTIONS "-fjump-tables")
endif()
