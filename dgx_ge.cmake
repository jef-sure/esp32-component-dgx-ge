# For the engine cloned into the project's components/: dgx_ge_params_of_pack from the project's CMakeLists, after
# project.cmake and before project(), the path from the project:
#     include(components/dgx-ge/dgx_ge.cmake)
#     dgx_ge_params_of_pack(main/assets/game_pack.h)
# The engine taken from the registry is not there yet at that point: call dgx_ge_params_of_pack from main/CMakeLists.txt
# instead (project_include.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/project_include.cmake")
