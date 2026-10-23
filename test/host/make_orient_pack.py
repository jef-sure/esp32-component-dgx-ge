"""
The pack of test_orient.c: an actor's orientation (its mirror and its quarter turn). A picture of every pixel its own
value, its masks and points; a kind whose scripts turn it (by a number, a param, a variable of the game; into a turn),
move it in its axes, read the turn, sense another, watch a mask in front; a show and a crowd whose actors and pictures
are turned.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from dgx_ge_pack import Pack  # noqa: E402

pack = Pack("OR")
art = pack.add_set("ART", [(16 * i, 0, 0) for i in range(1, 16)])
# 5 x 3, its origin at column 1, row 2: the pixels 1..15 row by row, so every one tells where it went
pack.add_image("ARROW", art, 5, 3, list(range(1, 16)), 4, (1, 2))
pack.add_image("HILT", art, 3, 1, [1, 2, 3], 4, (0, 0))
pack.add_image("BAR", art, 6, 1, [5] * 6, 4, (0, 0))  # long and thin: turned, it stands upright
arrow, hilt = 0, 1
pack.name_mask(1, "BODY")
pack.name_mask(2, "FEET")
# the body: the picture but its top right corner; the feet: its bottom row
pack.add_mask(arrow, 1, -1, -2, 5, 3, [1, 1, 1, 1, 0,  1, 1, 1, 1, 1,  1, 1, 1, 1, 1])
pack.add_mask(arrow, 2, -1, 0, 5, 1, [1, 1, 1, 1, 1])
pack.add_point(arrow, "hand", 3, -1, 1)  # forward and up of the origin
pack.add_point(hilt, "hand", 1, 0)       # where the hilt is held
pack.add_variable("facing", 0)

pack.add_anim("TURNER", """
event touched, go
param deg 0
param got 0
param d 0
param dx 0

script stand
    frame ARROW 100ms
    hold

# turned by a number, rounded to a quarter turn: each from no turn
script c45
    rotate 45
    hold
script c44
    rotate 44
    hold
script cm45
    rotate -45
    hold
script c135
    rotate 135
    hold
script cm90
    rotate -90
    hold
script c450
    rotate 450
    hold
script cm135
    rotate -135
    hold
script c225
    rotate 225
    hold
# the same by a param and by a variable of the game
script by_param
    rotate deg
    hold
script by_var
    rotate game_var.facing
    hold
# into a turn: a number, an expression
script to_180
    rotate 90
    rotate to 180
    hold
script to_expr
    rotate 90
    rotate to (deg * 90)
    hold
script twice
    rotate 90
    rotate 90
    rotate -270
    hold

# what is said in its axes, turned: velocity, accel, dx, dy, the picture's offset, a release
script moves
    rotate to 90
    velocity 40 0
    accel 0 300
    set got vx
    set d vy
    dx 3
    dy 2
    carry hand HILT
    frame ARROW 100ms at 2, 1
    release hand 120 -60
    shoot hand TURNER 300 0
    hold

# a path is of the axes it began in
script walk
    rotate to 90
    path x: t * 10, y: 0
    frame ARROW 1000ms
    rotate to 0
    frame ARROW 1000ms
    hold

# the turn read: rotation, degrees
script reads
    rotate to 270
    set got rotation
    goto up if rotation == 270
    set d 1
    hold
label up
    set d 2
    hold

# the one sensed: its turn, and where it is from here in this one's axes
script looks
    rotate to 90
    sense any within 100 where its.rotation == 180 into d, dx, _, got = its.rotation
    hold

# a mask watched in front: as it faces
script watches
    contact FEET front on touched
    frame ARROW 100ms
    frame ARROW 100ms
    frame ARROW 100ms
    hold
script watches_down
    rotate to 90
    contact FEET front on touched
    frame ARROW 100ms
    frame ARROW 100ms
    frame ARROW 100ms
    hold
script got_touched
    set got 1
    hold

# steered in its axes: the target down of its pictures is on its left in the scene
script steers
    rotate to 90
    steer seek 10, 100 to 0, 50
    hold

on touched: watches -> got_touched now
on touched: watches_down -> got_touched now
""")

pack.add_show("SHOW", pictures=[{"picture": "ARROW", "x": 10, "y": 10, "rotation": 90}, {"picture": "HILT", "x": 30, "y": 10}],
              actors=[{"name": "one", "kind": "TURNER", "x": 20, "y": 20, "flip": True, "rotation": 270},
                      {"name": "two", "kind": "TURNER", "x": 40, "y": 20}],
              cues=[(1000, "end", None)])
pack.add_crowd("CROWD", 8, (0, 0, 200, 200), [{"kind": "TURNER", "side": 1}],
               groups=[{"kind": "TURNER", "count": 2, "area": (10, 10, 50, 50), "rotation": 180},
                       {"kind": "TURNER", "count": 4, "area": (100, 100, 50, 50), "rotation": "any"}])

out = sys.argv[1]
pack.write(out, sys.argv[2])
