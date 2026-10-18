# The params an actor keeps, as the pack of the game needs them: its header (made by dgx_ge_pack.py) says how many
# at most, <PREFIX>_ANIM_PARAMS; the whole build (the engine and the game alike) gets DGX_GE_ANIM_PARAMS so many.
# In the project's CMakeLists, after project.cmake and before project():
#     include(<the engine's folder>/dgx_ge.cmake)
#     dgx_ge_params_of_pack(main/assets/game_pack.h)
function(dgx_ge_params_of_pack header)
	set(path "${CMAKE_CURRENT_LIST_DIR}/${header}")
	file(STRINGS "${path}" line REGEX "^#define [A-Z0-9_]+_ANIM_PARAMS [0-9]+$")
	if(NOT line)
		message(FATAL_ERROR "${path}: no _ANIM_PARAMS (made by dgx_ge_pack.py with animations?)")
	endif()
	string(REGEX REPLACE "^.* ([0-9]+)$" "\\1" params "${line}")
	idf_build_set_property(COMPILE_DEFINITIONS "DGX_GE_ANIM_PARAMS=${params}" APPEND)
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
endfunction()
