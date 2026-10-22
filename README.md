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

It is young: what each version brought is in [CHANGES.md](CHANGES.md).
Lately it has learned to read whole games from my sprite editor's files
(scenes, the graph of scenes, links, the game's own data), to let DGX make
its fonts for each board, and to start on three boards I keep on my desk.
Its interfaces will still change.

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
  platform sends what it mixes, from a task of its own if it likes, with
  its lock given to the mixer, so the game never takes one.
- **Python tools** (`tools/`) that turn pictures, scripts, dialogs and
  sounds into one pack and a C header for it, also straight from a file of
  my sprite editor: its sets, animations, tiles, scenes, graph and game.

The engine itself knows no board: the game sets up its own display, touch
and sound and gives the engine a DGX screen. For the boards games have run
on (the CYD; the Guition JC8012P4A1C, 10", and JC1060P470, 7") `boards/`
has ready set-up code, a directory a board, which the library does not
build: a game takes one board into its own build
(`boards/<board>/board.cmake`) with the ESP-IDF settings it needs
(`boards/<board>/sdkconfig.defaults`, and on an ESP32-P4 one for its chip's
revision), or copies them into its project and changes them.

One of the boards is the computer itself, `boards/host`: the game's own
`main.c` in a window of SDL2, the display of a board shown in it, the mouse
a finger, the keys buttons, SDL's sound, NVS in files. A Makefile of the
game includes `boards/host/host.mk` and `make` builds it (DGX 0.8.0 or
newer). The example `examples/hello` builds both ways, for the computer and
for the boards. All of it, from what to install to how the computer stands
in for a board and what it cannot show (speed, memory), is in
[docs/host-en.md](docs/host-en.md) ([по-русски](docs/host-ru.md)). The engine itself is
plain C and is tested on a host too.

## Getting started

You need ESP-IDF 5.3 or newer; I build the engine, the boards and the game
it was checked on with 6.0. The engine is in the ESP Component Registry;
add it to the project's `main`, and DGX 0.8 comes along by
itself:

```sh
idf.py add-dependency "jef-sure/dgx-ge^0.1.1"
```

DGX builds only what the project's `sdkconfig` turns on: the engine's
frame needs its virtual screens (`CONFIG_DGX_ENABLE_VSCREEN`), and the
display its bus and driver; without them the link stops at their missing
functions. Each board's `.cmake` names its own, and the PSRAM a board with
a large display needs for its frame buffers.

The component manager puts it into `managed_components/jef-sure__dgx-ge`
and makes `main` require it, so `main/CMakeLists.txt` needs no `REQUIRES`
for it (if it names it, the name is `jef-sure__dgx-ge`). There, after
`idf_component_register`, the build is told how many params an actor
keeps, as many as the game's pack needs:

```cmake
idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
                       EMBED_FILES "assets/game.pack")
dgx_ge_params_of_pack(assets/game_pack.h)
```

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

The tools lie in `managed_components/jef-sure__dgx-ge/tools/`. They need
Python 3 and Pillow; fonts made for a board need a host C
compiler, FreeType's headers and fontconfig as well.

## Tests

The tests are in the repository, not in the registry's package:

```sh
git clone https://github.com/jef-sure/esp32-component-dgx-ge
make -C esp32-component-dgx-ge/test/host
```

They take DGX from a checkout next to the engine's
(`../esp32-component-dgx`), or from the managed components of a project the
engine is cloned into; `DGX=<path>` takes any other.

## Documentation

In English and in Russian:

- [docs/overview-en.md](docs/overview-en.md) ([по-русски](docs/overview-ru.md)):
  how the engine is built and how a game is made on it, for someone who has
  never made one;
- [docs/animation-en.md](docs/animation-en.md) ([по-русски](docs/animation-ru.md)):
  the language of the animation scripts;
- [docs/workflow-en.md](docs/workflow-en.md) ([по-русски](docs/workflow-ru.md)):
  the way of a game from its data, whatever made them, to the board;
- [docs/host-en.md](docs/host-en.md) ([по-русски](docs/host-ru.md)):
  a game built and run on the computer.

The API itself is described in the headers, `include/*.h`.

## License

MIT, see [LICENSE](LICENSE).
