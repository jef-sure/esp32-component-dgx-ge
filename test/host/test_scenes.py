"""
The packer reads the scenes, the graph and the game of the sprite editor's file: a blob DGSC a scene (its layers,
pictures, zones, entries, sources of links, actors, grids with the numbers of their first cells) and its names in the
header; the links of a map, a blob of them (cells numbered through the map, each source's links together); the graph
and its maps in the editor's order of names, an entry's fires; the game's constants, timers, sounds, crops, shows,
chases. It writes build/layout/layout.pack for test_layout.c.
"""
import copy
import json
import os
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from dgx_ge_pack import Pack  # noqa: E402

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "layout")
os.makedirs(os.path.join(out, "hero"), exist_ok=True)
failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


im = Image.new("P", (4, 3))
im.putpalette([0, 0, 0, 255, 0, 0, 0, 255, 0] + [0] * 759)
im.putdata([0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2])
im.save(os.path.join(out, "hero", "stand-1.png"))
with open(os.path.join(out, "hero.anim"), "w") as f:
    f.write("event second, go\nparam hp 3\nparam speed 1\nscript stand\n    frame HERO_STAND_1 100ms\n    hold\nscript walk\n"
            "    frame HERO_STAND_1 100ms\n    hold\n")
with open(os.path.join(out, "rules.anim"), "w") as f:
    f.write("event death\nscript rest\n    hold\n")

doc = {
    "format": "dgx-ge-sprites", "formatVersion": 1, "name": "layout", "synth": "chip",
    "masks": [{"index": 1, "name": "body", "color": "#ff0000"}],
    "sets": [{"name": "hero", "defaultMask": 1, "frames": [{"image": "stand-1.png", "width": 4, "height": 3, "origin": {"x": 1, "y": 2}}]}],
    "tilesets": [{"name": "tiles", "cellWidth": 8, "cellHeight": 8, "tiles": [
        {"name": "floor", "kind": 1, "solid": {"top": True}, "pieces": [{"set": "hero", "image": "stand-1.png", "layer": "back"}]},
        {"name": "wall", "kind": 2, "solid": {"top": True, "left": True}, "pieces": [{"set": "hero", "image": "stand-1.png", "layer": "front"}]}]}],
    "screen": {"width": 64, "height": 32},
    "scenes": [
        {"name": "room1", "width": 64, "height": 32, "background": "#14142a", "music": "theme", "musicFade": 300, "map": "level", "index": 1, "kind": "room", "onEnter": "", "leave": {"down": "fell"},
         "layers": [
             {"name": "back", "factor": 256, "order": "added", "objects": [
                 {"type": "picture", "name": "torch", "set": "hero", "image": "stand-1.png", "x": 10, "y": 20, "key": 0, "flip": True,
                  "clip": {"x": 0, "y": 0, "width": 32, "height": 32}},
                 {"type": "actor", "name": "guard", "set": "hero", "script": "walk", "x": 30, "y": 28, "flip": False, "rotation": 270,
                  "params": {"hp": 5}},
                 {"type": "zone", "name": "door", "kind": "exit", "x": 50, "y": 10, "width": 8, "height": 16},
                 {"type": "entry", "name": "start", "x": 4, "y": 28, "flip": False, "arrive": ""}]},
             {"name": "front", "factor": 128, "order": "key", "objects": [
                 {"type": "picture", "name": "", "set": "hero", "image": "stand-1.png", "x": 40, "y": 30, "key": 3, "flip": False, "rotation": -180,
                  "clip": None},
                 {"type": "zone", "name": "pit", "kind": "trap", "x": 0, "y": 30, "width": 64, "height": 2}]}],
         "grids": [{"name": "grid", "tileset": "tiles", "x": 0, "y": 8, "cols": 3, "rows": 3, "cells": [[0, 1, 2], [1, 1, 0], [0, 0, 1]],
                    "mods": {"1,0": 7}, "variants": {"2,0": 70000}}],
         "exits": [{"edge": "right", "scene": "room2"}, {"edge": "", "name": "door_out", "zone": "door", "scene": "room2", "entry": "in",
                                                        "transition": "fade"}],
         # links: a plate (1,0) with two links, not one after the other in the list; an actor and a zone as sources
         "links": [{"from": {"grid": "grid", "col": 1, "row": 0}, "to": {"grid": "grid", "col": 2, "row": 2}, "event": "open", "value": 5},
                   {"from": {"object": "guard"}, "to": {"grid": "grid", "col": 0, "row": 1}, "event": "", "value": None},
                   {"from": {"grid": "grid", "col": 1, "row": 0}, "to": {"grid": "grid", "col": 0, "row": 2}, "event": "shut", "value": None},
                   {"from": {"object": "pit"}, "to": {"grid": "grid", "col": 1, "row": 1}, "event": "open", "value": None}]},
        {"name": "room2", "width": 64, "height": 32, "map": "level", "index": 2, "kind": "room",
         "layers": [{"name": "back", "factor": 256, "order": "added", "objects": [
             {"type": "entry", "name": "in", "x": 2, "y": 20, "flip": True, "fires": {"scene": "room1", "object": "guard"}}]}],
         "grids": [{"name": "floor", "tileset": "tiles", "x": 0, "y": 16, "cols": 2, "rows": 2, "cells": [[1, 0], [0, 2]]}],
         "links": [{"from": {"grid": "floor", "col": 1, "row": 1}, "to": {"grid": "floor", "col": 0, "row": 0}, "event": "open"}],
         "exits": [{"edge": "left", "scene": "room1"}]}],
    "graph": {"name": "WORLD", "start": {"scene": "room1", "entry": "start"}},
    "maps": [{"name": "level", "data": "", "rules": "rules", "number": 1,
              "exits": [{"name": "next", "scene": "room2", "entry": "in", "transition": "cut"}]}],
    "game": [{"name": "kinds", "value": 8}, {"name": "lives", "value": 3}],
    "timers": [{"name": "clock", "every": "1s", "reset": "never", "event": "second", "start": "game"}],
    "sounds": ["step"],
    "music": ["theme"],
    "crops": [{"name": "HEAD", "set": "hero", "image": "stand-1.png", "x": 0, "y": 0, "width": 2, "height": 1, "originX": 0, "originY": 0}],
    "shows": [{"name": "TITLE", "pictures": [{"picture": "HERO_STAND_1", "x": 0, "y": 10, "layer": 0, "flip": False, "color": 0}], "actors": [],
               "cues": [{"at": "1s", "role": "end", "event": ""}]}],
    "chases": [{"kind": "HERO", "sense": "always", "sides": ["left", "right"], "hearing": 0, "near": {"left": 4}, "steps": 0, "calm": ""}],
}


os.makedirs(os.path.join(out, "music"), exist_ok=True)
with open(os.path.join(out, "music", "theme.mml"), "w") as f:
    f.write("@1 = square 25%\nA t120 o4 @1 $ c e g\n")


def packed(d):
    with open(os.path.join(out, "layout.json"), "w") as f:
        json.dump(d, f)
    p = Pack("LAY")
    p.add_sprites(os.path.join(out, "layout.json"))
    return p


pack = packed(doc)
blobs = {n: i for i, (n, _) in enumerate(pack.blobs)}
images = {im[0]: i for i, im in enumerate(pack.images)}
check("SCENE_ROOM1" in blobs and "SCENE_ROOM2" in blobs, f"the blobs of the scenes {list(blobs)}")
b = pack.blobs[blobs["SCENE_ROOM1"]][1]
magic, version, layers, grids, width, height, npic, nact, nzone, nentry, nsrc, zero, grids_at, paper, has = struct.unpack_from("<IHBBhhHHHHHHI3sB", b, 0)
check((magic, version, layers, grids, width, height, npic, nact, nzone, nentry, nsrc, zero, paper, has) ==
      (0x43534744, 5, 2, 1, 64, 32, 2, 1, 2, 1, 3, 0, bytes((20, 20, 42)), 1),
      f"the head {(magic, version, layers, grids, width, height, npic, nact, nzone, nentry, nsrc, zero, paper, has)}")
# silence named, the fade as the engine's when not said (500 ms); a name new to the pack named by the scene
quiet = copy.deepcopy(doc)
quiet["scenes"][1]["music"] = "stop"
quiet["scenes"][0]["music"] = "dungeon"
del quiet["scenes"][0]["musicFade"]
qp = packed(quiet)
qb = {n: blob for n, blob in qp.blobs}
check(struct.unpack_from("<BBH", qb["SCENE_ROOM2"], 32) == (0, 2, 500), f"silence: {struct.unpack_from('<BBH', qb['SCENE_ROOM2'], 32)}")
check(struct.unpack_from("<BBH", qb["SCENE_ROOM1"], 32) == (qp.musics.index("dungeon"), 1, 500) and "dungeon" in qp.musics, "a new name, the fade unsaid")
# the scene's music: theme (its notes from music/theme.mml beside the document), its fade; room2 names none
check(struct.unpack_from("<BBH", b, 32) == (pack.musics.index("theme"), 1, 300), f"the music of room1 {struct.unpack_from('<BBH', b, 32)}")
check(struct.unpack_from("<BBH", pack.blobs[blobs["SCENE_ROOM2"]][1], 32) == (0, 0, 0), "room2 names no music")
check(pack.music_data[pack.musics.index("theme")] is not None, "the document's music, its notes beside it")
at = 36
check(struct.unpack_from("<hBBhBB", b, at) == (256, 0, 0, 128, 1, 0), "the layers: factor, order")
at += 4 * layers
stand = images["HERO_STAND_1"]
check(struct.unpack_from("<HhhhBBhhhh", b, at) == (stand, 10, 20, 0, 0, 1, 0, 0, 32, 32), f"the torch {struct.unpack_from('<HhhhBBhhhh', b, at)}")
check(struct.unpack_from("<HhhhBBhhhh", b, at + 18) == (stand, 40, 30, 3, 1, 2 << 1, 0, 0, 0, 0),
      "the picture of the front layer, its key, turned (-180: 180, two quarter turns, bits 1-2 of its flags), no clip")
at += 18 * npic
check(struct.unpack_from("<Hhhhh", b, at) == (0, 50, 10, 8, 16) and struct.unpack_from("<Hhhhh", b, at + 10) == (1, 0, 30, 64, 2), "the zones: exit 0, trap 1")
at += 10 * nzone
check(struct.unpack_from("<hhBB", b, at) == (4, 28, 0, 0), "the entry")
at += 6 * nentry
# the sources of links, in the order they first come: the plate (cell 1) from link 0, the guard (actor 0) from 2, the pit (zone 1) from 3
check([struct.unpack_from("<BBHH", b, at + 6 * i) for i in range(3)] == [(0, 0, 1, 0), (1, 0, 0, 2), (2, 0, 1, 3)],
      f"the sources {[struct.unpack_from('<BBHH', b, at + 6 * i) for i in range(3)]}")
at += 6 * nsrc
actor_at = struct.unpack_from("<H", b, at)[0]
hero = [a for a in pack.anims if a[0] == "HERO"][0]
check(struct.unpack_from("<HBBhhBB", b, actor_at) == (blobs["HERO"], hero[1].index("walk"), 0, 30, 28, 3 << 1, 1),
      "the guard: kind, script, layer, place, turned 270 (bits 1-2 of its flags)")
check(struct.unpack_from("<BBh", b, actor_at + 10) == (hero[3].index("hp"), 0, 5), "the guard's hp")
# the grid: its head 14, its 2 slots, 9 cells (34), 9 modifiers (43), 1 to a multiple of 4, the variants (36): 80
check(grids_at % 4 == 0 and grids_at == len(b) - 80, f"the grids from a multiple of 4: {grids_at} of {len(b)}")
ts, gx, gy, cols, rows, first, flags, slots = struct.unpack_from("<HhhHHHBB", b, grids_at)
check((ts, gx, gy, cols, rows, first, flags, slots) == (blobs["TILES"], 0, 8, 3, 3, 0, 3, 2), f"the grid {(ts, gx, gy, cols, rows, first, flags, slots)}")
g = grids_at + 14
check(list(b[g:g + 2]) == [0, 1], "the slots of the tiles: back, front as the scene's layers")
g += 2
check(list(struct.unpack_from("<9H", b, g)) == [0, 1, 2, 1, 1, 0, 0, 0, 1], "the cells")
g += 18
check(list(b[g:g + 9]) == [0, 7, 0, 0, 0, 0, 0, 0, 0], "the modifiers")
g += 9
check(g % 4 == 3, "the modifiers end off a multiple of 4: the variants after a gap")
g += -g % 4
check(list(struct.unpack_from("<9I", b, g)) == [0, 0, 70000] + [0] * 6, "the variants, from a multiple of 4")
# room2: its grid's cells numbered after room1's 9, its plate (cell 12) from link 4
b2 = pack.blobs[blobs["SCENE_ROOM2"]][1]
head2 = struct.unpack_from("<IHBBhhHHHHHHI3sB", b2, 0)
check(head2[3] == 1 and head2[10] == 1 and struct.unpack_from("<BBHH", b2, 36 + 4 + 6) == (0, 0, 12, 4), f"room2: a grid, a source {head2}")
check(head2[13:15] == (bytes(3), 0), f"room2: no background, no paper {head2[13:15]}")
check(struct.unpack_from("<HhhHHHBB", b2, head2[12])[3:6] == (2, 2, 9), "room2's grid: 2 x 2, its first cell 9")
# the links of the map, one list: the plate's two, the guard's (the event the source fires), the pit's, room2's plate's
lb = pack.blobs[blobs["WORLD_LEVEL_LINKS"]][1]
ev = {n: i for i, n in enumerate(pack.events)}
check(struct.unpack_from("<IHH", lb, 0) == (0x4B4C4744, 1, 5), f"the links' head {struct.unpack_from('<IHH', lb, 0)}")
rows = [struct.unpack_from("<HBBh", lb, 8 + 6 * i) for i in range(5)]
check(rows == [(8, ev["open"], 0, 5), (6, ev["shut"], 1, -32768), (3, 0xFF, 1, -32768), (4, ev["open"], 1, -32768), (9, ev["open"], 1, -32768)],
      f"the links {rows}")
# the entry in fires the guard's links as one comes: its first, 2
fires = {e[1]: e[6] for e in pack.graphs[0].entries}
check(fires == {pack.graphs[0].entry_names.index("start"): None, pack.graphs[0].entry_names.index("in"): 2}, f"the entries' fires {fires}")
# the header
h = pack.header("X")
for line in ("LAY_SCENE_ROOM1_PICTURE_TORCH 0", "LAY_SCENE_ROOM1_ACTOR_GUARD 0", "LAY_SCENE_ROOM1_ZONE_DOOR 0", "LAY_SCENE_ROOM1_ZONE_PIT 1",
             "LAY_SCENE_ROOM1_ENTRY_START 0", "LAY_SCENE_ROOM1_GRID_GRID 0", "LAY_SCENE_ROOM1_LAYER_FRONT 1", "LAY_SCENE_ROOM2_ENTRY_IN 0",
             "LAY_ZONE_EXIT 0", "LAY_ZONE_TRAP 1", "LAY_SCREEN_WIDTH 64", "LAY_SCREEN_HEIGHT 32",
             "LAY_WORLD_ENTRY_START 0", "LAY_WORLD_ENTRY_IN 1", "LAY_WORLD_EXIT_DOOR_OUT 0", "LAY_WORLD_EXIT_NEXT 1", "LAY_WORLD_MAP_LEVEL 0",
             "LAY_GAME_KINDS 0", "LAY_GAME_LIVES 1", "LAY_TIMER_CLOCK 0", "LAY_SOUND_STEP 0", "LAY_IMG_HEAD", "LAY_BLOB_TITLE",
             "LAY_BLOB_WORLD", "LAY_BLOB_WORLD_LEVEL_RULES", "LAY_BLOB_WORLD_LEVEL_LINKS"):
    check(f"#define {line}" in h, f"the header: {line}")
check("PICTURE__" not in h and "_PICTURE_ 1" not in h, "an unnamed picture has no constant")
# the graph: its scenes, sides, exits and entries as the editor gives them
g = pack.graphs[0]
check(g.entry_names == ["start", "in"] and g.exit_names == ["door_out", "next"], f"the names {g.entry_names} {g.exit_names}")
check(g.scenes[0][3][1] == 1 and g.scenes[1][3][0] == 0, "the sides: room1 right to room2, room2 left to room1")
check(g.scenes[0][5][3] == "fell", "the event of leaving room1 down")
# refused: an actor of a set with no animation, cells not of the size, two zones of one name, an object of no type
bad = copy.deepcopy(doc)
bad["scenes"][0]["layers"][0]["objects"][1]["set"] = "nobody"
cases = {"an actor of no animation": bad}
bad = copy.deepcopy(doc)
bad["scenes"][0]["grids"][0]["cells"] = [[0, 1], [1, 1]]
cases["cells not 3 x 2"] = bad
bad = copy.deepcopy(doc)
bad["scenes"][0]["layers"][1]["objects"][1]["name"] = "door"
del bad["scenes"][0]["links"][3]  # the pit's: the zone renamed, refused for its name, not for a link to no object
cases["two zones door"] = bad
bad = copy.deepcopy(doc)
bad["scenes"][0]["layers"][0]["objects"].append({"type": "lamp", "name": "x"})
cases["an object of no type"] = bad
bad = copy.deepcopy(doc)
bad["scenes"][0]["exits"][1]["scene"] = "nowhere"
cases["an exit to no scene"] = bad
# links refused: to an object, to a cell off its grid, from a picture, in a scene outside the maps; fires of a source of no links
bad = copy.deepcopy(doc)
bad["scenes"][0]["links"][0]["to"] = {"object": "door"}
cases["a link to an object"] = bad
bad = copy.deepcopy(doc)
bad["scenes"][0]["links"][0]["to"]["col"] = 3
cases["a link to a cell off its grid"] = bad
bad = copy.deepcopy(doc)
bad["scenes"][0]["links"][1]["from"] = {"object": "torch"}
cases["a link from a picture"] = bad
bad = copy.deepcopy(doc)
bad["scenes"].append({"name": "loose", "map": "", "layers": [{"name": "back", "objects": []}], "links": copy.deepcopy(doc["scenes"][1]["links"]),
                      "grids": copy.deepcopy(doc["scenes"][1]["grids"])})
cases["links outside a map"] = bad
bad = copy.deepcopy(doc)
bad["scenes"][1]["layers"][0]["objects"][0]["fires"] = {"scene": "room1", "grid": "grid", "col": 0, "row": 0}
cases["fires of a source of no links"] = bad
for music, fade in (("the theme", 300), (3, 300), ("theme", -1), ("theme", 70000), ("theme", "1s"), ("theme", True)):
    bad = copy.deepcopy(doc)
    bad["scenes"][0]["music"], bad["scenes"][0]["musicFade"] = music, fade
    cases[f"a music {music!r}, fade {fade!r}"] = bad
# a rotation: degrees clockwise, a multiple of 90; another, or not a number, refused
for rotation in (45, "90", 90.0, True):
    bad = copy.deepcopy(doc)
    bad["scenes"][0]["layers"][0]["objects"][1]["rotation"] = rotation
    cases[f"an actor's rotation {rotation!r}"] = bad
    bad = copy.deepcopy(doc)
    bad["scenes"][0]["layers"][0]["objects"][0]["rotation"] = rotation
    cases[f"a picture's rotation {rotation!r}"] = bad
for background in ("#14142", "14142a", "#14142g", 0x14142a):
    bad = copy.deepcopy(doc)
    bad["scenes"][0]["background"] = background
    cases[f"a background {background!r}"] = bad
for what, d in cases.items():
    try:
        packed(d)
        failed.append(f"{what} was not refused")
    except ValueError:
        pass
# the pack the engine's test opens (test_layout.c), from the good file
pack = packed(doc)
pack.write(os.path.join(out, "layout.pack"), os.path.join(out, "layout_pack.h"))
with open(os.path.join(out, "layout_pack.h")) as f:
    check("#define LAY_BLOB_CHASE" in f.read(), "the chases: the blob CHASE, made as the pack is written")
print("test_scenes.py: ok" if not failed else "test_scenes.py: " + "\n".join(failed))
sys.exit(1 if failed else 0)
