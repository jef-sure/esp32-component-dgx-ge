# A game on the computer: the board `host`

[По-русски](host-ru.md)

A game of DGX-GE is firmware for a board with an ESP32, but it doesn't have
to start there. One of the boards of
[jef-sure/boards](https://github.com/jef-sure/esp32-component-boards) is the
computer: the board `host`. The game's own project, with its `main.c`, its
pack and its fonts, is built by ESP-IDF for its `linux` target, and a window
of DGX shows the display of the board the game is meant for. The mouse in
that window is a finger, the keys are buttons, the sound comes out of the
speakers, and what the game keeps in NVS stays from one run to the next.
Nothing in the game changes for this. The same `app_main` runs, with the computer in
place of the board.

Up to DGX-GE 0.4.0 the engine had a computer board of its own,
`boards/host`, built by `make` with the engine's `host.mk`. It went away
with the engine's other boards (CHANGES). The board `host` of jef-sure/boards
builds the firmware's project itself, so there is no Makefile and there are
no stand-ins for ESP-IDF to keep in step. Since jef-sure/boards 0.0.5 and
DGX 0.10.11 it has what the engine's host had: the keys, a script of keys
and the mouse for a run with nobody at the computer, NVS kept. Typing on the
keyboard on the display with the computer's keys it has not yet (section 8).

## Contents

1. [What it is, and why it is made so](#1-what-it-is-and-why-it-is-made-so)
2. [What to install](#2-what-to-install)
3. [The first run: the example hello](#3-the-first-run-the-example-hello)
4. [Your own game on the computer](#4-your-own-game-on-the-computer)
5. [How the computer stands in for a board](#5-how-the-computer-stands-in-for-a-board)
6. [What a game may use of ESP-IDF](#6-what-a-game-may-use-of-esp-idf)
7. [With nobody at the computer](#7-with-nobody-at-the-computer)
8. [What the computer cannot tell, and what it has not yet](#8-what-the-computer-cannot-tell-and-what-it-has-not-yet)
9. [The engine's own tests](#9-the-engines-own-tests)
10. [When something goes wrong](#10-when-something-goes-wrong)

---

## 1. What it is, and why it is made so

A game talks to its hardware through `boards.h` of jef-sure/boards (the
overview, section 2.17). It sets up the display there, starts the touch and
the sound, and asks for the parts by kind, never by the name of a board.
ESP-IDF has a target for the computer, `linux`: the same project builds as
a program of the computer, with FreeRTOS (its tasks are threads there), the
log, the clock, NVS and the heap. What it doesn't have is the chip's
drivers. The board `host` fills that gap with the computer's own:

- the display is a window of DGX (`dgx_host_screen_init`): a panel behind a
  bus, as on the board shown, whose pixels go into the glass of the window
  in place of a wire, the glass turned as the board is held;
- the touch is the mouse, read by DGX-DI as a finger (DGX-DI 0.1.5 or
  later);
- the buttons are keys of the computer (DGX 0.10.11 or later);
- the sound is SDL's audio, fed by a task as on a board;
- the flash, and NVS on it, is a file kept beside the program.

So what runs on the computer is the firmware, not a viewer of the game: the
same `sdkconfig`, the same components, the same loop. The game keeps its
loop, and the window finds its moment inside it. Every firmware loop calls
`vTaskDelay`, or the task watchdog fires; on the computer DGX takes that
call over (the linker's `--wrap`), and the task that made the window shows
it there and reads the mouse, then sleeps as long as the game asked. A game
that ends each pass of its loop with the engine's `dgx_ge_pace(t0,
period_us)` is right as it is: that ends in `vTaskDelay` too. The keys are
read there as well.

The window shows a *display*; the computer's screen doesn't become one. It
is the display of a particular board, in pixels and in millimetres, the one
`board_select.py host --as <board>` named: `examples/hello` shows the 10″,
1280x800 and 217x136 mm, lying as it is held. `board_display_init` gives the
width, the height and the orientation it gives on that board. A game picks
its scale the way it does there, its fonts are made for that display, and
the on-screen pad is measured in the same millimetres.

DGX's window also counts what a board would pay: how long the bytes the
game sends would take on the wire of the board shown, when its display is
on SPI or QSPI (the CYD, the 4.3″; a display on MIPI-DSI has no wire to
count), and how much memory the program holds. It doesn't tell how long the
chip would take to draw them (section 8).

| Where | What it is |
| --- | --- |
| `src/board_host.c` of jef-sure/boards | the board: the window, the mouse, the keys, the sound |
| `sdkconfig.defaults.host` of the project | the board chosen (`CONFIG_BOARD_SELECT_HOST`), the display shown, its bus and its buttons, the stack of `app_main`: written by `board_select.py host --as <board> .` |
| `build/host/flash.bin` | the flash of the program, NVS on it, kept between runs |
| `CMakePresets.json` of the project | the preset `host`, building in `build/host` |
| `CMakeLists.txt` of the project | two lines for the computer (section 4) |
| `host/window/` of DGX | the window, the wire's count, the memory's count |

## 2. What to install

On Debian or Ubuntu, beside ESP-IDF 5.5 or later (its `linux` target comes
with it; jef-sure/boards needs 5.5), with jef-sure/boards 0.0.5 or later,
which takes DGX 0.10.11 and DGX-DI 0.1.5:

```sh
sudo apt install build-essential libsdl2-dev pkg-config python3-pil
```

| Package | What it's for |
| --- | --- |
| `build-essential` | gcc and binutils of the computer: the `linux` target builds with them, not with the chip's compiler |
| `libsdl2-dev`, `pkg-config` | SDL2, which DGX finds by `pkg-config` and links into the program |
| `python3-pil` | Pillow for the system's python3, which makes the pack (ESP-IDF's python has none) |

A game that asks for fonts of its own needs FreeType and fontconfig too, as
it does for a board: `sudo apt install libfreetype-dev fontconfig
fonts-dejavu-core`.

The build runs in a shell with ESP-IDF activated, as for a board. One thing
there gets in the way: the activation puts the inner `bin` directories of
the chip's toolchains on the `PATH`, and they have an `ld` of their own,
which the computer's compiler would take. The project's `CMakeLists.txt`
tells it to look in `/usr/bin` first (section 4).

## 3. The first run: the example hello

`examples/hello` is the smallest whole game of the engine. A robot stands
on the ground. It walks while an arrow is held, hops at a tap or a button
with a short chirp, and a label counts the hops. Its data are a game of the
sprite editor (`game_data/`), and its `main/main.c` builds for four boards
and for the computer (its [README](../examples/hello/README.md)).

```sh
cd esp32-component-dgx-ge/examples/hello
idf.py --preset host build
./build/host/hello.elf
```

ESP-IDF 5 doesn't read the presets: `idf.py -B build/host -D
SDKCONFIG=build/host/sdkconfig -D BOARD=host build`. The first build
downloads jef-sure/boards, DGX and DGX-DI into `managed_components/` and
takes about a minute; then only what changed is built again.

The window opens, and the log goes to the terminal:

```text
I (…) DGX HOST: window 1: a glass of 800x1280, 16 bits; no bus of the board counted
I (…) hello: hops 0
I (…) hello: a frame of 320x200, x4, on a display of 1280x800 of host; sound, a pointer, buttons
I (…) DGX HOST: window 1: the glass 1 times as large, 1280x800
```

The glass of the 10″ is 800x1280, upright as its panel is; the window turns
it as the board is held, lying, and the game draws on a display of
1280x800, its frame of 320x200 points four times larger. The window shows
the glass at the largest whole multiple that fits the desktop, and may be
stretched. The arrows (or WASD) walk, Space or J (A) and K (B) hop, and so
does B, the board's BOOT; the mouse is a finger, a click a hop. The pad on
the display isn't shown: the computer has arrows of its own. Closing the
window ends the program, and DGX says what the run cost:

```text
I DGX HOST: window 1, 800x1280, no bus counted
I DGX HOST: memory: 79768 bytes held now, 79848 at most at once, the largest block 64000
```

The 10″'s display is on MIPI-DSI, so there is no wire to count. The hops are
kept in NVS, in `build/host/flash.bin`: the next run starts with the count
this one left. Delete the file to start from zero.

**Another board's display.** `board_select.py` of jef-sure/boards names the
board shown; the CYD's is 320x240 pixels, 58x43 mm, on SPI at 40 MHz, whose
wire the window counts:

```sh
python3 managed_components/jef-sure__boards/tools/board_select.py host --as cyd_s028r .
rm build/host/sdkconfig
idf.py --preset host build
```

ESP-IDF reads the defaults only into a new `sdkconfig`, so the one made
before has to go.

**Changing the data.** Open `game_data/` in the editor, or change
`BOT.anim` as text. The build makes the pack again whenever anything in
`game_data/` or `tools/pack.py` changed, as it configures. By hand: `python3
tools/pack.py ../..` (the engine's folder).

## 4. Your own game on the computer

`tools/dgx_ge_new.py` of the engine makes a game that builds for the
computer as it is (the README). By hand, a project takes three things.

The board, chosen by `board_select.py` of jef-sure/boards, the display shown
named by `--as`:

```sh
python3 <boards>/tools/board_select.py host --as jc8012p4a1c .
```

It writes `sdkconfig.defaults.host`: the target `linux`, the board,
the display's size, and `CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384` (a word of
a stack is 8 bytes on the computer, so 128 KB: the task that makes the
window needs 48 KB of it left for SDL). It adds the preset `host` to
`CMakePresets.json`.

Two lines in the project's `CMakeLists.txt`, before `project.cmake`, with
the block that reads `sdkconfig.defaults.${BOARD}` (as `examples/hello`
has it):

```cmake
if(BOARD STREQUAL "host")
	set(COMPONENTS main)
	set(CMAKE_EXE_LINKER_FLAGS "-B/usr/bin/")
endif()
```

`COMPONENTS main` builds `main` and what it requires, nothing else: not
every component of ESP-IDF builds for the `linux` target, and without it
the build stops on the first that doesn't. So `main/CMakeLists.txt` names
what the game uses (`nvs_flash`, `esp_timer`, the engine). `-B/usr/bin/`
makes the computer's compiler look for its linker in `/usr/bin` before the
`PATH` (section 2).

The game itself needs nothing more. Its parts are asked for as on any board,
its buttons by their names (`board_button_find`, `board_button_name`; the
engine's `dgx_ge_key_named` gives the bit of a gamepad's name), and a part
the computer has not answers `false` there as it does on a board without it.

## 5. How the computer stands in for a board

| `boards.h` | On the computer |
| --- | --- |
| `board_display_init`, `board_dgx_screen` | the window of DGX, a 16-bit panel, the glass of the board shown turned as it is held; the width, the height, the orientation and the millimetres (`mm_w`, `mm_h`) as on the board |
| `board_dgx_bus` | `NULL`: a game gives its frame a strip at a time, as to a panel on a bus |
| `board_display_orientation` | turns the panel in the window as on the board |
| `board_touch_start`, `board_dgx_di_pointer` | the mouse, a finger of DGX-DI while its left button is held on the window, in pixels of the glass, with no calibration; it turns with the display |
| `board_button_count`, `_name`, `_find`, `_at` | keys: the board's own buttons first, then those of a gamepad it has not (below) |
| `board_sound_init`, `board_sound_start` | SDL's audio: 22050 Hz, mono, 16 bits; a task of FreeRTOS, `sound`, asks the source |
| NVS | ESP-IDF's own, on a flash kept in `build/host/flash.bin` |
| `heap_caps_*`, `esp_timer_get_time` | DGX's window answers them: the memory it counts, the computer's clock |

**The display and the wire.** The game draws on the screen of DGX as on the
board's, and what it sends goes through a bus and its buffer into the glass.
The title of the window and the report at the end say how long those bytes
would take on the board's wire, a pass of the loop at a time: the SPI or
QSPI of the board shown, at its clock. A display on MIPI-DSI keeps its
picture in memory and has no wire; nothing is counted for it.

**The mouse is one finger.** While the left button is held on the window,
the finger is on the glass where the mouse points; outside the glass it lies
on the nearest edge. It is a device of DGX-DI read through the same pointer
a touch board gives, so taps, swipes and their thresholds in millimetres
behave as there, and so do the engine's pad and keyboard on the display. A
mouse has one finger only: two buttons of the pad at once, or a gesture of
two fingers, can't be made with it.

**The keys are the buttons.** A button of the board shown that has the name
of a gamepad's is on that button's keys; another is on the key of its first
letter (BOOT on B). Then come the buttons of a gamepad the board has not:

| Button | Keys |
| --- | --- |
| `up`, `left`, `down`, `right` | W, A, S, D and the arrows |
| `a` | Space, J |
| `b` | K, Backspace |
| `start` | Enter |
| `select` | Tab |

A game reads them as on a board, each by its name. `hello` takes those of a
gamepad's names as their bits and any other (BOOT) as A, and shows the pad
on the display only where the board has no arrows: on the computer it has.

**The sound.** A task of FreeRTOS asks the source the game gave
`board_sound_start`, the engine's mixer, as a board's task does, and SDL
plays what it gave. The engine's lock of the mixer is a mutex of FreeRTOS,
as on a board. The level is the board's: `board_sound_t.mixer_volume`, in
percent of full scale, is the one the person chose last
(`board_sound_volume_keep`, kept in NVS), else that of the board's speaker
(the 10″'s 50), else 0, and then the game chooses: `hello` gives 32 of 255.

**NVS.** ESP-IDF's own NVS, on a flash it emulates in a file:
`build/host/flash.bin`, kept between runs while the partition table of the
build is the one it was made with. The calls and their errors are NVS's
own, so a game's code for it is checked as it is.

**The end of the program.** Closing the window ends it with status 0, after
DGX's report. So does `DGX_HOST_QUIT_MS` (section 7).

## 6. What a game may use of ESP-IDF

On the computer a game has what ESP-IDF's `linux` target has: tasks,
queues, semaphores and timers of FreeRTOS, `esp_timer_get_time`, the log,
`heap_caps_*`, NVS, `sdkconfig.h` with its `CONFIG_*` values. What isn't
there is the chip: GPIO, I2C, SPI, I2S, Wi-Fi, the watchdog. A game seldom
needs them, since its board's parts come through `boards.h`; code that does
goes under `CONFIG_IDF_TARGET_LINUX`. `ESP_PLATFORM` is defined on the
computer too, so it tells nothing here:

```c
#include "sdkconfig.h"
#if !CONFIG_IDF_TARGET_LINUX
    xTaskCreate(radio_task, "radio", 4096, NULL, 5, NULL);
#endif
```

A component the game takes only for a chip is required in
`main/CMakeLists.txt` under a condition on the target, as `COMPONENTS main`
builds every component `main` requires. Not on `CONFIG_*`: ESP-IDF reads the
requirements before the configuration is made.

```cmake
idf_build_get_property(target IDF_TARGET)
if(NOT target STREQUAL "linux")
	list(APPEND chip_requires esp_wifi)
endif()
idf_component_register(SRCS "main.c" PRIV_REQUIRES nvs_flash esp_timer ${chip_requires})
```

## 7. With nobody at the computer

DGX's window reads five variables of the environment, and SDL two of its own:

| Variable | What it does |
| --- | --- |
| `DGX_HOST_QUIT_MS=3000` | the program ends so many milliseconds after the window was made |
| `DGX_HOST_SCRIPT=game.script` | a file of keys and mouse presses and when they come (below) |
| `DGX_HOST_SHOT=game.ppm` | the glass written to this file when the program ends, a PPM |
| `DGX_HOST_HEAP_MAX=300000` | the bytes the program may hold at once: an allocation over it gets `NULL` |
| `DGX_HOST_BLOCK_MAX=65536` | the largest block it may get at once |
| `SDL_VIDEODRIVER=dummy` | no window opens; the game draws, the shot is taken as usual |
| `SDL_AUDIODRIVER=dummy` | no sound plays; the mixer is pulled as usual |

The script is a line a step: the time in milliseconds after the window was
made, then a key down or up by the name SDL gives it, or the mouse down,
moved or up at a pixel of the glass upright. A line comes no earlier than
the one above it; `#` begins a comment. `hello` walking right for a second,
then hopping twice:

```text
# walk right a second, then two hops with A (Space)
300 key down Right
1300 key up Right
1600 key down Space
1650 key up Space
2400 key down Space
2450 key up Space
```

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy DGX_HOST_SCRIPT=hops.script DGX_HOST_QUIT_MS=3500 \
    DGX_HOST_SHOT=hello.ppm ./build/host/hello.elf
python3 -c "from PIL import Image; Image.open('hello.ppm').save('hello.png')"
```

```text
I (…) hello: hops 0
I (…) hello: a frame of 320x200, x4, on a display of 1280x800 of host; sound, a pointer, buttons
I (…) hello: hops 1
I (…) hello: hops 2
```

Run again, it starts at `hops 2` and ends at `hops 4`: NVS kept the count.
A script that is wrong ends the program and names its line.

The memory a board has, roughly: a run with `DGX_HOST_HEAP_MAX` shows what
the game can't get there. `hello` holds some 80 KB at most; given 70 000
bytes, it gets no frame and says so:

```text
W DGX HOST: no memory for 64000 bytes: 7096 held, DGX_HOST_HEAP_MAX 70000, DGX_HOST_BLOCK_MAX 0
E (…) hello: no memory for the frame
I DGX HOST: memory: 5320 bytes held now, 7096 at most at once, the largest block 4736; 1 allocations refused
```

A check is a run followed by a look at what the game left: its log, its
picture, what it kept in NVS. The exit status says less than it seems: a
game whose `app_main` gave up still ends with 0 at `DGX_HOST_QUIT_MS`, as the
window stays until then; a script that is wrong ends it with 1. Delete
`build/host/flash.bin` first for a check that starts from nothing.

## 8. What the computer cannot tell, and what it has not yet

It can't tell whether the game keeps up on the board:

- **The chip's speed.** The computer is many times faster. DGX counts the
  wire of a display on SPI or QSPI, not the drawing (section 5).
- **The board's memory.** DGX counts what the program holds, and
  `DGX_HOST_HEAP_MAX` makes it as short as a board; PSRAM and internal RAM
  are one there.
- **Several fingers at once**: the mouse is one.
- **The board's hardware**: everything under `CONFIG_IDF_TARGET_LINUX`, the
  task watchdog, the board's sound chip.

What the engine's own host had, and the board `host` of jef-sure/boards has
not yet: **typing** into the keyboard on the display from the computer's
keys (`dgx_ge_keyboard_type`). The keys are buttons only.

## 9. The engine's own tests

The engine's tests are a separate thing from a game on the computer. They
check the engine's C code piece by piece, with sanitizers, built by `make`
with DGX's `host/dgx_host.mk`, and they come only with a clone of the
engine's repository. The registry's package doesn't carry them.

```sh
make -C esp32-component-dgx-ge/test/host
```

They find DGX among the managed components of the project the engine lies
in, or in a neighbouring `esp32-component-dgx`, and DGX-DI the same way.
Another copy can be named with `DGX=<path>` and `DI=<path>`. Each test
prints `ok`. `test_freertos` is built three times: with sound, without, and
as the `linux` target builds the engine, its board's thread filling the
mixer while the game plays.

## 10. When something goes wrong

**CMake finds the C compiler broken**, with `ld: unrecognised emulation
mode: elf_x86_64` in its output. The project's `CMakeLists.txt` lacks
`-B/usr/bin/` for the board `host` (section 4), and the computer's compiler
took the chip's `ld` from the `PATH`.

**`Failed to resolve component` or `component … not registered`** for a
component of the chip (`esp_hal_…`, `esp_driver_…`). The project lacks
`set(COMPONENTS main)` for the board `host` (section 4), or `main` requires
a component of the chip on the computer too (section 6).

**`no sdkconfig.defaults.host here: run board_select.py host . of
jef-sure/boards`.** The project's boards don't have the computer:
`board_select.py host --as <board> .`.

**`the linux target is the board host, a window of the computer: choose it
with board_select.py host --as <board> .`** The project was set to the
`linux` target by hand (`idf.py set-target linux`) and has no
`sdkconfig.defaults.host`.

**`the board host was asked for, but the configuration chose …`.** The
`sdkconfig` in `build/host` was made for another board, or the build was
started without its preset. Remove `build/host/sdkconfig` and build with
`idf.py --preset host build` (ESP-IDF 5: the command of section 3).

**`the task that makes the windows has … bytes of stack left, SDL needs
…`**, then the game's own "no display". `app_main`'s stack is too small:
`CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384`, as `board_select.py` writes it. An
`sdkconfig` made before `sdkconfig.defaults.host` keeps the old value:
remove it.

**The window is frozen.** The game's loop doesn't call `vTaskDelay`. That's
where the window is shown and its events read, so nothing is drawn and
closing it does nothing. The fix is in the game: call `vTaskDelay` once per
pass, as a firmware must anyway (`dgx_ge_pace` does).

**SDL can't start its video**, and the game says it has no display. There
is no desktop to open a window on: set `SDL_VIDEODRIVER=dummy`.

**`the pack of the game could not be made`.** The packer failed as the
build was configured. Its own message is just above, usually a typo in an
animation or a missing picture, named with its file and line; or the
system's python3 has no Pillow (`sudo apt install python3-pil`).

**`DGX_HOST_SCRIPT <file>:<line>: …`**, such as `no key of that name: the
names are SDL's, as Left, Space, A, Left Shift`. The script has a step the
window can't read: a key SDL doesn't know by that name, a time earlier than
the line above, a mouse step out of order. The program ends at once, with
status 1 (section 7).

**The count, or what the game keeps, starts from zero.** The partition
table of the build changed, so `build/host/flash.bin` was made anew; or the
file was deleted.

---

The overview of the engine is [overview-en.md](overview-en.md): the board
in section 2.17, fonts in 2.12, input in 2.14, saving in 2.15, sound in
2.16. The animation language is described in [animation-en.md](animation-en.md).
The example has its own [README](../examples/hello/README.md), and the
engine's [README](../README.md) is where to start. jef-sure/boards has the
board `host` in its own docs, and DGX the window ("An effect on the
computer" of its README).
