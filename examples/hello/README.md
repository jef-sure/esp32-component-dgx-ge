# hello

The smallest whole game of DGX-GE: one actor on a scene. A robot walks while
an arrow is held and hops at a tap or a button, with a sound; a label counts
the hops, and the count is kept over a restart.

The same `main/main.c` builds for every board of
[jef-sure/boards](https://github.com/jef-sure/esp32-component-boards) and for
the computer, its board `host`. It talks to the hardware only through
`boards.h`, and asks for the parts by kind: the display, the touch, the
sound, the board's button. The boards of this example, as `board_select.py`
of jef-sure/boards chose them: the CYD (`cyd_s028r`, the current one), the
Guition JC8012P4A1C (10"), JC1060P470C (7") and JC4827W543C (4.3"), and the
computer showing the 10".

Its data are a game of the sprite editor,
[dgx-ge-editor](https://github.com/jef-sure/dgx-ge-editor), laid out as the
editor's [workflow.md](https://github.com/jef-sure/dgx-ge-editor/blob/main/workflow.md)
says a game is: open `game_data/` in the editor to change them.

## What is where

| File | What it is |
| --- | --- |
| `main/main.c` | the game: the frame, the scene, the robot, the label, the sound, the count in NVS, the loop |
| `game_data/` | the data, the folder the editor keeps: |
| `game_data/hello.json` | the document: the kinds, the game's screen (320x200) and its colors (the palette UI: the sky, the label's ink), the sound's name |
| `game_data/BOT/*.png`, `game_data/GROUND/ground.png` | the robot's pictures and the ground, PNGs of a palette |
| `game_data/BOT.anim` | what the robot does: its pictures, strides, the hop, its sound and its signal, the rules between them |
| `game_data/sounds/hop.wav` | the chirp of a hop |
| `tools/pack.py` | the engine's packer over `game_data/`: `main/assets/hello.pack` and `hello_pack.h` (the build runs it) |
| `CMakeLists.txt`, `main/CMakeLists.txt`, `main/idf_component.yml`, `sdkconfig.defaults` | the build, for a board and for the computer |
| `sdkconfig.defaults.<board>`, `CMakePresets.json` | the boards, as `board_select.py` wrote them: not to edit, run it again |

## On a board

ESP-IDF 5.5 or later; 6 reads the presets, each board builds in
`build/<board>`:

```sh
idf.py build flash monitor                        # the CYD, the current board
idf.py --preset jc8012p4a1c build flash monitor   # the 10"; jc1060p470c, the 7"; jc4827w543c, the 4.3"
```

ESP-IDF 5 does not read them:
`idf.py -B build/<board> -D SDKCONFIG=build/<board>/sdkconfig -D BOARD=<board> build`.

The touch, where the board has one, is the pointer: a tap hops, and the pad
on the display (DGX-DI's) gives the arrows where the board has none of its
own. The board's buttons are taken by their names: those of a gamepad's
(`left`, `a`, `start`…) as theirs, another (BOOT) as A, a hop.

The 10" needs its touch panel's firmware, which may not be given away: make
`main/jc8012p4a1c_touch.fw` from the board's package with DGX-DI's
`tools/gsl_firmware.py` (jef-sure/boards: `docs/en/touch-firmware.md`).
Without it the 10" runs with its touch off.

Another board: `python3 <boards>/tools/board_select.py <board> .`, where
`<boards>` is a checkout of jef-sure/boards or
`managed_components/jef-sure__boards` after a first build.

## On the computer

The board `host` of jef-sure/boards: the linux target of ESP-IDF, the display
a window of DGX as large as the 10"'s (1280x800, the game's 320x200 four
times larger), the mouse a finger on it, the keys its buttons, the
computer's sound. Needs SDL2 (`libsdl2-dev`) and the system's python3 with
Pillow for the pack.

```sh
idf.py --preset host build
./build/host/hello.elf
```

| Key | Button |
| --- | --- |
| the arrows, WASD | walk |
| Space, J (A); K, Backspace (B); B (the board's BOOT) | hop |
| the mouse | a finger: a click hops |

The count is kept in `build/host/flash.bin`; delete it to start from zero.
The window shows another board after `board_select.py host --as <board> .`
and `rm build/host/sdkconfig` (`cyd_s028r`: 320x240).

With nobody at the computer: `DGX_HOST_SCRIPT` plays a file of keys and the
mouse, `DGX_HOST_QUIT_MS` ends the program, `DGX_HOST_SHOT` keeps the
window's last picture (the engine's `docs/host-en.md`, section 7):

```sh
printf '300 key down Right\n1300 key up Right\n1600 key down Space\n1650 key up Space\n' > hop.script
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy DGX_HOST_SCRIPT=hop.script DGX_HOST_QUIT_MS=2500 \
    DGX_HOST_SHOT=hello.ppm ./build/host/hello.elf
```

## The pack again

The build makes it whenever something in `game_data/` is newer than it, as it
configures. By hand: `python3 tools/pack.py ../..` (the engine's folder).

## A game of your own from it

`python3 ../../tools/dgx_ge_new.py <folder>` makes a project of its own
from this example, anywhere, renamed for the game: see the engine's README.
