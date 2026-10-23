# From data to a board: how a game of DGX-GE is made

[По-русски](workflow-ru.md)

A game of DGX-GE is two things: data and a little C. The data are pictures
with their colors, animation scripts, scenes, dialogs and the game's numbers;
the C is what the data cannot say. The engine does not care where the data
come from. I made a sprite editor for it,
[dgx-ge-editor](https://github.com/jef-sure/dgx-ge-editor), but the engine knows nothing of that editor. It reads one file, the
pack, and the game's C reads one header of numbers beside it, and both are
made by one program, the packer `tools/dgx_ge_pack.py`. Whatever makes the
data, my editor or another one, a Python script, a generator of levels, a
converter from some other format, comes in through that one door.

This document follows a game from the engine's side, from data lying in files
to a firmware running on a board. It says what a game is made of, the three
ways data get into the packer, what comes out of it and why, how a build makes
the pack again by itself, what a game's C does with the pack, how the firmware
and the computer build are put together, what a day of work looks like, and
what goes wrong at each step. It does not go through the engine's parts one by
one: that is the overview, [overview-en.md](overview-en.md). The script
language is in [animation-en.md](animation-en.md), the build for the computer
in [host-en.md](host-en.md), and the smallest whole game is the example
[hello](../examples/hello/README.md).

The way described here was checked by following it from nothing: two small
games, one with a pack script of its own and one with an editor's document,
were built and run on the computer with nobody at it, and both were built for
the CYD with ESP-IDF 6.0.1. The output quoted below comes from those runs
(long paths shortened to `…`).

## Contents

1. [The whole picture](#1-the-whole-picture)
2. [The data: three ways in](#2-the-data-three-ways-in)
3. [What comes out](#3-what-comes-out)
4. [The pack made again by the build](#4-the-pack-made-again-by-the-build)
5. [The C side](#5-the-c-side)
6. [Building](#6-building)
7. [The loop of work](#7-the-loop-of-work)
8. [When something goes wrong](#8-when-something-goes-wrong)

---

## 1. The whole picture

### What a game is made of

| Data | What it is | In the pack | Read on the board with |
| --- | --- | --- | --- |
| pictures and sets | PNGs of a palette; a set is pictures that share colors (15 for 4-bit pictures, up to 255 for 8-bit ones), value 0 transparent | a table of sets, a table of pictures, their pixels | `dgx_ge_pack_image`, `dgx_ge_pack_set_colors` (`dgx_ge_pack.h`) |
| origins, masks, points | where a picture stands; its zones for collisions (index 0..15); its named points (a hand) | in the picture's record; tables of masks and points; which masks meet | `dgx_ge_pack_masks`, `dgx_ge_pack_point`, `dgx_ge_pack_meets` |
| animation scripts | a `.anim` text a kind of actor: scripts, events, params, rules ([animation-en.md](animation-en.md)) | a blob a kind | `dgx_ge_anim_open`, `dgx_ge_kind` |
| tilesets | tiles of pictures with their rules (solid sides, pieces drawn by neighbours, lives of cells) | a blob a set | `dgx_ge_tiles.h` |
| scenes | layers, pictures on them, actors, zones, entries, grids of tiles | a blob `SCENE_<NAME>` a scene | `dgx_ge_layout.h` |
| the scene graph and maps | which scene leads where; a map's rules (a script); its links (a plate opens a gate) | blobs `<GRAPH>`, `<GRAPH>_<MAP>_RULES`, `<GRAPH>_<MAP>_LINKS` | `dgx_ge_graph.h`, `dgx_ge_links.h` |
| dialogs and translations | `.dlg` texts; `.po` files of the language chosen | a blob a dialog, the strings in one blob `STRINGS` | `dgx_ge_dialog.h`, `dgx_ge_talk_view.h` |
| styles, fonts | how a dialog looks; which fonts the game needs | a blob `STYLES`; the font requests in a file beside the pack | `dgx_ge_talk_view.h`; DGX's `dgx_font_<name>()` |
| the game's numbers | constants, timers, sounds, shows, chases, crops, crowds, text fields | blobs `GAME`, `TIMERS`, `SOUNDS`, `CHASE`, one a show, one a crowd; crops are pictures | `dgx_ge_game.h`, `dgx_ge_timers.h`, `dgx_ge_mixer.h`, `dgx_ge_show.h`, `dgx_ge_chase.h`, `dgx_ge_crowd.h` |
| raw blobs | bytes only the game makes sense of | a blob | `dgx_ge_pack_blob` |

The code of a game is C, and the engine's rule is to keep it small: as much as
possible as data, as little special C as possible. What typically stays C:

- the board and the frame: which display, how large the frame is shown
  (`dgx_ge_board.h`, `dgx_ge_frame_create`, `dgx_ge_frame_scale`);
- the loop and the clock: read the input, move the actors on to now, draw;
- what the input means: buttons and touches made events of actors;
- the answers to what scripts say (`say`): play a sound, count a signal,
  put a shot into the world, take an actor away;
- what is the game's own idea and cannot be said as data: where an enemy
  aims, when a row of a puzzle is full, what the hero's next cell holds;
- where saves are kept (NVS, a file).

What should not stay C: anything a script, a timer, a link, a tile's rules or a
dialog can say. A door that opens when a plate is trodden on is a link and a
life of a cell, not an `if` in the game; "an hour to finish" is a timer, not a
counter of frames; the lives of the hero are a param of a script. Such data
are checked by the packer before the game runs, and the C that would do the
same is checked by nobody until it misbehaves.

### The way of the data

```
  ON THE COMPUTER                                               ON THE BOARD

  PNGs, .anim, .dlg, .po ─┐
  an editor's document ───┼─► the packer (Pack) ─► mygame.pack ──────► EMBED_FILES ─► flash, read in place
  numbers of a script ────┘   tools/dgx_ge_pack.py  mygame_pack.h ───► the game's C: numbers by name
                                                    mygame.fonts ────► DGX makes the fonts for the board
                                                    mygame.letters      (the letters the fonts must have)
                                                    mygame.pot ──────► translators ─► po/<lang>.po ─► the packer
                                                    mygame.sounds ───► (optional) the samples, apart

  the firmware: idf.py build flash        (ESP-IDF, a board of boards/<board>/)
  the computer: make, make run            (boards/host/host.mk, the same main.c in a window of SDL2)
```

The pack and the header always come together and always from the packer. The
rest of this document is about the arrows.

---

## 2. The data: three ways in

The packer is a Python class, `Pack`, in `tools/dgx_ge_pack.py`. Every kind of
data has its `add_…` method, and `write` makes the pack and its header. There
are three ways to feed it, and they mix freely.

### 2.1. A script of your own

The plainest way: a short Python script of the game finds its files and calls
the packer. `examples/hello/tools/hello_assets.py` is the smallest such script.
For a made-up `mygame`:

```
mygame/
  art/hero_stand.png, art/hero_step.png, art/ground.png    PNGs of a palette
  anim/hero.anim                                           the hero's script
  tools/mygame_pack.py                                     the pack script
  main/main.c                                              the game
  main/assets/                                             what the pack script writes
```

`tools/mygame_pack.py`:

```python
"""
The pack of mygame: main/assets/mygame.pack and mygame_pack.h from art/ and anim/.
python3 tools/mygame_pack.py [<engine>]   (the engine: managed_components/jef-sure__dgx-ge unless given)
"""
import math
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
engine = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "managed_components", "jef-sure__dgx-ge")
sys.path.insert(0, os.path.join(engine, "tools"))
from dgx_ge_pack import Pack  # noqa: E402

pack = Pack("MYGAME")
pack.add_constants(STEP=3, STRIDE=1)    # names the script writes for numbers, MYGAME_STEP in C too
pack.add_game(walk_edge=12)             # a number the C reads: MYGAME_GAME_WALK_EDGE

hero = pack.add_set("HERO", [(230, 80, 60), (250, 220, 120), (40, 40, 60)])
pack.add_png("HERO_STAND", hero, os.path.join(ROOT, "art", "hero_stand.png"), origin=(4, 9))
pack.add_png("HERO_STEP", hero, os.path.join(ROOT, "art", "hero_step.png"), origin=(4, 9))
land = pack.add_set("LAND", [(70, 160, 60), (0, 0, 0), (90, 60, 40)])
pack.add_png("GROUND", land, os.path.join(ROOT, "art", "ground.png"), origin="top-left")
pack.add_set("UI", [(150, 205, 250)])   # the sky: the paper of the frame

path = os.path.join(ROOT, "anim", "hero.anim")
with open(path, encoding="utf-8") as f:
    pack.add_anim("HERO", f.read(), path=path)

rate = 22050
tick = [int(8000 * math.sin(2 * math.pi * 900 * t / rate) * (1 - t / 600)) for t in range(600)]
pack.add_sound("STEP", samples=tick, rate=rate)

out = os.path.join(ROOT, "main", "assets")
os.makedirs(out, exist_ok=True)
size = pack.write(os.path.join(out, "mygame.pack"), os.path.join(out, "mygame_pack.h"))
print(f"mygame.pack: {size} bytes")
```

`anim/hero.anim`:

```
# The hero: stands, walks while told to, and says each stride to the game.
event go, halt
param strides 0

script stand
    frame HERO_STAND 500ms
    goto stand

script walk
label stride
    frame HERO_STEP 120ms
    dx STEP
    frame HERO_STAND 120ms
    dx STEP
    add strides 1
    signal STRIDE            # a stride made
    sound 0                  # the pack's first sound, STEP
    branch
    goto stride

on go: stand -> walk now
on halt: walk -> stand branch
```

Run it with the system's `python3` (it needs Pillow), and it prints
`mygame.pack: 3220 bytes`. Keep the scripts in files of their own, as here,
and give the packer the path: its errors then name the file and the line.

The methods, each described in full in its docstring:

| Method | What it adds |
| --- | --- |
| `Pack("MYGAME")` | a pack; the prefix begins every name of the header (capitals, digits, `_`) |
| `add_set(name, colors)` | a set of colors, RGB; returns its number |
| `add_png(name, set, path, origin=…)` | a picture from a PNG of a palette: its colors found in the set by RGB (a color the set lacks: the nearest, with a warning), or `by_index=True` as they are; origin `"bottom-left"` (default), `"top-left"` or `(x, y)` |
| `add_image(name, set, w, h, values, bits)` | a picture from values, made by the script itself |
| `add_mask`, `name_mask`, `add_meets` | collision masks of a picture, their names, which of them meet |
| `add_point(image, name, x, y, z)` | a named point of a picture (a hand) |
| `add_constants(**names)` | names of numbers the scripts and dialogs write; the C reads them as `<P>_<NAME>` |
| `add_anim(name, text, path)`, `add_anims([...])` | a kind's script; kinds that sense one another, together |
| `add_tileset(name, w, h, tiles, slots)` | a set of tiles and their rules |
| `add_links`, `add_graph(Graph)` | a map's links; a graph of scenes (`tools/dgx_ge_graph.py`) |
| `add_game(**numbers)` | the game's constants, read in C by place |
| `add_timer(name, once=/every=/until=…)` | a timer of the game |
| `add_sound(name, wav= or samples=, rate=…)` | a sound by name and number; with no samples, a name only |
| `add_show`, `add_crowd`, `add_chase`, `add_crop` | a cut scene, a crowd, how a kind chases, a part of a picture as a picture |
| `translate(po)`, `add_dialog(text, path)` | the language of the pack; a dialog |
| `add_style(name, **fields)`, `add_font(name, …)`, `add_field(name, size)` | how dialogs look; a font the game needs; a text field a dialog edits |
| `add_blob(name, bytes)` | bytes of the game's own |
| `add_sprites(json)` | everything an editor's document holds (2.2) |
| `write(pack, header, sounds=None)` | the pack and its header, both or neither |
| `write_fonts`, `write_letters`, `write_pot` | the font requests, the letters printed, the template for translators (section 3) |

The order matters only where one thing names another, and then the packer says
so: sets before their pictures; pictures and `add_constants` before the scripts
that name them; a kind's script before the dialogs, tiles (a life), shows,
crowds and chases that name the kind; `translate` and `add_field` before the
dialogs; the data blob of a map before its graph. A script's `sound 0` is a
number, the order of `add_sound`, so a sound added first is sound 0.

### 2.2. A document of an editor

The second way: a tool writes one JSON document, and the packer reads all of
it with one call, `Pack.add_sprites(<document>)`. The format is named
`dgx-ge-sprites` (version 1). My sprite editor,
[dgx-ge-editor](https://github.com/jef-sure/dgx-ge-editor), writes it, and any other editor or generator may write
it too. The format is described in full beside that editor,
[sprite-format.md](https://github.com/jef-sure/dgx-ge-editor/blob/main/sprite-format.md); what the engine goes by is
the packer itself (`add_sprites` and the methods it calls, with their
docstrings) and the engine's tests, which write such documents and check what
the packer makes of them: `test/host/test_sprites.py` (sets, frames, masks,
points, tiles, styles, dialogs and a translation) and `test/host/test_scenes.py`
(scenes, the graph, maps, links and the game's numbers).

The document lies in a folder with what it refers to:

```
mygame/game_data/
  mygame.json              the document
  HERO/stand.png           the pictures of a set, in a folder of the set's name (or beside the document)
  HERO/step.png
  LAND/ground.png
  HERO.anim                the script of the set HERO (a kind), by the set's name
  dialogs/hello.dlg        the dialogs, every .dlg of this folder
  po/ru.po                 the translation, when the pack's language is ru
```

A small document of a whole scene:

```json
{
  "format": "dgx-ge-sprites", "formatVersion": 1, "name": "mygame",
  "masks": [],
  "constants": {"STEP": 3, "STRIDE": 1},
  "game": [{"name": "walk_edge", "value": 12}],
  "screen": {"width": 320, "height": 200},
  "sets": [
    {"name": "HERO", "frames": [
      {"image": "stand.png", "width": 8, "height": 10, "origin": {"x": 4, "y": 9}},
      {"image": "step.png", "width": 8, "height": 10, "origin": {"x": 4, "y": 9}}]},
    {"name": "LAND", "frames": [
      {"image": "ground.png", "width": 320, "height": 16, "origin": {"x": 0, "y": 0}}]}
  ],
  "palettes": [{"name": "UI", "colors": [[150, 205, 250]]}],
  "styles": [{"name": "talk", "font": "text", "place": [8, 6, -16, 30],
              "colors": {"fill": [24, 16, 8], "border": [200, 160, 80], "text": [255, 240, 200]}}],
  "scenes": [
    {"name": "room", "width": 320, "height": 200, "layers": [
      {"name": "back", "objects": [
        {"type": "picture", "name": "ground", "set": "LAND", "image": "ground.png", "x": 0, "y": 184}]},
      {"name": "actors", "objects": [
        {"type": "actor", "name": "hero", "set": "HERO", "script": "stand", "x": 40, "y": 183},
        {"type": "zone", "name": "goal", "kind": "exit", "x": 280, "y": 150, "width": 30, "height": 34}]}
    ]}
  ]
}
```

The format in outline, key by key:

| Key | What the packer makes of it |
| --- | --- |
| `format`, `formatVersion` | `"dgx-ge-sprites"`, 1; anything else is refused |
| `masks`, `meets` | the names of mask indices (`<PREFIX>_MASK_<NAME>`); which masks meet, as `add_meets` text |
| `constants` | `add_constants`: names of numbers for the scripts and dialogs, and `<PREFIX>_<NAME>` for the C |
| `sets` | a set of colors each; its `frames` pictures `<SET>_<FILE>` (or a frame's `packName`) with `origin`, drawn `masks` (rows of hex), `points`; `defaultMask`: a mask made of a picture's own pixels where none is drawn |
| `palettes` | sets of colors with no pictures, named as sets |
| `tilesets` | `add_tileset`: tiles and their pieces, which name pictures by set and file; a piece's `layer` becomes a slot |
| `styles` | `add_style`, in the editor's key names (`scrollMs`, `letterMs`, `colors.fill`, …) |
| `game`, `timers`, `sounds`, `crops`, `shows`, `chases` | `add_game`, `add_timer`, `add_sound` (names only), `add_crop`, `add_show`, `add_chase` |
| `screen` | `<PREFIX>_SCREEN_WIDTH`, `_HEIGHT` |
| `scenes` | a blob `SCENE_<NAME>` each: layers, objects of type `picture`, `actor`, `zone`, `entry`, grids of tiles; exits, links, a map |
| `graph`, `maps` | the graph of scenes and its maps (`add_graph`), a map's rules from `<rules>.anim` beside the document |

Keys the packer does not know are left alone, so an editor may keep its own
there. A few rules a writer of such documents should know:

- **Colors are taken by index.** A frame's PNG values are the set's values as
  they are, its palette gives their colors, and the frames of one set must
  agree: a value that is red in one PNG and green in another is refused. Value
  0 is transparent. A set whose pictures use no value above 15 is 4-bit, else
  8-bit.
- **Names.** A picture is `<SET>_<FILE>` in capitals (`HERO/stand.png` is
  `HERO_STAND`, `MYGAME_IMG_HERO_STAND` in the header), the name the scripts use
  too. Scene objects are named in the header by scene and kind:
  `MYGAME_SCENE_ROOM_ACTOR_HERO`.
- **Scripts.** A set with a `<SET>.anim` beside the document is a kind; all
  of them are added together, so they know each other whatever their order.
- **The language.** `pack.language = "ru"` before `add_sprites` makes it read
  `po/ru.po` before the dialogs; with no language the dialogs keep their
  msgids.

### 2.3. Both: a document and a script

An editor does not hold everything. `add_sprites` takes no sound samples (a
document names sounds, but has no samples for them), no font requests, no text
fields, no crowds and no blobs of the game's own. The third way is a script
that reads the document first and adds what the document lacks. That is what a
game made in an editor normally has, `tools/pack.py`:

```python
"""
The pack of mygame from game_data/ (a document of the editor, its pictures, <SET>.anim, dialogs/, po/) and what the
document has not: the samples of the sounds, the fonts' requests. python3 tools/pack.py <engine> [<language>]
"""
import math
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(sys.argv[1], "tools"))
from dgx_ge_pack import Pack  # noqa: E402

pack = Pack("MYGAME")
pack.language = sys.argv[2] if len(sys.argv) > 2 else None   # po/<language>.po beside the document
pack.add_sprites(os.path.join(ROOT, "game_data", "mygame.json"))

# what the document has not
rate = 22050
pack.add_sound("STEP", samples=[int(8000 * math.sin(2 * math.pi * 900 * t / rate) * (1 - t / 600)) for t in range(600)], rate=rate)
pack.add_font("text", points=8, mono=True)                   # the font the style "talk" names

out = os.path.join(ROOT, "main", "assets")
os.makedirs(out, exist_ok=True)
pack.write_letters(os.path.join(out, "mygame.letters"))
pack.write_fonts(os.path.join(out, "mygame.fonts"), os.path.join(out, "mygame.letters"))
pack.write_pot(os.path.join(out, "mygame.pot"))
size = pack.write(os.path.join(out, "mygame.pack"), os.path.join(out, "mygame_pack.h"))
print(f"mygame.pack: {size} bytes")
```

A sound the document only names (its `sounds`) gets its samples from the
script: a later `add_sound` of the same name gives them to it, at the number
the document gave it, so `sound 0` of a script still means the same sound. A
sound that already has samples is refused a second time (`ValueError: sound
STEP twice`).

### 2.4. Why not write the pack directly

It can be done. The pack is a plain binary file, and each part of it is
versioned and laid out in a header of the engine: the pack in
`include/dgx_ge_pack.h`, an animation in `dgx_ge_anim.h`, a tileset in
`dgx_ge_tiles.h`, a scene in `dgx_ge_layout.h`, and so on. The engine opens
a part only when its magic and version are its own. A tool that writes
these bytes itself loses everything the packer checks, though: that every name
a script, a dialog, a scene or a tile gives exists, that the frames of a set
agree on their colors, that the rules of a script do not contradict each
other, that the header names no two things alike, that the params an actor
needs fit the build. And when the engine changes a format, the packer changes
with it in the same release, while a writer of its own falls behind. So my
advice is to keep the packer as the one door: generate files the packer reads
(PNGs, `.anim` and `.dlg` texts, a document) or call its API from a script,
and let it write the bytes.

### 2.5. What the packer refuses

The packer stops at the first thing it cannot make sense of, and writes
nothing then (`write` puts the pack and the header in place together, or
neither). Its errors name the file and the line where there is one. Some, from
the checks of this text:

```text
dgx_ge_anim.AnimError: …/game_data/HERO.anim: line 11: no picture 'HERO_STEPP'
dgx_ge_anim.AnimError: …/game_data/HERO.anim: line 23: 'walk' on 'halt' is given otherwise at line 22
dgx_ge_dialog.DialogError: …/game_data/dialogs/hello.dlg: line 2: no kind 'HEROO'
ValueError: …/game_data/mygame.json: no picture step.png of set HERO
dgx_ge_dialog.DialogError: …/game_data/dialogs/hello.dlg: hello: '{n} Держи стрелку вправо, чтобы идти.' shows {n}, a number the line 'Hold the right arrow to walk.' has not
```

The first is a typo in a picture's name; the second, two rules that give one
script two answers to one event; the third, a dialog's role played by a kind
that does not exist; the fourth, a frame whose file is missing; the fifth, a
translation that shows a `{name}` its line does not have. Others: a
picture whose value has no color in its set, the frames of a set that disagree
on a color, two names that make the same name of the header (`stand-1` and
`stand_1`), a blob named like one the packer makes itself (`STRINGS`, `GAME`,
`TIMERS`, `STYLES`, `CHASE`, `SOUNDS`), a constant whose header name is
another thing's or whose value is not a whole number, a sound given samples
twice, a timer read and never described.
Every one of them would otherwise be a game misbehaving on a board; here it is
a line to fix before anything is built.

What is only likely to be a mistake is a warning, printed by `write`, and the
pack is made anyway: a color of a PNG that its set does not have, taken as the
nearest one.

---

## 3. What comes out

| File | What it is | Who reads it |
| --- | --- | --- |
| `mygame.pack` | the pack: a table of sets, pictures, blobs, masks, points, which masks meet, then the data; its last field is its fingerprint, the CRC-32 of the whole | the firmware, embedded whole (`EMBED_FILES`) |
| `mygame_pack.h` | the numbers of everything in the pack, by name | the game's C; the build (`dgx_ge_params_of_pack`, `host.mk`) |
| `mygame.fonts` | the game's fonts as requests of DGX's fonts (`write_fonts`) | DGX, which makes the fonts at build time (`dgx_ge_fonts`, `GAME_FONTS`) |
| `mygame.letters` | the letters the pack can print in its language (`write_letters`) | the font requests: a font made has these letters and no more |
| `mygame.typed` | what can be typed into the game's text fields (written by `write_fonts` when a font is `typed`) | the request of that font |
| `mygame.pot` | every line of the dialogs in its context, untranslated (`write_pot`) | translators: it becomes `po/<language>.po` |
| `mygame.sounds` | the samples of the sounds, when `write` is given a third path | a board that plays them embeds it; one without sound leaves it out of its flash |

**The pack lies in flash and is read in place.** A board has hundreds of
kilobytes of RAM and megabytes of flash, so nothing of the pack is copied:
`dgx_ge_pack_open` checks its header and keeps a pointer, a picture is a
pointer to its pixels, a blob a pointer and a size. The pack of `mygame` with
the scene, a dialog and a sound is 3512 bytes. Its fingerprint,
`dgx_ge_pack_id`, is what a saved game is tied to: a save made with one pack
is refused by a game with another, so a changed picture makes old saves
unreadable, rightly, since their numbers may mean other things now.

**The header names everything by number.** The C never writes a number of the
pack; it writes the name, and a name changed in the data is a compile error in
the code, not a wrong picture on the board. The header of the document of 2.2
begins so:

```c
/* Made by dgx_ge_pack.py: the numbers of mygame.pack. Do not edit. */
#pragma once

#define MYGAME_SET_HERO 0
#define MYGAME_SET_LAND 1
#define MYGAME_SET_UI 2
#define MYGAME_SET_COUNT 3

#define MYGAME_IMG_HERO_STAND 0
#define MYGAME_IMG_HERO_STEP 1
#define MYGAME_IMG_LAND_GROUND 2
#define MYGAME_IMG_COUNT 3

#define MYGAME_BLOB_HERO 0
#define MYGAME_BLOB_HELLO 1
#define MYGAME_BLOB_SCENE_ROOM 2
#define MYGAME_BLOB_STRINGS 3
#define MYGAME_BLOB_GAME 4
#define MYGAME_BLOB_STYLES 5
#define MYGAME_BLOB_SOUNDS 6
#define MYGAME_BLOB_COUNT 7
```

The families of names:

| Name | What |
| --- | --- |
| `<P>_SET_<NAME>`, `<P>_IMG_<NAME>`, `<P>_BLOB_<NAME>`, each with `_COUNT` | sets, pictures, blobs (a kind's script, a dialog, a scene, a tileset are blobs) |
| `<P>_<KIND>_SCRIPT_<s>`, `_EVENT_<e>`, `_PARAM_<p>`, `_GAME_<q>` | a kind's scripts, events, params, questions to the game |
| `<P>_EVENT_<e>` | the pack's events, one number for every kind |
| `<P>_GAME_<NAME>`, `<P>_TIMER_<NAME>`, `<P>_SOUND_<NAME>` | the game's constants, timers, sounds |
| `<P>_<NAME>` | the names of numbers the scripts write (`add_constants`, a document's `constants`), with the number itself |
| `<P>_MASK_<NAME>`, `<P>_POINT_<NAME>` | masks and points |
| `<P>_<TILESET>_TILE_<t>`, `_SLOT_<s>` | tiles (from 1; 0 is empty) and slots |
| `<P>_<GRAPH>_EXIT_`, `_ENTRY_`, `_MAP_` | names of the scene graph |
| `<P>_SCENE_<SCENE>_ACTOR_<n>`, `_ZONE_`, `_ENTRY_`, `_GRID_`, `_PICTURE_`, `_LAYER_`; `<P>_ZONE_<WORD>` | objects of a scene from a document, the kinds of zones |
| `<P>_STYLE_<s>`, `<P>_FONT_<f>`, `<P>_FONTS(X)`, `<P>_FONT_COUNT` | styles, fonts, the table of fonts by name |
| `<P>_<DIALOG>_ROLE_<r>`, `<P>_FIELD_<NAME>` (`_SIZE`) | roles of a dialog, text fields |
| `<P>_SCREEN_WIDTH`, `_HEIGHT` | the screen the document was laid for |

**The params an actor keeps** are the one number the build itself takes from
the header. Every actor has room for `DGX_GE_ANIM_PARAMS` params (4 unless
the build says otherwise, 8 bytes each), and a kind that declares more does not
open. So the header says how many the pack needs, and stops a build that keeps
fewer:

```c
#define MYGAME_ANIM_PARAMS 2
#include "dgx_ge_anim.h"
#if DGX_GE_ANIM_PARAMS < MYGAME_ANIM_PARAMS
#error "the actors of this pack keep 2 params, the build fewer (DGX_GE_ANIM_PARAMS): dgx_ge_params_of_pack(<this header>) in the game's CMakeLists, GAME_PACK_H in its Makefile of the computer"
#endif
```

`dgx_ge_params_of_pack(assets/mygame_pack.h)` in `main/CMakeLists.txt` hands
the number to the whole firmware build, the engine included; `GAME_PACK_H` in
the Makefile does the same for the computer. Every actor then costs as much
as the pack needs and no more.

**The fonts, the letters and the translation** come from the dialogs. For the
document of 2.2 with its Russian `.po`:

```text
$ cat main/assets/mygame.fonts
# The fonts of the game, as requests of DGX's fonts: made by dgx_ge_pack.py (Pack.add_font), not to be edited.
need text
size 8pt
mono 1
text mygame.letters
$ cat main/assets/mygame.letters
 ,.ГДабвдежийклопрстучы
```

---

## 4. The pack made again by the build

The pack has to follow its data without anyone remembering to run the pack
script. In projects made this way both builds run it themselves.

**The firmware.** `main/CMakeLists.txt` runs the pack script while the build
is configured (`execute_process`) and lists the data among the files whose
change configures it again (`CMAKE_CONFIGURE_DEPENDS`). After a change of the
data, `idf.py build` configures again, which makes the pack, and the
firmware embeds the new one:

```cmake
# the pack made as the build is configured; a change of its sources configures it again
# (engine: where the engine is, idf_build_get_property(engine MYGAME_ENGINE), section 6)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(game ${CMAKE_CURRENT_LIST_DIR}/..)
    find_program(GAME_PYTHON NAMES python3 PATHS /usr/bin /usr/local/bin NO_DEFAULT_PATH)
    if(NOT GAME_PYTHON)
        find_program(GAME_PYTHON NAMES python3 python REQUIRED)
    endif()
    file(GLOB inputs ${game}/tools/mygame_pack.py ${game}/anim/*.anim ${game}/art/*.png)
    execute_process(COMMAND ${GAME_PYTHON} ${game}/tools/mygame_pack.py ${engine} RESULT_VARIABLE packed)
    if(NOT packed EQUAL 0)
        message(FATAL_ERROR "the pack could not be made (tools/mygame_pack.py, see above)")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${inputs})
endif()
```

For a document, the inputs are its folder: `file(GLOB_RECURSE inputs
${game}/game_data/*)`. Three details matter:

- **The system's `python3`, not ESP-IDF's.** In an ESP-IDF shell, `python3`
  is ESP-IDF's own, which has no Pillow (`ModuleNotFoundError: No module
  named 'PIL'`). The `find_program` above looks in the system's directories
  first.
- **Not in the early expansion.** ESP-IDF reads every `CMakeLists.txt` of the
  components twice, once early only to learn what they require. The pack is
  made in the real pass alone.
- **New files.** The list is taken when the build is configured: a change of a
  file in it reconfigures, a file added to the folder becomes known at the
  next configure (`idf.py reconfigure`). In practice an added picture comes
  with a changed script or document, which reconfigures anyway.

After one changed line of `HERO.anim`, `idf.py build` showed:

```text
[0/1] Re-running CMake...
mygame.pack: 3512 bytes
…
[7/13] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/main.c.obj
```

**The computer.** The engine's `host.mk` takes the params from the pack's
header while make reads the Makefiles, before any rule runs, so the pack has
to be made then, not by a rule. `host.mk` does it itself: the game's Makefile
names the command that makes the pack and what the pack is made from, before
it includes `host.mk`:

```make
# the pack made again, before its header is read, whenever one of its inputs is newer than the header
GAME_PACK_MAKE   := python3 tools/mygame_pack.py $(ENGINE)
GAME_PACK_INPUTS := tools/mygame_pack.py $(wildcard anim/*.anim art/*.png)

include $(ENGINE)/boards/host/host.mk
```

When one of `GAME_PACK_INPUTS` is newer than `GAME_PACK_H` (or there is no
header yet), `host.mk` runs `GAME_PACK_MAKE` as it is read, then takes the
params from the new header; `make clean` never runs it. For a document the
inputs are its folder, `$(shell find game_data -type f)`, and the script.
After four params were added to `hero.anim`, one `make` did it all:

```text
the pack: python3 tools/mygame_pack.py …
mygame.pack: 3228 bytes
…
```

and every object was built with `DGX_GE_ANIM_PARAMS=5`. A pack that cannot be
made stops the build before anything is compiled:

```text
dgx_ge_anim.AnimError: …/anim/hero.anim: line 11: no picture 'HERO_STEPP'
…/boards/host/host.mk:70: *** the pack was not made: python3 tools/mygame_pack.py … failed (it says why above).  Stop.
```

`examples/hello/Makefile` does the same with its own script.

---

## 5. The C side

A game is an ESP-IDF program whose `app_main` sets the board up, opens the
pack and runs a loop. The same `main.c` builds for a board and for the
computer, because it talks to the hardware only through
`boards/dgx_ge_board.h`. Its parts, in the order they run, with the names of
the engine (the whole of the loop and its reasons are in
[overview-en.md, 2.18](overview-en.md#218-the-whole-game-loop)):

1. **The board**: `board_display_init(&board)` gives a DGX screen in
   landscape. `board_pointer_start`, `board_buttons`, `board_sound_start`
   say what else the board has (the CYD of the engine has no touch and no
   sound; the computer has all of them).
2. **The pack**: the build embeds it, and the game takes its bytes by the
   symbols of the file's name, `_binary_mygame_pack_start` and `_end`, then
   `dgx_ge_pack_open`.
3. **The frame**: `dgx_ge_frame_create` on the screen, at the game's own size
   (320x200 points), `dgx_ge_frame_scale` to show it larger on a large
   display, `dgx_ge_frame_band` as the board asks. Every set's colors go into
   the frame once (`dgx_ge_frame_add_colors`), which gives a map of each set's
   values to the frame's indices.
4. **The scene**: `dgx_ge_scene_create(frame, layers, set, actors)`; its set
   (what does not move) given once, by hand (`dgx_ge_scene_add`) or as a
   document laid it (`dgx_ge_layout_open`, `dgx_ge_layout_scene`).
5. **Kinds and actors**: a kind is the blob of its script. One actor can open
   it itself (`dgx_ge_anim_open`); a game of many takes them through
   `dgx_ge_kinds_init` and `dgx_ge_kind`, which opens each kind once.
   `dgx_ge_actor_start(&hero, anim, MYGAME_HERO_SCRIPT_STAND, x, y, now)`, or
   `dgx_ge_layout_actor_start(&room, MYGAME_SCENE_ROOM_ACTOR_HERO, &hero, &kinds,
   now)` for an actor the document placed.
6. **The loop**: the input (`dgx_ge_input_update(&input, board_buttons())`),
   then every actor moved on to now (`dgx_ge_actor_advance`), then the
   decisions, sent as events (`dgx_ge_actor_event`), then the frame's actors
   (`dgx_ge_scene_actors_begin`, `dgx_ge_scene_actor`) and
   `dgx_ge_scene_render`, which draws what changed and sends it. At the end
   `vTaskDelay`, at least one tick: on a board other tasks need it, and on
   the computer that is where the window is shown and the input read.
7. **`say`**: what scripts tell the game as their steps are made comes to one
   function of the game, `say(actor, what, value, at_ms, ctx)`: a sound
   (`DGX_GE_ANIM_SOUND`), a signal (`DGX_GE_ANIM_SIGNAL`), a shot, an actor
   that appears or vanishes, a timer started. The game does what only it can.

The heart of the game of 2.2, as it was built and run (the whole file is
about 150 lines):

```c
#include "assets/mygame_pack.h"
#include "dgx_fonts.h"
#include "dgx_ge_board.h"
#include "dgx_ge_layout.h"
#include "dgx_ge_pockets.h"
#include "dgx_ge_talk_view.h"
/* … the other headers of the engine, esp_timer.h, freertos/task.h */

extern const uint8_t pack_start[] asm("_binary_mygame_pack_start");
extern const uint8_t pack_end[] asm("_binary_mygame_pack_end");

static void say(dgx_ge_actor_t *actor, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    if (what == DGX_GE_ANIM_SOUND) dgx_ge_sound_play(&sound, value);
    if (what == DGX_GE_ANIM_SIGNAL && value == MYGAME_STRIDE) ++strides;
}

void app_main(void)
{
    board_t board;
    if (!board_display_init(&board)) return;
    if (!dgx_ge_pack_open(&pack, pack_start, (size_t)(pack_end - pack_start))) return;
    /* … the frame, the scene of 3 layers, every set's colors into the frame: maps[set], map_of[set] */

    /* the scene as the document lays it: its layers and pictures, its actor started by its name */
    size_t         size;
    const uint8_t *laid = dgx_ge_pack_blob(&pack, MYGAME_BLOB_SCENE_ROOM, &size);
    uint32_t       now  = (uint32_t)(esp_timer_get_time() / 1000);
    dgx_ge_scene_reset(scene, 0);
    if (!laid || !dgx_ge_layout_open(&room, laid, size) || !dgx_ge_layout_scene(&room, scene, &pack, map_of, pictures) ||
        !dgx_ge_layout_actor_start(&room, MYGAME_SCENE_ROOM_ACTOR_HERO, &hero, &kinds, now))
        return;
    dgx_ge_layout_zone(&room, MYGAME_SCENE_ROOM_ZONE_GOAL, &goal, NULL);

    /* the game's numbers by their names */
    const uint8_t *numbers = dgx_ge_pack_blob(&pack, MYGAME_BLOB_GAME, &size);
    const int      edge    = numbers && dgx_ge_game_open(&game, numbers, size) ? dgx_ge_game_value(&game, MYGAME_GAME_WALK_EDGE, 10) : 10;

    /* the dialog: the game's fonts by the names the pack gives them, the styles, the strings, the talk */
    static dgx_font_t *fonts[MYGAME_FONT_COUNT];
#define FONT_OF(N, n) fonts[MYGAME_FONT_##N] = dgx_font_##n();
    MYGAME_FONTS(FONT_OF)
#undef FONT_OF
    const uint8_t *styles = dgx_ge_pack_blob(&pack, MYGAME_BLOB_STYLES, &size);
    dgx_ge_talk_view_init(&view, styles, size, frame, fonts, MYGAME_FONT_COUNT);
    /* … dgx_ge_strings_open (MYGAME_BLOB_STRINGS), dgx_ge_dialog_open (MYGAME_BLOB_HELLO) */
    dgx_ge_actor_t *const cast[1] = {[MYGAME_HELLO_ROLE_HERO] = &hero};
    dgx_ge_talk_start(&talk, &hello, &(dgx_ge_cast_t){.roles = cast}, MYGAME_STYLE_TALK, now, say, NULL);

    dgx_ge_input_t input = {0};
    for (;;) {
        const int64_t t0 = esp_timer_get_time();
        now              = (uint32_t)(t0 / 1000);
        dgx_ge_input_update(&input, board_buttons());
        dgx_ge_talk_view_play(&view, &talk, &strings, now);
        dgx_ge_actor_advance(&hero, now, say, NULL);
        const bool on = (input.held & DGX_GE_KEY_RIGHT) && dgx_ge_actor_x(&hero) < FRAME_W - edge;
        dgx_ge_actor_event(&hero, on ? MYGAME_HERO_EVENT_GO : MYGAME_HERO_EVENT_HALT, now, say, NULL);

        dgx_ge_scene_actors_begin(scene);
        /* … the hero as an item of the frame: its picture (hero.image), its place, the map of its set */
        dgx_ge_talk_view_show(&view, &talk, scene, LAYER_TALK);
        dgx_ge_scene_render(scene);
        const int64_t spent = esp_timer_get_time() - t0;
        vTaskDelay(spent < 20000 ? pdMS_TO_TICKS((20000 - spent) / 1000) + 1 : 1);
    }
}
```

`examples/hello/main/main.c` is the same, written out whole and with every
step explained, with a label, a sound, the pointer and a count kept in NVS.

Beyond one scene, the same pattern holds for every part of the engine: the
game opens a blob by its name in the header and hands it to the part that reads
it.

- **A world larger than one scene.** The scene graph (`dgx_ge_graph_open` on
  `<P>_BLOB_<GRAPH>`) says where an exit leads; a scene left is not lost: what
  lies or lives in it is kept as records of the world (`dgx_ge_world.h`), a
  few bytes each, and becomes actors again, moved on to now, when the scene is
  shown again ([overview-en.md, 2.10](overview-en.md#210-a-living-world)).
- **Dialogs**: a talk (`dgx_ge_talk_start`) goes on by itself; the game sends
  it the interface's events (`dgx_ge_talk_view_event`, `_pointer`) and shows
  it (`dgx_ge_talk_view_show`); a dialog that pauses the game says so
  (`dgx_ge_talk_pauses`) ([overview-en.md, 2.12](overview-en.md#212-text-labels-dialogs-fonts)).
- **Saving**: the game begins a save with the pack's fingerprint
  (`dgx_ge_save_begin(&s, buffer, size, dgx_ge_pack_id(&pack), now)`), each
  part writes its own chunk (`dgx_ge_world_save`, `dgx_ge_timers_save`, …),
  the game its own, and keeps the bytes where it likes (NVS on a board, a
  file on the computer) ([overview-en.md, 2.15](overview-en.md#215-saving-the-game)).

---

## 6. Building

### The firmware

The engine is an ESP-IDF component in the registry; DGX comes with it. A
game adds it, and DGX-DI, which the boards with a touch screen and the
computer build need:

```sh
idf.py add-dependency "jef-sure/dgx-ge"
idf.py add-dependency "jef-sure/dgx-di"
```

The version to ask for is the one the engine's [README](../README.md) names.
The boards with an ESP32-P4 need two more components (their sound chip, the
7"'s touch panel); `examples/hello/main/idf_component.yml` lists them.

The files of a firmware project for the CYD:

```
mygame/
  CMakeLists.txt                  the project: the board's ESP-IDF settings, the engine's place, project(mygame)
  sdkconfig.defaults              the game's own settings, over the board's
  boards/cyd/sdkconfig.defaults   the board's settings, copied from the engine's boards/cyd/
  main/CMakeLists.txt             the board's file, the pack, the component
  main/idf_component.yml          the engine and DGX-DI
  main/main.c
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.22)
# the board's ESP-IDF settings first, the game's own over them (copied from the engine's boards/cyd/)
set(SDKCONFIG_DEFAULTS "boards/cyd/sdkconfig.defaults;sdkconfig.defaults")
if(NOT IDF_TARGET AND NOT DEFINED ENV{IDF_TARGET})
    set(IDF_TARGET esp32)
endif()
# where the engine is: the registry's download unless -D MYGAME_ENGINE=<path> says another
set(MYGAME_ENGINE ${CMAKE_CURRENT_LIST_DIR}/managed_components/jef-sure__dgx-ge CACHE PATH "the engine")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# for main/CMakeLists.txt: a property of the build reaches it even where ESP-IDF reads it early
idf_build_set_property(MYGAME_ENGINE ${MYGAME_ENGINE})
project(mygame)
```

`main/CMakeLists.txt` (the part that makes the pack is in section 4):

```cmake
idf_build_get_property(engine MYGAME_ENGINE)
include(${engine}/boards/cyd/board.cmake)

# … the pack made as the build is configured (section 4)

idf_component_register(
    SRCS "main.c" ${BOARD_SRCS}
    INCLUDE_DIRS "."
    REQUIRES jef-sure__dgx-ge jef-sure__dgx
    PRIV_REQUIRES esp_timer jef-sure__dgx-di ${BOARD_REQUIRES}
    EMBED_FILES "assets/mygame.pack"
)
# the params an actor keeps, as many as the pack's animations need
dgx_ge_params_of_pack(assets/mygame_pack.h)
# the fonts the pack asks for, made by DGX for this board's display at the frame's scale
dgx_ge_fonts(assets/mygame.fonts)
dgx_fonts_board(PIXELS ${BOARD_PIXELS} MM ${BOARD_MM})
dgx_fonts_scale(1)
dgx_fonts_rules(../fonts.rules)
```

Why each piece:

| Piece | Why |
| --- | --- |
| the board's `sdkconfig.defaults`, copied | DGX builds only what the `sdkconfig` turns on: the CYD's needs its SPI bus, its ILI9341 and the virtual screens the frame is. ESP-IDF reads the list of defaults before it downloads any component, so the board's file has to be in the project ([overview-en.md, 2.17](overview-en.md#217-the-board)) |
| `MYGAME_ENGINE` as a property of the build | `main/CMakeLists.txt` is also read early, only to learn what `main` requires, and `board.cmake` must be included there already, since it names some of that. In that early reading a component's properties are not there yet (`idf_component_get_property` on the engine fails), but a property of the build is |
| `board.cmake` | the board's file (`BOARD_SRCS`: a file of `main`'s build directory that includes the board's file, so that `main` builds no file of the engine's directory, which ESP-IDF 6 would warn of), the components it needs (`BOARD_REQUIRES`), its display in pixels and millimetres. `dgx_ge_board.h` comes with the engine itself; `BOARD_INCLUDE_DIRS` is empty and kept only for games that name it |
| `EMBED_FILES` | the pack in the firmware's flash; `mygame.pack` gives the symbols `_binary_mygame_pack_start` and `_end` |
| `dgx_ge_params_of_pack` | `DGX_GE_ANIM_PARAMS` for the whole build, from the header (section 3) |
| `dgx_ge_fonts` and DGX's `dgx_fonts_*` | the game's font requests handed to DGX with the display, the frame's scale and the rules of which faces may be taken; DGX makes them at build time, `dgx_font_<name>()` each. A game without fonts of its own leaves these out |

Then:

```sh
idf.py build flash monitor
```

The CYD build of the game of 2.2:

```text
-- DGX fonts: 1 need(s) and 0 list(s), generated into …/build/dgx_fonts; …
[143/1094] DGX: the fonts of the program by its needs
text: DejaVu Sans Mono Book 8 px (8 pt x 1), 23 glyphs
…
mygame.bin binary size 0x444b0 bytes. Smallest app partition is 0x100000 bytes. 0xbbb50 bytes (73%) free.
```

**Another board, or several.** Another of the engine's boards is another
`board.cmake`, another copied `sdkconfig.defaults` (on an ESP32-P4 a second
file, of the chip's revision) and another target. A game for several boards
makes the board a choice of the build: a cache variable in `CMakeLists.txt`
that picks the target and the list of defaults, handed to
`main/CMakeLists.txt` as a property of the build, as
`examples/hello/CMakeLists.txt` does with `HELLO_BOARD`
(`idf.py -B build-p4 -D SDKCONFIG=build-p4/sdkconfig -D HELLO_BOARD=jc8012p4a1c build`).
A board the engine does not have is the game's own: a file that implements
`dgx_ge_board.h`, or a DGX screen given straight to `dgx_ge_frame_create`.

**The engine from a checkout.** To build against a clone of the engine
instead of the registry's, name it in `main/idf_component.yml`
(`override_path:` under `jef-sure/dgx-ge`) and pass the same path as
`-D MYGAME_ENGINE=<engine>`.

### The computer

The same `main.c` builds for Linux with the engine's board `host`: a window of
SDL2 shows the board's display, the mouse is a finger, the keys are buttons.
All of it is in [host-en.md](host-en.md); the essentials are a Makefile beside
the project's `CMakeLists.txt` that names what `main/CMakeLists.txt` names
and includes the engine's `host.mk` last:

```make
# mygame on the computer: make, make run, make clean.
GAME             := mygame
GAME_SRCS        := main/main.c
GAME_INCLUDES    := main
GAME_EMBED       := main/assets/mygame.pack
GAME_PACK_H      := main/assets/mygame_pack.h
GAME_FONTS       := main/assets/mygame.fonts
GAME_FONT_RULES  := fonts.rules
HOST_DISPLAY     ?= 320x240
HOST_MM          ?= 58x43
ENGINE           ?= managed_components/jef-sure__dgx-ge
# the pack made again before its header is read (section 4)
GAME_PACK_MAKE   := python3 tools/pack.py $(ENGINE) ru
GAME_PACK_INPUTS := tools/pack.py $(shell find game_data -type f)

include $(ENGINE)/boards/host/host.mk
```

DGX and DGX-DI are taken from `managed_components/` (after one `idf.py
build`) or given as `DGX=` and `DGX_DI=`. Build in a shell where ESP-IDF is
not activated: its `ld` is the chip's, and `host.mk` stops on it.

---

## 7. The loop of work

| You changed | Run | What it shows |
| --- | --- | --- |
| a picture, a script, a dialog, the document | the pack script, or just `make` | the packer's checks pass: names, colors, rules, the header |
| anything | `make run` | the game in a window, played by hand |
| anything, before a commit | a run with nobody at the computer (below) | the game still does what it did: its log, its exit status, a picture |
| something heavy (more actors, a larger scene, a new font) | `idf.py build flash monitor` | what only the board can tell: RAM free, the time of a frame |
| a line of a dialog | `.pot` → `.po` → the pack | the translation is in, its letters in the fonts |

**Without anyone at the computer.** SDL's dummy drivers, a scripted hand and
a picture of the display at the end ([host-en.md, 9](host-en.md#9-with-nobody-at-the-computer)).
The game of 2.1, walking right for 1.2 seconds:

```text
$ SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy DGX_GE_HOST_INPUT="300 right+, 1500 right-" \
  DGX_GE_HOST_QUIT_MS=2500 DGX_GE_HOST_SHOT=a.ppm ./build-host/mygame
I host: a display of 320x240 pixels, 58x43 mm, in a window of 640x480
I mygame: strides 1, x 46
I mygame: strides 2, x 52
I mygame: strides 3, x 58
I mygame: strides 4, x 64
I mygame: strides 5, x 70
I host: the display written to a.ppm
```

Five strides of 240 ms in 1.2 seconds, six points each: real time, not
frames. The game of 2.2, the right arrow held for twelve seconds, logs
`the goal, after 40 strides` as the hero reaches the zone the document
placed. A check is such a run and a look at what it left (the exit status,
the log, the picture), in a script of the project.

**The scripts in words.** `tools/dgx_ge_explain.py` retells scripts as text,
which is a quick way to see what a script really says, or to show it to
someone who does not read the language:

```text
$ python3 tools/dgx_ge_explain.py --lang en game_data/HERO.anim
## HERO
**Params:** `strides` — starts at 0

### States

- **stand**: 1 frame HERO_STAND, 500 ms; round and round.
- **walk**: “stride”: 1 frame HERO_STEP, 120 ms; STEP forward; 1 frame HERO_STAND, 120 ms; STEP forward; strides += 1; a signal to the game 1; sound 0; may be broken off here; repeats from “stride”.

### Transitions

- `go`: stand → walk, at once
- `halt`: walk → stand, at a branch point
```

Given several files, it tells the events between the kinds too: who sends
each, who answers it. `--help` prints its usage.

**Translations.** The dialogs are written in one language, their msgids. The
pack script writes `mygame.pot`, every line in its dialog's context. A
translation is an ordinary gettext `.po`: `msginit -i mygame.pot -l ru -o
po/ru.po` makes a new one (of UTF-8, as the `.pot`'s header says), `msgmerge --update
po/ru.po mygame.pot` brings an old one up to the dialogs, and any editor of
`.po` files will do for the rest. Its header's `Plural-Forms` says how many
forms a line of a number has. The language is chosen when the pack is made
(`pack.translate(path)` before the dialogs, or `pack.language` for a
document), so a pack is of one language; a game in two languages is two packs,
or two builds.

**Fonts.** A game names the fonts it needs (`add_font`) and the packer
writes them as DGX's requests, with the letters the pack can print. DGX makes
them at build time for the display and scale the build gives, from the faces
of the computer that the rules allow (`fonts.rules`), so a font has exactly
the letters of the game, at the size of the board: 8 points of the frame are 8
pixels on the CYD and 32 on a display four times larger. A changed line of a
dialog changes the letters, and the next build makes the fonts again.
`python3 <dgx>/font2c/dgx_fonts.py list fonts.rules 16` shows which faces the
rules would take at a size.

**On the board.** The computer cannot tell whether the game fits and keeps up
([host-en.md, 10](host-en.md#10-what-the-computer-cannot-tell)). Log
`heap_caps_get_free_size(MALLOC_CAP_8BIT)` after the game is set up and the
time a pass of the loop takes; the CYD has no PSRAM, and every actor, scene
and record of the world comes out of its internal RAM. `idf.py size` tells
what the firmware itself takes.

**Saves.** A save is tied to the pack's fingerprint. Every change of the data
makes a new pack, and the saves of the old one are refused by
`dgx_ge_load_open`: on a board under development, expect to start from zero
after a change of the data.

---

## 8. When something goes wrong

| Where | What you see | Why, what to do |
| --- | --- | --- |
| the packer | `AnimError`, `DialogError`, `ValueError` with a file and a line | a name that does not exist, two rules in conflict, a missing picture (2.5); fix the data |
| the packer | `ModuleNotFoundError: No module named 'PIL'` | the pack script ran under ESP-IDF's Python; run it with the system's `python3`, and let the build find that one (section 4) |
| the packer | `ValueError: sound STEP twice` | samples given to one sound twice (by two `add_sound` calls with samples); a sound the document only names takes them once (2.3) |
| the build, configuring | `the pack could not be made (tools/mygame_pack.py, see above)` | the game's own message: the packer's error is printed above it |
| the build, configuring | `include could not find requested file: …/boards/cyd/board.cmake` | the engine is not where `MYGAME_ENGINE` points, or the engine downloaded is older than this document, whose boards had another layout |
| the build, configuring | `CMake Warning`s: `Source file '…/boards/cyd/cyd.c' belongs to component dgx-ge but is being built by component main` | ESP-IDF 6 says so when `main`'s build directory lies inside the engine's directory: an example built inside the engine's own repository. A game with the engine in `managed_components/`, or an example made from the registry, has none: `board.cmake` hands `main` a file of its own build directory that includes the board's file. Harmless either way |
| the computer, reading the Makefiles | `the pack was not made: <command> failed (it says why above)` | `host.mk` ran `GAME_PACK_MAKE` and it failed: the packer's error is printed above it |
| compiling | `#error "the actors of this pack keep N params, the build fewer (DGX_GE_ANIM_PARAMS)…"` | the build was not told the pack's params: `dgx_ge_params_of_pack` in `main/CMakeLists.txt`, `GAME_PACK_H` in the Makefile. On the computer, a pack made by a make rule of the game's own comes after `host.mk` read the old header: let `host.mk` make it (`GAME_PACK_MAKE`, section 4) |
| compiling | `MYGAME_…` undeclared | a name changed in the data: the header follows the data, the code must follow too |
| linking | `undefined reference to '_binary_my_game_pack_start'` | the symbols come from the embedded file's name: `mygame.pack` gives `_binary_mygame_pack_start`; a `-` or `.` becomes `_` |
| running | `the pack does not hold together` (the game's own log after `dgx_ge_pack_open` failed) | the bytes embedded are not a pack of this engine's version: a pack made by an older packer than the engine, or the wrong file embedded |
| running | a kind does not open (`dgx_ge_anim_open` or `dgx_ge_kind` gives nothing) | its blob is not an animation of this version, it declares more params than the build keeps, or there is no place left for kinds (the log says `kinds: no place for the kind of blob …`): raise the capacity given to `dgx_ge_kinds_init` |
| running | an actor stands still | it is not moved on (`dgx_ge_actor_advance` with the game's clock), its script holds, or the event sent means nothing where it is (`dgx_ge_actor_event` returns false; its rules decide, `dgx_ge_explain.py` shows them) |
| running | an actor is not seen | its set's colors not given to the frame, its layer, a later layer over it, or it is given outside `actors_begin` … `render` ([overview-en.md, If something goes wrong](overview-en.md#if-something-goes-wrong)) |
| running | text of a dialog missing or with holes | the font was made without those letters: the pack's language changed and the fonts were not made again, or a font is not marked to have the pack's texts |
| the board | the game resets, or `no memory for the frame` | RAM: fewer actors or layers, a smaller frame, fewer records of the world; measure with `heap_caps_get_free_size` |
| the board | the display stays black | the board's `sdkconfig.defaults` not in the list of defaults (DGX built without its driver links nothing), or an ESP32-P4 firmware built for another revision of the chip ([overview-en.md, 2.17](overview-en.md#217-the-board)) |

The build for the computer has its own list of what goes wrong,
[host-en.md, 12](host-en.md#12-when-something-goes-wrong).

---

The engine's [README](../README.md) is where to start, the overview
[overview-en.md](overview-en.md) explains the parts used above (the pack in
2.2, the scene in 2.5, actors and scripts in 2.7, the world in 2.10, text in
2.12, saving in 2.15, the board in 2.17), the script language is
[animation-en.md](animation-en.md), the computer is
[host-en.md](host-en.md), and the example is
[hello](../examples/hello/README.md).
