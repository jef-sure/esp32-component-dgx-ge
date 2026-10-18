"""A small pack for test_pack.c: two sets, pictures of every depth, a blob."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from dgx_ge_pack import Pack  # noqa: E402
from dgx_ge_graph import Graph  # noqa: E402

pack = Pack("TEST")
hero = pack.add_set("HERO", [(255, 0, 0), (0, 255, 0), (0, 0, 255)])
wide = pack.add_set("WIDE", [(i, i, i) for i in range(1, 201)])
pack.add_image("ONE", hero, 9, 2, [1, 0, 1, 0, 0, 0, 0, 0, 1,  0, 1, 0, 0, 0, 0, 0, 0, 0], 1, (4, 1))
pack.add_image("FOUR", hero, 3, 2, [1, 0, 3,  2, 2, 0], 4, (0, 1))
pack.add_image("EIGHT", wide, 2, 1, [200, 7], 8, (-3, 5))
pack.add_blob("LEVEL", b"hello, level")
four, one, eight = 1, 0, 2
pack.name_mask(1, "BODY")
pack.name_mask(2, "BLADE")
pack.add_mask(four, 2, 3, -1, 2, 1, [1, 1])                       # a blade beyond the picture
pack.add_mask(four, 1, 0, -1, 3, 2, [1, 0, 1,  1, 1, 0])
pack.add_mask(one, 1, -4, -1, 9, 2, [1, 0, 1, 0, 0, 0, 0, 0, 1,  0, 1, 0, 0, 0, 0, 0, 0, 0])
pack.add_meets("""
    foreign BLADE BODY   # a blade of one side cuts a body of another
    own 1 1              # bodies of one side bump
""")
pack.add_point(four, "hand", 3, -2, 2)     # before the body
pack.add_point(one, "hand", -1, 0, -1)     # behind it
pack.add_point(eight, "hand", 1, 1)        # where EIGHT is held
pack.add_point(eight, "hand", 1, 1)        # given again the same: nothing
# a kind of shot: its first script says what it is (a picture, a fall, its mask, what it does on meeting)
pack.add_anim("DART", """
script fly
    masks 2
    hit 2 on 1 damage 3 event hit
    accel 0 100
    frame EIGHT 1000ms
    hold
""")
pack.add_anim("HERO", """
event hit, stop, jump, landed, fell
define g 432
param action 0
param hp 3
param power 120
table steps 5 -7 300                  # constant numbers, read by index (0 outside)
game far near                         # questions to the game: game.far 0, game.near 1

script stand
    set action 0
    frame ONE 100ms
    goto stand

script run
    set action 1
    frame FOUR 50ms
    dx 2
    frame EIGHT 50ms at 1,-1
    signal 7
    dx 2
    branch
label run_cycle
    frame ONE 50ms
    dx 3
    sound 2
    goto run_cycle

script strike
    masks 1 2
    carry hand EIGHT                  # what is in the hand
    frame ONE 100ms
    hit 2 on 1 3 damage 4 event hit   # the blade on a body or a shield
    hit 2 on 2 recoil stop            # on a blade: this one stops
    frame FOUR 100ms
    masks 1
    carry hand none
    frame ONE 100ms
    hold

script fall
    set action 2
    accel 0 200
    frame ONE 1000ms
    hold

script walker
    velocity 40 0
    contact 1 below on landed off fell
label walking
    frame ONE 1000ms
    goto walking

script edge_stand
    velocity 0 0
    contact 1 off
    frame ONE 1000ms
    hold

script hurt
    add hp -5
    flip
    dx 4
    masks 0 3
    frame FOUR 100ms
    dx 5
    hold

script ease
    path x: smoothstep(t / 0.5) * 80, y: 0   # 80 points in half a second, starting and stopping smoothly
    frame ONE 1000ms
    hold

script hop
    velocity 0 0
    contact 1 below off fell
    frame ONE 50ms
    dx 120
    contact 1 below off hit   # the same watch, another event: the floor lost by the hop is told with the new one
    frame ONE 50ms
    hold

script plunge
    velocity 0 3000                    # 3 points a ms: a step goes from over the slab to inside it, past its top row
    contact 1 below within 1 on landed
label plunging
    frame ONE 1000ms
    goto plunging

script walkwall
    velocity 40 0
    contact 1 beside within 1 on hit   # a wall met at either side: hit, at the moment
label walkwall_on
    frame ONE 1000ms
    goto walkwall_on

script arc
    path x: 40*t, y: -power*t + g*t*t/2   # a throw: forward, up, and down again
    frame ONE 1000ms
    velocity 10, power / 2                 # off the path, by numbers worked out now
    add hp power - 100
    frame FOUR 1000ms
    hold

script throw
    carry hand EIGHT
    frame ONE 100ms
    release hand power / 2, -30           # 60 forward, 30 up
    frame FOUR 100ms
    release hand 5 5                      # nothing held any more: nothing let go
    hold

script tipsy
    set hp 3                              # as it is: no change, nothing told
    set hp now + 10                       # the moment of the step, seconds of the clock, and 10 more: told
    tell holder drank_wine                # an event of the pack for whoever holds it
    frame ONE 100ms
    add hp 0
    hold

script gunner
    frame ONE 100ms
    shoot hand DART 50, -20               # a dart from the hand: 50 forward, 20 up
    frame FOUR 100ms
    hold

script glass
    contact on landed                     # a contact: the event, the matrix says what follows
    accel 0 g
label glass_falling
    frame ONE 300ms                       # steps on the way: a contact is found between them too
    goto glass_falling

script broken
    velocity 0 0
    accel 0 0
    frame FOUR 1000ms
    hold

script drop
    contact on
    accel 0 g
    frame ONE 3000ms
    hold

script look                           # the nearest hero with 3 lives or more: how far, how far forward; its lives * 10
    sense HERO within 100 where its.hp >= 3 into action, power, _, hp = its.hp * 10
    frame ONE 100ms
    flip                                # facing the other way: forward is the other way too
    sense HERO within 100 where its.hp >= 3 into action, power
    frame ONE 100ms
    sense HERO within 10 into action    # nobody that near: -1
    frame ONE 100ms
    hold

script graze                          # the nearest cell of grass grown 2 or more: how far, forward, its number
    sense cell within 30 where its.mod >= 2 into action, power, _, hp = its.cell
    frame ONE 100ms
    hold

script chase                          # steering: the target 30 forward, 40 down, 50 off
    set action 30
    set power 40
    steer seek 100, 1000 to action, power     # 100 toward it at once: 60, 80
    frame ONE 100ms
    steer flee 100, 20 from action, power     # away: the velocity changes by 20 only: 48, 64
    frame ONE 100ms
    steer arrive 100, 1000, 100 to action, power   # within 100 of it: as fast as it is far, 50: 30, 40
    frame ONE 100ms
    steer pursue 50, 1000 to 100, 0, 0, 25    # 100 ahead going down 25 a second: reached in 2 s, where it will be: 100, 50
    frame ONE 100ms
    set action 90
    steer wander 10, 1000, 0 angle action     # the angle 90 (down), no spread: 0, 10
    frame ONE 100ms
    hold

script breed                          # a calf where it stands, given lives of half its power and one more, and action 7
    appear HERO, hit, 5, 6 with hp = power / 2 + 1, action = 7
    frame ONE 100ms
    hold

script bitwork                        # worked out by the runtime: hp and power are params
    set action xor(hp, bit(3)) + bits(power) + field(power, 3, 4)   # 3 ^ 8 = 11, 120 has 4 ones, (120 >> 3) & 15 = 15
    set power steps[hp - 2] + steps[hp - 4] + steps[hp + 5]          # steps[1] -7, steps[-1] 0, steps[8] 0
    set hp steps[action - 30]                                         # steps[0]
    frame ONE 100ms
    hold

script ask                            # the game's answers, by number; the dice cast only on the way taken
    goto asked if game.near > 2
    set power rnd(1000)
label asked
    set action game.far * 2
    hold

script lazy                           # and, or, ?: lazy: the dice cast only for the part worked out
    set action game.near > 2 and rnd(1000) >= 0
    set hp game.near > 2 or rnd(1000) >= 0
    set power game.near > 2 ? 7 : rnd(1000)
    hold

group alive: stand, run, fall, hop, walkwall
on hit: alive -> hurt now
on stop: run -> stand branch
on jump: stand -> run end
on hit: hurt -> fall now
on landed: glass -> broken now
on fell: walker -> fall now
on landed: walker -> edge_stand now
on landed: plunge -> edge_stand now
""")
# a set of tiles of 8 x 8: a block solid all round, a floor solid at its top drawn before the people
pack.add_tileset("TILES", 8, 8, [
    {"name": "block", "kind": 5, "solid": {"top": True, "left": True, "right": True, "bottom": True}, "pieces": [(four, 0, 0, 0, 0)]},
    {"name": "floor", "kind": 6, "solid": {"top": True}, "pieces": [(four, 2, -6, 1, 3), (four, 4, 0, 0, 0)]},
    # rules: a lip only where above is empty; a door by its modifier; water going round; a decoration of variant 1;
    # a gate raised by mod / 4
    {"name": "rules", "kind": 7, "pieces": [
        {"image": eight, "dx": 0, "dy": -7, "slot": 0, "around": {"up": "empty"}},
        {"by_mod": [one, four, eight, None], "slot": 0},
        {"cycle": [(one, 100), (four, 300)], "slot": 1},
        {"image": eight, "dx": 5, "dy": -2, "slot": 0, "variant": 1},
        {"image": four, "dx": 1, "dy": "-mod / 4", "slot": 1, "around": {"left": "not block", "down": ["block", "floor"]}},
    ]},
    # a body at step 1, a silhouette of color 2 of its set; an edge laid with the cell on the right, at step 0, as a card
    {"name": "edge", "kind": 8, "pieces": [
        {"image": four, "slot": 0, "step": 1, "draw": "silhouette", "color": 2},
        {"image": four, "dx": 9, "slot": 0, "with": "right", "step": 0, "draw": "card"},
    ]},
    # decoration by fields of the variant: a mark picked by bits 4..5, moved by bits 0..3; a fill of 3 x 2 in color mark + 1
    {"name": "decor", "kind": 9, "pieces": [
        {"pick": "mark", "pictures": [None, one, four], "dx": "shift", "slot": 0},
        {"draw": "fill", "size": [3, 2], "dx": 1, "slot": 1, "color": (hero, "mark + 1")},
        {"image": eight, "slot": 0, "when": "mark >= 2 and not shift == 3"},
    ]},
    # for sight only: a slab that closes its bottom (a ceiling), a door frame that closes its right; neither stops
    {"name": "slab", "kind": 10, "closes": {"bottom": True}, "pieces": []},
    {"name": "frame", "kind": 11, "closes": {"right": True}, "pieces": []},
    # bodies: a plate 2 high whose bottom is 2 above the bottom of the cell; a gate, a strip at the right let down as far
    # as its modifier says (8 - mod high), none at all from 6 on (high enough to pass)
    {"name": "plate", "kind": 12, "bodies": [{"dx": 0, "dy": -2, "w": 8, "h": 2}], "pieces": []},
    {"name": "gate", "kind": 13, "bodies": [{"dx": 6, "dy": 0, "w": 2, "h": "8 - mod", "when": "mod < 6"}], "pieces": []},
    # a shelf: a slab 2 high at the top of the cell, from its middle to the middle of the next cell (a body reaching out of its cell)
    {"name": "shelf", "kind": 14, "bodies": [{"dx": 4, "dy": -6, "w": 8, "h": 2}], "pieces": []},
    # for a look only (sense ... seen): a thicket, a point through it costs 4; a hedge nothing is seen through
    {"name": "thicket", "kind": 15, "veil": 3, "pieces": []},
    {"name": "hedge", "kind": 16, "veil": 7, "pieces": []},
    # a plate only for mask 2 (one of some masks: a person's floor as its rules say), beside one for every mask at its left half
    {"name": "ward", "kind": 15, "bodies": [{"dx": 0, "dy": -2, "w": 8, "h": 2, "for": [2]}, {"dx": 0, "dy": -2, "w": 4, "h": 2}],
     "pieces": []},
    # a door: closes nothing, stops nothing; its opening 12 wide (4 into the next cell), 6 - mod high from the bottom
    {"name": "door", "kind": 16, "opening": {"dx": 0, "dy": 0, "w": 12, "h": "6 - mod"}, "pieces": []},
], ["back", "front"], fields={"shift": ("variant", 0, 4), "mark": ("variant", 4, 2)})
# a thing of pockets: five go into one place; dropped, it lies, kept by the world
pack.add_anim("KNIFE", """
stack 5
event dropped, picked
param poison 0
script held
    masks 2
    frame EIGHT 1000ms
    hold
script lying
    keep                    # lying, the world keeps it; in a hand it is no object of the world
    masks 1
    frame (EIGHT + amount - 1) 1000ms   # a stack lying shows so many
    hold
script poisoned
    set poison 1
    goto held
on dropped: held -> lying now
on picked: lying -> held now
""")
# one who has pockets: three places; draws a knife, puts it away, throws knives while he has any
pack.add_anim("CARRIER", """
pockets 3
script idle
    frame FOUR 100ms
    hold
script arm
    draw hand KNIFE
    frame FOUR 100ms
    frame FOUR 100ms
    stow hand
    frame FOUR 100ms
    hold
script throw
label again
    goto idle if count(KNIFE) == 0
    shoot hand KNIFE 40 0
    frame FOUR 100ms
    goto again
script twice
    draw hand KNIFE
    draw hand KNIFE         # the hand holds one: nothing
    frame FOUR 100ms
    hold
""")
# a heap: one picture for what lies in it (its box)
pack.add_anim("HEAP", """
script lying
    keep
    masks 1
    frame FOUR 1000ms
    hold
""")
# dialogs: a translation for the dialog post, its strings in the language of the pack; the same msgid in another
# dialog (other) is another context: not translated there
pack.translate(os.path.join(os.path.dirname(__file__), "test.po"))
pack.add_dialog("""
dialog post
roles guard CARRIER "Guard", kid CARRIER "Kid", hero HERO
style talk
label start
    say guard "Halt! Who goes there?"
    say! guard "Read this."     # important
    choose kid
        "A friend." -> friend
        "Have a knife." if count(KNIFE) > 0 -> gift
        "Nobody." if count(KNIFE) > 5 -> nobody
label friend
    style alert
    say game "You may pass."
    end
label gift
    give kid KNIFE 2 to guard
    tell hero hit
    say guard "Thanks."
    goto start
label nobody
    end
""")
# the style talk made (alert is named by post and never made: the default one); the game's first font
pack.add_style("talk", place=(10, -10, 200, 43), padding=(2, 2, 2, 2), font="TerminusTTFMedium12", scale=(2, 1), wait_ms=4000,
               fill=(0, 0, 80), border=(255, 255, 0), border_width=2, text=(255, 255, 255), bar=(0, 128, 0), name_color=(255, 0, 0),
               show_name=True, appear="letters", letter_ms=20, align="middle")
# a frame of a picture: 4 x 3 of hero, cut by insets 1 into nine (its middle 2 x 1)
pack.add_image("BOX", hero, 4, 3, [1, 2, 2, 3,  2, 0, 0, 2,  3, 2, 2, 1], 4, (0, 0))
pack.add_style("boxed", frame="picture", picture="BOX", insets=(1, 1, 1, 1), place=(10, 20, 40, 30), padding=(3, 3, 3, 3),
               font="TerminusTTFMedium12", fill=(1, 2, 3), border=(4, 5, 6))
# a portrait: a part of FOUR as a picture of its own; a style that shows it on the left in a room of 6, a dialog of a role
# with a face and one without
pack.add_crop("FACE", "FOUR", 1, 0, 2, 2, (0, 1))
# a soft pen: handwriting in 8 bits a point; the same asked of a font of pixels stays 1 bit
pack.add_style("inked", place=(0, 0, 120, 40), padding=(2, 2, 2, 2), font="font0ss", scale=(20, 12), pen="soft",
               fill=(200, 180, 120), text=(40, 20, 0))
pack.add_style("pixels_soft", place=(0, 0, 120, 40), padding=(2, 2, 2, 2), font="TerminusTTFMedium12", pen="soft")
pack.add_style("faced", place=(0, 0, 60, 30), padding=(2, 2, 2, 2), font="TerminusTTFMedium12", portrait="left", portrait_width=6)
pack.add_dialog("""
dialog faces
roles guard CARRIER "Guard" face FACE, kid CARRIER "Kid"
style faced
    say guard "Hi."
    choose kid
        "Bye." -> out
label out
    end
""")
pack.add_dialog("""
dialog other
label only
    say game "Thanks." 2s       # information: on by itself
    say! game "Long news." 1s   # important: its time runs once it is seen whole
    choose game 3s -> late      # no answer: late
        "Again." -> only
    end
label late
    say game "Too late."
    end
""")
# a page of settings: the game's actor (SETTINGS: params only) and controls of its params: a switch (its picture by
# its value), a slider 0..3 (more, less)
pack.add_anim("SETTINGS", """
param sound 1
param volume 2
script rest
    hold
""")
pack.add_anim("SWITCH", """
event activate
param value 0
param least 0
param most 1
param step 1
script rest
    frame (ONE + value) 1000ms
    hold
script flip
    set value 1 - value
    goto rest
on activate: rest -> flip now
""")
pack.add_anim("SLIDER", """
event more, less
param value 0
param least 0
param most 3
param step 1
script rest
    frame (ONE + min(value, 2)) 1000ms
    hold
script up
    set value min(value + 1, 3)
    goto rest
script down
    set value max(value - 1, 0)
    goto rest
on more: rest -> up now
on less: rest -> down now
""")
pack.add_dialog("""
dialog page
game SETTINGS
choose game
    "Sound" control SWITCH sound
    "Loud" if volume > 2 -> loud    # shown before the slider that shows it
    "Volume" control SLIDER volume
    "Quiet" control SWITCH sound -> back   # the switch, then on
    "Back" -> back
label loud
    say game "Loud."
label back
    end
""")
# lines of a number: a param of the one saying it, its forms in the language of the pack (three in test.po), one without
pack.add_dialog("""
dialog clock
game SETTINGS
say game "{volume} minute left" "{volume} minutes left" 1s
say game "Volume {volume}." 1s
say game "Sound {sound}, volume {volume}." 1s
end
""")
# lives of cells: a latch (its modifier: 0 shut .. 12 up) goes up a step of 4 every 100 ms when told to open, signals at
# the top, stays up as long as its own param stay says (a link may say), comes down and rests; broken, its tile is
# another and its modifier keeps its low bits with bit 7 set
pack.add_anim("LATCH", """
event open, break
param mod 0
param tile 0
param stay 300
param ups 0
script rest
    hold
script opening
label up
    frame 0 100ms
    set mod mod + 4
    goto up if mod < 12
    signal 7
    add ups 1
script staying
label wait
    frame 0 100ms
    set stay stay - 100
    goto wait if stay > 0
script closing
label down
    frame 0 100ms
    set mod mod - 4
    goto down if mod > 0
    hold
script broken
    set tile 0
    set mod (mod & 0x0F) | 0x80
    hold
on open: rest -> opening now
on break: rest -> broken now
on break: staying -> broken now
""")
# a flicker: every 100 ms a frame 0..8 by the dice of the game, the remainder of 23 by 5 kept in its own param
pack.add_anim("FLICKER", """
event light
param mod 0
param tile 0
param left 0
script rest
    hold
script burning
    set left 23 % 5
label tick
    frame 0 100ms
    set mod rnd(8)
    goto tick
on light: rest -> burning now
""")
# links of a map: a source from 0 sets cells 1 (its event), 2 (break, its own) and, last, 3 (its event, staying 100);
# a source from 3 sets cell 0 only; a place kept for nobody before it
pack.add_links("LINKS", [(1, None, False, None), (2, "break", False, None), (3, None, True, 100), (None, None, False, None), (0, None, True, None)])
# a graph of scenes: a hall of two rooms (its data the blob LEVEL) and a cellar; the rooms side by side, both ways; a
# hatch of the first one way down to the cellar, over a slope, falling; the hall's rules: died twice, back at the mark,
# the third time from the start (and counting anew); its end leads to the cellar
graph = Graph("WORLD")
graph.map("hall", data="LEVEL", rules="""
event death, end
param deaths 0
script rest
    hold
script died
    add deaths 1
    goto mark if deaths <= 2
    set deaths 0
    go start
    goto rest
label mark
    go checkpoint
    goto rest
script ended
    press 9
    go next
    goto rest
on death: rest -> died now
on end: rest -> ended now
""")
graph.map("cellar", number=12)
# the world of a map acted on by a script (rules, mostly): a cell told an event with a value, a button pressed, a cell
# cleared; the game does them
pack.add_anim("WARDEN", """
event loose, fell
param n 3
script rest
    hold
script act
    tell cell 31 + n, loose, n * 2
    press 7
    clear 40 + 1
    goto rest
on loose: rest -> act now
# where it is: on past the line, it calls one of the HERO kind and is gone
script walk
    frame 0 100ms
    dx 10
    goto walk if x < 120
    appear HERO, loose, x + 5, y, left
    vanish
    hold
# standing needs a floor: begun with nothing within 8 points below, it falls at once
script stand
    frame 0 100ms
    contact 1 below within 1 off fell first within 8
    frame 0 1000ms
    hold
on fell: stand -> rest now
""")
h1, h2, c1 = graph.scene("hall", 1), graph.scene("hall", 2, on_enter="end"), graph.scene("cellar", 1)
graph.side(h1, "right", h2)
graph.side(h2, "left", h1)
graph.leave(h1, "right", "end")      # told as one leaves the first room to the right
graph.leave(h2, "down", "death")     # no scene below: an edge out of the map
graph.exit(c1, "lift", h1, "start", "seamless")
graph.exit(h1, "hatch", c1, "slope", "fade")
graph.exit("hall", "start", h1, "start")
graph.exit("hall", "checkpoint", h2, "mark")
graph.exit("hall", "next", c1, "slope", "fade")
graph.entry(h1, "start", 10, 20)
graph.entry(h2, "mark", 5, 6, left=True, fires=3)
graph.entry(c1, "slope", 40, 0, arrive="fall")
graph.start(h1, "start")
pack.add_graph(graph)
# the constants of the game: what the engine's parts take as they are, and one of its own; given again, its place kept
pack.add_game(kinds=16, lives=4, minutes=50)
pack.add_game(minutes=60, timers=3)
# timers: a clock of the game, going from its start, every second; a bomb once in 5 s whatever one does (its event boom);
# an alarm every 500 ms no longer than 2 s, stopped as one leaves the scene (its event ring); a watch reads the clock
pack.add_anim("WATCH", """
event ring
param left 0
param lost 0
script look
    set left 60 - timer(clock) / 1000
    goto over if timer(clock) >= 3000
    hold
label over
    set lost 1
    hold
script rang
    set left timer(alarm)
    hold
on ring: look -> rang now
""")
# sounds: two names only (they play nothing), then some with samples: 8 bits as given, a tone in ADPCM, stereo as the
# middle and the side (the side at half the rate), one going round
assert pack.add_sound("step") == 0 and pack.add_sound("splash") == 1
import math  # noqa: E402
TONE = [0, 256, -256, 32512, -32768, 1024]
BEEP = [int(9000 * math.sin(i / 7)) for i in range(200)]
WIDE = [(int(8000 * math.sin(i / 5)), int(-4000 * math.sin(i / 5) + 3000 * math.sin(i / 3))) for i in range(64)]
HUM = [256, 512, 768, 1024]
pack.add_sound("tone", samples=TONE, rate=8000, codec="pcm8")
pack.add_sound("beep", samples=BEEP, rate=8000)
pack.add_sound("wide", samples=WIDE, rate=22050, stereo=True)
pack.add_sound("hum", samples=HUM, rate=8000, codec="pcm8", loop=True)
pack.add_sound("hush", samples=HUM, rate=8000, codec="pcm8", gain=0.5)  # half as loud: 128 256 384 512, in 8 bits 0 256 256 512
pack.add_timer("clock", every="1s", start="game")
pack.add_timer("bomb", once="5s", event="boom")
pack.add_timer("alarm", every="500ms", until="2s", reset="scene", event="ring")
# a button, the life of its cell: pressed, its tile down (7) and a timer of the cell started; the timer's event, up (6)
pack.add_anim("BUTTON", """
event press, release
param mod 0
param tile 0
script rest
    hold
script pressed
    set tile 7
    timer start down
    sound 1
    hold
script up
    set tile 6
    hold
on press: rest -> pressed now
on release: rest -> up now
""")
pack.add_timer("down", once="300ms", reset="map", event="release")
# a show: a backdrop of FOUR, two actors who wave when the cues tell them (the second facing the other way), the end
pack.add_anim("WAVE", """
event wave
script still
    frame ONE 1000ms
    hold
script waving
    frame FOUR 100ms
    frame EIGHT 100ms
    goto waving
on wave: still -> waving now
""")
# chasing: the waving one hears the hero 50 points off, follows by the sides (20 near them) and doors, two scenes from
# home at most, calm after 3 s out of hearing; the hero (as an enemy) sees, follows only down, past the floor already
pack.add_chase("WAVE", sense="hearing", sides=("left", "right", "doors"), hearing=50, near={"left": 20, "right": 20}, steps=2, calm="3s")
pack.add_chase("HERO", sense="sight", sides=("down",), near={"down": -1})
pack.add_show("SHOW", pictures=[{"picture": "FOUR", "x": 10, "y": 20, "layer": 0}, {"picture": "ONE", "x": 30, "y": 20, "layer": 0, "color": 3}],
              actors=[{"name": "a", "kind": "WAVE", "x": 50, "y": 60, "layer": 1}, {"name": "b", "kind": "WAVE", "x": 80, "y": 60, "flip": True, "layer": 1}],
              cues=[("1s", "end", None), ("700ms", "b", "wave"), (300, "a", "wave")])
# a show of pictures alone, no actors: it lasts until its end
pack.add_show("STILL", pictures=[{"picture": "FOUR", "x": 0, "y": 1, "layer": 0}], actors=[], cues=[("2s", "end", None)])
# a crowd: a wolf that hunts sheep, sheep that run from it (they sense each other: made together), a mother that bears,
# a walker turned by the bounds of its world
wolf = pack.add_image("WOLFP", hero, 5, 5, [1] * 25, 4, (2, 2))
sheep = pack.add_image("SHEEPP", hero, 3, 3, [2] * 9, 4, (1, 1))
pack.add_mask(wolf, 1, -2, -2, 5, 5, [1] * 25)          # its body
pack.add_mask(wolf, 2, 3, -1, 2, 3, [1] * 6)            # its jaws, before it
pack.add_mask(sheep, 1, -1, -1, 3, 3, [1] * 9)
pack.add_anims([("SHEEP", """
event eaten, born, bumped
param d -1
param dx 0
param dy 0
param wvx 0
param wvy 0
param angle 0
param gene 0
param hp 5

script live
    masks 1
    sense WOLF within 60 into d, dx, dy, wvx = its.vx, wvy = its.vy
    goto run if d >= 0
    steer wander 20, 10, 30 angle angle
    frame SHEEPP 100ms
    goto live
script run
    sense WOLF within 60 into d, dx, dy, wvx = its.vx, wvy = its.vy
    goto live if d < 0
    steer evade 40, 20 from dx, dy, wvx, wvy
    frame SHEEPP 100ms
    goto run
script dead
    velocity 0 0
    frame SHEEPP 200ms
    vanish
    frame none 100ms
    hold
group alive: live, run
on eaten: alive -> dead now
"""), ("WOLF", """
event ate
param d -1
param dx 0
param dy 0
param tvx 0
param tvy 0
param angle 0
param food 0
param seen 0

script hunt
    masks 1 2
    hit 2 on 1 damage 2 event eaten recoil ate
    sense SHEEP within 300 into d, dx, dy, tvx = its.vx, tvy = its.vy, seen = its.hp
    goto roam if d < 0
    steer pursue 60, 30 to dx, dy, tvx, tvy
    frame WOLFP 100ms
    goto hunt
script roam
    steer wander 25, 8, 20 angle angle
    frame WOLFP 100ms
    goto hunt
script fed
    add food 1
    goto hunt
group hungry: hunt, roam
on ate: hungry -> fed now
"""), ("MOTHER", """
event born
param gene 7
script bear
    frame SHEEPP 500ms
    appear SHEEP, born, x + 10, y, left with gene = gene * 2
    appear SHEEP, born, x - 10, y with gene = gene * 3
    frame SHEEPP 1000ms
    hold
"""), ("WALKER", """
event bumped
script go
    masks 1
    velocity 50 0
    contact 1 front on bumped
label going
    frame SHEEPP 1000ms
    goto going
script turn
    flip
    goto go
on bumped: go -> turn now
""")])
pack.add_crowd("FIELD", 8, (0, 0, 200, 150), seed=7,
               kinds=[{"kind": "WOLF", "side": 1, "layer": 1, "by_y": True}, {"kind": "SHEEP", "side": 2, "layer": 1, "damage": "hp", "by_y": True}],
               groups=[{"kind": "WOLF", "count": 1, "area": (10, 10, 20, 20)}, {"kind": "SHEEP", "count": 3, "area": (120, 80, 60, 50), "flip": "either"}])
pack.add_crowd("NURSERY", 2, (0, 0, 100, 100), kinds=[{"kind": "MOTHER", "side": 3}, {"kind": "SHEEP", "side": 2}],
               groups=[{"kind": "MOTHER", "area": (50, 50, 1, 1)}])
# one who looks (seen: as the tiles veil it, 100 and 60 points) at one who stands still
pack.add_anims([("MARKER", """
script still
    masks 1
    frame SHEEPP 1000ms
    goto still
"""), ("LOOKOUT", """
param d -1
param far -1
param hedge -1
param reach 20
param near -1
script look
    add reach 10                                # its nose: farther at every step (a radius worked out at the step)
    sense MARKER within reach into near
    sense MARKER within 100 seen into d
    sense MARKER within 60 seen into far
    sense cell within 100 seen into hedge
    frame SHEEPP 100ms
    goto look
""")])
pack.add_crowd("BLIND", 2, (0, 0, 80, 32), kinds=[{"kind": "LOOKOUT", "side": 5}, {"kind": "MARKER", "side": 6}],
               groups=[{"kind": "LOOKOUT", "area": (8, 16, 1, 1)}, {"kind": "MARKER", "area": (58, 16, 1, 1)}])
pack.add_crowd("PEN", 1, (0, 0, 60, 40), kinds=[{"kind": "WALKER", "side": 4}], groups=[{"kind": "WALKER", "area": (30, 20, 1, 1)}])
pack.write(sys.argv[1], sys.argv[2])


# What the samples of the ADPCM sounds decode to, by IMA ADPCM done here apart from the engine: test_mixer.c checks the
# engine's decoder against it (and so the packer's encoder against both)
def ima_decode(state, nibbles, count):
    from dgx_ge_pack import IMA_STEPS, IMA_INDEX
    pred, index, out = state[0], state[1], []
    for n in range(count):
        code = nibbles[n // 2] >> 4 if n & 1 else nibbles[n // 2] & 15
        step = IMA_STEPS[index]
        delta = (step >> 3) + (step if code & 4 else 0) + (step >> 1 if code & 2 else 0) + (step >> 2 if code & 1 else 0)
        pred = max(-32768, min(32767, pred - delta if code & 8 else pred + delta))
        index = max(0, min(88, index + IMA_INDEX[code & 7]))
        out.append(pred)
    return out


from dgx_ge_pack import ima_adpcm_stream  # noqa: E402
beep = ima_decode(*ima_adpcm_stream(BEEP), len(BEEP))
mid, dif = [(l + r) // 2 for l, r in WIDE], [(l - r) // 2 for l, r in WIDE]
side = [sum(dif[i:i + 2]) // len(dif[i:i + 2]) for i in range(0, len(dif), 2)]
m, d = ima_decode(*ima_adpcm_stream(mid), len(mid)), ima_decode(*ima_adpcm_stream(side), len(side))
wide = [(m[i] + d[i >> 1], m[i] - d[i >> 1]) for i in range(len(WIDE))]
with open(os.path.join(os.path.dirname(sys.argv[2]), "test_sounds.h"), "w") as f:
    f.write("/* made by make_test_pack.py: the sounds' samples as given and as IMA ADPCM decodes them apart from the engine */\n")
    f.write(f"static const int16_t tone_given[] = {{{', '.join(map(str, TONE))}}};\n")
    f.write(f"static const int16_t hum_given[] = {{{', '.join(map(str, HUM))}}};\n")
    f.write(f"static const int16_t beep_given[] = {{{', '.join(map(str, BEEP))}}};\n")
    f.write(f"static const int16_t beep_decoded[] = {{{', '.join(map(str, beep))}}};\n")
    f.write(f"static const int32_t wide_decoded[][2] = {{{', '.join('{%d, %d}' % lr for lr in wide)}}};\n")
