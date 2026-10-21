# DGX-GE: an engine for your own games on a microcontroller

[Русская версия](overview-ru.md)

DGX-GE is a small engine for 2D and 2.5D games on ESP32 microcontrollers,
built on top of the DGX graphics library. I had wanted a game engine of my
own for a long time. I had ideas about what it should be, but only in
pieces: a game is pictures, animations and rules rather than code, and time
in it is real time rather than "so much per frame". To make them fit
together I needed a real game that would not let me skip anything, and I
picked the smallest one that was enough: Prince of Persia of 1989. It builds
on the engine, and its traps, doors, guards and fights are described as
data: there is far less game-specific C in it than in the original project.

Today the engine is an ESP-IDF component: a scene of layers that works out
by itself what to redraw; actors that live by animation scripts written as
text; levels made of tiles with rules; collisions by masks; a world that
goes on living with nobody watching, with traps, timers and chases; dialogs,
labels, cut scenes and saved games. It is broader than its first game. To
check that, I wrote Pac-Man on it, which took a couple of hours, and along
the way the engine learned to walk from cell to cell and to keep labels on
the screen. It keeps growing toward other styles of 2D and 2.5D games, so
that a game is left with nothing to write but its own ideas. Neither Prince
of Persia nor Pac-Man comes with it: the examples here are taken from them
because they are what the engine was tested on.

Most of the engine is game logic: scripts, rules, the world, collisions,
dialogs, saving. That is plain C that knows nothing about the display or the
chip. Its ties to the hardware are thin. It draws through DGX, but the DGX
calls sit in four files out of twenty-seven: the frame, pictures, text and
the scene. From ESP-IDF it needs only the component build and a few log
messages. So the engine builds on a computer without trouble, and there are
no boards in it at all: which screen and which buttons is for the game to
decide. I run it on a CYD (ESP32-2432S028R, an ESP32 with a 320×240 screen)
and on two Guition boards with an ESP32-P4: the JC8012P4A1C (10 inches,
1280×800, the game's picture four times larger) and the JC1060P470
(7 inches, 1024×600, three times larger).

This text is for people who want to make a game but have never done it.
How games work is explained along the way. You need a little C (variables,
functions, structs) and to know how to run a Python script.

**How to read it.**

- [Part 1](#part-1-how-a-game-works-without-code): the ideas, without code.
  Read all of it.
- [Part 2](#part-2-the-engine-part-by-part): the parts of the engine one at
  a time; come back to it as you need them.
- [Part 3](#part-3-try-it): how to build and run.
- At the end, a [glossary](#glossary) of all the engine's words.

The API in detail is in the headers, `include/*.h`. How an animation script
lives in time is in [animation-en.md](animation-en.md).

---

## Part 1. How a game works, without code

### A game is a world where things act on each other

What makes a game interesting is not its pictures but what happens in it.
The prince steps on a floor plate, and in the next room a gate rises; a few
seconds later it comes down by itself. The tip of a sword catches a guard:
the guard is wounded and backs away, and if he is out of lives, he falls.
The prince reaches for a jug and the jug ends up in his hand; he drinks and
gains a life. Every time someone acts, someone responds, and there are
rules behind it.

Everything that acts and responds like this the engine calls an **actor**.
That is not only the hero and the enemies, but also the sword, the jug, the
gate, the falling plate. Each actor has a state of its own: standing,
running, wounded, open, falling. There are things that can happen to it,
**events** such as "wounded", "taken", "open". And there are rules for how
it responds to them.

This is what the engine was made for: so that such connections are written
down as **data**, not as code.

- An actor's **script**: what it does and how it looks while doing it.
- The **transition matrix**: how it responds to events.
- **Masks**: what touches what: a sword's tip and a body, a hand and the jug.
- **Links**: what sets off what: the plate sets off the gate.
- **Timers**: what happens later: the gate will come down.

The game's code is left for what truly belongs to the game: "the right
arrow makes the prince run", "on this level time runs faster".

Underneath all this runs a **loop**, as in any game: see which buttons are
pressed; let the world live through the time that has passed, with actors
moving, meeting and responding to each other; draw what has changed. The
engine takes care of drawing and of time entirely.

### The theater: the engine's words

The engine's words come from the theater, and that is a good hint at how it
works:

| Word | In the theater | In the engine |
| --- | --- | --- |
| **Scene** | where the play is performed | what is visible now: a room, a level, a maze |
| **Set** | walls, furniture | what does not move: walls, floor, columns. Given once, when the scene is shown |
| **Actors** | those who act | the hero, enemies, a sword, a jug, a gate, a falling plate |
| **Script** | a role | what the actor does, how it looks and how it responds to what happens to it |
| **Director** | decides what comes next | your game: "button pressed: jump", "out of lives: game over" |

An actor does not decide *when* to jump; the game does. But *how* the jump
looks (which pictures, how high, how long it lasts) and what happens if the
actor is wounded in mid-jump is written in its script, not in code.

### Animation is text

How an actor moves is written not in code but in a small text file,
`.anim`. Here is a real piece of animation from Pac-Man, which the engine
was tested on:

```
event left, right, up, down, stop, die

script left                     # chews while moving left
    frame PAC_LEFT_1 40ms       # a picture for 40 milliseconds
    frame PAC_LEFT_2 40ms
    frame PAC_LEFT_1 40ms
    frame PAC_FULL 40ms
    goto left                   # and round again

script still_left               # ran into a wall: the mouth freezes
    frame PAC_LEFT_1 1000ms
    hold                        # stay on the last frame

script dying
    frame PAC_DIE_0 133ms
    ...
    frame PAC_DIE_10 133ms
    frame none 500ms            # nothing for half a second
    signal 1                    # tell the game: "that's it, dead"
    hold

group any: whole, right, up, left, down, still_right, still_up, still_left, still_down
on left: any -> left now        # event left: from any of them, straight into left
on stop: left -> still_left now
on die: any -> dying now
```

There are three ideas here.

- A **script** is a sequence of pictures, each shown for a time, with
  commands between them (move, turn around, tell the game).
- **Events** are what the game tells an actor: "left", "stop", "die".
- **`on` rules** say which script to switch to on an event: at once
  (`now`), at a convenient moment (`branch`) or when the current script
  ends (`end`).

The game's code only sends events, for example "left arrow pressed" →
`left`. Everything else is data, and it can be changed without recompiling
the C.

### Who touched whom

To find out whether a sword hit a guard, pictures have **masks**: invisible
zones such as "body", "blade", "shield". The script says which masks are
active at the moment: the tip, say, only during a lunge. The engine finds
where the masks of different actors overlap and tells the game. The game
decides what to do: wound, knock back, parry.

### 2.5D: who is in front of whom

"2.5D" means flat pictures that have depth: the hero walks *behind* a column
but *in front of* a wall. The engine does this with **layers**. Layers are
drawn one after another, like a stack of transparent sheets:

```
  layer 0: the far wall
  layer 1: the floor, the back half of a column
  layer 2: the hero and the enemies
  layer 3: the front half of the column       ← covers the hero
```

The column is drawn as two pieces in different layers, and the hero ends up
between them. How many layers there are and what goes into which is up to
the game. Prince of Persia has five layers, Pac-Man three: the maze, the
actors, the labels.

Within a layer, things can also be ordered by a **key**. In a game seen
from above, for example, the key is the y of the feet: whoever is lower on
the screen is closer to the viewer and is drawn later.

### Time is real time

There are two ways to move a hero:

- "2 points per frame";
- "60 points per second".

The first is simpler, but then the hero is slower on a slow board and
faster on a fast one. The engine uses the second: everything moves by
**real time**. A slow board shows fewer frames, but the hero arrives at the
same place at the same time.

Your game has to keep this in mind as well. If the code itself checks "no
floor here: fall", the check happens when the loop gets to it, not at the
moment the floor ran out. So important decisions are made:

- at the moment of an event, which the engine delivers with its exact time
  ("the foot touched the floor at 1503 ms");
- or on an even **tick**, for example no more than 12 times a second, as in
  Prince of Persia.

More on this in [animation-en.md](animation-en.md), section 2.

### A world larger than the screen

A level usually does not fit on the screen. There are several ways to deal
with that:

- **Rooms**, as in Prince of Persia: the screen is one room, and a room has
  neighbors to the left, right, above and below. The engine keeps a map of
  how they connect, called the **scene graph**.
- **Scrolling**: the screen is a window onto a large world, and it follows
  the hero.
- **A grid of cells** (tiles): the level is built of identical small
  squares, like a tiled floor. That is easy to store and easy to change:
  opening a door means changing one cell.

### A game is data

The engine's main principle: a game is a collection of **pictures,
animations and rules**, with as little C code as possible. Traps, doors,
buttons, potions, guards and dialogs are described as data, and the engine
knows how to run them.

The data is prepared on the computer by Python scripts. The script checks
everything that can be checked in advance: typos in names, references to
pictures that do not exist, rules that contradict each other. An error
shows up right away at build time, not on the board half an hour into a
game.

### The board is small, and the engine never forgets it

All of this runs on a microcontroller with hundreds of kilobytes of memory,
not gigabytes, and a screen attached by a slow bus. So the engine is very
sparing with memory and with the screen. The game hardly needs to think
about it, but it helps to know where some of the engine's habits come from.

**Pictures are color numbers.** Pictures are stored not as colors but as
numbers into a table of colors, a **palette**, like a paint-by-numbers
picture: 4 bits a point instead of 16. Number 0 is always transparent. The
game's frame in memory is numbers too: 320×200 points take 64 KB rather
than 128, and they are turned into the display's colors on the way to the
screen. As a bonus, palette effects are almost free: change the palette and
the screen darkens, flashes or changes color, without redrawing a single
picture.

**Nothing is copied needlessly.** Pictures, animations and all the game's
data are read straight from flash, where they lie. Anything that is not
doing anything is kept as a small record rather than as an actor: a thing
in a pocket is 8 bytes, a flying bullet 16, an object in a room nobody is
looking at is a record of when and where it entered its script. Only what
is living on the scene right now becomes an actor, which is about two
hundred bytes.

**Only what changed goes to the screen.** Sending a whole frame to the CYD's
screen takes about 32 ms, so done that way you cannot get more than 30
frames a second. But usually little changes: the hero took a step, a torch
flickered. The engine finds the changed places itself and sends only those;
in Prince of Persia a frame usually costs 1–2 ms. Each time the game simply
says where the actors are now, and the engine compares that with the last
time.

---

## Part 2. The engine, part by part

### 2.1. From a drawing to the screen

```
  ON THE COMPUTER                               ON THE BOARD
  PNG drawings
  .anim texts, dialogs, levels
            │
            ▼
  the game's Python script ──► .pack file ──► in flash, read in place
  (the Pack class)             + a C header         │
                                 of numbers         ▼
                                           pictures, colors, animations, data
                                                    │
         buttons ──► game ──► actors ──► scene ──► frame ──► display
```

### 2.2. The resource pack

**Why.** All the game's pictures, animations and data are gathered in one
file. It lies in the board's flash and is read straight from there; nothing
is copied into RAM.

**How.** The pack is built by the game's Python script with the `Pack` class
from `tools/dgx_ge_pack.py`:

```python
from dgx_ge_pack import Pack

pack = Pack("MYGAME")
hero = pack.add_set("HERO", colors)                     # a set of pictures with colors of its own
pack.add_png("HERO_STAND", hero, "stand.png", origin="bottom-left")
pack.add_blob("LEVEL_1", open("level1.bin", "rb").read())
pack.add_anim("HERO", open("hero.anim").read())
pack.write("mygame.pack", "mygame_pack.h")
```

Along with the pack you get a C header in which everything has a number:
`MYGAME_IMG_HERO_STAND`, `MYGAME_HERO_SCRIPT_RUN`,
`MYGAME_HERO_EVENT_JUMP`. The code refers to resources only by these
numbers, so the compiler catches a typo in a name.

On the board:

```c
dgx_ge_pack_t pack;
dgx_ge_pack_open(&pack, data, size);                    /* copies nothing */
dgx_image_t img;
dgx_ge_pack_image(&pack, MYGAME_IMG_HERO_STAND, &img);  /* the pixels stay in the pack */
size_t n;
const uint8_t *level = dgx_ge_pack_blob(&pack, MYGAME_BLOB_LEVEL_1, &n);
```

A **blob** is just bytes, whose meaning is known to whoever reads them. That
is how levels, animations, dialogs and game constants are kept in the pack.

Besides pictures and animations, the pack can hold:

- masks;
- anchor points (where the hero's hand is, to put a sword in it);
- the scene graph;
- timers;
- dialogs and their lines in the chosen language;
- styles for showing text;
- game constants.

`Pack` has an `add_...` method for each kind of data.

### 2.3. Pictures and colors

A **picture** is DGX's `dgx_image_t`, 1, 4 or 8 bits a point. **0 is always
transparent.**

- **1 bit:** a single color, which the game supplies. Good for silhouettes
  and text.
- **4 bits:** values 1..15 are the colors of its set.
- **8 bits:** values 1..255.

A **set** is a group of pictures with shared colors: "hero", "dungeon",
"guard".

The **origin** is "where the picture's feet are". The picture is placed on
the scene by this point and mirrored around it when the character turns
round. If the hero's feet are somewhere else in a jump frame, that is drawn
by the artist, not handled by code.

**Colors on the screen.** The frame has one palette of 256 entries. When the
game starts using a set, it hands the set's colors to the frame and gets
back a table "picture value → index in the frame":

```c
uint8_t map[255];
const uint8_t (*rgb)[3];
int n = dgx_ge_pack_set_colors(&pack, MYGAME_SET_HERO, &rgb);
dgx_ge_frame_add_colors(frame, rgb, n, map);   /* map[v-1] is the frame's index for value v */
```

Equal colors of different sets share one index. On a new level
`dgx_ge_frame_forget_colors()` frees the palette. Index 0 of the frame is
the **paper**, the background color.

### 2.4. The frame

**Why.** The frame (`dgx_ge_frame_t`) is the game's picture in RAM plus a
list of the places that changed. The scene draws into the frame, and the
frame sends only what changed to the display.

```c
dgx_ge_frame_t *frame = dgx_ge_frame_create(display, x, y, 320, 200);
...
dgx_ge_frame_present(frame);    /* send what changed */
```

You usually do not need to call `present` yourself: `dgx_ge_scene_render()`
draws and sends on its own.

- **Cost.** Width × height bytes: 320×200 is 64 KB, plus about 1.5 KB on top
  (palettes, the list). The same frame in 16-bit colors would take 128 KB.
- **Nearby places are sent together.** Every transfer to the display costs
  the time to "open a window": about 58 µs on a CYD, as much as 100 extra
  points. So two nearby rectangles are sent as one when the gap between
  them costs less than a window.
- **Palette effects:** `dgx_ge_frame_set_palette` for darkening, a flash, a
  tint. The picture is not redrawn, but the whole frame goes to the
  display.
- **Scale** (`dgx_ge_frame_scale(frame, n)`). The frame is shown n times
  larger: on the 10-inch display 320×200 ×4 is exactly 1280×800. It costs
  almost no memory, since the enlarging is done on the way out, row by row.
  The game and the scripts notice nothing.
- **Smoothing** (`dgx_ge_frame_smooth`). When the frame is enlarged 2, 3 or
  4 times, the steps of slanted edges are filled in, as in emulators of old
  consoles: `DGX_GE_SMOOTH_ON` is Scale2x (Scale3x at ×3, Scale2x twice at
  ×4), `DGX_GE_SMOOTH_SFX` their SFX variants, which keep thin slanted lines
  whole but cost more. The colors stay the same. DGX does the work, the
  same way with overlays and without.
- **Overlays in display pixels.** On a large display, text and dialogs are
  drawn at the real resolution over the enlarged frame, not as big squares.
  This takes hardly any extra memory. Pictures in them (controls, a
  portrait, a border) are enlarged to the frame's scale in blocks, like the
  game itself: a scene item has a `scale` field for this.
- **A layer over the frame** (DGX 0.6.6, `dgx_overlay.h`). A translucent
  picture over the game, such as on-screen buttons, lives in a virtual
  screen of its own attached to the display, and DGX mixes it with the
  frame on the way out. Before each transfer the frame asks DGX what changed
  in the layer and sends that place again, even if nothing moved in the
  game. The game does not need to do anything for this. The layer is
  visible only over the game's picture: beyond its edge the frame sends
  nothing.
- **Scrolling** (`dgx_ge_frame_scroll`). When the scene's window has moved,
  the frame's pixels are moved in memory, and the display, if it can, moves
  the picture by itself. Only the strips that came into view are drawn.

### 2.5. The scene

**Why.** The scene (`dgx_ge_scene_t`) knows what is drawn where and works
out by itself what to redraw. The game describes what stands where and
draws nothing itself.

```c
dgx_ge_scene_t *scene = dgx_ge_scene_create(frame, 4, 300, 16);  /* layers, set pieces, actors */
dgx_ge_scene_layer(scene, LAYER_SKY, 0, DGX_GE_ORDER_ADDED);     /* the sky stays put */
```

#### An item is anything you can see

Anything you can see is an **item** (`dgx_ge_item_t`):

- a picture and the table of its colors;
- the x and y of its origin;
- a layer and a key;
- a clipping rectangle of its own, if needed;
- flags: mirrored, a one-color silhouette, a fill (a rectangle of color
  with no picture), a card (a picture on a rectangle of its own color).

For example, this is how an actor is put on the scene:

```c
dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = im, .map = maps[set],
                                           .x = x, .y = y, .layer = LAYER_ACTORS});
```

#### The set and the actors

- The **set** is given when the scene is shown (`dgx_ge_scene_add`). A
  piece of the set can be changed later by its number
  (`dgx_ge_scene_change`): another frame of a flame, a raised gate.
- **Actors** are given anew every frame, between
  `dgx_ge_scene_actors_begin()` and `dgx_ge_scene_render()`. The scene
  compares them with the previous frame itself. An actor that has not moved
  is not redrawn.

The set and the actors of one layer are sorted together, so a piece of the
set can stand in front of one actor and behind another: the key decides.

#### Redrawing, and rebuilding quietly

Nothing is saved from under sprites. A changed place is simply drawn again,
layer by layer from the bottom up. The frame is in memory anyway, and this
is cheaper.

If you need to rebuild the whole set but send only a small piece (one tile
changed), there is a "quiet" mode: between `dgx_ge_scene_set_begin()` and
`dgx_ge_scene_set_end()` whatever is added marks nothing, and the game says
itself what changed (`dgx_ge_scene_invalidate`).

#### The window and parallax

The **scene's window** is the point of the world that is at the top-left
corner of the screen right now (`dgx_ge_scene_window`). There is no separate
camera: scrolling is the window moving.

Each layer has its own **window factor**:

- 256: the layer moves with the window;
- 128: half as fast, a distant background. This is **parallax**, and it
  gives a sense of depth;
- 0: the layer stays put: the sky, the lives bar.

#### Scene points and touches on the screen

All the engine's coordinates are in **scene points**, not display pixels.
Usually a scene point is a pixel of the game's graphics. That way the game's
logic does not depend on which display it is shown on or at what scale.

When you need to convert one into the other (where did the finger touch),
there are ready-made functions:

```c
int x, y;
if (dgx_ge_scene_point_at(scene, layer, touch_x, touch_y, &x, &y)) ...  /* what is under the finger */
dgx_ge_scene_pixel_at(scene, layer, x, y, &sx, &sy);                     /* where the point is on the display */
```

#### A scene from the editor

A scene does not have to be assembled in code: it can be drawn in the
sprite editor, with layers, pictures on them, characters, zones, entrances
and tile grids. The packer puts each such scene in as a blob
`SCENE_<NAME>`, and the names of its objects as constants in the header
(`<PREFIX>_SCENE_<SCENE>_ZONE_<NAME>` and so on). The game lays it out
through `dgx_ge_layout.h`: the layers and pictures go into its scene, the
characters are started, the zones and entrances are taken by number, the
grids are put in place. To the engine such a scene is no different from one
built in code.

```c
dgx_ge_layout_t l;
dgx_ge_layout_open(&l, blob, size);
dgx_ge_layout_scene(&l, scene, pack, maps, images);                  /* layers and pictures */
dgx_ge_layout_actor_start(&l, GAME_SCENE_ROOM1_ACTOR_GUARD, &guard, kinds, now);
dgx_ge_layout_zone(&l, GAME_SCENE_ROOM1_ZONE_DOOR, &door, &kind);   /* a rectangle and a kind */
```

### 2.6. Tiles: a level made of cells

**Why.** Many levels are built of identical small squares: floor, wall,
brick, a dot in a maze. Storing a level as a grid of tile numbers is
compact, and changing it is easy.

**How.** A grid of cells (`dgx_ge_tiles.h`):

- a cell holds a tile number (0 is empty); the game keeps the cells and
  changes them;
- a tile is drawn in **pieces**, and each piece goes into its own layer.
  That way one cell ends up partly behind the hero and partly in front of
  him, and can reach into neighboring cells;
- a tile has **solid sides**: top, bottom, left, right. From them the engine
  knows where one can stand and what one runs into.

```c
dgx_ge_grid_t grid = {.tileset = &tiles, .maps = maps, .cells = cells, .cols = 20, .rows = 12,
                      .layer_of = {LAYER_BACK, LAYER_FRONT}, .edge = 3};
dgx_ge_grid_add(&grid, scene);            /* the pieces of every cell go into the scene's set */
```

When a cell changes, the grid lays out again only what the change reaches
(`dgx_ge_grid_relay`). For that the game gives it a little memory (`laid`:
2 bytes a cell). If relaying fails, the game rebuilds the whole set
"quietly":

```c
uint16_t laid[20 * 12 + 1];
grid.laid = laid;
dgx_ge_grid_add(&grid, scene);
/* ... cell col, row has changed ... */
if (!dgx_ge_grid_relay(&grid, scene, col, row, false)) {
    dgx_ge_scene_set_begin(scene);
    dgx_ge_grid_add(&grid, scene);
    dgx_ge_scene_set_end(scene);
}
```

**Piece rules** are what makes tiles powerful. Almost everything old games
did in code is data here:

| Rule | What it does | Example |
| --- | --- | --- |
| `around` | the piece is drawn only if the neighbors are of a given kind | grass where above is empty |
| `by_mod` | picture and offset by the cell's state (its **modifier**) | a door closed or open |
| `cycle` | pictures in a loop over time | water, a torch |
| `variant`, `pick` | variety worked out in advance | different cracks on bricks |
| `when` | a condition as an expression | the piece is there only if… |
| `with` | the piece is laid together with a neighboring cell | the right edge of a floor |
| fill, `draw`, `color` | a rectangle of color, a card, a silhouette | — |

A **modifier** is a number in a cell that the game or a script changes: how
far a gate is raised, which frame the spikes are on.

What data cannot express is drawn by a **hook** of the game: a function the
engine calls for each cell. A Prince of Persia room needs no hook; it is
data from end to end.

Details are in `include/dgx_ge_tiles.h`.

#### Who hides whom when there are few layers

Normally the layer and the key decide what is drawn in front of what. But
sometimes the hero's head goes up into a ceiling slab during a jump, or a
hand into a wall. For such cases tiles have "closing" sides (`closes`).
From them the scene hides the parts of an actor that are under the ceiling
or behind the wall (`dgx_ge_scene_space`, the actor flag
`DGX_GE_ITEM_OPEN_SPACE`). And a door with stairs behind it has an opening
(`opening`): an actor that has gone into it (flag `DGX_GE_ITEM_INSIDE`) is
visible only through the opening, hidden by the door jamb and the raised
door leaf.

### 2.7. Actors and scripts

#### The actor

An **actor** (`dgx_ge_actor_t`) is something that moves by a script. It
has:

- a position x, y, a speed and an acceleration (in 1/256 of a point, so
  that slow movement is smooth);
- the picture it is showing now;
- a direction (mirrored or not);
- active masks;
- **params**: numbers the actor keeps with it: lives, "what I am doing
  now".

```c
dgx_ge_anim_t anim;
dgx_ge_anim_open(&anim, dgx_ge_pack_blob(&pack, MYGAME_BLOB_HERO, &n), n);
dgx_ge_actor_start(&hero, &anim, MYGAME_HERO_SCRIPT_STAND, x, y, now_ms);
...
dgx_ge_actor_advance(&hero, now_ms, say, ctx);                    /* what it has become by "now" */
dgx_ge_actor_event(&hero, MYGAME_HERO_EVENT_JUMP, now_ms, say, ctx);
```

An actor is not a scene item. Every frame the game makes an item out of it:
it takes the picture by number, the position and the flags, and gives them
to the scene.

**`advance` always catches up with "now".** However much time has passed
since the last call, all the steps of the script are carried out in order.
The signals of the steps that were skipped all reach the game, each with
its own time. Time comes from the game's clock, which stands still while
the game is paused.

**`say`** is the game's function through which the script talks to it: "a
step touched the floor", "sound 3", "the hand let go".

#### Script commands

The full description of the language is at the start of
`tools/dgx_ge_anim.py`; how a script lives in time is in
[animation-en.md](animation-en.md). The commands you need most:

| Command | What it does |
| --- | --- |
| `frame KID_15 83ms` | a picture for a time, optionally with an offset `at 2,-1`; `frame none` shows nothing |
| `dx 4`, `dy -2` | move (forward is the way the picture faces) |
| `flip` | turn around |
| `velocity`, `accel` | speed and acceleration: flying, falling |
| `signal 3`, `sound 1` | tell the game; play a sound |
| `masks 0 2` | which masks are active now |
| `hit 2 on 1 damage 3 event hurt` | what mask 2 does when it meets mask 1 |
| `set`, `add` | change a param |
| `branch` | here the script may be cut short by a `branch` rule |
| `goto run`, `goto turn if x < 166` | go to another script, optionally on a condition |
| `hold` | stay on the last frame |
| `carry`, `release`, `draw`, `stow`, `shoot` | hold in a hand, let go, take out of a pocket, put away, shoot |
| `contact on landed` | watch for touching the floor and walls, and send an event |
| `timer start bomb` | start a timer |
| `tell`, `press`, `appear`, `vanish` | tell another actor, press a button on the map, call in an actor, disappear |

A script with no `goto` or `hold` at its end runs on into the next one in
the text.

#### Expressions

In conditions and numbers you can write **expressions**: comparisons,
`and`, `or`, `not`, arithmetic, random numbers `rnd(255)` and **constant
tables**:

```
table strike_chance 75 100 75 50
goto strike if strike_chance[skill] > rnd(255)
```

This is how a Prince of Persia guard decides whether to strike: by data,
not by code.

#### The transition matrix

Taken together, the `on` rules form a **matrix** of script × event:

```
group ground: stand, run
on jump: ground -> leap now      # at once
on stop: run -> stand branch     # at the nearest branch point
on jump: stand -> leap end       # when the script reaches its end
```

- An empty entry of the matrix: the event does nothing here.
- The order of the rules means nothing; there are no priorities.
- Two different responses to the same event in the same script are an
  error, and the pack build will catch it.
- **The game checks the conditions.** Whether the hero can jump right now
  is decided by the code, which sends the event only when he can. The
  matrix says what follows from the event.

#### Params

Params are declared in `.anim` with an initial value; the script changes
them with the `set` and `add` commands, and the game reads them by number:

```c
if (hero.params[MYGAME_HERO_PARAM_HP] <= 0) dgx_ge_actor_event(&hero, MYGAME_HERO_EVENT_DIE, now_ms, say, ctx);
```

How many params each actor keeps is worked out by the pack build: as many
as the greediest animation needs (Part 3, "Try it", `dgx_ge_params_of_pack`).

#### Scripts in plain words

`tools/dgx_ge_explain.py` retells scripts in words, in Russian or in
English: what each state does, how long it lasts, where it leads, who sends
which event and who responds to it. It helps you make sense of someone
else's animation (and of your own old one).

```
- “taken” (`taken`): sent by KID (reaching: hit); answered by POTION: lying → in hand; …
```

### 2.8. Collisions

**Masks** are 1-bit zones of a picture, numbered 0..15: body, blade,
shield. They are set by the pack build script (`Pack.add_mask`) or by the
sprite editor; for a picture without masks, its opaque points serve as the
mask. Masks are mirrored together with the picture.

```c
dgx_ge_body_t hero  = {&pack, hero_image, hx, hy, hero_flip, hero_actor.masks};
dgx_ge_body_t enemy = {&pack, enemy_image, ex, ey, enemy_flip, enemy_actor.masks};
dgx_ge_bodies_overlap(&hero, &enemy, 8, hit, ctx);   /* hit(mask_a, mask_b, areas, count, ctx) */
```

The answer is not separate points but **areas** of overlap, each with its
bounding box, center and number of points. What follows from that is up to
the game.

When there are many bodies, a **registry** keeps them
(`dgx_ge_collisions_t`). `dgx_ge_collisions_look()` checks only the bodies
that moved, and only against those they can reach. Who meets whom at all:

- bodies of the same **owner** (a hand and the sword in it) never meet;
- bodies of the same **side** (the hero and the stone he threw) meet by the
  `own` table, bodies of different sides by the `foreign` table. Both live
  in the pack:

```
foreign TIP BODY      # a tip wounds
foreign TIP BLADE     # a blade parries
own STONE BODY        # your own stone can fall on your head
```

Collisions only report; they never move anyone.

### 2.9. Things, pockets, shots

- A **thing** is an actor with an animation of its own: a sword, a jug, a
  dagger. Its properties (poison, how much it heals) are in its script.
  While a thing is in a hand, the engine draws it at the **anchor point** of
  the holder's current picture; otherwise it lives on its own. Picking up,
  holding and throwing are script commands.
- **Pockets** (`dgx_ge_pockets.h`) are what a character carries besides its
  hands: a sword in its sheath, daggers in a belt. A thing in a pocket does
  nothing, so it is not an actor but an 8-byte record. The script takes
  things out and puts them away (`draw hand SWORD`, `stow hand`).
- **Shots** (`dgx_ge_shots.h`) are bullets and arrows. There are many of
  them and they live briefly, so they are not actors either, but records of
  16 bytes each. Where a shot is now is computed by a formula. The engine
  itself finds what a shot hits first along its path: a wall or a body.

### 2.10. A living world

All of this is needed once a game has something besides the hero and the
enemies on the screen.

- The **scene graph** (`dgx_ge_graph.h`) is the map of how scenes connect.
  It holds the scenes and their neighbors on each side, named exits ("the
  door to the cellar"), entry points and the kind of transition: a sharp
  `cut`, a `fade` through black, a `wipe`, or `seamless`. The map can have
  rules of its own: the engine tells them that a scene was entered, or left
  by one of its sides, and the rules decide what follows.
- **World objects** (`dgx_ge_world.h`) are what lies in scenes nobody is
  looking at: a dropped sword, a broken plate. What is kept is not an actor
  but a record of when and where it entered its script. When the hero comes
  back to the room, the game restores the actor and brings it up to "now",
  as if it had been living all along. The records are kept in a ring: when
  there is no room left, a new one takes the place of the oldest.
- **Living cells** (`dgx_ge_cells.h`) are cells with a life of their own: a
  gate rises and falls, a plate shakes and drops, spikes spring out, a torch
  burns. What exactly they do is an animation script run over the cell.
- **Links** (`dgx_ge_links.h`) say who sets off what: a plate underfoot
  opens a gate in another room. This is data too. Cells in links are named
  by numbers, one numbering for the whole map; for scenes from the editor
  the packer assigns them, numbering the cells of the grids of the map's
  scenes one after another, and a scene remembers the first link of each of
  its sources: a cell, a character, a zone (`dgx_ge_layout.h`).
- **Timers** (`dgx_ge_timers.h`) are events by time: once, every N ms, or
  until a deadline. A timer belongs to the game, a map, a scene or an actor.
  It can keep running whatever happens (a grenade explodes even if everyone
  has left), or be reset when the player leaves its scene (an alarm goes
  quiet once you are out of the room) or its map.
- A **chase** (`dgx_ge_chase.h`) is an enemy that follows the hero into
  neighboring scenes. Whether it sees the hero or hears him, which sides it
  passes through, how far from home it goes, when it calms down: all of
  that is data.
- **Game constants** (`dgx_ge_game.h`) are numbers the author writes: how
  many timers, how many world objects, "an hour to save the princess".

### 2.11. Walking from cell to cell

`dgx_ge_walk.h` moves from the middle of one cell to the middle of the
next, as in Pac-Man, Bomberman or Snake:

- a turn pressed in advance happens at the first cell where it is
  possible;
- it does not walk into a wall but stops;
- it can leave across the edge of the grid and come out on the other side
  (a tunnel);
- it can cut corners, like the real Pac-Man.

The game says where walking is allowed. In each cell the game learns
"arrived" at the exact moment of arrival and can choose where to go next (a
ghost picks its way toward its target: `dgx_ge_walk_toward`).

### 2.12. Text: labels, dialogs, fonts

- A **label** (`dgx_ge_label.h`) is a line of text that stays on the screen
  all the time: the score, lives, "READY!". The game changes the text or the
  number, and the engine redraws only that place.
- A **text field** (`dgx_ge_text.h`) is a rectangle into which text is laid
  out by words and lines. If the text does not fit, the font is made smaller
  first. Important text scrolls; unimportant text is cut off with "...". It
  can be written in a DGX **handwritten font**: a pen writes the letters
  stroke by stroke.
- **Dialogs** (`dgx_ge_dialog.h`, with the language in
  `tools/dgx_ge_dialog.py`): who says what, the answers to choose from
  (each may appear only under a condition), what that changes in the world,
  things handed over. Lines are translated with ordinary `.po` files, and
  the language is chosen when the pack is built. A dialog can **pause the
  game** while it is open (a `pause` line; a settings menu): the game asks
  `dgx_ge_talk_pauses` and does not advance the world. The talk itself keeps
  its own time running (timed lines, letters, controls), while events for
  actors and things handed over land at the moment where the world stands
  (`ahead_ms`).
- The **talk view** (`dgx_ge_talk_view.h`) is how a dialog looks: its place
  on the screen, border, font, colors, a portrait of the speaker, letters
  appearing one by one. All of this is a **style**, which is also data in
  the pack.
- **Fonts by DGX font requests** (`Pack.add_font`, `dgx_ge_fonts`). The game
  says which fonts it needs ("text, 12 points, monospaced", "DGX's
  handwritten font as it is"); the packer writes this beside the pack as DGX
  font requests, together with the letters the pack prints, and the game's
  build hands them to DGX (`dgx_ge_fonts(assets/<game>.fonts)` in
  `main/CMakeLists.txt`). DGX picks a face from the computer's fonts by a
  rules file and renders it at the right size for the board, including four
  times larger for a big display; the host makes the same fonts from the
  same file (`dgx_fonts.py build`). The game's font table goes by name: the
  pack header gives `<PREFIX>_FONTS(X)`, and a DGX font is called
  `dgx_font_<name>()`.

### 2.13. Shows

A **show** (`dgx_ge_show.h`) is a scene that plays by itself: the titles,
the story before the game, the ending. It is a backdrop of pictures, actors
with their own scripts, and the **director's cues**: "at 2500 ms, an event
for such-and-such actor". The last cue is the end. A button can cut the
show short.

### 2.14. Input and menus

Input is handled in three layers:

- the **board** reads the hardware: buttons, a gamepad, a touchscreen, or
  the keyboard and mouse on a computer;
- the **engine** keeps track of what is pressed and notices presses and
  releases itself;
- the **game** decides what a button means: a jump, the next line of a
  dialog, a menu item.

The engine itself does not read hardware. Button and touch drivers are part
of the board, which is to say of your game (section 2.17).

#### Buttons

`dgx_ge_input_t` keeps three sets of buttons:

- `held`: pressed now;
- `pressed`: pressed since the last time;
- `released`: released since the last time.

Each button is a bit. The layout is that of an ordinary gamepad (SDL and
browsers name them the same way): the d-pad gives the arrows, then `A`
(also `ACTION`), `B`, `X`, `Y`, `START`, `SELECT` (also `BACK`), `HOME`, the
shoulder buttons `LB` and `RB`, the triggers `LT` and `RT`, and the stick
presses `L3` and `R3`. A game that needs only the arrows and a couple of
buttons is not bothered by the rest. The word is 32 bits wide, and the game
can use the free bits for buttons of its own.

Binding buttons means building the mask of pressed buttons each time and
handing it to the engine. For example, buttons on the board's pins that
connect to ground:

```c
uint32_t board_buttons(void)
{
    uint32_t held = 0;
    if (!gpio_get_level(PIN_LEFT))  held |= DGX_GE_KEY_LEFT;
    if (!gpio_get_level(PIN_RIGHT)) held |= DGX_GE_KEY_RIGHT;
    if (!gpio_get_level(PIN_JUMP))  held |= DGX_GE_KEY_UP;
    if (!gpio_get_level(PIN_FIRE))  held |= DGX_GE_KEY_ACTION;
    return held;
}

/* in the game loop */
dgx_ge_input_update(&input, board_buttons());
if (input.pressed & DGX_GE_KEY_UP) ...       /* just pressed */
if (input.held & DGX_GE_KEY_RIGHT) ...       /* held down */
```

The engine does not care where the mask came from: the board's pins, a
gamepad, a keyboard on the computer, or a **scenario**, a list of "from
this moment on, these are pressed" (`dgx_ge_input_scripted`). A scenario is
useful for demos, tests and a game that plays itself while nothing is
connected to the board.

#### Sticks and triggers

With a real joystick, not only the direction matters but also how far it
is tilted: tilt it a little and the hero walks, push it all the way and he
runs. These are **axes**, separate from the buttons:

- `DGX_GE_AXIS_LX`, `LY`, `RX`, `RY`: two sticks, from −32767 to 32767, 0 at
  the center, y pointing down as in the scene;
- `DGX_GE_AXIS_LT`, `RT`: the triggers, from 0 to 32767.

The board hands the axes over already **calibrated**: the center at zero,
the full travel at the full scale. How that was obtained (an ADC, a
microcontroller in the joystick, Bluetooth) does not matter to the engine.
The axes must be handed over before the buttons:

```c
int16_t axes[DGX_GE_AXES] = {lx, ly, rx, ry, lt, rt};   /* as the board read them */
dgx_ge_input_axes(&input, axes);
dgx_ge_input_update(&input, board_buttons());
```

The engine takes off the **dead zone**: a small tilt around the center that
counts as zero, because otherwise a stick at rest drifts a little. For a
stick the zone is round, over both axes together; otherwise the stick would
stick along the diagonals. Past its edge the reading starts again from zero,
without a jump. By default it is about 8 %; the game can set its own
(`input.dead`): aiming wants a smaller one, menus a larger one.

Axes and buttons can be joined by a bridge (`input.bridge`, flags):

- `DGX_GE_BRIDGE_STICK_KEYS`: the left stick pushed past halfway presses
  the arrows. Then menus, dialogs and games made for buttons work with the
  stick. An arrow is released a little before the point where it was
  pressed, so that it does not chatter at the edge;
- `DGX_GE_BRIDGE_TRIGGER_KEYS`: the triggers pressed past halfway press `LT`
  and `RT`;
- `DGX_GE_BRIDGE_KEYS_STICK`: the arrows push the left stick all the way
  while the stick itself is at rest. Then a game made for the stick can be
  played from a keyboard, and a demo can be recorded as a button scenario.

A script reads an axis right in an expression, from −1 to 1 (a trigger from
0 to 1):

```
velocity axis(lx) * 120, 0                   # the further the tilt, the faster
steer seek 120, 60 to axis(lx), axis(ly)     # smooth speeding up and turning after the stick
```

The game says once which input the scripts see:
`dgx_ge_expr_input = &input;`. A `dgx_ge_input_t` is one player: for a
second gamepad you set up a second one.

#### Touch and mouse

The **pointer** is a single point: where it is on the display (in pixels)
and whether it is pressed. The board reads the touchscreen or the mouse and
hands that to the engine:

```c
dgx_ge_input_pointer(&input, x, y, down);   /* the engine finds presses and releases itself */
```

A touchscreen driver usually also has to **calibrate** the touch and
**rotate** it the same way the display is rotated. Then x and y match the
pixels the player sees. To find out which scene point is under the finger,
there is `dgx_ge_scene_point_at` (section 2.5).

The pointer is for menus, dialogs, an inventory. On-screen buttons over the
picture, as on a tablet, are neither the pointer nor the game's business: a
touch on such a button is taken by the platform before the game sees it,
and reaches the engine as a pressed button, an axis or a character. The
game gets the same input as from a gamepad (Part 4, "What's not ready yet").

#### Buttons in dialogs and menus

Dialogs and menus understand not buttons but **interface events**
(`dgx_ge_ui_t`):

| Event | What it does |
| --- | --- |
| `DGX_GE_UI_NEXT`, `DGX_GE_UI_PREV` | the next or previous choice or item |
| `DGX_GE_UI_MORE`, `DGX_GE_UI_LESS` | more or less on a slider or a switch |
| `DGX_GE_UI_ACTIVATE` | choose, go on |
| `DGX_GE_UI_BACK` | go back |

Which button means what is up to the game. For example:

```c
if (input.pressed & DGX_GE_KEY_DOWN)   dgx_ge_talk_view_event(&view, &talk, DGX_GE_UI_NEXT, now_ms);
if (input.pressed & DGX_GE_KEY_UP)     dgx_ge_talk_view_event(&view, &talk, DGX_GE_UI_PREV, now_ms);
if (input.pressed & DGX_GE_KEY_ACTION) dgx_ge_talk_view_event(&view, &talk, DGX_GE_UI_ACTIVATE, now_ms);
dgx_ge_talk_view_pointer(&view, &talk, &input.pointer, now_ms);   /* a touch on a line or an answer */
```

Menus made of **widgets** (`dgx_ge_widgets.h`) work the same way:

- `dgx_ge_widgets_key(tree, event)` moves the focus and presses;
- `dgx_ge_widgets_pointer(tree, &input.pointer)` finds the widget under the
  finger.

Widgets are a thin layer for menus and settings: a button, a list, a
switch, a slider, text, a picture, and boxes for layout.

#### Buttons and the hero

How the buttons control the hero is written by the game: "arrow pressed,
the hero is standing: send the event `run`". What happens next is decided
by the transition matrix of his animation (section 2.7). The conditions
("can he jump right now") are checked by the game too, and the event is
sent only when he can.

One press must not act twice. If "up" closed the inventory, the hero must
not jump in the same instant, and the button that turned past the last line
of a dialog must not swing the sword. To prevent that, a button a menu has
already taken is **spent**:

```c
if (talking) {
    if (input.pressed & DGX_GE_KEY_ACTION) dgx_ge_talk_view_event(&view, &talk, DGX_GE_UI_ACTIVATE, now_ms);
    dgx_ge_input_spend(&input, input.pressed);   /* everything pressed went into the talk */
}
```

A spent button shows up in none of `held`, `pressed` and `released` until
it is let go. Pressed again, it works as usual.

### 2.15. Saving the game

`dgx_ge_save.h` turns the state of the world into bytes that are the same
on any machine. Each part of the engine writes and reads its own chunk (the
world, cells, timers, pockets), and the game writes its own:

```c
dgx_ge_save_t s;
dgx_ge_save_begin(&s, buffer, capacity, dgx_ge_pack_id(pack), now_ms);
dgx_ge_save_chunk(&s, "GAME"); dgx_ge_save_u8(&s, level); dgx_ge_save_chunk_end(&s);
dgx_ge_world_save(world, &s);
size_t n = dgx_ge_save_end(&s);  /* 0: it did not fit */
```

A save remembers the fingerprint of the data it was made with
(`dgx_ge_pack_id`, the pack's CRC-32, written by the packer): after
flashing with a different pack, an old save will not open. Loading is all
or nothing: a part first checks its chunk (counts, numbers, kinds), then
takes it, and if it refuses, stays as it was. `dgx_ge_*_load_check` only
checks, so the game can check every chunk before changing anything:

```c
dgx_ge_load_t l;
uint32_t saved_ms;
if (dgx_ge_load_open(&l, bytes, n, dgx_ge_pack_id(pack), &saved_ms) &&
    dgx_ge_world_load_check(world, &l) && dgx_ge_timers_load_check(&timers, &l)) {
    /* the scene we are going to, and what was saved on top of it */
    dgx_ge_world_load(world, &l, now_ms - saved_ms);
    dgx_ge_timers_load(&timers, &l, now_ms - saved_ms);
}
```

On loading, all times are shifted, and the game goes on as if no time had
passed. Where the bytes go (a file, the board's memory) is up to the game.
It is best to save at the boundary between scenes: on entering a level, at
a checkpoint.

### 2.16. Sound

A sound is a number in the pack. The `sound` command in a script reaches
the game, and the game passes it to an output (`dgx_ge_sound_t`: `play` and
`stop`). Usually the output is the engine's mixer (`dgx_ge_mixer.h`): the
pack's sounds (8-bit or IMA ADPCM, mono or stereo) play on voices, and
their sum, as 16-bit samples, is sent over I2S by a task of the board. A
game with sound hardware of its own (a buzzer, a DAC) has its own `play`
and `stop`.

### 2.17. The board

The engine itself knows nothing about boards. Screens and chips differ a
great deal, so the game sets up the hardware (display, touch, sound) and
gives the engine a DGX screen. For the boards games have already run on,
the engine has ready-made setup code in the `boards/` directory: CYD,
Guition JC8012P4A1C (10″) and JC1060P470 (7″), one file per board, plus a
shared header `dgx_ge_board.h`. The engine library does not build them and
does not depend on their drivers. A game picks one board explicitly, in its
`main/CMakeLists.txt`:

```cmake
include(${CMAKE_CURRENT_LIST_DIR}/../managed_components/jef-sure__dgx-ge/boards/cyd.cmake)
idf_component_register(SRCS "main.c" ${BOARD_SRCS}
                       INCLUDE_DIRS "." ${BOARD_INCLUDE_DIRS}
                       PRIV_REQUIRES ${BOARD_REQUIRES})
```

The frame's scale, the fonts, the sounds and whatever the game stores stay
with the game. If the engine's version does not suit you, copy the board's
file into your `main/` and do it your own way.

### 2.18. The whole game loop

The main loop looks roughly like this:

```c
for (;;) {
    const uint32_t now = esp_timer_get_time() / 1000;
    dgx_ge_input_update(&input, read_buttons());

    /* first the steps whose time has come, then the decisions by the buttons and the world:
       the logic looks at the hero where he has already got to */
    dgx_ge_actor_advance(&hero, now, say, &game);
    game_control(&game, &input, now);

    /* what is visible: this frame's actors */
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_item_t item;
    if (hero_item(&hero, &item)) dgx_ge_scene_actor(scene, &item);

    dgx_ge_scene_render(scene);       /* draw what changed and send it */
}
```

There is no waiting inside the loop: the next frame is drawn as soon as the
previous one is ready, and the actors' time is real time.

---

## Part 3. Try it

### What you need

- A computer with Linux (other systems should work too, but it has been
  tested on Linux).
- **ESP-IDF 6.0**, the build environment for the ESP32.
- **Python 3** and Pillow, to build the pack.
- For fonts made for a board: a C compiler for the computer, FreeType
  (`libfreetype-dev`, `pkg-config`) and fontconfig. A game without fonts of
  its own does not need these.
- A board with an ESP32 and a screen DGX supports. You do not need one to
  start.

### First on the computer, without a board

The engine's core is plain C on top of DGX's virtual screens, so it builds
for a computer too: the hardware is replaced by ESP-IDF stubs that come with
DGX. The engine's tests are in its repository, not in the registry's
package:

```sh
git clone https://github.com/jef-sure/esp32-component-dgx-ge
make -C esp32-component-dgx-ge/test/host
```

DGX is taken from a neighboring directory, `esp32-component-dgx`, or from
the managed components of a project the engine is cloned into; another copy
can be given with `DGX=<path>`.

Your own game can be built the same way: the picture the board would have
drawn can be saved to a file or shown in a window. Which DGX files and which
stubs that takes can be seen in `test/host/Makefile`.

### Your own project

1. Add the engine to your ESP-IDF project from the registry:
   `idf.py add-dependency "jef-sure/dgx-ge^0.1.1"`. It lands in
   `managed_components/jef-sure__dgx-ge`, `main` requires it by itself, and
   DGX comes along as its dependency (`jef-sure/dgx`).
2. Write a Python script that builds the pack (`Pack`, section 2.2). How to
   write each kind of data is described at the start of
   `tools/dgx_ge_pack.py`. Every kind at once, from pictures to dialogs,
   goes into the engine's test pack, `test/host/make_test_pack.py` in the
   repository. It is a test, not a game, but it serves well as a reference.
3. In `main/CMakeLists.txt`, after `idf_component_register`, call
   `dgx_ge_params_of_pack`. It learns from the pack header how many params
   each actor has to keep:

   ```cmake
   dgx_ge_params_of_pack(assets/mygame_pack.h)
   ```

4. Put the pack into the firmware (`EMBED_FILES "assets/mygame.pack"` in
   `main/CMakeLists.txt`). If the game has named its fonts (`Pack.add_font`),
   hand their requests to DGX in the same place: `dgx_ge_fonts(assets/mygame.fonts)`,
   `dgx_fonts_board`, `dgx_fonts_scale`, `dgx_fonts_rules` (section 2.12).
5. Set up the board. For the CYD and the two Guition boards there is
   ready-made setup code in `boards/` (section 2.17). For any other board,
   create the DGX bus and screen yourself (the `examples/` of DGX show how
   for different displays) and give the screen to the engine
   (`dgx_ge_frame_create`).
6. Build and flash: `idf.py build flash monitor`.
7. Start with one actor on an empty scene. Then add a set, then a second
   actor, then collisions. Every step can be checked on the computer.

### Tools

| Tool | What it does |
| --- | --- |
| `tools/dgx_ge_pack.py` | builds the pack and the C header |
| `tools/dgx_ge_anim.py` | the animation language: scripts, events, rules; the language is described at the start of the file |
| `tools/dgx_ge_dialog.py` | the dialog language |
| `tools/dgx_ge_graph.py` | the scene graph |
| `tools/dgx_ge_explain.py` | retells scripts in words |
| `font2c/dgx_fonts.py` of DGX | fonts: which face the rules will pick (`pick`, `list`), a font as JSON for editors (`json`); the fonts themselves are made by the build from the requests |
| `tools/dgx_ge_text.py`, `dgx_ge_font.py` | text layout as the engine does it, for the editor |
| sprite editor | a separate program, to be published later: pictures, masks, animations, the state machine, scenes, dialogs, with a live preview in motion. The pack reads its files through `Pack.add_sprites`: sets, masks, animations, tilesets, styles, dialogs, scenes, the scene graph and the game's data (constants, timers, sounds, shows, crops, chases) |

### If something goes wrong

- **An error while building the pack**: read it carefully; it names the
  file, the line and the name. Most often it is a typo or a reference to
  something that does not exist.
- **An actor is not visible.** Check, in this order:
  - whether you gave the frame the colors of its set
    (`dgx_ge_frame_add_colors`);
  - which layer it is in;
  - whether a later layer covers it;
  - whether it was added between `actors_begin` and `render`.
- **Movement depends on the board.** Look for a decision the code makes "at
  the moment of the call" rather than on an event (Part 1, "Time is real
  time").

---

## Part 4. What's not ready yet

- **Sound:** the mixer and output over I2S are there; volume of the world's
  sounds by distance, and music, will come later.
- **A generated scrolling world** (an endless river with a little plane):
  the ring grid (`dgx_ge_grid_scroll`) and moving the origin of coordinates
  (`dgx_ge_scene_shift`, `dgx_ge_actor_shift`) are done; trying them out in
  a scrolling game is still ahead.
- **Shrinking the frame** (showing the scene smaller than the display): not
  done; the converter would shrink the pictures instead.
- **A 16-bit frame** for large displays: later.
- **Saving in the middle of a scene**, with an actor halfway through a step:
  for now only at the boundary between scenes.
- **A link to an object** (`to: {object: …}` in the editor): for now a
  link's target can only be a cell; the game tells a character its event.
- **Text input.** An on-screen gamepad and an on-screen keyboard exist in
  DGX-DI (the on-screen input component): the gamepad gives the game
  ordinary buttons, the keyboard a stream of characters. The engine does not
  have an input field to take that stream yet.
- **Examples shipped with the engine:** none yet. Neither of the two games
  it was tested on comes with it.

---

## Glossary

| Word | Meaning |
| --- | --- |
| **actor** | something that moves by a script: the hero, an enemy, a thing |
| **axis** | how far a stick is tilted or a trigger pressed: a number, not yes or no |
| **blob** | bytes in the pack whose meaning is known to whoever reads them |
| **dead zone** | a small tilt of a stick around the center that counts as zero |
| **event** | a message to an actor: "jump", "stop", "wounded" |
| **frame** | the game's picture in memory and its sending to the display; also one picture of a script (`frame`) |
| **hook** | a game function the engine calls where data is not enough |
| **item** | one picture on the scene with everything needed to draw it |
| **key** | the order within a layer: a smaller key is further away |
| **layer** | a sheet in the stack; layers are drawn in order |
| **living cell** | a grid cell that does something by itself: a door, a trap, a torch |
| **mask** | an invisible zone of a picture for collisions: body, blade |
| **matrix** (transition matrix) | the `on` rules: which script an actor switches to on an event |
| **modifier** (mod) | a number for the state of a cell: how far a door is open |
| **origin** | "where the picture's feet are": the point a picture is placed by and mirrored around |
| **pack** | the file with all the game's resources, read from flash in place |
| **palette** | a "number → color" table |
| **paper** | the frame's background color, index 0 |
| **parallax** | distant layers move slower than near ones, which gives depth |
| **param** | a number an actor keeps with it: lives, state |
| **scene** | what is visible now, and everything that stands on it |
| **scene graph** | the map: which scenes there are and how to get from one to another |
| **scene point** | the unit of the game's coordinates; on the display it may be larger than a pixel |
| **script** | an actor's role: pictures shown for a time, and commands |
| **set** (of a scene) | the scene's items that do not move, given when it is shown |
| **set** (of pictures) | pictures with shared colors; easy to confuse with a scene's set, so tell them apart by context |
| **show** | a scene that plays by itself, by the director's cues |
| **signal** | a message from a script to the game: "a step touched the floor" |
| **tick** | the even rhythm in which the game makes its decisions (up to 12 times a second in Prince of Persia) |
| **tile** | a number in a grid cell, and how it is drawn |
| **window** (of a scene) | the point of the world at the top-left corner of the screen; scrolling is its movement |
