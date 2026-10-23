# Changes

## 0.3.0 - 2026-10-23

Four pieces of glue a maze game had to write in C, now the engine's. A minor version: the game's answer to `game.<name>` takes one more parameter.

- **Changed (breaking): the game's answer to `game.<name>` is told who
  asks:** `dgx_ge_ask_fn(what, who, ctx)`, `who` the actor whose script
  asks (the one advanced or sent an event, one an event reaches from inside
  another's step, an actor the engine makes for a moment to play a script:
  a cell's life, a thing, a control of a dialog, valid only during the
  call); NULL when no actor asks. One function answers every actor of a
  kind about itself, with no context swapped around each call.
- **The pack's header gives the game's constants to C:** `#define
  <PREFIX>_<NAME> <number>` for each of `Pack.add_constants` and a
  document's `constants` (a negative number in parentheses). A constant
  whose header name is another thing's, or another constant's, is refused,
  naming the constant. `add_constants` takes whole 32-bit numbers only
  (`83`, or `83.0` from JSON): `2.5`, `"83"` and `True` are refused instead
  of being cut by `int()`.
- **A walker bound to an actor** (`dgx_ge_walk_bind_t`,
  `dgx_ge_walk_bind_start`, `_play`, `_want`): the actor is put at the
  walker's point at its moment with its velocity along the way, so it moves
  smoothly between calls; it is told the side it goes as that changes, at
  that moment (`turn + side`, optionally into a param), and `stop` once when
  it stands; its speed comes from a param at each cell's middle; it starts
  from where the actor stands, even between cells. While bound the binding
  moves the actor; `actor = NULL` unbinds it, from arrive too. 16 bytes
  beside the walker on a 32-bit chip; nothing in the actor.
- **`dgx_ge_collisions_hits()`**: one call a frame sends the `hit` lines of
  the actors whose masks met, both ways (damage off the param named by
  `dgx_ge_collisions_damage()`, the event to the one met, the recoil to
  itself), in the order of the bodies' handles, then masks; an optional
  function of the game changes a hit, passes it by or stops the rest; a body
  removed by an event is not touched again. `dgx_ge_collisions_set_actor()`
  puts an actor's body in as it is drawn and keeps the actor.
  `dgx_ge_collisions_actor()` takes, and `_actor_of()` returns, a non-const
  actor. The crowd sends its hits so too: meetings of one frame now go in
  the order of the bodies' handles.

## 0.2.0 - 2026-10-22

Fixes after a review of 0.1.1, each with a host test that fails without it.

### The frame

- **Smoothed pictures leave no trails.** With Scale2x/3x or SFX at x2-x4 a
  changed place is sent with the points around it that its neighbours'
  smoothing looks at (one, two with SFX or at x4), and after a scroll the
  points at the edge the picture moved to are sent again. Unsmoothed,
  nothing more is sent; smoothed, some 6% more pixels at x2/x3 and 13% at
  x4.
- An overlay that stays still while the frame scrolls no longer leaves a
  copy where the display's move carried it.
- A scene change given after `dgx_ge_scene_window` in the same frame (e.g.
  `dgx_ge_grid_relay` on a scrolling map) is drawn in its place, not
  shifted twice.
- Upside down on a display that cannot turn its rows itself, the smoothed
  picture is exactly the upright one mirrored.

### Scripts and time

- **Motion no longer depends on how time is cut into `advance` calls.** The
  actor keeps what a call moves beyond 1/256 of a point (`rest_*`), so slow
  speeds and slow accelerations move at any call rate. An actor grows by 8
  bytes on a 32-bit chip.
- A `now` event sent to an actor from its own `say` starts the new script
  at that step's moment instead of corrupting the step; one stamped before
  the moment the actor has been moved to takes its motion back to it.
- `dgx_ge_layout_actor_start` sets `entered_flip` and `entered_params`;
  `dgx_ge_actor_shift` moves `entered_x`/`entered_y`.
- **Params are 32 bits** wherever they live in RAM: an actor's `params`
  and `entered_params`, a world record's, a pocket thing's, a cell life's
  own params, the numbers a dialog line shows. A moment of `now` kept in a
  param (`set drunk now + 10`) holds as long as the game's clock runs (its
  milliseconds come round after 49 days); it stopped at 32767 s, 9 h 6 min.
  `set`, `add` and a hit's damage stop at the limits of 32 bits; `set` and
  `add` take a constant up to ±2^24 (exact as a float, beyond 16 bits the
  assembler writes an expression). The pack's data stays 16-bit (starting
  values, tables, layouts, links): the pack is the same byte for byte.
  Saves are version 3 (params in 32 bits); a version 2 save still loads.
  On a 32-bit chip an actor grows by 4 bytes a param (PoP's 8 params: 192
  to 224 bytes), a world record from 24 to 28, a pocket thing from 8 to
  12, a cell life from 20 to 24, a talk from 108 to 116; PoP's `.bss` +520
  bytes on each board. API: `dgx_ge_strings_numbers` takes
  `const int32_t *`, `dgx_ge_cells_event` and `dgx_ge_cells_set` an
  `int32_t` value; `dgx_ge_param` and `dgx_ge_load_param` are new.
- `now` is exact at every whole second of the clock (it fell short of
  one after 37 h, so `now >= moment` came a frame late) and half as far
  off between them.

### Dialogs and text

- A choice the talk returns to with other answers (as many), or whose
  control swaps an answer, is laid out again (the talk counts the choices
  it shows, `choices`).
- A button before the talk view's first play no longer crashes: an empty
  text box draws nothing, and the view takes no event for a step it has
  not shown.
- Long answers wrap: the highlight covers all of an answer's lines, the
  list scrolls until it is in view, a tap on any of its lines picks it.
- A line with numbers and the list of answers have no length limits (they
  were cut at 95 and 511 bytes): kept on the heap, as long as needed, while
  shown. The talk view is 88 bytes smaller and a static 512-byte buffer is
  gone. A label's text is cut between letters, never inside one.
- The talk view marks and hit-tests its text right of a left portrait,
  where it is drawn.
- A translation may put a line's `{names}` in its own order; the packer
  refuses a name the line does not have.

### Tiles, masks, the world

- `dgx_ge_world_add` takes a free place before it evicts the oldest record
  not kept; `dgx_ge_world_boxes` makes all boxes or none.
- **The world's boxes run out like its places.** With no box free,
  `dgx_ge_world_new_box` takes the box of the oldest record not kept and
  forgets that record whole (it used to give 0, and a record went into the
  world without the things it held: a guard came back without his sword).
  `dgx_ge_world_add_holding` adds an object with a character's pockets in
  one call: a box, the record, everything moved; -1 and nothing changed when
  it cannot.
- Overlaps of more than 65535 points no longer divide by zero (`points`
  capped at 0xFFFF).
- Touches among more than 32 tile bodies are exact (new
  `dgx_ge_mask_touch_rect_where`).
- `dgx_ge_grid_push_out` goes on out of every body in the way, and returns
  false when it finds no way out.
- A scene holds up to 65535 set pieces and actors together (was 4095), in
  the same order of drawing.
- Shots look at the terrain at every point: a floor of one point is never
  passed.
- Walking: no jump when a turn is asked during a corner cut; a cut reaches
  at most one cell.
- Repeated items honour the picture's origin and mirroring.

### Sound and boards

- **The mixer keeps the game and the board's task apart itself.**
  `dgx_ge_mixer_t` has a `lock` that a platform filling from a task of its
  own sets before the task starts; every mixer call takes it, so
  `dgx_ge_mixer_output` is safe while a board sends sound. `free` keeps the
  lock. The mixer grows by 4 bytes.
- `board_sound_start` gives the mixer its task's lock;
  `board_sound_lock`/`board_sound_unlock` are gone (wrapping mixer calls in
  them would now deadlock). A failed write to the codec is logged once and
  waited out. The JC1060P470's GT911 is tried at 0x5D, then 0x14, and its
  I2C device is released after a failure. Each board names the DGX options,
  PSRAM and ESP32-P4 revision its sdkconfig needs.
- **Needs DGX 0.8** (the fonts DGX ships are all of free licenses: the host
  test of text layout takes DroidSans in place of Arial). The engine's host
  tests build DGX as DGX gives it for a computer, `host/dgx_host.mk`.
  `-ftree-switch-conversion` only under GCC. The README and the
  overviews agree on ESP-IDF 5.3 or newer (built with 6.0).

### A board a directory

Not a fix: it changes the paths a game names.

- **`boards/<board>/` holds everything about a board:** its file
  (`cyd/cyd.c`), `board.cmake` (was `boards/<board>.cmake`) and the ESP-IDF
  settings it needs, `sdkconfig.defaults`: the chip, the flash, DGX's bus
  and driver of its display, PSRAM, the Boya flash driver. A game's
  `include(.../boards/cyd.cmake)` becomes `include(.../boards/cyd/board.cmake)`.
- **An ESP32-P4 board has a file a chip revision besides:**
  `sdkconfig.defaults.rev3` (400 MHz, PSRAM at 250 MHz) and
  `sdkconfig.defaults.rev1` (below 3: `CONFIG_ESP32P4_SELECTS_REV_LESS_V3`,
  360 and 200 MHz). The JC8012P4A1C has both (`.rev1` built, not tried on
  such a board), the JC1060P470 `.rev1`.
- A game lists them before its own file, a later one over an earlier:
  `SDKCONFIG_DEFAULTS="<board>/sdkconfig.defaults;<board>/sdkconfig.defaults.rev3;sdkconfig.defaults"`.
  With the engine from the registry, copy the board's files into the
  project: ESP-IDF checks the files of the list before it downloads the
  components into `managed_components/`.

### The game on the computer

Not a fix: a board more, and the engine's first example.

- **`boards/host`: the computer as a board.** A game's own `main.c` runs in
  a window of SDL2 that shows a board's display (`HOST_DISPLAY`, `HOST_MM`;
  the 10"'s by default). The mouse is a finger of DGX-DI (its pad and
  keyboard pressed with it), the keys are buttons, an open keyboard takes
  typing, SDL audio pulls the mixer, NVS keeps to files, `vTaskDelay` shows
  the window. `DGX_GE_HOST_QUIT_MS`, `DGX_GE_HOST_SHOT` and
  `DGX_GE_HOST_INPUT` run a game with nobody at the computer.
- **`boards/host/host.mk`** builds a game for the computer from a Makefile
  that names its sources, embedded files, pack header and font requests:
  `make`, `make run`. Needs DGX 0.8.0 or newer (its `host/dgx_host.mk`),
  DGX-DI, SDL2 and GNU ld; tried on Linux only.
- **`GAME_PACK_MAKE` and `GAME_PACK_INPUTS`** of a game's Makefile: `host.mk`
  makes the pack again, when one of the inputs is newer than its header,
  before it reads the header's params, so the first `make` after the params
  grow builds with the new number.
- **No warnings of ESP-IDF 6 about a board's files**: `boards/` is the
  engine's own include directory (`BOARD_INCLUDE_DIRS` is empty now), and
  `board.cmake` gives `main` a file of the build that includes the board's.
- **`board_buttons_able`, `board_buttons()`** in `dgx_ge_board.h`: a board's
  own buttons (the computer's keys); the three boards have none.
- **`examples/hello`**: one actor on a scene, a label, a sound, a count in
  NVS; the same `main.c` for the computer (`make`) and for the CYD and both
  Guition boards (`idf.py`).
- **`docs/workflow-en.md`, `docs/workflow-ru.md`**: the way of a game from
  its data, whatever made them (a script of the packer's API, a document of
  the format `add_sprites` reads, or both), to the board: what comes out of
  the packer, the pack made again by the build, the C side, building, the
  loop of work, the errors by stage. The overview and the README link it;
  both go out in the package.
- **`docs/host-en.md`, `docs/host-ru.md`**: the build and run on the
  computer in a document of its own, from what to install to what goes
  wrong: where DGX and DGX-DI come from, every variable of a game's
  Makefile, what `host.mk` does step by step, how each part of a board is
  stood in for and why, runs with nobody at the computer, what the computer
  cannot tell, the errors and their fixes. The overview keeps a paragraph
  and the link; both go out in the package.

### The packer

- It refuses what used to pass silently: a prefix other than capitals,
  digits and `_`; header names that come out alike (`stand-1`/`stand_1`); a
  blob, animation or dialog named like the packer's own blobs (`STRINGS`,
  `GAME`, `TIMERS`, `STYLES`, `CHASE`, `SOUNDS`); an empty last script; a
  graph's side to no scene, an unknown side or transition, an exit or start
  by an entry the room lacks; frames of a set whose palettes disagree, or a
  value with no colour in its palette (a short first palette no longer
  shifts the colours).
- The pack header stops the build (`#error`) when `DGX_GE_ANIM_PARAMS` is
  below what the pack needs (a forgotten `dgx_ge_params_of_pack`).
- Errors of scripts and dialogs name their file (`add_anim`/`add_dialog`
  take `path=`).
- `.po` files with a BOM are read whole; `.po` and dialog texts are read in
  NFC.
- `write()` writes the pack, header and sounds whole or not at all; names
  given to the API are cleaned in the header; pixels are read without
  `Image.getdata` (going in Pillow 14).
- `tools/dgx_ge_font.py` is removed: DGX's `dgx_fonts.py json` reads fonts.
- A sound a document only names gets its samples from a later
  `add_sound` of the same name (it was refused as given twice).
- A translation of a line of no numbers may not show a `{name}` the line
  has not (it would have been printed as written).
- The template of translations (`write_pot`) begins with a header saying
  UTF-8, so `msginit` makes a `.po` of UTF-8 from it.
- The pack header ends with a line's end; its `#error` of too few params
  names the computer's `GAME_PACK_H` as well as `dgx_ge_params_of_pack`.
- `dgx_ge_explain.py --help`.

## 0.1.1 - 2026-10-21

The first version in the ESP Component Registry (0.1.0 was taken back: its
package carried the host tests).

- **The package is the include list and nothing else.** The component
  manager only adds the include list back after its excludes, so the
  manifest now excludes everything first; `test/` and `.gitignore` no
  longer go out.
- **`dgx_ge_params_of_pack` comes from `project_include.cmake`** and is
  called from `main/CMakeLists.txt` after `idf_component_register`, the
  path from there; that works for the engine taken from the registry,
  which is not there before `project()`. `dgx_ge.cmake` stays for a clone
  in `components/`.
- The README and the overviews tell how to take the engine from the
  registry (`idf.py add-dependency "jef-sure/dgx-ge^0.1.1"`), the boards'
  CMake snippets by their path in `managed_components/`, and the tests from
  the repository.
- **The scroll bar can be seen.** Its track has a colour of its own
  (halfway between the box's fill and its border), the bar under the chosen
  answer stops short of it, and it has an arrow at each end. The whole bar
  is one target for a finger, the box's height and 10 points past its right
  edge: a tap by an end scrolls a line, on the track above or below the
  knob a page, and the knob follows a drag (a button with the new widget
  flag `DGX_GE_WIDGET_DRAG`). It is 5 points wide; controls move left of it.
- **Input fields in dialogs.** An answer can edit a text of the game
  (`"Name" field name`, `Pack.add_field`, `dgx_ge_field_t` in the cast):
  activate starts editing, `dgx_ge_talk_view_type` takes characters from
  any keyboard, a `_` caret blinks, `dgx_ge_talk_view_editing` says where
  the field is. `Pack.add_font(..., typed=True)` marks the font that shows
  typed text: it also gets the letters of the field's keyboard layout (a
  `<pack>.typed` file beside the letters).
- **`dgx_ge_frame_cover`**: a rectangle of the display the frame does not
  send to, for something that draws there itself (a keyboard on the
  display), exact to the pixel at any scale.
- **The boards' keyboard**: `board_keyboard_open`, `_close`, `_typed` on the
  two ESP32-P4 boards, DGX-DI's keyboard taking its fingers before the
  gamepad and the game; the CYD has none.
- RAM: the talk view grows by 112 bytes on a 32-bit chip (one more widget
  for the bar, the arrows' pictures); the state of editing, 52 bytes and the
  fields' text boxes, is allocated only when a field is shown, and the
  keyboard's 0.8 KB only while it is open. The frame grows by 8 bytes.

## 0.1.0 - 2026-10-21

Needs DGX 0.7.2 or newer.

- **Games from the sprite editor's file.** `Pack.add_sprites` now reads the
  whole game, not only sets and animations: scenes (a blob `SCENE_<NAME>`
  each, opened by the new `dgx_ge_layout.h`: layers, pictures, zones,
  entries, actors started as laid, grids), the graph of scenes and its maps,
  links and the links an entry fires, and the game's own data (constants,
  timers, sounds, crops, shows, chases). The cells of a map are numbered
  through all its scenes, and a scene keeps the first link of each source
  (`dgx_ge_layout_cell_at`, `dgx_ge_layout_links_of`).
- **Names instead of numbers in scripts.** A mask may be named where one is
  expected (`masks BODY TIP`, `hit TIP on BODY`), and the game's constants
  (`Pack.add_constants`, or `constants` in the editor's file) stand wherever
  a number may, in animation scripts and in the conditions of dialogs.
- **Fonts as DGX's font requests.** `Pack.add_font` takes a size in points of
  the frame (mono, a family) or a font used as it is; `Pack.write_fonts`
  writes the requests in DGX's format beside the pack, and
  `Pack.write_letters` the letters the pack prints, numbers of placeholders
  included. `dgx_ge_fonts(<file>)` in `project_include.cmake` hands them to
  DGX; the pack's header gives `<PREFIX>_FONTS(X)` for a table by name. The
  engine's own font tool (`tools/dgx_ge_fonts.py`) is gone.
- **Smoothing** by DGX everywhere, under overlays too: `dgx_ge_frame_smooth`
  takes `DGX_GE_SMOOTH_NONE`, `_ON` (Scale2x, Scale3x, Scale2x twice at 4)
  or `_SFX`; the engine's own Scale2x is gone.
- **Boards** the games have run on, in `boards/`: the CYD and the Guition
  JC8012P4A1C and JC1060P470, with a common `dgx_ge_board.h`, a CMake
  snippet each, display, touch, on-screen pad and I2S sound. Not built by
  the library; a game takes one or copies it.
- **Dialogs** may pause the game while they are open (`pause`,
  `DGX_GE_DIALOG_PAUSE`, `dgx_ge_talk_pauses`); pictures of a layer in
  display pixels follow the frame's scale (`dgx_ge_item_t.scale`).
- **Input**: the standard game pad's buttons (B, X, Y, HOME, shoulders,
  triggers, sticks' presses), a swipe from an edge, touch in dialogs and
  widgets; `dgx_ge_input_di.h` joins a DGX-DI pointer to the engine's input.
- The frame takes what changed in DGX's overlay layer.

## 0.0.1 - 2026-10-18

- The first published version: the engine's logic whole (frame, pack,
  scene, actors and their scripts, tiles, masks, the world, dialogs, text,
  widgets, cut-scenes, saved games, the sound mixer), the tools that make a
  pack, the tests on the host and the documents. No board code: a game sets
  up its own hardware.
