# hello

The smallest whole game of DGX-GE: one actor on a scene. A robot walks while
an arrow is held and hops at a tap or a button, with a sound; a label counts
the hops, and the count is kept over a restart.

The same `main/main.c` builds two ways:

- **for the computer**, with `make`: a window of SDL2 shows the display of a
  board, the mouse is a finger, the keys are buttons;
- **for a board** of the engine, with `idf.py`: the CYD, the Guition
  JC8012P4A1C (10") or the JC1060P470 (7").

It talks to the hardware only through `boards/dgx_ge_board.h` of the engine,
and on the computer the engine's board `boards/host` takes the place of a
real one: how that is built and run, and what each step does, is in
[../../docs/host-en.md](../../docs/host-en.md) ([по-русски](../../docs/host-ru.md)).

## What is where

| File | What it is |
| --- | --- |
| `main/main.c` | the game: the frame, the scene, the robot, the label, the sound, the count in NVS, the loop |
| `anim/bot.anim` | what the robot does: its pictures, strides, the hop, its sound and its signal, the rules between them |
| `art/*.png` | its pictures and the ground, PNGs of a palette |
| `tools/hello_assets.py` | the pack from them: `main/assets/hello.pack` and `hello_pack.h` |
| `Makefile` | the build for the computer (includes the engine's `boards/host/host.mk`) |
| `CMakeLists.txt`, `main/CMakeLists.txt`, `main/idf_component.yml`, `sdkconfig.defaults` | the build for a board |

## On the computer

Needs: Linux, gcc, SDL2 (`libsdl2-dev`), python3 with Pillow, and DGX 0.8.0 or
later with DGX-DI 0.1.1 or later, as checkouts or as ESP-IDF downloaded them
into `managed_components/` (an `idf.py build` of this example does that).

```sh
make DGX=<dgx> DGX_DI=<dgx-di>      # build-host/hello
make run DGX=<dgx> DGX_DI=<dgx-di>
```

Without `DGX=` and `DGX_DI=` the ones in `managed_components/` are taken.

| Key | Button |
| --- | --- |
| arrows | the cross |
| Shift | A |
| Z, X, C | B, X, Y |
| Enter | START |
| Space | SELECT (BACK) |
| the mouse | a finger: a tap is a touch |

The window shows the 10"'s display, 1280x800, the game's 320x200 four times
larger. Another board's: `make HOST_DISPLAY=320x240 HOST_MM=58x43` (the CYD).
The count is kept in `nvs/hello/hops`, beside where the program runs.

With nobody at the computer, for a check or a picture:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
DGX_GE_HOST_INPUT="300 right+, 1300 right-, 1600 a+, 1650 a-, 2400 tap 640 400" \
DGX_GE_HOST_QUIT_MS=3000 DGX_GE_HOST_SHOT=hello.ppm ./build-host/hello
```

## On a board

```sh
idf.py build flash monitor                                                     # the CYD
idf.py -B build-p4 -D SDKCONFIG=build-p4/sdkconfig -D HELLO_BOARD=jc8012p4a1c build flash
idf.py -B build-p7 -D SDKCONFIG=build-p7/sdkconfig -D HELLO_BOARD=jc1060p470 build flash
```

The 10" needs its touch panel's firmware, which may not be given away: make
`main/jc8012p4a1c_touch.fw` from the board's package with DGX-DI's
`tools/gsl_firmware.py`.

An example made from the registry (`idf.py create-project-from-example`) has
no engine around it: copy the board's directory `boards/<board>/` of the
engine (its `sdkconfig.defaults*`) into `boards/` beside this README. ESP-IDF
looks for the files of `SDKCONFIG_DEFAULTS` before it downloads the engine.

## The pack again

After a change of `art/` or `anim/`: `python3 tools/hello_assets.py`. The
Makefile does it itself when they are newer than the pack.
