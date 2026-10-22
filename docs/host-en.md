# A game on the computer: the board `host`

[По-русски](host-ru.md)

A game of DGX-GE is firmware for a board with an ESP32, but it doesn't have
to start there. The engine has one board more than the ones in my drawer:
the computer. Its directory is `boards/host`. The game's own `main.c`, its
pack and its fonts are built for Linux, and a window of SDL2 shows the
display of the board the game is meant for. The mouse in that window is a
finger, the keys are buttons, the sound comes out of the speakers, and what
the game keeps in NVS is stored in files. Nothing in the game changes for
this. The same `app_main` runs, with the computer in place of the board.

I made it so that a game can be written and checked without a board on the
desk, or before the board has arrived, and so that a game can check itself
before a commit with nobody watching. This document explains the board
`host` from start to finish: what to install, where each piece comes from,
what `make` does and where its results go, how each part of a board is
stood in for and why it was done that way, how to run a game with no one at
the computer, what the computer cannot tell you about the board, and what
goes wrong and how to fix it.

I'll be plain about the limits up front. It has been built and run on
Linux with gcc, and once with clang; macOS and Windows have not been tried.
More importantly, nobody has yet sat in front of the window: the window,
the sound, the mouse and the keys have been checked only headless, with
SDL's dummy drivers, a scripted hand (`DGX_GE_HOST_INPUT`), pictures of the
display written to files, and the NVS files read back afterwards. If you
have a screen, the first thing worth trying is `make run` in
`examples/hello`. If something about the window, the sound or the input is
wrong there, it's a part nobody has looked at yet.

## Contents

1. [What it is, and why it is made so](#1-what-it-is-and-why-it-is-made-so)
2. [What to install](#2-what-to-install)
3. [DGX and DGX-DI](#3-dgx-and-dgx-di)
4. [The first run: the example hello](#4-the-first-run-the-example-hello)
5. [A Makefile for your own game](#5-a-makefile-for-your-own-game)
6. [What make does](#6-what-make-does)
7. [What a game may use of ESP-IDF](#7-what-a-game-may-use-of-esp-idf)
8. [How the computer stands in for a board](#8-how-the-computer-stands-in-for-a-board)
9. [With nobody at the computer](#9-with-nobody-at-the-computer)
10. [What the computer cannot tell](#10-what-the-computer-cannot-tell)
11. [The engine's own tests](#11-the-engines-own-tests)
12. [When something goes wrong](#12-when-something-goes-wrong)

---

## 1. What it is, and why it is made so

A game talks to its hardware through one header, `boards/dgx_ge_board.h`
(the overview, section 2.17). It sets up the display there, reads the
touch and the buttons, starts the sound, and shows the on-screen pad and
keyboard. Apart from that header, a game takes very little from ESP-IDF:
the clock (`esp_timer_get_time`), the pause at the end of each pass of its
loop (`vTaskDelay`), the log, `heap_caps_*` memory, and NVS for whatever it
keeps. A computer can provide all of that. So the computer became a board
like the others: one more implementation of `dgx_ge_board.h` (`host.c`),
NVS on files (`host_nvs.c`), small stand-ins for the few headers of ESP-IDF
a game includes (`stubs/`), and one Makefile fragment (`host.mk`) that
builds it all.

The obvious alternative is a separate viewer program for each game, with
its own loop, keys and window. I didn't want that, because such a program
and the firmware drift apart: what you check on the computer would no
longer be what runs on the board. A board for the computer that replaced
`main.c` with a loop of its own would have the same flaw. So the game keeps
its loop, and the board finds its moment inside it. Every firmware loop
has to call `vTaskDelay`, or the task watchdog fires. On the computer,
`vTaskDelay` is where the board shows the display in the window, reads the
mouse and the keyboard, plays the scripted input, and decides whether it's
time to quit. Then it sleeps for as long as the game asked.

The board shows a *display*; the computer's screen doesn't become one. The
window is the display of a particular board, in pixels and in millimetres,
1280x800 and 217x136 mm by default (the Guition JC8012P4A1C, 10″). A game
then picks its scale the way it does on that board, its fonts are made for
that display, and touch gestures and the size of the on-screen pad are
measured in the same millimetres. To see how a game looks on the CYD, you
build it with the CYD's display (section 4).

None of this costs the boards anything. `boards/host` never goes into a
firmware. The real boards gained only `board_buttons_able` (`false`) and an
empty `board_buttons()`, which the computer needed for its keys.

The files:

| File | What it is |
| --- | --- |
| `boards/host/host.mk` | the build: a game's Makefile includes it last |
| `boards/host/host.c` | the board: the window, the mouse, the keys, the sound, the loop's hook, `main()` |
| `boards/host/host_nvs.c` | NVS on files |
| `boards/host/stubs/` | the headers of ESP-IDF the computer stands in for, ahead of DGX's own stubs |
| `boards/dgx_ge_board.h` | the header every board implements, this one included |
| `examples/hello/` | the example that builds both for the computer (`make`) and for the boards (`idf.py`) |

## 2. What to install

On Debian or Ubuntu:

```sh
sudo apt install build-essential libsdl2-dev python3 python3-pil git
```

| Package | What it's for |
| --- | --- |
| `build-essential` | GNU `make` (4.3 or later), gcc (as `cc`), and binutils, whose GNU `ld` embeds the game's files (`ld -r -b binary`) |
| `libsdl2-dev` | SDL2 and its `sdl2-config`, which tells `host.mk` how to compile against SDL and link with it |
| `python3`, `python3-pil` | Python runs the engine's pack scripts and DGX's font maker; Pillow reads the PNGs a pack is made from |
| `git` | to fetch the engine, DGX and DGX-DI, if you take them as clones (section 3) |

A game that asks for fonts of its own (`GAME_FONTS`, section 5) also needs
these. A game that uses only fonts shipped with DGX, like `hello`, doesn't:

```sh
sudo apt install libfreetype-dev pkg-config fontconfig fonts-dejavu-core
```

| Package | What it's for |
| --- | --- |
| `libfreetype-dev` | FreeType: DGX's `font2c`, the program that renders glyphs, is compiled against it during the build |
| `pkg-config` | how `dgx_fonts.py` finds FreeType's flags (on newer releases the package is `pkgconf`; `pkg-config` still installs it) |
| `fontconfig` | `fc-list`, which `dgx_fonts.py` uses to find the faces installed on the computer |
| `fonts-dejavu-core` | an example of faces to draw from; any installed fonts the game's requests and rules allow will do |

These are the same as for fonts made for a board, because they're made the
same way.

`host.mk` needs GNU make 4.3 or later: it and the example's Makefile make
two files by one rule (grouped targets, `&:`, new in 4.3), and `host.mk`
reads a file with `$(file <…)` (section 6). Every current Linux distribution has a newer
one; `make --version` tells which you have.

ESP-IDF isn't needed for the computer. It's needed only for firmware, and
it can be one way to get DGX and DGX-DI (section 3). One warning, which
section 12 explains: **build for the computer in a shell where ESP-IDF's
environment is not active.** That environment puts the chip's `ld` first
on the `PATH`, and a computer's program can't be linked with it. `host.mk`
notices this and stops at once, naming that `ld`.

## 3. DGX and DGX-DI

The engine stands on two other components: DGX, my display library, and
DGX-DI, its display input (touch, the on-screen pad and keyboard). For the
computer, `host.mk` needs DGX 0.8.0 or later. That is the release that
carries `host/dgx_host.mk`, the list of DGX's sources for a computer build,
and `host/stubs`, its stand-ins for ESP-IDF. DGX-DI must be 0.1.1 or later.
I use 0.1.2, the release made for DGX 0.8.0. There are three ways to get
them, and `host.mk` accepts any of them.

**As clones, with no ESP-IDF at all.** Put the three repositories side by
side:

```sh
git clone https://github.com/jef-sure/esp32-component-dgx-ge
git clone --branch 0.8.0 https://github.com/jef-sure/esp32-component-dgx
git clone --branch 0.1.2 https://github.com/jef-sure/esp32-component-dgx-di
```

Then give their directories to `make` as `DGX=` and `DGX_DI=`. The paths
are relative to the directory `make` runs in:

```sh
cd esp32-component-dgx-ge/examples/hello
make DGX=../../../esp32-component-dgx DGX_DI=../../../esp32-component-dgx-di
```

The engine's own tests find the two clones in this layout without being
told (section 11).

**As ESP-IDF downloads them.** In an ESP-IDF project that depends on the
engine, the component manager puts DGX and DGX-DI into
`managed_components/jef-sure__dgx` and `managed_components/jef-sure__dgx-di`
the first time the project is configured (`idf.py reconfigure` or the
first `idf.py build`). When `DGX` and `DGX_DI` aren't given, `host.mk`
looks there, relative to the directory `make` runs in, so in such a project
a plain `make` is enough.

**As components of the project.** If you keep DGX and DGX-DI under
`components/` as `components/jef-sure__dgx` and
`components/jef-sure__dgx-di`, `host.mk` finds them there too, after
`managed_components/`. Under any other name, give the path.

If neither the given path nor the default holds what `host.mk` needs, it
stops before building anything. Section 12 lists its messages.

## 4. The first run: the example hello

`examples/hello` is the smallest whole game of the engine. A robot stands
on the ground. It walks while an arrow is held, hops at a tap or at A or B
with a short chirp, and a label counts the hops. The count is kept in NVS
and survives a restart. What the robot does is its script,
`anim/bot.anim`. Its pictures are `art/*.png`, and `tools/hello_assets.py`
turns both into the pack, `main/assets/hello.pack`, and its header,
`hello_pack.h`. Its `main/main.c` builds for the computer with `make` and
for the CYD and both Guition boards with `idf.py` (its
[README](../examples/hello/README.md) has the board side).

With the clones of section 3:

```sh
cd esp32-component-dgx-ge/examples/hello
make DGX=../../../esp32-component-dgx DGX_DI=../../../esp32-component-dgx-di
```

The example's Makefile looks for the engine's `host.mk` in two places: in
the engine around it (`../../boards/host/host.mk`), and in
`managed_components/jef-sure__dgx-ge`, where ESP-IDF puts the engine when
the example was made from the registry. Elsewhere, give it as
`HOST_MK=<engine>/boards/host/host.mk`.

`make` prints one long `cc` line for each C file: the game's one file, the
board, the engine, eight files of DGX-DI, and DGX with its fonts, 83 in
all. Then it embeds the pack and links:

```text
cc -I…/boards/host/stubs -Imain -I…/include -I…/boards … -c main/main.c -o build-host/obj/…_examples_hello_main_main.o
cc … -c …/boards/host/host.c -o build-host/obj/…_boards_host_host.o
…
cd main/assets/ && ld -r -b binary -z noexecstack -o …/build-host/embed/hello.pack.o hello.pack
cc -std=gnu11 -O2 -g … build-host/embed/hello.pack.o -lSDL2 -lm -o build-host/hello
```

It takes about ten seconds on one core. `make -j` uses all of them. The
program is `build-host/hello`. To run it:

```sh
make run DGX=../../../esp32-component-dgx DGX_DI=../../../esp32-component-dgx-di
```

or just `./build-host/hello`. `make run` builds first if anything changed,
which is why it needs the same `DGX=` and `DGX_DI=`. The window opens, and
the log goes to the terminal (stderr):

```text
I host: a display of 1280x800 pixels, 217x136 mm, in a window of 1280x800
I hello: a frame of 320x200, x4, on a display of 1280x800; sound, a pointer, buttons
```

The game's frame is 320x200 points. On the 10″'s display it's shown four
times larger. The window starts at the largest whole multiple of the
display that fits in nine tenths of your desktop, never less than once, and
you can resize it.

| Key | Button |
| --- | --- |
| arrows | the cross |
| Shift (either) | A |
| Z, X, C | B, X, Y |
| Enter | START |
| Space | SELECT (back) |
| the left mouse button | a finger: a click is a tap, a drag is a swipe |

Closing the window ends the program. The hops are counted in the file
`nvs/hello/hops`, under the directory the program was started from. Delete
`nvs/` to start from zero.

**Another board's display.** The CYD's display is 320x240 pixels, 58x43
mm:

```sh
make HOST_DISPLAY=320x240 HOST_MM=58x43 DGX=… DGX_DI=…
./build-host/hello
```

```text
I host: a display of 320x240 pixels, 58x43 mm, in a window of 640x480
I hello: a frame of 320x200, x1, on a display of 320x240; sound, a pointer, buttons
```

The display's size is compiled into the board, so another display
rebuilds all 83 objects, and so does going back to the 10″. `make` notices
the change by itself, whether it came from the command line or from the
Makefile (section 6). To keep both builds ready at once, give each display
a build directory of its own: `make HOST_BUILD=build-cyd HOST_DISPLAY=…`.

**Changing the art or the script.** `python3 tools/hello_assets.py`
rebuilds the pack. `make` also does it on its own when the script, `art/`
or `anim/` is newer than the pack's header, before it builds anything. It
prints what it runs:

```text
the pack: python3 tools/hello_assets.py
hello.pack: 6196 bytes
```

## 5. A Makefile for your own game

A game that builds for a board with ESP-IDF builds for the computer from a
`Makefile` placed beside its `CMakeLists.txt`. The Makefile names the same
things `main/CMakeLists.txt` names, in make's words, and includes the
engine's `host.mk` last. For a made-up `mygame` that takes the engine from
the registry:

```make
GAME            := mygame
GAME_SRCS       := main/main.c main/world.c
GAME_INCLUDES   := main
GAME_EMBED      := main/assets/mygame.pack
GAME_PACK_H     := main/assets/mygame_pack.h
GAME_PACK_MAKE   := python3 tools/mygame_assets.py
GAME_PACK_INPUTS := tools/mygame_assets.py $(wildcard art/*.png anim/*.anim)
GAME_FONTS      := main/assets/mygame.fonts
GAME_FONT_RULES := fonts.rules
GAME_DEFINES    := BOARD_SCALE=4
HOST_DISPLAY    := 1280x800
HOST_MM         := 217x136
include managed_components/jef-sure__dgx-ge/boards/host/host.mk
```

If the engine sits in your project as `components/dgx-ge` (a clone or a
submodule), include `components/dgx-ge/boards/host/host.mk` instead. Every
path in the Makefile is relative to the directory `make` runs in, which
normally means the project's root.

| Variable | What it means | The same for a board |
| --- | --- | --- |
| `GAME` | the program's name: `build-host/<GAME>`; `game` if not set | the project's name |
| `GAME_SRCS` | the game's C files | `SRCS` in `main/CMakeLists.txt` |
| `GAME_INCLUDES` | the directories of its headers | `INCLUDE_DIRS` |
| `GAME_EMBED` | files embedded in the program whole, with the same symbols as on a board: `mygame.pack` gives `_binary_mygame_pack_start` and `_end` | `EMBED_FILES` |
| `GAME_PACK_H` | the pack's header. Its `<PREFIX>_ANIM_PARAMS` says how many params an actor keeps, and the whole build gets `DGX_GE_ANIM_PARAMS` from it | `dgx_ge_params_of_pack` |
| `GAME_PACK_MAKE` | the command that makes the pack and its header (optional). `make` runs it before anything else when the header is missing or older than one of `GAME_PACK_INPUTS` | — |
| `GAME_PACK_INPUTS` | what the pack is made from: its script, the art, the scripts of animation, any document it reads | — |
| `GAME_FONTS` | the game's font requests, the file the pack script writes with `Pack.write_fonts` (optional) | `dgx_ge_fonts` |
| `GAME_FONT_RULES` | the rules of which installed faces may be taken (optional) | `dgx_fonts_rules` |
| `GAME_FONT_SCALE` | the scale the fonts are made for, in pixels of the display per point of the frame. If not set, `BOARD_SCALE` from `GAME_DEFINES`, else 1 | `dgx_fonts_scale` |
| `GAME_DEFINES` | the game's preprocessor definitions, without `-D` | `target_compile_definitions` |
| `HOST_DISPLAY` | the display shown, in pixels; `1280x800` if not set | the board's display |
| `HOST_MM` | the same display in millimetres, for gestures and the pad's size; `217x136` if not set | the board's `board_mm_w`, `board_mm_h` |
| `DGX`, `DGX_DI` | where DGX and DGX-DI are (section 3) | the component manager |
| `HOST_BUILD` | where everything built goes; `build-host` if not set | `idf.py -B` |
| `HOST_SANITIZE` | `1` builds with AddressSanitizer and UBSan: an index past an array, a read of freed memory, a signed overflow, each reported with its file and line | — |
| `CC` | the C compiler: make's own default, `cc`, or the one given, as in `make CC=clang` | the toolchain |

Only `GAME_SRCS` is strictly needed. The rest depends on what the game
has. `make` builds, `make run` builds and runs, `make clean` deletes
`HOST_BUILD`. Rules of your own can go in the same Makefile before the
`include` line. `host.mk` sets the default goal to the program, so a rule
written first won't become what a plain `make` builds.

Let `GAME_PACK_MAKE` and `GAME_PACK_INPUTS` make the pack, rather than a
make rule of your own or a `$(shell …)` line. The pack's header says how
many params an actor keeps, and that number goes into the command of every
file, so the header must be up to date before `host.mk` reads it. A make
rule runs only after all the Makefiles have been read, which is too late.
`host.mk` runs `GAME_PACK_MAKE` while it is being read, at the one moment
that works (section 6). And keep code that only a board can run under
`#ifdef ESP_PLATFORM` (section 7).

## 6. What make does

`host.mk` is short, and each step in it is there for a reason. Here they
are in order.

**It finds its pieces.** It finds the engine from its own location, so it
works wherever the engine lies. It takes `DGX` and `DGX_DI` as given or
looks in `managed_components/` and `components/`. It checks that DGX has
`host/dgx_host.mk` and that DGX-DI has `include/dgx_di.h`, and otherwise
stops with a message. Then it includes DGX's `host/dgx_host.mk`. That file
is part of DGX's contract: the list of DGX's sources for a computer, in
groups, and the flags they need. DGX builds its own tests from it too. An
earlier version of this board kept its own list of DGX's files, and that
list broke whenever DGX rearranged them. Now DGX's list belongs to DGX.

**It asks SDL for its flags.** `sdl2-config --cflags` gives the include
path (`-I/usr/include/SDL2 -D_REENTRANT` on Debian), and `sdl2-config
--libs` gives the link (`-lSDL2`). Without `sdl2-config` there is no build.
I picked SDL2 because one library covers the window, the sound, the keys
and text input, every Linux distribution packages it, and `sdl2-config`
hands over the flags. I didn't consider SDL3.

**It makes the pack, if it is out of date.** When the game names
`GAME_PACK_MAKE` and the header `GAME_PACK_H` is missing or older than one
of `GAME_PACK_INPUTS`, `host.mk` prints `the pack: <command>` and runs the
command right there, while it is being read. That is before it reads the
header, and so before any file is compiled. If the command fails, `host.mk`
stops with `the pack was not made: <command> failed (it says why above)`,
and the command's own messages are just above it. `make clean` never runs
it.

**It reads the number of params from the pack's header.** The pack script
writes a line like `#define MYGAME_ANIM_PARAMS 2` into the header.
`host.mk` picks the number out with `sed` and compiles everything with
`-DDGX_GE_ANIM_PARAMS=2`, the game and the engine alike, because both must
agree on the size of an actor. CMake's `dgx_ge_params_of_pack` does the
same for a board. The header also guards against a mismatch: if the build
keeps fewer params than the pack needs, it stops with `#error` naming
`dgx_ge_params_of_pack`.

**It makes the fonts, if the game asked for any.** The rule runs DGX's
`font2c/dgx_fonts.py build` on `GAME_FONTS` and writes into
`HOST_BUILD/dgx_fonts/`, with the display shown and the game's scale:

```text
python3 …/font2c/dgx_fonts.py build main/assets/mygame.fonts build-host/dgx_fonts --scale 4 \
    --pixels 1280 800 --mm 217 136
label: DejaVu Sans Book 32 px (8 pt x 4), 10 glyphs
the fonts used and their licenses: …/build-host/dgx_fonts/fonts-used.txt; make sure you may use every one of them
dgx_fonts.c and dgx_fonts.h written
```

A size in points is multiplied by the scale (8 pt at x4 is 32 px), and a
size in millimetres is measured on the display given by `--pixels` and
`--mm`. That's why the fonts follow `HOST_DISPLAY` and `HOST_MM`, as on a
board they follow `dgx_fonts_board`. `dgx_fonts.py` compiles DGX's `font2c`
against FreeType (with `cc`, flags from `pkg-config`), finds faces with
fontconfig's `fc-list`, and writes `dgx_fonts.c`, compiled with the game,
and `dgx_fonts.h`, which declares `dgx_font_<name>()` for each request. It
rewrites them only when they change. `fonts-used.txt` beside them lists
each font the game uses with what its file says of its license. The rule
runs again when the requests, the rules, or the `.letters` and `.typed`
files beside the requests change, and when the display or the scale does. Every object depends on `dgx_fonts.h`,
so the fonts are made before anything is compiled. The overview's section
2.12 says more about the requests and the rules.

**It embeds the game's files.** Each file of `GAME_EMBED` becomes an object
in `HOST_BUILD/embed/`:

```text
cd main/assets/ && ld -r -b binary -z noexecstack -o …/build-host/embed/mygame.pack.o mygame.pack
```

GNU `ld -r -b binary` wraps any file into an object with three symbols,
`_binary_<name>_start`, `_end` and `_size`, where `<name>` is the path *as
given to ld*, with every character that can't be in a C name turned into
`_`. That's why `host.mk` changes into the file's directory and hands `ld`
the bare file name. From the project root, the same file would give
`_binary_main_assets_mygame_pack_start`. From its own directory it gives
`_binary_mygame_pack_start`, which is the symbol ESP-IDF's `EMBED_FILES`
makes on a board, so one `extern` declaration in the game serves both:

```c
extern const uint8_t mygame_pack_start[] asm("_binary_mygame_pack_start");
extern const uint8_t mygame_pack_end[] asm("_binary_mygame_pack_end");
```

`-z noexecstack` marks the object as not needing an executable stack, so
recent linkers don't warn about it. Because only the file's name counts,
two embedded files with one name in different directories would collide.
They would on a board too.

**It compiles every C file into an object of its own.** The files are the
game's `GAME_SRCS`. Then the board, `host.c` and `host_nvs.c`. Then every
`src/*.c` of the engine. Then eight files of DGX-DI: the pointer, its
calibration, the touch, rectangles, the pad and its layouts, the keyboard
and its layouts. Then all of DGX's groups from `dgx_host.mk`: screens,
drawing, virtual screens, text, morphing, the frame buffer, and DGX's
fonts. Last comes `dgx_fonts.c`, if there is one. They aren't put in a
library. All of them are linked, which costs nothing on a computer. Each
object is named by the whole absolute path of its source with `/` turned
into `_`, for example
`build-host/obj/_home_me_mygame_main_world.o`. That looks clumsy, but it's
deliberate. A game's `main/world.c` and `util/world.c`, or a game's
`world.c` and something of the same name in DGX, would otherwise both
become `world.o`, and one would silently replace the other. Next to each
object, `-MMD -MP` writes a `.d` file that lists the headers it included,
so a changed header rebuilds exactly what includes it.

**It puts the include path in a particular order.** The order is the point
of it:

| Order | Directory | Why there |
| --- | --- | --- |
| 1 | the engine's `boards/host/stubs` | the computer's versions of `freertos/task.h`, `esp_timer.h`, `esp_heap_caps.h`, `nvs.h`, `nvs_flash.h` must be found before DGX's |
| 2 | `GAME_INCLUDES` | the game's headers |
| 3 | `HOST_BUILD/dgx_fonts` | `dgx_fonts.h`, if the game has fonts |
| 4 | the engine's `include` and `boards` | the engine, and `dgx_ge_board.h` |
| 5 | DGX-DI's `include` | the pointer, the pad, the keyboard |
| 6 | DGX's `host/stubs` | the rest of ESP-IDF a computer build needs: `esp_err.h`, `esp_log.h`, `freertos/FreeRTOS.h`, `freertos/semphr.h`, `driver/gpio.h` and a few more |
| 7 | DGX's `include` | DGX |
| 8 | SDL's, from `sdl2-config` | SDL |

The first row is the important one. In DGX's stubs, `vTaskDelay` is a
macro that does nothing, which is all DGX's own tests need. The engine's
`freertos/task.h` comes first and declares a real `vTaskDelay`, which
`host.c` defines: the hook of section 1. The engine's `esp_heap_caps.h`
takes DGX's with `#include_next` and adds `heap_caps_get_free_size`, which
games ask for and DGX doesn't stub.

Two headers are forced into every file with `-include`. First comes DGX's
`esp_log.h`, where `ESP_LOGE` and `ESP_LOGW` print and the rest are
silent. Then the engine's `esp_log_host.h` redefines `ESP_LOGI` to print
too, because a game's informational messages are what you watch on the
computer, just as you watch the serial port on a board. `ESP_LOGD` and
`ESP_LOGV` stay silent.

The flags are `-std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter
-Wno-sign-compare -Wno-format`, with `-DHOST_DISPLAY_W`, `_H`,
`-DHOST_MM_W`, `_H` from `HOST_DISPLAY` and `HOST_MM`,
`-DDGX_GE_ANIM_PARAMS` from the pack, and `GAME_DEFINES`. `ESP_PLATFORM`
is not defined. `HOST_SANITIZE=1` adds `-O1 -fsanitize=address,undefined
-fno-omit-frame-pointer`.

**It links.** `cc` links the objects, the embedded files, SDL's libraries
and `-lm` into `HOST_BUILD/<GAME>`.

**What makes it rebuild.** A changed source or header rebuilds what
depends on it. Every object also depends on the Makefiles that were read,
the game's own and `host.mk`, so editing either rebuilds everything.
Everything that goes into the compiler's command is kept as well, in
`HOST_BUILD/config`: the compiler, the include path, the flags, the
definitions with `DGX_GE_ANIM_PARAMS` from the pack, the display shown, the
fonts' scale, and which `ld` is on the `PATH`. At each `make`, `host.mk`
compares the command it would use now with that file and rewrites the file
only when they differ. Every object, the embedded files and the fonts
depend on it. So `make` again does nothing, while
`make HOST_DISPLAY=320x240`, `make CC=clang`, other `GAME_DEFINES`, or a
pack whose actors keep more params rebuild everything. No object is left
compiled the old way next to new ones, which matters most for the params:
the game and the engine must agree on the size of an actor. Because the
pack is made before its header is read, one `make` is enough: if an actor
gains params, that same run remakes the pack and builds everything with
the new number.

**Where things end up:**

```text
build-host/
├── mygame                 the program
├── config                 the compiler's command as last used: a change of it rebuilds everything
├── obj/                   an .o and a .d per C file, named by its whole path
├── embed/mygame.pack.o    the embedded files
└── dgx_fonts/             dgx_fonts.c, dgx_fonts.h, fonts-used.txt, font2c and its working files
nvs/                       what the game keeps, created where the program runs (section 8)
```

## 7. What a game may use of ESP-IDF

On the computer, a game has everything `dgx_ge_board.h` gives it, plus
this much of ESP-IDF:

| What | On the computer |
| --- | --- |
| `esp_timer_get_time()` | the computer's monotonic clock, in microseconds. Like a board's, it doesn't start at 0 |
| `vTaskDelay(ticks)`, `pdMS_TO_TICKS` | the pause of the loop, and the board's hook (section 8). One tick is one millisecond, as on a board with `CONFIG_FREERTOS_HZ=1000` |
| `ESP_LOGE`, `ESP_LOGW`, `ESP_LOGI` | printed to stderr as `E tag: …`, `W tag: …`, `I tag: …` |
| `ESP_LOGD`, `ESP_LOGV` | silent |
| `heap_caps_malloc`, `_calloc`, `_realloc`, `_free` | plain `malloc` and friends; the `MALLOC_CAP_*` flags are accepted and ignored |
| `heap_caps_get_free_size`, `heap_caps_get_largest_free_block` | always 1 GiB |
| `esp_err_t`, `ESP_OK`, `ESP_FAIL`, the common `ESP_ERR_*` | as in ESP-IDF; `esp_err_to_name` says only `ESP_OK` or `error` |
| NVS: `nvs_flash_init`, `_deinit`, `_erase`, `nvs_open`, `nvs_close`, `nvs_commit`, `nvs_erase_key`, `nvs_erase_all`, `nvs_set_`/`nvs_get_` `blob`, `str`, `i8` … `u64` | on files (section 8), with ESP-IDF's error codes |

That covers what a game loop usually needs. The rest of ESP-IDF isn't
there: tasks of its own (`xTaskCreate`), queues, timers of FreeRTOS, GPIO,
I2C, SPI, Wi-Fi, `sdkconfig.h` and its `CONFIG_*` values, anything that
talks to hardware past the board. DGX's stubs do declare a few such things
(`driver/gpio.h` with functions that do nothing, a binary semaphore that
exits the program if taken before it was given), but only so that DGX
itself compiles. A game shouldn't lean on them. Its code that needs the
real things goes under `#ifdef ESP_PLATFORM`. ESP-IDF defines that symbol
and the computer build doesn't:

```c
#ifdef ESP_PLATFORM
    xTaskCreate(radio_task, "radio", 4096, NULL, 5, NULL);
#endif
```

## 8. How the computer stands in for a board

This is each function of `dgx_ge_board.h` as the computer does it, with
the reason for each choice.

| The board | On the computer |
| --- | --- |
| `board_display_init` | starts SDL's video, opens the window and makes the display: a 16-bit virtual screen of DGX of `HOST_DISPLAY`. `board.lcd` is that screen, `board.bus` is `NULL` |
| `board_band` | 0: the display is memory, so the frame goes to it a row at a time |
| `board_mm_w`, `board_mm_h` | `HOST_MM` |
| `board_orient`, `board_turn` | the window lies on a desk and isn't turned round: `board_turn` does nothing, and `board_orient` refuses upside down, so a frame upside down flips its rows itself, as on the CYD |
| `board_pointer_start`, `board_pointer` | the mouse, through DGX-DI's pointer as on the touch boards |
| `board_touch_device` | the mouse as a device of DGX-DI, one finger, its range the display's |
| `board_pad_able`, `board_pad_*` | `true`: DGX-DI's pad, shown over the game and pressed with the mouse |
| `board_keyboard_able`, `board_keyboard_*` | `true`: DGX-DI's keyboard, pressed with the mouse; the computer's keyboard types into it as well |
| `board_buttons_able`, `board_buttons` | `true`: the keys of the computer. The three real boards answer `false` and 0 |
| `board_sound_rate`, `board_sound_volume` | 22050 Hz; 32 |
| `board_sound_start` | starts SDL's audio on its own: mono, 16 bits, a buffer of 512 frames, pulling the mixer; `false` when there is no audio, and the game runs silent |

**The display and the window.** The game draws on a 16-bit virtual screen
of the board's size, just as it draws on the board's display. The colors
are the same 16-bit ones the board shows, so a gradient bands the way it
will there. When the board's hook runs, at most every 15 ms, it converts
the screen to 32-bit pixels, uploads them as a texture, and presents it
with a vsync renderer, or a software renderer if no vsync renderer can be
made. SDL's logical size keeps the display's proportions when the window is
stretched, with bars at the sides or top and bottom, and scales by nearest
pixel, so pixel art stays sharp. The logical size also means SDL reports
mouse positions in pixels of the display, whatever the window's size. A
click in the bars counts as a click at the nearest edge.

**The mouse is one finger.** While the left button is down, the finger is
on the display where the mouse points. Moving with the button down is a
drag. The mouse is a device of DGX-DI, read through the same pointer the
touch boards use, so taps, swipes and their thresholds in millimetres
behave as they do there. The same goes for the pad and the keyboard of
DGX-DI, which you press with the mouse as you would with a finger. A mouse
has only one finger, though: two pad buttons at once, or a gesture of two
fingers, can't be made with it. The keys can hold several buttons at once.

**The keys are the board's own buttons.** The real boards have no buttons
of their own. A game takes their buttons from the on-screen pad, and the
computer is the first board with `board_buttons_able`. I gave the keys
this role instead of showing the pad all the time, because a keyboard is
natural at a computer and a pad would cover the picture. A game that ORs
`board_buttons()` with `board_pad_buttons()` works on both kinds of board.
`hello` shows its pad only where the board has no buttons, so on the
computer it doesn't show one.

| Key | `DGX_GE_KEY_*` |
| --- | --- |
| Left, Right, Up, Down | `LEFT`, `RIGHT`, `UP`, `DOWN` |
| left or right Shift | `A` |
| Z | `B` |
| X | `X` |
| C | `Y` |
| Enter | `START` |
| Space | `SELECT` |

**Typing.** While DGX-DI's keyboard is open on the display, the computer's
keyboard types into it. Letters arrive as their code points (anything SDL
gives as text, so other layouts too). Backspace gives U+0008 (erase),
Enter or the keypad's Enter gives U+000A (done), and Esc gives U+001B
(cancel), the same codes the on-screen keyboard gives. Meanwhile
`board_buttons()` returns 0, so typing a Z doesn't also press B. Scripted
buttons are held back as well.

**The sound.** SDL's audio thread calls back for 512 samples at a time, and
the callback has the engine's mixer fill them, much as a board's I2S task
does. The mixer is touched from two threads, the game's and SDL's, so it
needs a lock. The board gives it a mutex of its own, an `SDL_mutex`,
rather than SDL's audio-device lock, because the mixer takes its lock
inside the audio callback too (`dgx_ge_mixer_fill`), where SDL already
holds the device's lock. The mutex is in place before the audio device
starts, so even the first fill is guarded. The volume is 32 of 255, a
little louder than the 10″ plays through its small speaker.

The sound is started apart from the display: video in
`board_display_init`, audio in `board_sound_start`. A computer with no
sound, or an audio driver that can't start, is no reason to lose the
picture. The log says so, `board_sound_start` returns `false`, and the
game goes on with its display, pointer and buttons, only silent:

```text
I host: a display of 1280x800 pixels, 217x136 mm, in a window of 1280x800
W host: no sound: Audio target 'nonesuch' not available
I hello: a frame of 320x200, x4, on a display of 1280x800; a pointer, buttons
```

**NVS on files.** A namespace is a directory, and a key is a file in it,
under `nvs/` in the directory the program was started from, or under the
directory `DGX_GE_HOST_NVS` names. The first byte of each file is the
type: `b` for a blob, `s` for a string (stored with its terminating 0, as
NVS counts it), or `'0'` plus the size in bytes for a whole number. The
value follows. `hello`'s count of three hops looks like this:

```sh
$ od -An -tx1 nvs/hello/hops
 34 03 00 00 00
$ od -An -tu4 -j1 nvs/hello/hops
          3
```

The behavior matches a board where it matters to a game. Opening a
namespace read-only before anything was written to it gives
`ESP_ERR_NVS_NOT_FOUND`. Reading a key as another type gives
`ESP_ERR_NVS_TYPE_MISMATCH`. A buffer smaller than the value gives
`ESP_ERR_NVS_INVALID_LENGTH` and the size it needs. Writing to a handle
opened read-only gives `ESP_ERR_NVS_READ_ONLY`. Names are at most 15
characters. A value is written whole when it's set, to a file beside the
key that is then renamed over it, so a crash can't leave half a value, and
`nvs_commit` has nothing left to do. `nvs_erase_all` deletes the files of
a namespace, and `nvs_flash_erase` deletes the whole directory. At most 16
handles may be open at once. One difference from a board: a signed and an
unsigned number of one size share a type here, so a `u32` read as an
`i32` succeeds on the computer and is a mismatch on a board. I didn't
reproduce NVS's pages and entries. To a game, files behave the same, and
you can read and delete them by hand.

**The loop.** `main()` of the board calls the game's `app_main()`. Each
`vTaskDelay` the game calls then does the board's work, in this order: it
handles the window's events (the mouse, typed text, the window being
closed), plays whatever is due of `DGX_GE_HOST_INPUT`, quits if
`DGX_GE_HOST_QUIT_MS` has passed, shows the display if 15 ms have passed
since it was last shown, and finally sleeps for the ticks asked. A game
that never calls `vTaskDelay` freezes the window (section 12).

**The end of the program.** Closing the window, Ctrl+C in the terminal, or
`kill` all reach the program as SDL's quit event. It ends with status 0,
writing the display to `DGX_GE_HOST_SHOT` first if that is set. If
`app_main` returns, the game has given up (it should have said why in the
log). The board then logs `E host: app_main returned` and ends with status
1. If the display was never set up, or `DGX_GE_HOST_QUIT_MS` is set, it
ends at once. Otherwise it keeps the window open until it is closed, so you
can see what the game left on it.

## 9. With nobody at the computer

Four environment variables let a game run without anyone at the keyboard.
That's useful for a check before a commit or in CI, or for a picture for
the docs:

| Variable | What it does |
| --- | --- |
| `DGX_GE_HOST_QUIT_MS=3000` | the program ends this many milliseconds after the display was set up (`board_display_init`) |
| `DGX_GE_HOST_SHOT=game.ppm` | when the program ends, the display is written to this file as a PPM image |
| `DGX_GE_HOST_INPUT="…"` | the hand: buttons and taps at given moments (below) |
| `DGX_GE_HOST_NVS=dir` | where NVS lives instead of `./nvs`, e.g. a fresh temporary directory for each check |

Two of SDL's own variables complete the set. `SDL_VIDEODRIVER=dummy`
opens no window, and `SDL_AUDIODRIVER=dummy` plays no sound. Everything
else runs as usual: the game draws on its display, the mixer is pulled,
NVS is written, and the shot is taken from the display as it is.

The hand is a list of moments separated by commas, each a time in
milliseconds from the moment the display was set up, in increasing order:

| Moment | Meaning |
| --- | --- |
| `300 right+` | from 300 ms on, the button is held |
| `1300 right-` | at 1300 ms it's let go |
| `2400 tap 640 400` | at 2400 ms a finger touches the display at x 640, y 400 (pixels of the display, not points of the frame) for 60 ms |

The button names are those of `DGX_GE_KEY_*` in lower case: `left right up
down a b x y start select`. Several can be held at once. A tap holds the
finger down for 60 ms because DGX-DI's pointer steadies a touch over a few
reads before it believes in it, and a shorter touch might not register.
The moments are checked in `vTaskDelay`, so they land with the precision of
the game's loop, 20 ms in `hello`.

A moment the board can't read stops the hand there: the moments after it
are never played, and the log says where it stopped. A name that isn't a
button, or a button without its `+` or `-`, is such a moment:

```text
E host: DGX_GE_HOST_INPUT: "jump" is no button held (+) or let go (-), nor a tap, at "100 jump+, 200 a+"
E host: DGX_GE_HOST_INPUT: "right" is no button held (+) or let go (-), nor a tap, at "100 right"
```

So is text that isn't a moment at all, a name in capitals or a missing
time: `E host: DGX_GE_HOST_INPUT: not understood at "…"`.

A run of `hello`: walk right for a second, hop with A, hop with a tap, and
take a picture at the end:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
DGX_GE_HOST_INPUT="300 right+, 1300 right-, 1600 a+, 1650 a-, 2400 tap 640 400" \
DGX_GE_HOST_QUIT_MS=3000 DGX_GE_HOST_SHOT=hello.ppm ./build-host/hello
```

```text
I host: a display of 1280x800 pixels, 217x136 mm, in a window of 1280x800
I hello: a frame of 320x200, x4, on a display of 1280x800; sound, a pointer, buttons
I host: the display written to hello.ppm
```

The PPM is the whole display, 1280x800 here, about 3 MB. Image viewers
open it, and Pillow turns it into a PNG:

```sh
python3 -c "from PIL import Image; Image.open('hello.ppm').save('hello.png')"
```

A check is a run followed by a look at what the game left behind: its exit
status, its NVS, its picture. This one fails unless two hops were counted.
It uses `timeout -s KILL` because a frozen game ignores gentler signals
(section 12):

```sh
#!/bin/sh
set -e
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
export DGX_GE_HOST_NVS="$(mktemp -d)"
DGX_GE_HOST_INPUT="300 a+, 350 a-, 900 b+, 950 b-" DGX_GE_HOST_QUIT_MS=2000 \
    timeout -s KILL 30 ./build-host/hello
test "$(od -An -tu4 -j1 "$DGX_GE_HOST_NVS/hello/hops" | tr -d ' ')" = 2
echo "hello: two hops counted"
```

The exit status tells whether the game kept going: 0 when it ran until
`DGX_GE_HOST_QUIT_MS`, 1 when `app_main` gave up. Comparing shots
against a reference picture of your own takes a few more lines and works
the same way.

## 10. What the computer cannot tell

The computer shows how a game looks, plays, sounds, and what it keeps. It
can't show whether the game fits on the board and keeps up there:

- **Speed.** The computer is many times faster, has no PSRAM, and has no
  bus to the display. The time a frame takes on it says nothing about the
  board.
- **Memory.** It doesn't run out, and `heap_caps_get_free_size` answers 1
  GiB. Whether the board's RAM is enough shows on the board only.
- **The board's display**: its bus, its refresh, its tearing.
- **Several fingers at once**: the mouse is one finger.
- **The board's hardware**: everything under `#ifdef ESP_PLATFORM`, the
  task watchdog, the board's own sound chip.
- **The signedness of NVS numbers** (section 8).

There are also things nobody has seen yet. The window, the sound, the mouse and
the keyboard have been run only with the dummy drivers and the scripted
hand. Only Linux has been tried: gcc throughout, clang once (it builds and
runs, with a few warnings in DGX's own files). macOS's linker, ld64, has no
`-b binary`, so the embedding step would need another way there. Windows
under MSYS2 hasn't been tried.

## 11. The engine's own tests

The engine's tests are a separate thing from a game on the computer. They
check the engine's C code piece by piece, with sanitizers, and they come
only with a clone of the engine's repository. The registry's package
doesn't carry them.

```sh
make -C esp32-component-dgx-ge/test/host
```

They find DGX among the managed components of the project the engine lies
in, or in a neighbouring `esp32-component-dgx`, and DGX-DI the same way.
Another copy can be named with `DGX=<path>` and `DI=<path>`. Without
DGX-DI, the one test that needs it is skipped. With the three clones of
section 3 side by side, the command above needs nothing more. Each test
prints `ok`, and all of them take about half a minute with `-j8`.

## 12. When something goes wrong

**`host.mk` stops before building.** These are its own messages:

| Message | Cause | Fix |
| --- | --- | --- |
| `DGX: no host/dgx_host.mk under '': DGX 0.8.0 or later, its directory as DGX=.` | DGX isn't in `managed_components/` or `components/` (the quotes are empty: nothing was found) | give `DGX=<path>`, or let ESP-IDF download it (section 3) |
| `DGX: no host/dgx_host.mk under '<path>': …` | the directory isn't DGX, or it's a DGX older than 0.8.0, which has no `host/` | point at DGX 0.8.0 or later |
| `DGX_DI: no DGX-DI under '<path>': its directory as DGX_DI=.` | no `include/dgx_di.h` there | give `DGX_DI=<path>` to DGX-DI 0.1.1 or later |
| `SDL2: no sdl2-config (libsdl2-dev, or SDL2 of the package manager).` | SDL2's development files aren't installed, or `sdl2-config` isn't on the `PATH` | `sudo apt install libsdl2-dev` |

**`the pack was not made: <command> failed (it says why above)`.** The
pack script failed. Its own message is just above, usually a typo in an
animation or a missing picture, named with its file and line. Fix it and
run `make` again.

**`sed: can't read main/assets/mygame_pack.h: No such file or
directory`**, followed by an `#error` from the header or by actors
misbehaving. `GAME_PACK_H` names a header that doesn't exist, and the
Makefile doesn't say how to make it. Set `GAME_PACK_MAKE` and
`GAME_PACK_INPUTS` (section 5), or build the pack by hand first.

**`ld is ESP-IDF's (…/.espressif/…/bin/ld): build for the computer in a
shell without ESP-IDF activated.`** The shell has ESP-IDF's environment
activated, and its `PATH` puts the chip's `ld` first. The embedding step
would make objects for the chip with it, and `cc` would take it for the
link, which fails. `host.mk` stops before either happens. Open a new shell
without ESP-IDF and run `make` there. Nothing needs cleaning: the `ld` in
use is part of `HOST_BUILD/config` (section 6), so a build begun with the
wrong one is built again, embedded files included.

**`no host.mk of the engine: an idf.py build of this example downloads it
into managed_components/, or HOST_MK=<engine>/boards/host/host.mk.`** The
example's Makefile can't find the engine (section 4). In a project made
from the registry, run `idf.py reconfigure` (or `idf.py build`) once so
ESP-IDF downloads the engine, or give `HOST_MK=`.

**Fonts.** `dgx_fonts.py: fc-list: No such file or directory: fontconfig
is required to find fonts` means fontconfig is missing: `sudo apt install
fontconfig`. `dgx_fonts.py: the FreeType development files (libfreetype-dev)
are not installed, so font2c cannot be built: Package freetype2 was not
found …` means `sudo apt install libfreetype-dev pkg-config`. When a face
can't be found or isn't allowed, `dgx_fonts.py` names the request and the
rule. `python3 …/font2c/dgx_fonts.py list <rules> <size>` shows which
faces a rule would accept. A game without `GAME_FONTS` needs none of this.

**The window is frozen and the program won't stop.** The game's loop
doesn't call `vTaskDelay`. That's the only place where the board shows the
display and reads events, so nothing is drawn, nothing is read, and
`DGX_GE_HOST_QUIT_MS` is never checked. Even Ctrl+C and `kill` do nothing:
SDL turns them into a quit event, and nobody reads it. `kill -KILL` stops
it. The fix is in the game: call `vTaskDelay` once per pass of the loop, as
a firmware must anyway, at least `vTaskDelay(1)`. A loop that waits on
something of its own (a flag set by a task, a semaphore) will hang here
too.

**`E host: SDL: …` at start, then the game's own "no display".** SDL
couldn't start its video: there is no desktop to open a window on. On such
a machine, as in CI, set `SDL_VIDEODRIVER=dummy`.

**`W host: no sound: …`.** SDL's audio couldn't start. The game runs on
without sound (section 8). In CI, `SDL_AUDIODRIVER=dummy` keeps the sound
path running without a device.

**`E host: DGX_GE_HOST_INPUT: …`.** The scripted hand stopped at the moment
the message quotes: a name that isn't a button, a button without `+` or
`-`, a name in capitals, a missing time (section 9). The moments after it
are never played, so a check that relies on them will fail further on.

**`make` stops at `host.mk` or the game's Makefile on `&:` or `$(file <…)`.**
The make is older than 4.3, which brought grouped targets (`&:`). Install a
newer GNU make.
`make --version` tells which you have.

**The program ends with status 1 and `E host: app_main returned`.** The
game gave up. The line above it, from the game, usually says why: no
display, a pack that doesn't hold together, no memory for the frame.
Without `DGX_GE_HOST_QUIT_MS`, the window stays open until it's closed. In
a check, always set `DGX_GE_HOST_QUIT_MS`, so a game that gave up ends at
once.

**A crash, or memory going wrong.** Build with sanitizers into a directory
of its own and run the same thing:

```sh
make HOST_SANITIZE=1 HOST_BUILD=build-asan
./build-asan/mygame
```

AddressSanitizer reports a bad read or write with the file, the line and
the stack. UBSan reports overflows and bad shifts.

**macOS.** The build stops at the embedding step, because the system's
linker has no `-b binary`. GNU binutils installed as `gld`, or a small
script that writes each file as a C array with the same symbol names,
might get past it, but neither has been tried. Linux (or a Linux virtual
machine) is the way that is known to work.

---

The overview of the engine is [overview-en.md](overview-en.md): the board
header in section 2.17, fonts in 2.12, input in 2.14, saving in 2.15, sound
in 2.16. The animation language is described in
[animation-en.md](animation-en.md). The example has its own
[README](../examples/hello/README.md), and the engine's
[README](../README.md) is where to start.
