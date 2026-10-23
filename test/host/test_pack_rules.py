"""
The packer's refusals and the care it takes of what it writes: the prefix of a pack, names the header would give twice,
the names of its own blobs, the palettes of a set's frames, an empty last script, a graph's references, errors that
name the file, a .po with a BOM or in NFD, outputs written whole or not at all, the params of the pack checked as the
game compiles.
"""
import json
import os
import shutil
import struct
import subprocess
import sys
import unicodedata
import zlib

from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from dgx_ge_anim import AnimError, assemble  # noqa: E402
from dgx_ge_dialog import DialogError, read_po  # noqa: E402
from dgx_ge_graph import Graph  # noqa: E402
from dgx_ge_pack import Pack  # noqa: E402

here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, "build", "pack_rules")
shutil.rmtree(out, ignore_errors=True)
os.makedirs(out)
failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


def refused(what, call, words=()):
    """call raises a ValueError (an AnimError, a DialogError) whose message has all the words."""
    try:
        call()
    except (ValueError, AnimError, DialogError) as e:
        check(all(w in str(e) for w in words), f"{what}: {e}")
        return
    except Exception as e:  # noqa: BLE001
        check(False, f"{what}: {type(e).__name__} {e}, not a refusal")
        return
    check(False, f"{what}: taken")


SCRIPT = "script stand\n    hold\n"

# the prefix begins every name of the header, and the build finds <PREFIX>_ANIM_PARAMS by it: capitals, digits, _
for bad in ("mygame", "My_Game", "my-game", "1GAME", "", "MY GAME"):
    refused(f"Pack({bad!r})", lambda: Pack(bad), ("prefix",))
for good in ("MYGAME", "MY_GAME2", "T"):
    try:
        Pack(good)
    except ValueError as e:
        check(False, f"Pack({good!r}): {e}")

# names the header gives alike (stand-1 and stand_1), or not as C names when given straight: cleaned, and refused twice
p = Pack("T")
s = p.add_set("hero", [(255, 0, 0)])
p.add_image("stand-1", s, 1, 1, [1], 4)
header = p.header("T")
check("#define T_IMG_STAND_1 0" in header and "#define T_SET_HERO 0" in header, f"names cleaned in the header: {header[:200]}")
p.add_image("stand_1", s, 1, 1, [1], 4)
refused("stand-1 and stand_1", lambda: p.header("T"), ("T_IMG_STAND_1",))
p = Pack("T")
p.add_blob("count", b"x")  # T_BLOB_COUNT is the header's own
refused("a blob named COUNT", lambda: p.header("T"), ("T_BLOB_COUNT",))
p = Pack("T")
p.add_anim("hero-one", SCRIPT)
check("#define T_HERO_ONE_SCRIPT_STAND 0" in p.header("T"), "an animation's name cleaned in the header")

# the names of the blobs the packer adds itself as it writes the pack
for name in ("GAME", "STRINGS", "TIMERS", "STYLES", "CHASE", "SOUNDS", "strings"):
    refused(f"a blob named {name}", lambda: Pack("T").add_blob(name, b"x"), ("packer",))
refused("an animation named TIMERS", lambda: Pack("T").add_anim("TIMERS", SCRIPT), ("packer",))
refused("animations, one named GAME", lambda: Pack("T").add_anims([("HERO", SCRIPT), ("GAME", SCRIPT)]), ("packer",))
refused("a dialog named styles", lambda: Pack("T").add_dialog('dialog styles\n    say game "Hi."\n    end\n'), ("packer",))
# written twice, the packer's own blobs are made again, not kept from before
p = Pack("T")
p.add_game(LIVES=3)
p.write(os.path.join(out, "twice.pack"))
p.add_game(LIVES=5)
p.write(os.path.join(out, "twice.pack"))
check([n for n, _ in p.blobs].count("GAME") == 1 and dict(p.blobs)["GAME"][-4:] == (5).to_bytes(4, "little"), "the blob GAME of the second write")

# the frames of a set: values taken as they are, the colors of the set from the palettes of the frames that use them
sprites = os.path.join(out, "sprites")
os.makedirs(os.path.join(sprites, "hero"))


def png(name, palette, values):
    im = Image.new("P", (len(values), 1))
    im.putpalette([c for rgb in palette for c in rgb])  # as oxipng leaves it: no more colors than the picture uses
    im.putdata(values)
    im.save(os.path.join(sprites, "hero", name))


def sprites_doc(frames):
    with open(os.path.join(sprites, "t.json"), "w") as f:
        json.dump({"format": "dgx-ge-sprites", "formatVersion": 1, "name": "t", "sets": [
            {"name": "hero", "frames": [{"image": n, "width": w, "height": 1, "origin": {"x": 0, "y": 0}} for n, w in frames]}]}, f)
    return os.path.join(sprites, "t.json")


png("a.png", [(0, 0, 0), (255, 0, 0)], [0, 1])
png("b.png", [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)], [1, 2, 3])
p = Pack("T")
try:
    p.add_sprites(sprites_doc([("a.png", 2), ("b.png", 3)]))
    check(p.sets[0][1] == [(255, 0, 0), (0, 255, 0), (0, 0, 255)], f"the colors of a set whose first frame has a short palette: {p.sets[0][1]}")
except Exception as e:  # noqa: BLE001
    check(False, f"a first frame of a short palette: {e}")
png("c.png", [(0, 0, 0), (9, 9, 9)], [1])  # value 1 another color than in a.png
refused("frames whose palettes differ on a value they use", lambda: Pack("T").add_sprites(sprites_doc([("a.png", 2), ("c.png", 1)])),
        ("c.png", "value 1"))
check(len(Image.open(os.path.join(sprites, "hero", "a.png")).getpalette()) == 6, "a palette of two colors saved so")
# a value past the palette of its own picture: it has no color
p = Pack("T")
s = p.add_set("X", [(255, 0, 0), (0, 255, 0), (0, 0, 255)])


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


with open(os.path.join(out, "short.png"), "wb") as f:  # 8 bits a pixel, values 0 1 2, a palette of two (Pillow would not save it so)
    f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 3, 1, 8, 3, 0, 0, 0)) + chunk(b"PLTE", bytes([0, 0, 0, 0, 255, 0]))
            + chunk(b"IDAT", zlib.compress(bytes([0, 0, 1, 2]))) + chunk(b"IEND", b""))
refused("a value of no color in its palette (add_png)", lambda: p.add_png("SHORT", s, os.path.join(out, "short.png")), ("value 2",))

# without Image.getdata (Pillow 14 drops it): the sprites still read
saved = Image.Image.getdata
Image.Image.getdata = None
try:
    p = Pack("T")
    p.add_sprites(sprites_doc([("a.png", 2), ("b.png", 3)]))
    p.add_png("D", 0, os.path.join(sprites, "hero", "a.png"))
except Exception as e:  # noqa: BLE001
    check(False, f"the sprites without Image.getdata: {type(e).__name__} {e}")
finally:
    Image.Image.getdata = saved

# a script with no steps: in the middle it runs on into the next, the last has nothing, the engine does not open it
refused("an empty last script", lambda: assemble("script stand\n    hold\nscript idle\n"), ("idle",))
try:
    assemble("script idle\nscript stand\n    hold\n")
except AnimError as e:
    check(False, f"an empty script before another: {e}")

# a graph's references: a side to a scene that is not, by a side that is not, an exit to an entry its scene has not
g = Graph("W")
g.map("m")
a, b = g.scene("m", 1), g.scene("m", 2)
g.entry(b, "door", 10, 10)
g.start(a, "door")
refused("a start at an entry its scene has not", lambda: g.blob(lambda n: 0xFFFF, lambda e: 0), ("door",))
g = Graph("W")
g.map("m")
a, b = g.scene("m", 1), g.scene("m", 2)
g.entry(a, "start", 0, 0)
g.entry(b, "door", 10, 10)
g.start(a, "start")
g.exit(a, "out", b, "gate")
refused("an exit to an entry its scene has not", lambda: g.blob(lambda n: 0xFFFF, lambda e: 0), ("gate",))
g = Graph("W")
g.map("m")
a = g.scene("m", 1)
g.entry(a, "start", 0, 0)
g.start(a, "start")
g.side(a, "right", 5)
refused("a side to no scene", lambda: g.blob(lambda n: 0xFFFF, lambda e: 0), ("right",))
refused("a side that is not", lambda: g.side(a, "rite", a), ("rite",))
g.side(a, "right", a)
g.entry(7, "start", 0, 0)
refused("an entry of no scene", lambda: g.blob(lambda n: 0xFFFF, lambda e: 0), ("entry of no scene",))
refused("leaving by a side that is not", lambda: g.leave(a, "back", "gone"), ("back",))
refused("an exit by a transition that is not", lambda: g.exit(a, "out", a, "start", "dissolve"), ("dissolve",))

# errors of scripts and dialogs name the file (or the animation, the dialog) besides the line
refused("an error of an animation", lambda: Pack("T").add_anim("HERO", "script stand\n    jump\n"), ("HERO", "line 2"))
refused("an error of an animation from a file", lambda: Pack("T").add_anim("HERO", "script stand\n    jump\n", path="hero.anim"),
        ("hero.anim", "line 2"))
refused("an error of animations together", lambda: Pack("T").add_anims([("HERO", SCRIPT), ("WOLF", "script x\n    jump\n")]), ("WOLF", "line 2"))
refused("an error of a dialog", lambda: Pack("T").add_dialog('dialog hi\n    shout game "Hi."\n'), ("dialog hi", "line 2"))
refused("an error of a dialog from a file", lambda: Pack("T").add_dialog('dialog hi\n    shout game "Hi."\n', path="hi.dlg"),
        ("hi.dlg", "line 2"))
with open(os.path.join(sprites, "hero.anim"), "w") as f:
    f.write("script stand\n    jump\n")
refused("an error of the editor's animation", lambda: Pack("T").add_sprites(sprites_doc([("a.png", 2)])), (os.path.join(sprites, "hero.anim"), "line 2"))
os.remove(os.path.join(sprites, "hero.anim"))
os.makedirs(os.path.join(sprites, "dialogs"))
with open(os.path.join(sprites, "dialogs", "hi.dlg"), "w") as f:
    f.write('dialog hi\n    shout game "Hi."\n')
refused("an error of the editor's dialog", lambda: Pack("T").add_sprites(sprites_doc([("a.png", 2)])),
        (os.path.join(sprites, "dialogs", "hi.dlg"), "line 2"))
shutil.rmtree(os.path.join(sprites, "dialogs"))

# a .po with a BOM (as some editors save it), and one in NFD (as macOS writes names): read as any other, in NFC
po = 'msgid ""\nmsgstr "Plural-Forms: nplurals=3; plural=n%10==1 && n%100!=11 ? 0 : 1;\\n"\n\nmsgctxt "hi"\nmsgid "Hi."\nmsgstr "Й!"\n'
with open(os.path.join(out, "bom.po"), "w", encoding="utf-8-sig") as f:
    f.write(po)
got = read_po(os.path.join(out, "bom.po"))
check(("", "") in got and got.get(("hi", "Hi.")) == "Й!", f"a .po with a BOM: {got}")
with open(os.path.join(out, "nfd.po"), "w", encoding="utf-8") as f:
    f.write(unicodedata.normalize("NFD", po + '\nmsgctxt "hi"\nmsgid "Йо."\nmsgstr "Ёж."\n'))
got = read_po(os.path.join(out, "nfd.po"))
check(got.get(("hi", "Hi.")) == "Й!" and got.get(("hi", "Йо.")) == "Ёж.", f"a .po in NFD read in NFC: {got}")
p = Pack("T")
p.translate(os.path.join(out, "nfd.po"))
p.add_dialog(unicodedata.normalize("NFD", 'dialog hi\n    say game "Йо."\n    end\n'))
check(p.strings.texts == ["Ёж."], f"a dialog in NFD translated by its msgid in NFC: {p.strings.texts}")

# the outputs whole or not at all: a header that cannot be written leaves the pack written before as it was
p = Pack("T")
p.add_blob("A", b"first")
p.write(os.path.join(out, "keep.pack"), os.path.join(out, "keep.h"))
before = open(os.path.join(out, "keep.pack"), "rb").read()
p = Pack("T")
p.add_blob("A", b"second")
try:
    p.write(os.path.join(out, "keep.pack"), os.path.join(out, "no such folder", "keep.h"))
    check(False, "a header into no folder: written")
except OSError:
    pass
check(open(os.path.join(out, "keep.pack"), "rb").read() == before, "the pack as it was when its header could not be written")
check(not [n for n in os.listdir(out) if n.endswith(".tmp")], f"temporary files left: {os.listdir(out)}")
# a header the packer refuses (names alike) leaves both as they were
p = Pack("T")
p.add_blob("A", b"third")
p.add_blob("a", b"third")
try:
    p.write(os.path.join(out, "keep.pack"), os.path.join(out, "keep.h"))
except ValueError:
    pass
check(open(os.path.join(out, "keep.pack"), "rb").read() == before, "the pack as it was when its header was refused")

# the params of the pack as the game compiles: a build that keeps fewer than the pack needs stops there
p = Pack("PR")
p.add_anim("HERO", "".join(f"param p{i} 0\n" for i in range(6)) + SCRIPT)
p.write(os.path.join(out, "pr.pack"), os.path.join(out, "pr_pack.h"))
with open(os.path.join(out, "pr.c"), "w") as f:
    f.write('#include "pr_pack.h"\nint hero_params = PR_ANIM_PARAMS;\n')
cc = os.environ.get("CC", "gcc")


def compiles(*flags):
    return subprocess.run([cc, "-fsyntax-only", "-I", os.path.join(here, "..", "..", "include"), "-I", out, *flags, os.path.join(out, "pr.c")],
                          capture_output=True, text=True)


r = compiles()
check(r.returncode != 0 and "dgx_ge_params_of_pack" in r.stderr and "GAME_PACK_H" in r.stderr, f"a build without the params of the pack: {r.returncode} {r.stderr[-300:]}")
check(compiles("-DDGX_GE_ANIM_PARAMS=5").returncode != 0, "a build with fewer params than the pack needs")
for n in ("6", "8"):
    r = compiles(f"-DDGX_GE_ANIM_PARAMS={n}")
    check(r.returncode == 0, f"a build with {n} params: {r.stderr[-300:]}")
p = Pack("PS")
p.add_anim("HERO", "param a 0\n" + SCRIPT)  # 2, the engine's 4 enough
p.write(os.path.join(out, "ps.pack"), os.path.join(out, "ps_pack.h"))
with open(os.path.join(out, "ps.c"), "w") as f:
    f.write('#include "ps_pack.h"\nint hero_params = PS_ANIM_PARAMS;\n')
r = subprocess.run([cc, "-fsyntax-only", "-I", os.path.join(here, "..", "..", "include"), "-I", out, os.path.join(out, "ps.c")], capture_output=True, text=True)
check(r.returncode == 0, f"a pack of few params, built without them: {r.stderr[-300:]}")

# the game's constants in the header, <PREFIX>_<NAME>, for its C: the numbers the scripts are given, a negative one
# as C reads it; one name of the header for two things refused, naming the constant, the same number or not
p = Pack("K")
p.add_constants(TICK_MS=83, ODD=-5, LEAST=-2 ** 31, MOST=2 ** 31 - 1, WHOLE=7.0)
p.add_constants(TICK_MS=84)  # replaced, its place kept
p.add_anim("HERO", "script stand\n    frame 0 TICK_MS ms\n    hold\n")
p.write(os.path.join(out, "k.pack"), os.path.join(out, "k_pack.h"))
header = p.header("K")
check(["#define K_TICK_MS 84", "#define K_ODD (-5)", "#define K_LEAST (-2147483647 - 1)", "#define K_MOST 2147483647", "#define K_WHOLE 7"]
      == [line for line in header.splitlines() if line.startswith(("#define K_TICK", "#define K_ODD", "#define K_LEAST", "#define K_MOST", "#define K_WHOLE"))],
      f"the constants in the header: {header}")
with open(os.path.join(out, "k.c"), "w") as f:
    f.write('#include "k_pack.h"\n_Static_assert(K_TICK_MS == 84 && K_ODD * 2 == -10 && 0 - K_ODD == 5, "numbers");\n'
            '_Static_assert(K_LEAST == -2147483647 - 1 && K_LEAST < 0 && sizeof(K_LEAST) == sizeof(int) && K_MOST == 2147483647, "the ends");\n')
r = subprocess.run([cc, "-fsyntax-only", "-Wall", "-Werror", "-DDGX_GE_ANIM_PARAMS=4", "-I", os.path.join(here, "..", "..", "include"), "-I", out,
                    os.path.join(out, "k.c")], capture_output=True, text=True)
check(r.returncode == 0, f"the constants as C reads them: {r.stderr[-300:]}")
for bad in (2.5, "83", True, None, 2 ** 31, -2 ** 31 - 1):
    refused(f"a constant {bad!r}", lambda: Pack("K").add_constants(X=bad), ("constant X", "whole 32-bit"))
p = Pack("K")
p.add_constants(GAME_LIVES=0)
p.add_game(LIVES=3)  # K_GAME_LIVES 0, the place of LIVES: the same line, still two things
refused("a constant named as a place of the game", lambda: p.header("K"), ("constant GAME_LIVES", "K_GAME_LIVES", "#define K_GAME_LIVES 0", "another name"))
p = Pack("K")
p.add_anim("HERO", SCRIPT)
p.add_constants(EVENT_X=1, HERO_SCRIPT_STAND=1)  # an animation's name after the constants
refused("a constant named as a script", lambda: p.header("K"), ("constant HERO_SCRIPT_STAND", "#define K_HERO_SCRIPT_STAND 0"))
p = Pack("K")
p.add_constants(tick=1, TICK=1)
refused("two constants alike in upper case", lambda: p.header("K"), ("constant TICK", "K_TICK", "constant tick"))
p = Pack("K")
p.add_constants(SET_COUNT=0)
refused("a constant named as the count of sets", lambda: p.header("K"), ("constant SET_COUNT", "#define K_SET_COUNT 0"))

# a sound a document names (only its name) gets its samples from the game's script, at its number; a second time not
p = Pack("SN")
first = p.add_sound("STEP")
try:
    check(p.add_sound("STEP", samples=[0, 1000, -1000, 0], rate=8000) == first and p.sound_data[first] is not None, "a named sound given its samples")
except ValueError as e:
    check(False, f"a named sound given its samples: {e}")
refused("a sound given samples twice", lambda: p.add_sound("STEP", samples=[0, 1], rate=8000), ("STEP", "twice"))
q = Pack("SM")
q.add_sound("A")
refused("a sound named twice", lambda: q.add_sound("A"), ("A", "twice"))

# the header ends with a line's end, whatever its last name is
p = Pack("NL")
p.add_anim("HERO", SCRIPT)
p.screen = (320, 240)  # as a document's screen sets it: its lines come last, with no empty line after them
check(p.header("NL").endswith("\n") and not p.header("NL").endswith("\n\n"), f"the header's end: {p.header('NL')[-20:]!r}")

# a translation of a line of no numbers shows no {name} the line has not (it would be printed as it is written)
from dgx_ge_dialog import Strings  # noqa: E402
s = Strings()
s.translations[("d", "Hello")] = "Привет, {hero}"
refused("a translation showing a name its line has not", lambda: s.id("d", "Hello"), ("{hero}", "Hello"))
s.translations[("d", "Press {A}")] = "Нажми {A}"
check(s.texts[s.id("d", "Press {A}")] == "Нажми {A}", "a translation showing the line's own braces")

# the template of translations says its charset (msginit makes a .po of UTF-8 from it), and read_po takes it back
s = Strings()
s.id("d", "Hello")
pot = s.pot()
check(pot.startswith('msgid ""\nmsgstr ""\n"Content-Type: text/plain; charset=UTF-8\\n"'), f"the template's header: {pot[:80]!r}")
with open(os.path.join(out, "t.po"), "w", encoding="utf-8") as f:
    f.write(pot.replace('msgid "Hello"\nmsgstr ""', 'msgid "Hello"\nmsgstr "Привет"'))
po = read_po(os.path.join(out, "t.po"))
check(po.get(("d", "Hello")) == "Привет" and "charset=UTF-8" in po.get(("", ""), ""), f"a .po made from the template: {po}")

print("test_pack_rules.py: ok" if not failed else "test_pack_rules.py:\n  " + "\n  ".join(failed))
sys.exit(1 if failed else 0)
