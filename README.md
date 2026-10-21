# DGX-GE

DGX-GE is a small engine for 2D and 2.5D games on ESP32 microcontrollers,
built on top of my display library [DGX](https://github.com/jef-sure/esp32-component-dgx).
I had wanted a game engine of my own for a long time. My thoughts about it
were scattered: a game is pictures, animations and rules rather than code,
and time in it is real time rather than "so much per frame". To bring them
together I needed a real game that would not let me skip anything, and I took
the smallest one that was enough: Prince of Persia of 1989. It builds on the
engine, with its traps, doors, guards and fights described as data, and far
less C of its own than the original had. Neither that game nor anything of
it comes with the engine; it is only what the engine was checked on.

This is version 0.1.0. Since the first one it has learned to read whole
games from my sprite editor's files (scenes, the graph of scenes, links,
the game's own data), to let DGX make its fonts for each board, and to
start on three boards I keep on my desk. Its interfaces will still change.

## What is in it

- **A frame of palette indices** that finds what changed and sends only
  that to the display, as it is or scaled up by an integer factor, in
  blocks or smoothed (Scale2x, Scale3x or their SFX variants), through DGX;
  text and dialogs over it at the display's own resolution.
- **A pack in flash**: pictures of 1, 4 and 8 bits, read where they lie;
  scripts, dialogs, tiles, fonts, sounds and the game's own tables beside
  them.
- **A scene of layers** and **actors that live by animation scripts**
  written as text: frames, moves, events, a small language of expressions,
  a matrix of transitions between scripts.
- **Tiles with rules**, **collisions by masks**, contacts found at the
  exact moment they happen.
- **A world that lives on without a viewer**: things lying about, pockets,
  shots, timers, chases, crowds, cells with lives of their own.
- **Dialogs, labels, widgets and text** laid out in fonts of DGX, a dialog
  able to pause the game; cut-scenes; saved games.
- **Fonts as requests**: the game names a font by its size in points of the
  frame, the packer writes DGX's font requests beside the pack together with
  the letters the pack prints, and DGX makes each font for the board.
- **Touch** through [DGX-DI](https://github.com/jef-sure/esp32-component-dgx-di)
  when the game uses it: a pointer, gestures and an on-screen pad become the
  engine's input.
- **A sound mixer** of 8-bit and IMA ADPCM samples, mono or stereo; the
  platform sends what it mixes.
- **Python tools** (`tools/`) that turn pictures, scripts, dialogs and
  sounds into one pack and a C header for it, also straight from a file of
  my sprite editor: its sets, animations, tiles, scenes, graph and game.

The engine itself knows no board: the game sets up its own display, touch
and sound and gives the engine a DGX screen. For the boards games have run
on (the CYD; the Guition JC8012P4A1C, 10", and JC1060P470, 7") `boards/`
has ready set-up code, which the library does not build: a game takes one
board into its own build (`boards/<board>.cmake`), or copies its file into
`main/` and changes it.
The engine is plain C and builds on a host as well, which is how it is
tested.

## Getting started

You need ESP-IDF 5.3 or newer and DGX 0.7.2 or newer, which the component
takes from the ESP Component Registry by itself. The engine itself is not in
the registry yet: clone it into your project's `components/` under the name
`dgx-ge`, which is the name ESP-IDF then knows it by.

```sh
git clone https://github.com/jef-sure/esp32-component-dgx-ge components/dgx-ge
```

The project's `CMakeLists.txt` tells the build how many params an actor
keeps, as many as the game's pack needs:

```cmake
cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(components/dgx-ge/dgx_ge.cmake)
dgx_ge_params_of_pack(main/assets/game_pack.h)
project(my-game)
```

and `main` requires `dgx-ge`.

A Python script of the game builds its pack with `tools/dgx_ge_pack.py`
(`Pack`: `add_set`, `add_png`, `add_anim`, `add_dialog` and the rest, then
`write`), which gives the pack and its header. The game embeds the pack
(`EMBED_FILES`), opens it with `dgx_ge_pack_open()` and builds its scene
from it. A game with fonts of its own names them in its script
(`Pack.add_font`) and hands their requests to DGX in `main/CMakeLists.txt`:

```cmake
dgx_ge_fonts(assets/game.fonts)
dgx_fonts_board(PIXELS 1280 800 MM 217 136)
dgx_fonts_scale(4)
dgx_fonts_rules(../fonts.rules)
```

The tools need Python 3 and Pillow; fonts made for a board need a host C
compiler, FreeType's headers and fontconfig as well.

## Tests

```sh
make -C test/host
```

The tests take DGX from the managed components of the project the engine
sits in, or from a DGX checkout next to it (`../esp32-component-dgx`);
`DGX=<path>` takes any other.

## Documentation

In English and in Russian:

- [docs/overview-en.md](docs/overview-en.md) ([по-русски](docs/overview-ru.md)):
  how the engine is built and how a game is made on it, for someone who has
  never made one;
- [docs/animation-en.md](docs/animation-en.md) ([по-русски](docs/animation-ru.md)):
  the language of the animation scripts.

The API itself is described in the headers, `include/*.h`.

## License

MIT, see [LICENSE](LICENSE).
