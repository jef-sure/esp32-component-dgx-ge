"""The packer reads the sprite editor: pictures by <SET>_<FILE>, origins, drawn masks, default masks, mask names, an animation next to it."""
import json
import os
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from dgx_ge_pack import Pack  # noqa: E402

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "sprites")
os.makedirs(os.path.join(out, "hero"), exist_ok=True)


def png(name, rows):
    im = Image.new("P", (len(rows[0]), len(rows)))
    im.putpalette([0, 0, 0, 255, 0, 0, 0, 255, 0] + [0] * 759)
    im.putdata([int(c) for row in rows for c in row])
    im.save(os.path.join(out, "hero", name))


png("stand-1.png", ["0110", "0110", "2222"])          # 4 x 3, the origin at its bottom middle
png("hit-1.png", ["011000", "011111", "222000"])      # a blade out to the right
doc = {
    "format": "dgx-ge-sprites", "formatVersion": 1, "name": "t",
    "masks": [{"index": 1, "name": "body", "color": "#ff0000"}, {"index": 2, "name": "the blade", "color": "#0000ff"}],
    "meets": "foreign the_blade body\nown body body  # bump\n",
    "sets": [{
        "name": "hero", "defaultMask": 1, "canvas": {"left": -2, "top": -2, "width": 7, "height": 3},
        "frames": [
            {"image": "hit-1.png", "width": 6, "height": 3, "origin": {"x": 1, "y": 2}, "points": {"hand": {"x": 4, "y": -1}, "back": {"x": -1, "y": -1, "z": -2}},
             "masks": {"1": {"x": -1, "y": -2, "width": 3, "height": 3, "rows": ["6", "6", "e"]},
                       "2": {"x": 2, "y": -1, "width": 3, "height": 1, "rows": ["e"]}}},
            {"image": "stand-1.png", "width": 4, "height": 3, "origin": {"x": 1, "y": 2}, "masks": {}},
        ]}],
    "tilesets": [{"name": "stone", "cellWidth": 16, "cellHeight": 16, "tiles": [
        {"name": "wall", "kind": 3, "solid": {"top": True, "left": True, "right": True, "bottom": True}, "closes": {"left": True},
         "bodies": [{"dx": 0, "dy": -1, "w": 16, "h": 15}],
         "pieces": [{"set": "hero", "image": "stand-1.png", "dx": 0, "dy": 0, "layer": "back", "key": 0}]},
        {"name": "ledge", "kind": 4, "solid": {"top": True},
         "pieces": [{"set": "hero", "image": "hit-1.png", "dx": 2, "dy": -3, "layer": "front", "key": 1},
                    {"set": "hero", "image": "stand-1.png", "dx": 0, "dy": 0, "layer": "back", "key": 0}]},
        {"name": "gate", "kind": 5, "pieces": [
            {"by_mod": [{"set": "hero", "image": "stand-1.png"}, {"set": "hero", "image": "hit-1.png"}], "dy": "-mod / 4",
             "layer": "back", "around": {"up": "not wall", "down": ["wall", "ledge"]}, "variant": 2},
            {"cycle": [{"set": "hero", "image": "stand-1.png", "ms": 100}, {"set": "hero", "image": "hit-1.png", "ms": 50}], "layer": "front"}]}]}],
    "styles": [{"name": "talk", "frame": "picture", "picture": {"set": "hero", "image": "stand-1.png"}, "insets": [1, 1, 1, 1],
                "place": [4, -4, -8, 30], "font": "Mono12", "scale": [2, 1], "waitMs": 500, "colors": {"text": [1, 2, 3], "bar": [9, 9, 9]}}],
}
with open(os.path.join(out, "t.json"), "w") as f:
    json.dump(doc, f)
with open(os.path.join(out, "hero.anim"), "w") as f:
    f.write("event go\nscript stand\n    frame HERO_STAND_1 100ms\n    hold\n")

os.makedirs(os.path.join(out, "dialogs"), exist_ok=True)
os.makedirs(os.path.join(out, "po"), exist_ok=True)
with open(os.path.join(out, "dialogs", "hi.dlg"), "w") as f:
    f.write('dialog hi\nroles hero HERO\nstyle talk\n    say hero "Hello."\n    end\n')
with open(os.path.join(out, "po", "ru.po"), "w") as f:
    f.write('msgctxt "hi"\nmsgid "Hello."\nmsgstr "Привет."\n')
pack = Pack("T")
pack.language = "ru"
sets = pack.add_sprites(os.path.join(out, "t.json"))
names = [im[0] for im in pack.images]
failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


check(sets == {"hero": 0} and pack.sets[0][1] == [(255, 0, 0), (0, 255, 0)], "the set and its colors")
check(names == ["HERO_HIT_1", "HERO_STAND_1"] + [f"TALK_FRAME_{i}" for i in range(9)], f"picture names {names}")
# the frame of the style talk: stand-1 cut by its insets (1 each) into nine pictures that make it again, row after row
from dgx_ge_pack import unpack_pixels  # noqa: E402
_, _, fw, fh, _, _, fbits, fdata = pack.images[1]
whole = unpack_pixels(fdata, fw, fh, fbits)
again, ok = [[None] * fw for _ in range(fh)], True
for i in range(9):
    _, _, w, h, ox, oy, bits, data = pack.images[2 + i]
    x0 = (0, 1, fw - 1)[i % 3]
    y0 = (0, 1, fh - 1)[i // 3]
    ok = ok and (w, h) == ((1, fw - 2, 1)[i % 3], (1, fh - 2, 1)[i // 3]) and (ox, oy) == (0, 0)
    for k, v in enumerate(unpack_pixels(data, w, h, bits)):
        again[y0 + k // w][x0 + k % w] = v
check(ok and [v for row in again for v in row] == whole, "the nine pictures of the frame make the picture again")
for insets in ((0, 1, 1, 1), (1, 1, fw - 1, 1)):
    try:
        pack.add_style("bad", frame="picture", picture="HERO_STAND_1", insets=insets)
        check(False, f"insets {insets} were taken")
    except ValueError:
        pass
check(pack.images[0][4:6] == (1, 2), "the origin of hit-1")
masks = sorted(pack.masks)
check([(m[0], m[1], m[2], m[3], m[4], m[5]) for m in masks] == [(0, 1, -1, -2, 3, 3), (0, 2, 2, -1, 3, 1), (1, 1, -1, -2, 4, 3)],
      f"masks {[m[:6] for m in masks]}")
check(sum(bin(b).count("1") for b in masks[2][6]) == 8, "the default mask of stand-1: its 8 points")
check(pack.mask_names == {1: "BODY", 2: "THE_BLADE"}, f"mask names {pack.mask_names}")
check([a[0] for a in pack.anims] == ["HERO"], "the animation next to the JSON")
check([t[0] for t in pack.tilesets] == ["STONE"] and pack.tilesets[0][1] == ["wall", "ledge", "gate"] and pack.tilesets[0][2] == ["back", "front"],
      f"tilesets {pack.tilesets}")
tiles = dict(pack.blobs)["STONE"]
check(struct.unpack_from("<IHHHHHHHHHHH", tiles, 0)[2:7] == (16, 16, 3, 2, 5), "the header of the tiles")
gate = struct.unpack_from("<HBBhhHHBBhHBBBBBB", tiles, 28 + 3 * (12 + 36) + 3 * 32)
check(gate[1:3] == (2, 1) and gate[5] == 0xFFFF and gate[6] != 0xFFFF and gate[8] == 2 and gate[12 - 1] == 2, f"the gate: by_mod of 2, dy an expression, variant 2, two conditions {gate}")
check(struct.unpack_from("<HBBHBBH", tiles, 28)[1::3] == (15, 2), "the wall: solid all round, closes only its left, as the editor says")
check(struct.unpack_from("<HBBHBBH", tiles, 28)[5:] == (1, 0) and struct.unpack_from("<IHHHHHHHHHHHH", tiles, 0)[12] == 1
      and struct.unpack_from("<hhhhHHHHH", tiles, len(tiles) - 20) == (0, -1, 16, 15, 65535, 65535, 65535, 65535, 65535),
      "the wall: its body as the editor gives it (dx 0, dy -1, 16 x 15, no expressions), the last of the blob")
check(struct.unpack_from("<HBBHBB", tiles, 28 + 12) == (4, 1, 2, 1, 1, 0), "the ledge: kind 4, solid top, two pieces from the second, closes as solid")
check(struct.unpack_from("<H", tiles, 28 + 10)[0] == 0xFFFF, "the wall: no life")
# a tile seen by its neighbours as another (seen_as): the editor's field reaches add_tileset
seen = dict(doc, tilesets=[{"name": "seen", "cellWidth": 16, "cellHeight": 16, "tiles": [
    {"name": "floor", "pieces": [{"set": "hero", "image": "stand-1.png", "layer": "back"}]},
    {"name": "floor_down", "seen_as": "floor", "pieces": [{"set": "hero", "image": "stand-1.png", "layer": "back"}]}]}])
with open(os.path.join(out, "seen.json"), "w") as f:
    json.dump(seen, f)
sp, given = Pack("S"), []
real = sp.add_tileset
sp.add_tileset = lambda name, w, h, tiles, *rest, **more: given.append(tiles) or real(name, w, h, tiles, *rest, **more)
sp.add_sprites(os.path.join(out, "seen.json"))
check(given and [t.get("seen_as") for t in given[0]] == [None, "floor"], f"seen_as from the editor {given and [t.get('seen_as') for t in given[0]]}")
# a frame named in the pack by the editor (packName): the picture, and the pieces that name its file, take that name
import copy
named = copy.deepcopy(seen)
named["sets"][0]["frames"][1]["packName"] = "DAGGER"
named_dir = os.path.join(out, "named")
os.makedirs(named_dir, exist_ok=True)
with open(os.path.join(named_dir, "named.json"), "w") as f:
    json.dump(named, f)
with open(os.path.join(named_dir, "hero.anim"), "w") as f:
    f.write("script stand\n    frame DAGGER 100ms\n    hold\n")  # the editor writes the name of the pack
np_ = Pack("N")
np_.add_sprites(os.path.join(named_dir, "named.json"), images=out)
names = [im[0] for im in np_.images]
check(names[:2] == ["HERO_HIT_1", "DAGGER"] and "TALK_FRAME_0" in names, f"pack names {names}")  # the style's frame cut out of DAGGER too
check(dict(np_.blobs).get("SEEN") is not None, "the tiles naming the file of DAGGER found it")
# palettes: sets of colors of no pictures; a piece's color by one of them; the header names it
pal = copy.deepcopy(seen)
pal["palettes"] = [{"name": "ega", "colors": [[0, 0, 168], [0, 168, 0]]}]
pal["tilesets"][0]["tiles"][0]["pieces"][0]["color"] = ["ega", 2]
pal_dir = os.path.join(out, "pal")
os.makedirs(pal_dir, exist_ok=True)
with open(os.path.join(pal_dir, "pal.json"), "w") as f:
    json.dump(pal, f)
pp = Pack("P")
pp.add_sprites(os.path.join(pal_dir, "pal.json"), images=out)
check([st[0] for st in pp.sets] == ["HERO", "EGA"] and pp.sets[1][1] == [(0, 0, 168), (0, 168, 0)], f"palettes as sets {pp.sets}")
check("#define P_SET_EGA 1" in pp.header("P"), "the header names the palette")
bad = copy.deepcopy(pal)
bad["palettes"][0]["name"] = "Hero"  # HERO in the header, as the set
with open(os.path.join(pal_dir, "bad.json"), "w") as f:
    json.dump(bad, f)
try:
    Pack("B").add_sprites(os.path.join(pal_dir, "bad.json"), images=out)
    check(False, "a palette named as a set was taken")
except ValueError:
    pass
twice = Pack("T")
twice.add_set("KID", [(1, 2, 3)])
try:
    twice.add_set("Kid", [(1, 2, 3)])
    check(False, "two sets of one name in the header were taken")
except ValueError:
    pass
# a life: an animation added before the tiles, by name; none such: refused
lp = Pack("L")
lp.add_set("A", [(1, 2, 3)])
lp.add_image("P", 0, 1, 1, [1], 1, (0, 0))
lp.add_anim("DOOR", "param mod 0\nscript rest\n    hold\n")
lp.add_tileset("T", 8, 8, [{"name": "door", "life": "DOOR", "pieces": [{"image": 0}]}], ["back"])
check(struct.unpack_from("<H", dict(lp.blobs)["T"], 28 + 10)[0] == [n for n, _ in lp.blobs].index("DOOR"), "the door's life: the blob of its animation")
try:
    lp.add_tileset("U", 8, 8, [{"name": "door", "life": "NOPE", "pieces": [{"image": 0}]}], ["back"])
    check(False, "a life of no animation was taken")
except ValueError:
    pass
laid = struct.unpack_from("<9I", tiles, 28 + 3 * 12 + 2 * 36)
check(laid == (1, 0, 0, 0, 0, 0, 0, 0, 0), f"the gate: its pieces at step 0 of its own cell {laid}")
piece = struct.unpack_from("<HBBhhHHBBhHBBBBBB", tiles, 28 + 3 * (12 + 36) + 32)
check(piece == (1, 1, 0, 2, -3, 0xFFFF, 0xFFFF, 1, 0xFF, 1, 0, 0, 0, 0, 0, 0xFF, 0), f"its first piece: its picture, 2, -3, the slot front, key 1 {piece}")
distinct = struct.unpack_from("<IHHHHHHHHHHH", tiles, 0)[11]
once = struct.unpack_from("<HH", tiles, 28 + 3 * (12 + 36) + 5 * 32)
check(distinct == 2 and once == (1, 0), f"the pictures each once, as they come: stand-1, hit-1 {once}")
at = struct.unpack_from("<HH", tiles, 28 + 3 * (12 + 36) + 5 * 32 + 2 * distinct + 4)[0]
check(once[at] == 0, "the picture of that piece: hit-1, the first picture of the pack")
# a by_mod whose end repeats is kept as far as its last change: the last stands for any modifier above
short = Pack("S")
short.add_set("A", [(1, 2, 3)])
short.add_image("P", 0, 1, 1, [1], 1, (0, 0))
short.add_tileset("T", 8, 8, [{"name": "t", "pieces": [{"by_mod": [None, 0, 0, 0, None, None, None]}]}], ["back"])
t = dict(short.blobs)["T"]
for bad in ("or", "mod", "min", "t", "2x"):
    try:
        short.add_tileset("B", 8, 8, [{"name": "t", "pieces": [{"image": 0, "dx": bad if bad != "2x" else 0}]}], ["back"], {bad: ("variant", 0, 1)})
        check(False, f"a field named {bad} was taken")
    except ValueError:
        pass
# a blob name once: a dialog named as an animation is refused, not a second number of the same name
try:
    short.add_blob("T", b"again")
    check(False, "a blob named twice was taken")
except ValueError:
    pass
check(struct.unpack_from("<IHHHHHHHHHHH", t, 0)[8] == 5 and struct.unpack_from("<HBB", t, 28 + 12 + 36)[1] == 5, "a by_mod trimmed to 5 of 7")
# the fields of a piece from the editor: laid with the cell on the right at step 2, a silhouette in color 2 of hero;
# a card by the modifier, none in state 1
edge = dict(doc, tilesets=[{"name": "edge", "cellWidth": 16, "cellHeight": 16, "tiles": [{"name": "edge", "pieces": [
    {"set": "hero", "image": "stand-1.png", "layer": "back", "with": "right", "step": 2, "draw": "silhouette", "color": ["hero", 2]},
    {"by_mod": [{"set": "hero", "image": "hit-1.png"}, None], "layer": "back", "draw": "card"},
    {"pick": "mark", "pictures": [None, {"set": "hero", "image": "hit-1.png"}], "dx": "mark * 2", "layer": "back"},
    {"draw": "fill", "size": [4, 2], "layer": "back", "color": ["hero", "mark + 1"]}]}],
    "fields": {"mark": ["variant", 3, 2]}}])
with open(os.path.join(out, "u.json"), "w") as f:
    json.dump(edge, f)
other = Pack("U")
other.add_sprites(os.path.join(out, "u.json"))
u = dict(other.blobs)["EDGE"]
first = struct.unpack_from("<HBBhhHHBBhHBBBBBB", u, 28 + 12 + 36)
second = struct.unpack_from("<HBBhhHHBBhHBBBBBB", u, 28 + 12 + 36 + 32)
check(first[12:] == (4, 2, 2, 0, 2), f"with right, step 2, a silhouette in color 2 of hero {first}")
check(struct.unpack_from("<9I", u, 28 + 12) == (1, 0, 0, 0, 4, 0, 0, 0, 0), "laid: step 0 of its own cell, step 2 with the cell on the right")
check(second[1:3] == (2, 1) and second[14] == 1, f"a card by the modifier of two {second}")
u_pieces, u_distinct = struct.unpack_from("<IHHHHHHHHHHH", u, 0)[6], struct.unpack_from("<IHHHHHHHHHHH", u, 0)[11]
picture = struct.unpack_from("<H", u, 28 + 12 + 36 + u_pieces * 32 + 2 * u_distinct + 4 * (second[0] + 1))[0]
check(picture == 0xFFFF, "none in state 1")
third = struct.unpack_from("<HBBhhHHBBhHBBBBBBHHBBH", u, 28 + 12 + 36 + 2 * 32)
fourth = struct.unpack_from("<HBBhhHHBBhHBBBBBBHHBBH", u, 28 + 12 + 36 + 3 * 32)
check(third[1:3] == (2, 1) and third[17] != 0xFFFF and third[5] != 0xFFFF, f"picked by a field, moved by it {third}")
check(fourth[14] == 3 and fourth[19:21] == (4, 2) and fourth[15] == 0 and fourth[18] != 0xFFFF, f"a fill of 4 x 2, its color an expression {fourth}")
check(pack.points == {(0, 0): (4, -1, 1), (0, 1): (-1, -1, -2)}, f"the points of hit-1, z 1 unless given {pack.points}")
header = pack.header("T")
check("#define T_POINT_HAND 0" in header and "#define T_POINT_BACK 1" in header, "names of points")
check("#define T_STONE_TILE_LEDGE 2" in header and "#define T_STONE_SLOT_FRONT 1" in header, "names of tiles and slots")
blob = pack.build()
check(struct.unpack_from("<H", blob, 18)[0] == 3, "three masks in the pack")
check(struct.unpack_from("<I", blob, 44)[0] and struct.unpack_from("<32H", blob, struct.unpack_from("<I", blob, 44)[0])[:3] == (0, 2, 0)
      and struct.unpack_from("<32H", blob, struct.unpack_from("<I", blob, 44)[0])[16:19] == (0, 4, 2), "which masks meet, both ways round")
for wrong, what in (("foreign BODY HAND", "no mask"), ("own body body\nown BODY 1", "given twice"), ("both body body", "'own|foreign")):
    try:
        pack.add_meets(wrong)
        check(False, f"meets '{wrong}' taken")
    except ValueError as e:
        check(what in str(e), f"meets '{wrong}': {e}")
# styles and dialogs of the editor: a style as given, a dialog of dialogs/, its line in the language of po/ru.po
st = pack.style_data.get("talk", {})
check(pack.styles == ["talk"] and st.get("place") == [4, -4, -8, 30] and st.get("font") == "Mono12" and st.get("wait_ms") == 500
      and st.get("text") == (1, 2, 3) and st.get("picture") == "HERO_STAND_1" and st.get("frame") == "picture" and pack.fonts == ["Mono12"],
      f"the style talk as the editor gives it: {st}")
check(any(n == "HI" for n, _ in pack.blobs) and pack.strings.texts == ["Привет."], f"the dialog hi in Russian: {pack.strings.texts}")
# the animations of the sets made together: the first senses the second and the second the first, whatever their order
import shutil
herd_dir = os.path.join(out, "herd")
herd = {"format": "dgx-ge-sprites", "formatVersion": 1, "name": "herd", "masks": doc["masks"], "sets": []}
for kind in ("wolf", "sheep"):
    os.makedirs(os.path.join(herd_dir, kind), exist_ok=True)
    shutil.copy(os.path.join(out, "hero", "stand-1.png"), os.path.join(herd_dir, kind, "stand-1.png"))
    herd["sets"].append({"name": kind, "defaultMask": 1, "frames": [{"image": "stand-1.png", "width": 4, "height": 3, "origin": {"x": 1, "y": 2}}]})
with open(os.path.join(herd_dir, "herd.json"), "w") as f:
    json.dump(herd, f)
with open(os.path.join(herd_dir, "wolf.anim"), "w") as f:
    f.write("param d -1\nparam food 0\nscript hunt\n    sense SHEEP within 99 into d\n    frame WOLF_STAND_1 100ms\n    goto hunt\n")
with open(os.path.join(herd_dir, "sheep.anim"), "w") as f:
    f.write("param fear 0\nparam dx 0\nscript graze\n    sense WOLF within 50 into fear, dx\n    frame SHEEP_STAND_1 100ms\n    goto graze\n")
hp = Pack("H")
try:
    hp.add_sprites(os.path.join(herd_dir, "herd.json"))
    check([a[0] for a in hp.anims] == ["WOLF", "SHEEP"] and [n for n, _ in hp.blobs][:2] == ["WOLF", "SHEEP"], f"the herd's animations {hp.anims}")
except Exception as e:
    check(False, f"the wolf and the sheep sense each other: {e}")
print("test_sprites.py: ok" if not failed else "test_sprites.py: " + "; ".join(failed))
sys.exit(1 if failed else 0)
