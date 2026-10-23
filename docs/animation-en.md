# DGX-GE animation: how a script works

[Русская версия](animation-ru.md)

Almost all movement in the engine is built on this: a character walks,
jumps, strikes and dies, a torch burns, a corpse decays. This document
explains in detail how a script lives in time and how one script hands over
to another.

The full description of the `.anim` language is at the top of
`tools/dgx_ge_anim.py`, the API is in `include/dgx_ge_anim.h`, and the big
picture is in [overview-en.md](overview-en.md).

---

## 1. A script is frames with durations

A script is a sequence of **steps**. A step is a few commands followed by a
**frame**: a picture to show for so many milliseconds.

```
script wave
    frame HAND_UP 200ms        # step 1: a picture for 200 ms
    signal 1                   # step 2: a command first,
    frame HAND_DOWN 300ms      #        then a picture for 300 ms
    goto wave                  # and back to the start
```

- A step begins at the moment the previous frame ends.
- The commands of a step run instantly, in order, at the moment the step
  begins.
- Then the step's frame is shown, and the next step comes after the frame's
  duration.

This is the timeline of the script when it is started at moment 0:

```
ms    0          200                    500         700
      │ HAND_UP  │ signal 1, HAND_DOWN   │ HAND_UP    │ signal 1, HAND_DOWN ...
```

`goto wave` runs at 500 together with the next step: it is a command before
the frame `HAND_UP` and takes no time of its own.

The commands of a step:

| command | what it does |
| --- | --- |
| `frame P 83ms` | show picture P for so many ms; this ends the step |
| `frame P 83ms at 2,-1` | the same, with the picture moved from the actor forward and down |
| `dx 3`, `dy -2` | move the actor at once by so many scene points: forward (back when negative), down |
| `flip` | turn round |
| `velocity 40 0` | velocity in scene points a second, x forward; holds until the next movement command |
| `accel 0 300` | acceleration in scene points a second a second |
| `path x: 40*t, y: g*t*t/2` | position by a formula of time, section 3 |
| `contact on`, `contact on landed`, `contact off` | look for contacts with the terrain or not; with an event named, the engine sends it by itself at the moment of contact, section 3 |
| `contact 1 below on landed off fell` | watch whether mask 1 touches anything solid below (`inside`, `below`, `above`, `left`, `right`, `beside` for either left or right, `front`, `back` by the way it faces, `any`; `within 2` for no farther than so many points, 1 without it): one event at the moment it touches, another at the moment it lets go; `contact 1 off` stops watching; `… off fell first within 8`: if there is nothing within 8 points on that side when watching begins, `fell` comes at once (a frame that needs a floor begun over nothing); section 3 |
| `masks 0 2` | which collision masks are active from this step on |
| `carry hand SWORD_3`, `carry hand none` | which frame shows what is at point `hand` from this step on (the pose of the hand); `none` for its own frame; section 5 |
| `release grip 52, -110` | let go what is at the point, at this velocity (x forward); the game is told; section 5 |
| `tell holder drank_wine` | an event for the one holding this actor (a flask drunk); the game passes it on |
| `tell cell 451, loosened, 3` | for the world of the map (mostly from the map's rules): the life of a map cell (numbered as in `dgx_ge_cells`) gets the event, and its own param gets the value; the game does it |
| `press 720` | the button in a map cell is pressed as if stepped on (its links fire); the game does it |
| `clear 421` | whatever lies in a map cell is gone; the game does it |
| `appear KID, mouse_go, 200, 55, left` | an actor of a kind (an animation of the pack) appears at x, y and hears an event (its matrix picks the script); where it lives is the game's decision (in PoP, the player's room; for a crowd, the crowd) |
| `appear HERB, born, x, y with g1 = xor(g1, bit(rnd(14))), hp = 3` | the same, and the newcomer gets params: the expressions are worked out by the one that made it appear, at the moment of the step; the names are params of the appearing kind (the actor's own kind, one added to the pack earlier, or one built together with it by `Pack.add_anims`); the game writes them in by calling `dgx_ge_anim_said_params(child)` in its `say`, after starting the newcomer and before its event |
| `vanish` | the actor leaves the game; the game removes it |
| `hit 2 on 1 damage 3 event hurt` | what mask 2 does when it meets mask 1 of another actor: damage, an event for that actor, and `recoil` for an event to itself; section 5 |
| `set hp 3`, `add hp -1` | the actor's params |
| `signal 3` | tell the game |
| `sound 1` | a sound |
| `branch` | a branch point: the script may be interrupted here (section 4) |
| `hold` | stay on the last frame |
| `goto name` | go on from a label of this script or from the start of another script |

The picture `P` is the name of a picture in the pack, or a number. A number
is a frame number the game draws by itself from its own table; that is how
PoP does it.

**Forward** is the way the unflipped picture faces. After `flip`, forward is
the other way. The side is taken at the moment of each command, so a `flip`
followed by `dx 3` in the same step moves the actor in the new direction.

---

## 2. Time is real

Frame durations are real milliseconds, not numbers of display frames.

An actor keeps a single number, the **moment of the next step**. Every
display frame the game calls

```c
dgx_ge_actor_advance(&actor, now_ms, say, ctx);
```

and the engine works out the actor's state for "now": it runs the steps one
by one until the moment of the next step is later than now. No frames pile up anywhere, neither in a queue nor in memory.

**A fast device** calls `advance` often, say every 20 ms. Each call runs 0
or 1 steps, and every picture of the script gets its time on the screen.

**A slow device** calls it less often. Say that for the script `wave` the
calls come at moments 0 and 600. At 600 the engine runs two steps: the one
at 200 (`signal 1`, frame `HAND_DOWN`) and the one at 500 (frame
`HAND_UP`). The screen shows `HAND_UP`, and `HAND_DOWN` is never shown at
all. But:
- signal 1 still reaches the game, in its order, only later;
- the path the actor covers is exactly the same.

A slow device shows fewer pictures, but in the same time the character ends
up in the same place as on a fast one. There are no fixed ticks.

**The binding to time is strict.** The state of an actor at moment t is
determined by the script, the moment it started and the events, and
`advance` works it out for now however much time has passed. No time is
lost. The price is that every step between the previous call and now has to
be run: their `dx` add up into the path, `flip` and `set` change the state,
and branch points may switch the script.

**The game's clock.** `advance` is given the game's time, not the wall
clock: it stands still while the game is paused, and after the pause the
actor carries on from where it was.

Long processes, a corpse decaying or a tree growing, are written as long
steps ("corpse, 5 minutes") and are worked out in a couple of steps even if
the scene has not been shown for half an hour.

---

## 3. Movement between steps

`dx` and `dy` move the actor in a jump, at the moment of a step. `velocity`
and `accel` set **smooth** movement, which goes on between steps too:

```
x(t) = x0 + v·t + a·t²/2,   v(t) = v0 + a·t
```

The position is computed exactly at the moment of every step, and for now
at every call of `advance`. So a parabolic jump (`velocity 40 -200`,
`accel 0 600`) follows the same curve at any call rate.
Coordinates and velocities are kept in 1/256 of a scene point (scene points are explained in overview-en.md, 2.5).
What a call moves beyond those units is not dropped: the actor keeps it
(of the place to 1/2000000 of a unit, of the velocity, while it
accelerates, to 1/1000), and the next call goes on from it. So the way
adds up to the same unit however the time is cut into calls: an eighth
of a point a second covers 12.5 points in 100 s at a call a millisecond,
at 60 calls a second and in one call alike. A fractional velocity
(`velocity 1000 / 83, 0`, a point per game tick with a tick of 83 ms) is
kept to 1/256 of a point a second, not rounded to whole points a second.
A velocity the game sets on an actor that does not accelerate is taken as
it is, and so is a place it sets on one at rest.

`velocity` sets the velocity anew; it does not add to it. To stop an actor,
use `velocity 0 0` and `accel 0 0`.

### Expressions

Any number in a command can be an expression: numbers (fractional too),
`+ - * /`, parentheses, `min`, `max`, `abs`, `sin`, `cos`, `sqrt`, `exp`,
`smoothstep` (0 up to 0, 1 after 1, smooth in between: s²(3−2s), with no
jerk at either end; `path x: smoothstep(t / 0.5) * 80, y: 0` covers 80
points in half a second and stays there),
named
constants from `define`, the actor's params, and its velocity at that
moment, `vx` and `vy` (scene points a second, x forward):
`velocity vx * 0.9, -vy * 0.7` is a bounce. Where the actor is now is `x`
and `y` (scene points, and x here is not "forward" but as in the scene):
`goto turn if x < 166` turns round on reaching a place.

The comparisons `<`, `<=`, `>`, `>=`, `==`, `!=` and `and`, `or`, `not`
give 1 or 0, so they work in arithmetic as well: `velocity (hp > 0) * 40, 0`
moves while the actor is alive. Precedence, from weakest to strongest: `or`,
`and`, `not`, comparison, `+ -`, `* /`. Comparisons do not chain:
`0 < x < 5` is an error; write `0 < x and x < 5`.

`and`, `or` and `c ? a : b` are lazy, as in C: in `a and b` the part `b` is
not evaluated when `a` is 0; in `a or b`, when `a` is not 0; in `c ? a : b`
only the chosen part is evaluated. The value is the same either way; what
changes is what the evaluation itself does: `rnd` moves the dice only when
its part is evaluated. `goto strike if near and chance > rnd(255)` rolls
only when near; `power > 0 ? rnd(9) : 0` only with some power. When a part
is made of constants, the assembler decides it by itself: `0 and x` is 0,
`1 ? a : b` is `a`.

Bits of integers: `&`, `|`, `%` and the functions `xor(a, b)`, `bits(x)`
(how many ones), `bit(n)` (2ⁿ; 0 for n outside 0..30), and
`field(x, first, count)` (`count` bits starting at bit `first`). A negative
number is in two's complement: `bits(-1)` = 32. A param holds 32 bits,
but the bits go through a float on their way, so 24 of them come through
exact (0..16777215), and those bits inverted are `xor(x, 16777215)`. Take
genes kept in two params of 15 bits: the kinship with a mate
whose genes were copied into `mate_g1` is `bits(xor(g1, mate_g1))`, a
mutation is `xor(g1, bit(rnd(14)))`, a trait is `field(g1, 4, 3)`.

A table of constants holds numbers that no formula can express, by index:

```
table strike_chance 75 100 75 75 75 50 100 220 0 60 30 255
param skill 0

script guard_think
    goto strike if strike_chance[skill] > rnd(255)
```

A table is declared before it is read; its values are constants,
−32768..32767. The index is the integer part of the expression, and outside
the table the value is 0. With a constant index the assembler puts the
number in at once. Tables live in the animation's blob after its code and
cost an actor nothing; tiles have none.

A question to the game asks for something only the game knows (a distance
by its rules, whether one actor sees another, whether there is floor
ahead):

```
game distance sees                 # questions in order: distance 0, sees 1

script think
    goto calm if game.sees == 0
    goto strike if game.distance < 29 and strike_chance[skill] > rnd(255)
```

`game.<name>` is the game's answer at the moment of the step: the game sets
its function `dgx_ge_expr_ask(number, who, context)` and the context
`dgx_ge_expr_ask_ctx`; with no function the answer is 0. `who` is the actor
whose script asks, so one function answers every actor of a kind, each about
itself: the actor being advanced or sent an event, and also an actor that an
event reaches from inside another's step (the game's `say` sending it one).
An actor the engine makes for a moment to play a script (a cell's life, a
thing lying or in a pocket, a control of a dialog) asks as itself too; its
params and kind are there, and the pointer is valid only during the call.
`who` is NULL when no actor asks. Names are declared
before they are read, and their numbers are in the pack header as
`<PREFIX>_<ANIMATION>_GAME_<NAME>`. This costs an actor nothing. There are
no questions in `path` or in tiles: a path is computed from `t` alone.
`rnd` is rolled only on a step that has run, and only in the part of the
expression that is evaluated (`and`, `or`, `?:` are lazy), so the dice are
rolled exactly on the road taken.

An axis of the player's game pad is `axis(<name>)`: the sticks `lx`, `ly`,
`rx`, `ry` from −1 to 1 and the triggers `lt`, `rt` from 0 to 1, as they
are at the moment of the step:

```
script walk
    velocity axis(lx) * 120, 0      # the tilt is the speed (with expressions, operands go by a comma)
    frame WALK_1 83ms
    goto walk
```

The game says once which input the scripts read: `dgx_ge_expr_input =
&input` (`include/dgx_ge_input.h`, with the dead zone already taken off);
with no input the axes are 0. This works only in animation scripts: not in
`path` (a path is computed from `t` alone), not in tiles (a cell is laid out
again only when it changes, so the stick would be seen as it was back then)
and not in dialog conditions.

The same language is used in tiles (overview-en.md, 2.6): there the names
are the cell's modifier `mod`, its variant `variant`, and their fields by
the names the tile set gives them.

```
define gravity 432
define tick 1000 / 12
param power 200

script throw
    velocity 40, -power        # with expressions the operands are separated by a comma
    accel 0 gravity
    frame STONE tick
```

An expression of numbers and constants alone is worked out by the assembler
at once and costs nothing on the board. One with params is worked out at the
moment of the step: `velocity`, `accel`, `set`, `add` (and in `path`). The
frame duration, `dx`, `dy`, `at`, `signal`, `sound` and `masks` take
constants only.

An expression counts in floats, which hold whole numbers exactly up to
2²⁴ (16 777 216); a larger one comes out rounded. A param holds a whole
number of 32 bits: `set` and `add` round the value and stop at the limits
of 32 bits instead of overflowing. What lies in the pack is of 16 bits,
−32768..32767: a param's starting value, a table's numbers, the params a
layout gives. `set` and `add` take a constant up to ±2²⁴ as well (beyond
16 bits the assembler keeps it as an expression).

Numbers shared by the whole game are written in the text as names, and the
packer turns them into numbers. A mask, wherever one is expected (`masks`,
`hit … on …`, `contact`), is called by its name, the same as in `meets` and
in the header: `masks BODY TIP`, `hit TIP on BODY damage 1`; a number works
too. The game gives the packer its own numbers as constants
(`Pack.add_constants(TICK_MS=83, TILE_FLOOR=2)`, or the `constants` field in
the editor's file): their names can go anywhere a number can, like a
script's own `define` (`define tick 1000 / TICK_MS`, `signal CLOCK_OUT`).
Such a name cannot be redefined by a `define`. The header gives the same
numbers to the game's C as `<PREFIX>_<NAME>` (`#define MYGAME_TICK_MS 83`), so
a signal or a tile is written once, in the data, and C does not repeat it. A
value is a whole 32-bit number, as the scripts' numbers are: `2.5` or `"83"`
is refused, not cut to a number the data did not say. A constant whose header
name is already another thing's (`GAME_LIVES` beside `add_game(LIVES=…)`,
`SET_COUNT`, `HERO_SCRIPT_STAND`) is refused too, even with the same number.

### `path`: position by a formula

```
path x: 40*t, y: -power*t + gravity*t*t/2     # a throw
path x: 0, y: 3*sin(t*6)                      # sways in place
path x: min(t, 0.5)*80, y: 0                  # rolls 40 points and stops
```

- `t` is seconds since the `path` began. The actor's position is the point
  where the `path` began plus the formula; x is forward, by the side the
  actor faced when it began.
- It holds until another movement command replaces it: `path`, `velocity`
  or `accel`. Changes of frames and scripts do not interrupt it. After it is
  replaced, the actor goes on at the velocity the formula had.
- `dx` and `dy` during a `path` move its origin.
- The position at any moment is computed straight from the formula, so the
  binding to time is exact, with no stepping through and no accumulated
  error.
- The actor's velocity (which the game may read) is the derivative of the
  formula at that moment.
- Params in the formula are read at every evaluation, so the game can change
  the movement on the fly by changing a param.
- `vx` and `vy` are not allowed in a path formula: the formula itself gives
  the velocity, and reading the actor's own previous velocity would make the
  position depend on the frame rate.

The cost: the board evaluates the formula, in float, at every `advance` of
an actor on a `path`; the ESP32 has hardware floating point. The evaluator's
stack holds 16 values, and an expression is up to 255 bytes.

### Contact with the terrain

The terrain is whatever is solid in the scene for actors to touch: floors,
walls, bumpers. The engine does not know it; the game describes it (later
the tile module will) with two functions in `dgx_ge_terrain_t`: "does the
actor's body, placed here, touch anything solid" and "what to do on
contact". The game gives an actor its terrain (`actor.terrain`), and the
script tells it to look for contacts:

```
script fly
    contact on          # look for contacts; contact off stops looking
    accel 0 gravity
label flying
    frame BALL 1000ms
    goto flying
```

- **Where the engine looks.** At the boundaries of the script's steps and
  at now. Outside before and inside now means the contact happened
  somewhere in between. A contact counts only on entering: while the actor
  stays inside, it is not reported again.
- **What the game decides.** Its function gets the contact: the time
  interval where it happened, the approximate place, the surface normal and
  what was hit. It may decide anything: bounce (a ball), kill the velocity
  (a soft floor), add to it (a pinball bumper), pass through, or send an
  event to the matrix.
- **An event instead of code.** With `contact on landed` the engine itself
  refines the moment of contact, puts the actor there and sends it `landed`
  at that moment; what comes next is up to the matrix of its kind: a flask
  goes `flying -> shatter`, a stone `flying -> lying`. The new script sets
  its own movement (`velocity 0 0`). The game's function, if there is one,
  is called first; if it has moved the actor itself (a bounce), no event is
  sent.
- **Exact, when it has to be.** There are no steps inside the interval, the
  movement is continuous, and `dgx_ge_actor_state_at()` gives the state at
  any moment of it. `dgx_ge_contact_refine()` refines the moment of contact
  by bisection to the millisecond; the game may also solve the equation
  itself. `dgx_ge_actor_restart()` puts the actor at the exact moment of
  contact with a new velocity, and the engine goes on from there to now.
  This is how a ball bounces the same way at any frame rate.
- **Contact by masks.** `contact on` looks at the actor's base point.
  `contact 1 below on landed off fell` watches a mask: whether it touches
  anything solid on that side (the floor under the feet is below, a wall
  ahead is `front`, a ceiling is above, water is `inside`), no farther than
  `within N` points. When it touches, the first event comes; when it stops
  touching (the floor has run out), the second. Each comes at its exact
  moment. The engine checks contacts not all the time but at separate
  moments: at the bounds of the script's steps and at each frame's "now".
  When a contact began or ended between two such checks, the engine finds
  its moment by bisection, puts the actor there and sends the event, and
  the matrix decides the rest.
  A mask entirely inside something solid (inside, with no side within
  `within`) touches it from every side: a step that carried it through an
  edge (because it was fast, or the edge is thin) is a contact too, from its
  first millisecond (feet that flew into a floor slab have landed on it). A
  mask that went in partly touches from the sides where its points outside
  see something solid (the edge it crossed): `front` will not fire on a wall
  behind the actor's back that an edge of the picture has crept into;
  `within 0` sees only "inside", and then any side counts.
  Rest gives no events: standing on a floor, the actor gets nothing.
  Whether a mask touches anything here is answered by the game's terrain
  (`dgx_ge_terrain_t.sides`); the tile grid and the body registry give
  ready answers (`dgx_ge_grid_touch_sides`,
  `dgx_ge_collisions_touch_sides`), and the game asks for itself, when it
  needs to, "what is touching what right now" (`dgx_ge_grid_touching`,
  `dgx_ge_collisions_touching`: the mask, the side, the area, what was
  met). Up to `DGX_GE_ANIM_CONTACTS` (2) masks can be watched at once.
- **A ray.** `dgx_ge_terrain_ray()` gives the first solid thing on a
  segment from one point to another: whether the guard sees the prince,
  where a shot will hit, whether a fast projectile has slipped through a
  wall. The terrain may provide its own exact ray (the tile grid goes cell
  by cell); otherwise the engine walks along the segment in steps and
  refines the place of the hit to a point.

> **Watch out for thin walls.** The engine looks for contacts only at step
> boundaries and at now. If an actor moves farther than a wall is thick
> between two such moments, it misses the contact and goes straight
> through. That is a mistake of the game's developer, not of the engine. To
> avoid it:
> - **make steps shorter:** give a fast actor frames of 20–40 ms rather
>   than a second, so that between checks it moves less than a wall is
>   thick;
> - **do not make walls thinner than the path** the fastest actor covers in
>   one step;
> - **check the path with a ray:** for a bullet or a stone, cast
>   `dgx_ge_terrain_ray()` from the previous place to the new one in the
>   contact function or in the game's code.

A test ball is made this way. Prince of Persia has no ball: it is thrown
into a room of the game only to try contacts with the terrain. It bounces,
settles, rolls with a constant deceleration and stops at the computed
moment, falls off an edge, and bounces off a wall.

### Sense: `sense`

`sense` is a step that says "find the nearest one and write down where it
is": an actor of the named kind, anyone (`any`) or a grid cell (`cell`),
within a radius, meeting a condition:

```
param hp 3
param age 0
param sex 0
param enemy 0
param edx 0
param mate 0
param food 0

script graze
    sense WOLF within 80 into enemy, edx       # a wolf closer than 80 points: the distance, x forward
    goto flee if enemy >= 0 and enemy < 50
    sense HERB within 60 where its.sex != sex and its.age > 3 into mate
    sense cell within 30 where its.mod >= 2 into _, _, _, food = its.cell
    frame GRAZE 200ms
    goto graze

script flee
    frame RUN 100ms
    goto graze
```

- The radius is a number or an expression of the actor's own, worked out at
  the moment of the step: with `sense SHEEP within 30 - full / 4 into d` a
  hungry wolf smells farther (below 0 counts as 0). A sense of smell from
  genes works the same way.
- `seen` after the radius means only one that can be seen: the look's path
  to it costs no more than the radius, and the terrain sets the cost by the
  tiles' veil (a tile's `veil`: 0 is transparent, 1..6 make each point
  through it cost 2..7, 7 cannot be seen through). `sense SHEEP within 120
  seen into d` is a wolf's eye: it does not see a sheep in a bush, and in a
  forest it sees less far. Without `seen` it is a nose that smells through
  everything. With `seen` a cell itself can be seen; its own veil does not
  count.
- `into d, dx, dy` gives the distance (in points, rounded; −1 means nobody)
  and the offset to the one found: x forward (as with `velocity`), y down.
  Trailing ones may be left out; one not needed in the middle is `_`. With
  nobody found, d = −1 and dx = dy = 0.
- Copies come next: `<param> = <expression>`, in which `its.` is the one
  found. This is how an actor copies into itself what it will need later: a
  mate's genes, the number of a grass cell. If nobody is found, the copies
  are left untouched.
- `where` is a condition the one found must meet; it is checked during the
  search, so a nearer one that does not fit does not hide one farther away
  that does.
- `its.<param>` is the found one's param by its kind's numbering, so it
  works only with a named kind (the actor's own kind has its own params;
  another kind must be added to the pack earlier). An actor also has
  `its.vx`, `its.vy` (its velocity, x forward from the searcher),
  `its.image`, `its.script`, `its.flip`; a cell has `its.tile`, `its.mod`,
  `its.cell` (the game's number of the cell, for `tell cell`).

Who can be found is the game's decision: the actor's terrain has a `sense`
callback, and the answers to it come from `dgx_ge_sense_bodies` (the body
registry: bodies for which the game has named an actor with
`dgx_ge_collisions_actor` or that it put in with
`dgx_ge_collisions_set_actor`; the kind is known for animations opened through
`dgx_ge_kind`) and `dgx_ge_sense_cells` (the grid: cells that are not
empty). The game joins the two and gives a cell its own number. Without the
callback `sense` finds nobody. The search happens at the moment of the step, by
where the bodies stand in the registry.

### Steering: `steer`

`steer` sets the velocity the way Reynolds' steering behaviors do: a
desired velocity by the behavior, approached by no more than the "force"
per step and never faster than the "speed". The target is usually what
`sense` found; the axes are x forward.

```
script flee
    sense WOLF within 120 into enemy, edx, edy
    goto graze if enemy < 0
    steer flee speed, 40 from edx, edy        # away from the wolf; the speed is a param (from genes)
    frame RUN 83ms
    goto flee
```

| Form | Desired velocity |
|---|---|
| `steer seek <speed>, <force> to <dx>, <dy>` | toward the target, at full speed |
| `steer flee <speed>, <force> from <dx>, <dy>` | away from the target, at full speed |
| `steer arrive <speed>, <force>, <radius> to <dx>, <dy>` | toward the target; inside the radius, in proportion to the distance, 0 at the target |
| `steer pursue <speed>, <force> to <dx>, <dy>, <vx>, <vy>` | toward where the target will be after distance / speed at its velocity vx, vy |
| `steer evade <speed>, <force> from <dx>, <dy>, <vx>, <vy>` | away from that place |
| `steer wander <speed>, <force>, <spread> angle <param>` | along an angle (degrees, 0 forward, 90 down) that shifts every step by a random amount within ± spread (the game's dice) |

- **Force** is how many points a second the velocity may change by in one
  `steer`, that is in one step, not in a second. So the same force changes
  the velocity faster when the steps are frequent. For example: a step every
  83 ms, and the velocity must change by no more than 700 points/s in a
  second; in a step that is 700 × 0.083 ≈ 58, and that is the force. A large
  force turns at once, a small one makes a smooth arc.
- The velocity holds until the next step; `steer` zeroes the acceleration
  and cancels a `path`. Between steps the movement is straight, and contacts
  are looked for as usual.
- A target at zero (`sense` found nobody, dx = dy = 0) gives a desired
  velocity of 0, and the actor brakes.
- Every operand is an expression.

---

## 4. Changing scripts: events and the matrix

A script does not decide by itself when to change to another one, unless it
reaches a `goto`. A change is caused by an **event**: the game has decided
it is time and sends

```c
dgx_ge_actor_event(&actor, EVENT_JUMP, now_ms, say, ctx);
```

What follows from an event is given by the "script × event" **matrix**. A
row of the matrix is the script the actor is in now. A cell says where to go
and **when**:

| when | what it means |
| --- | --- |
| `now` | at once: the current frame is cut short and the new script begins at this moment |
| `branch` | at the next `branch` point of the current script, or at its end |
| `end` | when the current script reaches its end |

**The end of a script** is the place where it would go on to another script
or stop: a `goto` (even to a label inside itself, which is the end of one
round of a loop) or a `hold`.

An empty cell means the event does nothing here, and `dgx_ge_actor_event`
returns false.

The cells are filled by rules:

```
group ground: stand, run
on jump: ground -> leap now
on stop: run -> stand branch
on land: fall -> stand end
```

The order of the rules means nothing, there are no priorities, and two
different answers for one cell are a build error.

### Example: a run interrupted at a branch point

```
script run
    frame RUN_1 80ms
    dx 4
    frame RUN_2 80ms
    branch
    dx 4
    frame RUN_3 80ms
    goto run
```

The actor is started in `run` at moment 0, and the event `stop`, answered
by `run -> stand branch`, comes at 100:

```
ms    0        80         160                       ...
      │ RUN_1  │ dx 4,    │ branch: the waiting
      │        │ RUN_2    │ transition is taken, then stand
              ↑
              100: stop is remembered and waits for a branch point
```

Had `stop` come at 170, the branch point would already have passed, and the
transition would happen at the end of the round, on `goto run` at 240.

### Fine points

- **There is only one waiting transition.** Another event answered with
  `branch` or `end` replaces the one waiting.
- **A `now` answer first works out the state at the moment of the event.**
  The engine runs the steps that have already come, then enters the new
  script at the moment of the event and runs its first step right away. So
  the new script's frame shows up in the same display frame.
- **A stopped actor** (`hold`) answers any event at once, even one answered
  with `branch` or `end`: it has nothing left to wait for.
- **An event from the actor's own step.** The game may answer what a step
  says (a signal, a param changed) with a `now` event to the same actor,
  right in its `say`. The new script begins at the moment of that step, at
  once after the `say`; what was left of the step is not made.
- **An event from the past.** A `now` event stamped earlier than the
  moment the actor has already been moved to (a step of another actor,
  made late in a long call; a game that starts a script a tick back) takes
  the motion back to its moment: the new script begins there, its frames
  and its place alike. Steps the actor made after that moment are not
  undone.
- **The matrix row changes only on entering a script:** at the start, by an
  event, or by a `goto` to another script. A label is not a script: a
  `goto` to a label leaves the actor in the same row.
- **A script without `goto` or `hold` at its end** runs on into the next
  script in the text, but the matrix row stays the same. That is almost
  always a mistake, and the editor warns about it. End every script with
  `goto` or `hold`. The last script has nothing to run on into: an empty
  one is an error of the build.

---

## 5. What a script tells the game

`signal N` and `sound N` call the function `say` that the game passed to
`advance` and `event`:

```c
static void say(dgx_ge_actor_t *actor, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    if (what == DGX_GE_ANIM_SIGNAL && value == SIGNAL_FOOT_DOWN) ...
    if (what == DGX_GE_ANIM_SOUND && now_ms - at_ms < 100) play(value);   /* don't play a stale sound */
}
```

`at_ms` is the moment of the step the signal belongs to.

### What is in the hand: `carry`

A frame's point (`hand`) says where something is held; what is held is said
by the script: `carry hand SWORD_3` puts this picture in the hand from this
step on, and `carry hand none` leaves it empty. It holds until the next
`carry` of the same point, like `masks`. The sword in the swing and in the
strike are different frames of the sword, and the script changes them
together with the frames of the body. An actor has two such slots
(`DGX_GE_ANIM_CARRIED`): a sword and a shield.

The game takes the picture with `dgx_ge_actor_carried(actor, point)` and
hangs it on the point of the body's frame (`dgx_ge_item_held`,
`include/dgx_ge_scene.h`). Whether to show it at all (whether the sword is
drawn) is the game's decision.

Characters with different pictures have scripts of their own: in PoP the
prince and the guard move by the same table of the original, but in the
same frames they hold different frames of the sword. The converter makes
two sets from it, `kid.anim` and `guard.anim`, with the same numbering of
scripts and events.

### A held item is an actor

What is held can be an actor with scripts of its own: a sword, a flask, a
stone. What it is (masks, `hit`, states) is given by its kind; how it is
held, by the holder. `dgx_ge_actor_hold(holder, point, item)` puts it into
the hand, and the holder's `carry` sets its frame by the pose of the hand
(`dgx_ge_held_image`: the frame for the pose, otherwise the item's own
frame). The masks active on a held item are those active both on it and on
the holder (`dgx_ge_held_masks`), so in the swing the tip is not dangerous.
`release` lets the item go: it is free and flies at the velocity of the
throw from the moment of the step; with `dgx_ge_actor_drop` it falls out
without a throw.

### Pick up and throw: the game puts it in, the script lets go

What has been picked up is known to the game: the prince crouched over a
potion, and the game puts the flask into his hand with
`dgx_ge_actor_carry(actor, point, picture)`, the same slot that `carry`
fills. The script lets go at its own moment of the movement:

```
script throwpotion
    frame 229 83ms        # got up with the flask
    frame 230 83ms
    frame 231 83ms        # hand forward
    release grip 52, -110 # let go: 52 forward, 110 up, points a second
    frame 232 83ms
```

`release <point> [vx vy]` frees the slot and tells the game
(`DGX_GE_ANIM_RELEASE`, with the point's number as the value) along with the
moment of the step. What was released and how fast is in `actor->released`
(the picture, vx with the actor's side already applied, and vy). The
velocities are expressions, as with `velocity`; when they are not given,
they are 0 0. If nothing is in the hand, nothing happens. The game makes of
the released thing whatever it wants: a flying flask, a thrown stone. It
finds where the hand was from the frame's point at the moment of the step.

### Pockets: `draw`, `stow`, `count`, `goto … if`

Besides hands, a character may have pockets (`dgx_ge_pockets.h`): a sword
in its scabbard, daggers in the belt, a potion. How many places there are is
declared by the character's animation; how many things of one kind go into
one place, by the animation of the thing's kind; whether the world keeps a
dropped thing is set by the `keep` step in the script of its state:

```
pockets 6          # in the character: six places
stack 10           # in the "dagger" kind: up to ten in one place

script lying       # in the dagger
    keep           # the world keeps it lying and does not give it up to newer things (a flask's shards it does not keep)
```

A thing's kind is its animation in the pack (the packer sets the blob
number, as for `shoot`). The holder's scripts draw and stow things at the
moment of the movement:

```
script engarde
    draw hand SWORD          # the sword from the scabbard into the hand (no sword, nothing happens)
    ...
script resheathe
    ...
    stow hand                # what is in the hand goes into a pocket
script throwdagger
    goto stand if count(DAGGER) == 0   # nothing to throw: stand
    shoot hand DAGGER 220 0            # the dagger is taken from a pocket
```

- `draw <point> <kind>` takes one thing of the kind from the pockets into
  the hand: it becomes the pockets' actor for that point (one per point,
  `DGX_GE_ANIM_CARRIED`), in the script and with the params it had when it
  was stowed. If the hand is busy or there is no such thing, nothing
  happens.
- `stow <point>` puts what is in the hand into a pocket (a 12-byte record:
  kind, script, params, how many). Only identical things stack: a poisoned
  dagger will not go in with plain ones. If it does not fit, it stays in the
  hand.
- `shoot` by an actor that has pockets takes the projectile from them; if
  there is none, there is no shot. Without pockets it works as before.
- `count(KIND)` in expressions is how many things of the kind are in the
  pockets; `goto <label or script> if <expression>` jumps when the
  expression is not 0 (like `goto`, it is where a transition waiting for the
  end is taken).

The game gives an actor its pockets after the start (`actor->pockets`);
listing them, checking whether something fits, moving things from one set
of pockets to another and laying them out into the world are done by
`dgx_ge_pockets_*`.

What is laid out goes onto a stack of the same things, next to other
things, or onto a heap (`dgx_ge_world_lay`). The actor knows how many lie
in its stack (`amount`), and the frame may depend on that: the frame is then
an expression in parentheses, with the pack's pictures as names in it:

```
script lying
    frame (DAGGERS_1 + min(amount, 10) - 1) 1000ms
```

### The blow: `hit`

Damage is a property not of the fighter but of the blow: a jab takes off
less than an uppercut, a sweep knocks you down. So a blow is described in
the script, on the steps of the blow, next to the masks:

```
script strike
    masks BODY TIP
    hit TIP on BLADE recoil parried   # tip on blade: the striker gets parried
    frame STRIKE_1 83ms
    hit TIP on BODY damage 1          # tip on body: damage 1
    hit TIP on BLADE recoil parried
    frame STRIKE_2 83ms
    masks BODY                        # the blow is over
```

- Events are names of the pack, with one number for all kinds (the packer
  sees to that), so `event taken` on the prince's hand is the same `taken`
  as in the flask's matrix. A name used in `hit` need not be declared; the
  kind that declares it is the one that reacts to it.
- `hit <mask> on <masks>` says what the actor's own mask does on meeting
  these masks of another actor (`on BODY SHIELD` for the body or the
  shield). Then, optionally: `damage N` for damage, `event e` for an event
  to the one met, `recoil e` for an event to itself (bouncing off a block).
- Consecutive `hit` lines go together. They hold until the next `hit`
  lines, a `masks` command or another script replaces them: a blow is part
  of the movement it is written in.
- The meeting of masks is found by `dgx_ge_collisions_look` (collisions,
  overview-en.md, 2.8). For the bodies of actors `dgx_ge_collisions_hits`
  does the rest: it asks `dgx_ge_actor_hit(actor, own mask, other's mask,
  &hit)` of both and sends the events and the recoils, in the order of the
  bodies' handles. A game with rules of its own asks `dgx_ge_actor_hit`
  itself and sends what it decides. What an event means is decided by the
  matrix of the one who gets it: the same blow is a wound in a stance, a
  stagger in a block, and nothing at all on someone lying down.
- The damage comes off the param the game names for a body
  (`dgx_ge_collisions_damage`); with none named it comes off nothing. Other
  rules (partly in a block, less and less in a series) are the game's: its
  function sees each hit before it is sent and may change the damage, or
  the game sends the hits itself.

PoP's sword fighting is described this way: the sword's picture has two
masks, the tip (`TIP`) and the whole blade (`BLADE`). On the strike frames
a tip that meets the blade of a parrying opponent is beaten off; on the last
frame a tip on the body wounds. The fighting code has no frame numbers left
in it. What the game does next (a life lost, death, a fall off an edge, the
one who parried holding his blade) is a matter of its rules.

A signal is a moment that matters to the game's logic: a foot touched the
floor, a hand let go of a stone, a character climbed a row higher. What the
number means is the game's decision. Signals come in the order of the steps.
On a slow device or after a long break they come later than their moment,
but they are never lost, and `at_ms` shows when they should have happened.

---

## 6. Params

Params are numbers every actor keeps for itself: lives, "what it is
doing".

```
param hp 3          # declared before its first use, starts at 3
param action 0

script drink
    add hp 1
    frame DRINK_1 100ms
    ...
```

- `dgx_ge_actor_start` gives the params their starting values.
- When a script changes a param (`set`, `add`) and the value actually
  differs, the game is told `DGX_GE_ANIM_PARAM` with the param's number and
  the moment of the step: there it changes what the scripts know nothing
  about (the palette of the world when the character is drunk).
- In expressions `now` is the moment of the step in seconds of the game's
  clock: `set drunk now + 10` means "drunk until such-and-such a second".
  A param holds such a moment as long as the clock runs: the clock counts
  milliseconds in 32 bits and comes round after 49 days 17 hours. `now` is
  exact at every whole second, so `goto sober if now >= drunk` is met at
  its very second; between seconds it is a float, off by up to 2 ms in the
  first 10 hours of the clock, 4 ms in the first day, 31 ms in ten days, a
  quarter of a second near the end.
  Keep moments in whole seconds: `now * 1000` is exact only for the first
  4 h 39 min (2²⁴ ms) and no longer fits in 32 bits after 24 days.
- A moment in a param is a number to the engine: a load moves on the times
  the saved chunks hold (dgx_ge_save.h), not params. A game that keeps
  moments in params goes on after a load with its clock where the save
  left it.
- Params are kept when scripts change.
- `set` and `add` stop at the limits of 32 bits instead of overflowing.
- The game reads them by the numbers from the pack header:
  `actor.params[MYGAME_HERO_PARAM_HP]`.

The engine does not know what params mean and checks no thresholds. "Out of
lives" is the game's decision, and the game sends the event.

---

## 7. What a frame is to the game

A script sets only the picture's number, its offset from the actor
(`at dx,dy`) and the side (`flip`). An actor is not a scene item. Every
display frame the game makes a scene item (`dgx_ge_item_t`) from it by
itself: it takes the picture by number, places it by the actor's position
and offset, mirrors it, and picks the layer and the key. So one and the same
animation can be drawn in different ways: in PoP the frame number is first
looked up in the game's frame table, and the guard's table is a different
one from the prince's.

---

## 8. Where to check it

- The engine's host test, `test/host/test_anim.c`, covers the timeline of
  steps, the now/branch/end transitions, turning and moving, and params.
- The sprite editor (a separate program, the Animations tab). Its player
  repeats the engine's `dgx_ge_actor_event()`: you can send events with
  buttons and watch what happens.
