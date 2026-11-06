# The engine is plain C on top of DGX screens and builds on a host as well, see test/host. No board in the library: a
# game sets up its hardware (its display, its touch, its sound) by jef-sure/boards, or by its own code, and gives the
# engine a screen of DGX.
idf_component_register(
	SRCS "src/dgx_ge_image.c" "src/dgx_ge_frame.c" "src/dgx_ge_scene.c" "src/dgx_ge_pack.c" "src/dgx_ge_anim.c" "src/dgx_ge_input.c" "src/dgx_ge_mask.c" "src/dgx_ge_tiles.c" "src/dgx_ge_expr.c" "src/dgx_ge_world.c" "src/dgx_ge_shots.c" "src/dgx_ge_pockets.c" "src/dgx_ge_dialog.c" "src/dgx_ge_text.c" "src/dgx_ge_talk_view.c" "src/dgx_ge_cells.c" "src/dgx_ge_links.c" "src/dgx_ge_graph.c" "src/dgx_ge_game.c" "src/dgx_ge_timers.c" "src/dgx_ge_show.c" "src/dgx_ge_chase.c" "src/dgx_ge_walk.c" "src/dgx_ge_label.c" "src/dgx_ge_save.c" "src/dgx_ge_widgets.c" "src/dgx_ge_crowd.c" "src/dgx_ge_mixer.c" "src/dgx_ge_music.c" "src/dgx_ge_layout.c" "src/dgx_ge_glue.c" "src/dgx_ge_freertos.c"
	INCLUDE_DIRS "include"
	# esp_timer: the loop paced by its clock (dgx_ge_pace); FreeRTOS, the lock of the sound, ESP-IDF's always. Not
	# src/dgx_ge_di.c, the touch of the display as the game's: DGX-DI's, a game that has it compiles it (dgx_ge_di.cmake)
	REQUIRES jef-sure__dgx esp_timer
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
