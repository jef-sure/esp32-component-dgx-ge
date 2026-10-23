"""
A set of a ready palette (Pack.add_set(palette=), a document set's "palette"): the engine's palettes (nes, gameboy,
pico8) and the project's, every color of the pictures brought to the nearest as the eye sees it, only the colors taken
in the set and in the palette's order, the pictures renumbered as colors come, transparency kept, the warning of the
colors brought, the refusals.
"""
import io
import json
import os
import shutil
import struct
import sys
import zlib
from contextlib import redirect_stderr

from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import dgx_ge_pack  # noqa: E402
from dgx_ge_pack import Pack, engine_palettes, nearest_color, read_palette, unpack_pixels  # noqa: E402

here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, "build", "palettes")
shutil.rmtree(out, ignore_errors=True)
os.makedirs(out)
failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


def refused(what, call, words=()):
    """call raises a ValueError whose message has all the words."""
    try:
        call()
    except ValueError as e:
        check(all(w in str(e) for w in words), f"{what}: {e}")
        return
    except Exception as e:  # noqa: BLE001
        check(False, f"{what}: {type(e).__name__} {e}, not a refusal")
        return
    check(False, f"{what}: taken")


def png(path, colors, rows):
    """A PNG of a palette: colors are those of its values 0.. (0 transparent whatever its color), rows of values."""
    im = Image.new("P", (len(rows[0]), len(rows)))
    im.putpalette([c for rgb in colors for c in rgb])
    im.putdata([v for row in rows for v in row])
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im.save(path)
    return path


def pixels(pack, image):
    _, _, w, h, _, _, bits, data = pack.images[image]
    return bits, unpack_pixels(data, w, h, bits)


def warnings_of(pack, name):
    """What write prints on stderr, the pack written into out."""
    err = io.StringIO()
    with redirect_stderr(err):
        pack.write(os.path.join(out, f"{name}.pack"), os.path.join(out, f"{name}_pack.h"))
    return [line for line in err.getvalue().splitlines() if line]


def palette(name):
    with open(os.path.join(os.path.dirname(dgx_ge_pack.__file__), "palettes", f"{name}.txt"), encoding="utf-8") as f:
        return read_palette(f.read(), name)


# the engine's palettes: as their sources give them, every color once
check(engine_palettes() == ["gameboy", "nes", "pico8"], f"the engine's palettes: {engine_palettes()}")
NES, GB, PICO = palette("nes"), palette("gameboy"), palette("pico8")
check(len(NES) == 54 and len(set(NES)) == 54 and NES[0] == (0x57, 0x57, 0x57) and NES[13] == (0, 0, 0)
      and NES[-1] == (0xB3, 0xB3, 0xB3) and NES.count((255, 255, 255)) == 1, f"nes: {len(NES)} colors, {NES[:2]} .. {NES[-1]}")
check(GB == [(0x9B, 0xBC, 0x0F), (0x8B, 0xAC, 0x0F), (0x30, 0x62, 0x30), (0x0F, 0x38, 0x0F)], f"gameboy: {GB}")
check(len(PICO) == 16 and PICO[0] == (0, 0, 0) and PICO[7] == (0xFF, 0xF1, 0xE8) and PICO[8] == (0xFF, 0x00, 0x4D), f"pico8: {PICO}")

# the distance is the eye's (CIE76 in CIELAB, D65), not RGB's: red is L 53.2, a 80.1, b 67.2; a dark green (0, 64, 0) is
# nearer PICO-8's dark green than its black to the eye, the other way round by the sum of the squares of RGB
L, a, b = dgx_ge_pack._lab((255, 0, 0))
check(abs(L - 53.24) < 0.05 and abs(a - 80.09) < 0.1 and abs(b - 67.20) < 0.1, f"Lab of red: {L, a, b}")
check(nearest_color(PICO, (0, 64, 0))[0] == 3, f"(0, 64, 0) to the eye: {PICO[nearest_color(PICO, (0, 64, 0))[0]]}")
check(nearest_color(PICO, (255, 0, 77)) == (8, 0.0), "a color of the palette: itself, distance 0")
check(nearest_color([(10, 10, 10), (10, 10, 10)], (12, 12, 12))[0] == 0, "of equals the first")

# a script's set of pico8: each color of the PNG to the nearest of the palette, the set only the colors taken and in the
# palette's order (the PNG gives them in another), its value 0 transparent though its color is one of the palette's
p = Pack("T")
s = p.add_set("hero", palette="pico8")
check(p.sets[s][1] == [], "a set of a ready palette has no colors before its pictures")
art = png(os.path.join(out, "hero", "a.png"), [(255, 0, 77), (250, 240, 230), (255, 0, 77), (0, 64, 0)], [[0, 1, 2], [3, 2, 0]])
i = p.add_png("A", s, art, origin="top-left")
check(p.sets[s][1] == [PICO[3], PICO[7], PICO[8]], f"the colors taken, in the palette's order: {p.sets[s][1]}")
check(pixels(p, i) == (4, [0, 2, 3, 1, 3, 0]), f"the pixels to the set's values: {pixels(p, i)}")

# a second picture takes a color before them in the palette: the first is renumbered, the set keeps the palette's order
art = png(os.path.join(out, "hero", "b.png"), [(0, 0, 0), (30, 40, 80), (255, 0, 77)], [[1, 2]])
j = p.add_png("B", s, art, origin="top-left")
check(p.sets[s][1] == [PICO[1], PICO[3], PICO[7], PICO[8]], f"a color taken later: {p.sets[s][1]}")
check(pixels(p, i) == (4, [0, 3, 4, 2, 4, 0]) and pixels(p, j) == (4, [1, 4]), f"renumbered: {pixels(p, i)}, {pixels(p, j)}")
# a crop of a picture is of the set's values as they are, and is renumbered with the set
k = p.add_crop("A_HEAD", "A", 1, 0, 2, 1)
check(pixels(p, k) == (4, [3, 4]), f"a crop: {pixels(p, k)}")
# values given as they are: the numbers of the palette's colors, 1 its first (16 its last, 3 its dark purple)
m = p.add_image("C", s, 2, 1, [16, 3], 4)
check(p.sets[s][1] == [PICO[1], PICO[2], PICO[3], PICO[7], PICO[8], PICO[15]] and pixels(p, m) == (4, [6, 2])
      and pixels(p, i) == (4, [0, 4, 5, 3, 5, 0]) and pixels(p, k) == (4, [4, 5]), f"add_image by the palette's numbers: {p.sets[s][1]}")
# one warning for the set: the colors brought (each once), the farthest, by write
w = warnings_of(p, "hero")
far = nearest_color(PICO, (0, 64, 0))[1]
check(len(w) == 1 and "set hero: 3 colors of its pictures not in palette pico8" in w[0] and "(0, 64, 0)" in w[0]
      and os.path.join("hero", "a.png") in w[0] and str(PICO[3]) in w[0] and f"distance {far:.1f}" in w[0], f"the warning: {w}")
# drawn in the palette's own colors: no warning
p = Pack("T")
s = p.add_set("ok", palette="gameboy")
p.add_png("OK", s, png(os.path.join(out, "ok.png"), [(1, 2, 3), GB[3], GB[0]], [[1, 2, 0]]))
check(p.sets[s][1] == [GB[0], GB[3]] and warnings_of(p, "ok") == [], "the palette's own colors: no warning")

# more than 15 colors taken: a picture of 4 bits becomes one of 8 (its values pass 15)
p = Pack("T")
s = p.add_set("wide", palette="nes")
i = p.add_png("FEW", s, png(os.path.join(out, "few.png"), [(0, 0, 0)] + NES[40:43], [[1, 2, 3]]))
check(pixels(p, i) == (4, [1, 2, 3]), f"three colors: {pixels(p, i)}")
j = p.add_png("MANY", s, png(os.path.join(out, "many.png"), [(0, 0, 0)] + NES[:20], [list(range(1, 21))]))
check(len(p.sets[s][1]) == 23 and pixels(p, j) == (8, list(range(1, 21))) and pixels(p, i) == (8, [21, 22, 23]),
      f"23 colors: {pixels(p, i)}, {pixels(p, j)}")

# a palette given by its colors, and one of the project by name (a document's palettes come before the engine's)
p = Pack("T")
s = p.add_set("mine", palette=[(0, 0, 0), (255, 255, 255)])
p.add_png("MINE", s, png(os.path.join(out, "mine.png"), [(0, 0, 0), (240, 240, 240), (20, 20, 20)], [[1, 2, 1]]))
check(p.sets[s][1] == [(0, 0, 0), (255, 255, 255)], f"a palette of its colors: {p.sets[s][1]}")

# a document: a set of a palette of the project by name, frames of palettes of their own (their values need not match:
# the colors are what count), a set of the engine's gameboy; the document's palette is a set still, as before
os.makedirs(os.path.join(out, "doc", "ship"), exist_ok=True)
os.makedirs(os.path.join(out, "doc", "land"), exist_ok=True)
png(os.path.join(out, "doc", "ship", "one.png"), [(0, 0, 0), (200, 0, 0), (0, 0, 210)], [[1, 2], [0, 1]])
png(os.path.join(out, "doc", "ship", "two.png"), [(9, 9, 9), (0, 0, 255), (250, 250, 250)], [[2, 1]])
png(os.path.join(out, "doc", "land", "hill.png"), [(0, 0, 0), (20, 60, 20), (150, 190, 20)], [[1, 2, 0]])
doc = {"format": "dgx-ge-sprites", "formatVersion": 1, "name": "t",
       "palettes": [{"name": "nes", "colors": [[255, 255, 255], [255, 0, 0], [0, 0, 255]]}],
       "sets": [{"name": "ship", "palette": "nes", "frames": [
           {"image": "one.png", "width": 2, "height": 2, "origin": {"x": 0, "y": 1}},
           {"image": "two.png", "width": 2, "height": 1, "origin": {"x": 0, "y": 0}}]},
           {"name": "land", "palette": "gameboy", "frames": [{"image": "hill.png", "width": 3, "height": 1, "origin": {"x": 0, "y": 0}}]}]}
with open(os.path.join(out, "doc", "t.json"), "w") as f:
    json.dump(doc, f)
p = Pack("T")
made = p.add_sprites(os.path.join(out, "doc", "t.json"))
names = [im[0] for im in p.images]
check(made == {"ship": 0, "land": 1, "nes": 2}, f"the sets of the document: {made}")
check(p.sets[0][1] == [(255, 255, 255), (255, 0, 0), (0, 0, 255)], f"ship of the project's nes, all three taken: {p.sets[0][1]}")
check(pixels(p, names.index("SHIP_ONE")) == (4, [2, 3, 0, 2]) and pixels(p, names.index("SHIP_TWO")) == (4, [1, 3]),
      f"ship's frames: {pixels(p, 0)}, {pixels(p, 1)}")
check(p.sets[1][1] == [GB[0], GB[3]] and pixels(p, names.index("LAND_HILL")) == (4, [2, 1, 0]), f"land of gameboy: {p.sets[1][1]}")
check(p.sets[2] == ("NES", [(255, 255, 255), (255, 0, 0), (0, 0, 255)]), f"the document's palette, a set as before: {p.sets[2]}")
w = warnings_of(p, "doc")
check(len(w) == 2 and "set SHIP: 3 colors" in w[0] and "palette nes" in w[0] and "set LAND: 2 colors" in w[1]
      and "palette gameboy" in w[1], f"the warnings of the document: {w}")

# refusals: a name of no palette (it says those there are), both or neither of colors and a palette, a number past the
# palette, a palette of no colors or too many, a palette's text that is not colors
p = Pack("T")
p.palettes["guards"] = [(1, 2, 3)]
refused("a palette of no name", lambda: p.add_set("x", palette="c64"), ("no palette 'c64'", "guards", "gameboy", "nes", "pico8"))
refused("colors and a palette", lambda: p.add_set("x", [(1, 2, 3)], palette="nes"), ("one of them",))
refused("neither", lambda: p.add_set("x"), ("one of them",))
refused("an empty palette", lambda: p.add_set("x", palette=[]), ("0 colors",))
refused("a palette of 256", lambda: p.add_set("x", palette=[(i, 0, 0) for i in range(256)]), ("256 colors",))
s = p.add_set("gb", palette="gameboy")
refused("a value past the palette", lambda: p.add_image("X", s, 1, 1, [5], 4), ("color 5", "palette gameboy", "has 4"))
refused("a palette's text", lambda: read_palette("; a comment\n#00ff00\n#00ff0\n", "bad.txt"), ("bad.txt: line 3", "#00ff0"))
refused("a palette of no colors", lambda: read_palette("; nothing\n", "none.txt"), ("none.txt", "0 colors"))
check(read_palette("#00FF00 ; green\n\n  #0000ff\n", "t") == [(0, 255, 0), (0, 0, 255)], "a palette's text: comments, blanks, either case")
os.makedirs(os.path.join(out, "bad", "x"), exist_ok=True)


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


with open(os.path.join(out, "bad", "x", "f.png"), "wb") as f:  # values 1 2, a palette of two (Pillow would not save it so)
    f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 1, 8, 3, 0, 0, 0)) + chunk(b"PLTE", bytes([0, 0, 0, 1, 1, 1]))
            + chunk(b"IDAT", zlib.compress(bytes([0, 1, 2]))) + chunk(b"IEND", b""))
with open(os.path.join(out, "bad", "t.json"), "w") as f:
    json.dump({"format": "dgx-ge-sprites", "formatVersion": 1, "name": "t", "sets": [
        {"name": "x", "palette": "pico8", "frames": [{"image": "f.png", "width": 2, "height": 1, "origin": {"x": 0, "y": 0}}]}]}, f)
refused("a value of no color in a document set of a palette", lambda: Pack("T").add_sprites(os.path.join(out, "bad", "t.json")),
        ("f.png", "value 2 has no color"))
with open(os.path.join(out, "bad", "t.json"), "w") as f:
    json.dump({"format": "dgx-ge-sprites", "formatVersion": 1, "name": "t", "sets": [
        {"name": "x", "palette": "vga", "frames": [{"image": "f.png", "width": 2, "height": 1, "origin": {"x": 0, "y": 0}}]}]}, f)
refused("a document set of no palette", lambda: Pack("T").add_sprites(os.path.join(out, "bad", "t.json")), ("no palette 'vga'", "nes"))

print("test_palettes.py: ok" if not failed else "test_palettes.py:\n  " + "\n  ".join(failed))
sys.exit(1 if failed else 0)
