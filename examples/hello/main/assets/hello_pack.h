/* Made by dgx_ge_pack.py: the numbers of hello.pack. Do not edit. */
#pragma once

#define HELLO_SET_BOT 0
#define HELLO_SET_GROUND 1
#define HELLO_SET_UI 2
#define HELLO_SET_COUNT 3
#define HELLO_SET_COLOR_COUNT 10

#define HELLO_IMG_BOT_STAND 0
#define HELLO_IMG_BOT_WALK_1 1
#define HELLO_IMG_BOT_WALK_2 2
#define HELLO_IMG_BOT_HOP 3
#define HELLO_IMG_GROUND 4
#define HELLO_IMG_COUNT 5

#define HELLO_BLOB_BOT 0
#define HELLO_BLOB_SOUNDS 1
#define HELLO_BLOB_COUNT 2

#define HELLO_SOUND_HOP 0
#define HELLO_SOUND_COUNT 1

#define HELLO_EVENT_WALK 0
#define HELLO_EVENT_STOP 1
#define HELLO_EVENT_HOP 2

#define HELLO_ANIM_PARAMS 2
#include "dgx_ge_anim.h"
#if DGX_GE_ANIM_PARAMS < HELLO_ANIM_PARAMS
#error "the actors of this pack keep 2 params, the build fewer (DGX_GE_ANIM_PARAMS): dgx_ge_params_of_pack(<this header>) in the game's CMakeLists, GAME_PACK_H in its Makefile of the computer"
#endif

#define HELLO_BOT_SCRIPT_STAND 0
#define HELLO_BOT_SCRIPT_WALK 1
#define HELLO_BOT_SCRIPT_HOP 2
#define HELLO_BOT_SCRIPT_LEAP 3
#define HELLO_BOT_EVENT_WALK 0
#define HELLO_BOT_EVENT_STOP 1
#define HELLO_BOT_EVENT_HOP 2
