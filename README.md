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
Lately it has learned to read whole games from the files of my sprite editor
([dgx-ge-editor](https://github.com/jef-sure/dgx-ge-editor))
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
  its lock given to the mixer, so the game never takes one. Beside the
  samples, music by notes (MML) on the synthesizer the game chooses: a
  chip's square, triangle and noise, the NES's, or FM after the OPL2, with
  sets of instruments as data; the same C builds as WebAssembly for the
  editor, sample for sample.
- **Python tools** (`tools/`) that turn pictures, scripts, dialogs and
  sounds into one pack and a C header for it, also straight from a file of
  my sprite editor ([dgx-ge-editor](https://github.com/jef-sure/dgx-ge-editor)): its sets, animations, tiles,
  scenes, graph and game.

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

## On a board

A frame here is not tied to a rate. Actors live by the game's clock, and the
loop draws as soon as the frame before is out, sending only what changed;
a game says how often it steps (Prince of Persia steps 12 times a second, as
the original did). So what I give is what a frame costs: the time of one
frame that sent anything, and how many pixels of the display it sent.
Measured on 2026-10-23, from the engine of this version, ESP-IDF 6.0.1.
Prince of Persia runs its demo, three rooms in 40 seconds; each of its
numbers is of the log line the firmware writes once a second (the frames
that second sent: their time on average, the slowest, their pixels).

| | CYD (ESP32, 240 MHz; 320×240 over SPI) | Guition JC8012P4A1C (ESP32-P4, 400 MHz; 1280×800 over MIPI-DSI) |
| --- | --- | --- |
| A frame of 320×200 | at 1:1 | at ×4 |
| Prince of Persia in a room: a frame, on average | 5.9–13.0 ms; half of the seconds under 7.4 | 3.0–9.0 ms; half of the seconds under 4.7 |
| …the slowest frame of a second | 21 ms | 16 ms |
| …its pixels | 580–2 800 | 14 000–57 000 |
| …the room drawn anew, entering it | 39–44 ms | 37–41 ms |
| `examples/hello`, the whole frame | 29.3 ms, 64 000 pixels | 21.3 ms, 1 024 000 pixels |
| RAM free once the game runs (PoP / hello) | 190 012 / 226 488 bytes | 29.7 / 27.6 MB, most of it PSRAM |
| RAM the firmware's data take (`.data` + `.bss`; PoP / hello) | 24 844 / 14 396 bytes | 31 244 / 20 796 bytes |
| code kept in internal RAM (PoP / hello) | 52 515 / 52 135 bytes (IRAM, apart from the data's) | 68 392 / 68 392 bytes (DIRAM, shared with the data) |
| flash, code and constants (PoP / hello) | 1.34 MB (the pack 0.97 MB) / 0.24 MB | 1.74 / 0.49 MB |

A game where little moves at once has a lot of room: a frame of a room in
play takes 3 to 13 ms of the 83 ms a step of the game has. A frame where
everything changes costs about 40 ms on both boards. The free RAM is
what the game finds once it runs, after everything static and the frame;
the RAM of the data and the code in internal RAM are what the firmware
takes before it starts, the first of them the part a game's own globals
grow. Both firmwares log their numbers once a second (`room …: N pictures
a second, … us each on average, … at worst, … pixels each`; hello's the
same), so a game of yours can be measured the same way.

## Getting started

You need ESP-IDF 5.3 or newer; I build the engine, the boards and the game
it was checked on with 6.0. The engine is in the ESP Component Registry;
add it to the project's `main`, and DGX comes along by itself:

```sh
idf.py add-dependency "jef-sure/dgx-ge"
```

With no version it takes the newest, and `dependencies.lock` beside the
project keeps the versions taken until `idf.py update-dependencies`.

DGX builds only what the project's `sdkconfig` turns on: the engine's
frame needs its virtual screens (`CONFIG_DGX_ENABLE_VSCREEN`), and the
display its bus and driver; without them the link stops at their missing
functions. Each board's `.cmake` names its own, and the PSRAM a board with
a large display needs for its frame buffers.

The component manager puts it into `managed_components/jef-sure__dgx-ge`
and makes `main` require it, so `main/CMakeLists.txt` needs no `REQUIRES`
for it (if it names it, the name is `jef-sure__dgx-ge`). There, after
`idf_component_register`, the build is told how many params an actor
keeps, as many as the game's pack needs, how many `now` events an
actor's own step may send, waiting in turn (the engine's default, 4,
written out), and which synthesizers of music the firmware carries (the
engine's default, FM, written out; `chip`, `nes`, `fm`):

```cmake
idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
                       EMBED_FILES "assets/game.pack")
dgx_ge_params_of_pack(assets/game_pack.h)
dgx_ge_now_events(4)
dgx_ge_synths(fm)
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
