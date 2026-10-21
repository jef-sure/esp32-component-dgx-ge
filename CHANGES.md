# Changes

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
