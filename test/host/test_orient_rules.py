"""
An actor's orientation in the tools: rotate of a script (a number kept as quarter turns, rounded as the engine rounds;
an expression worked out at the step; into a turn), rotation and its.rotation read, a dialog's condition reading it,
the explainer's words; the packer's rotation of a show's pictures and actors and of a crowd's groups.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from dgx_ge_anim import EXPR, ITS_BODY, OP, AnimError, Expr, assemble  # noqa: E402
from dgx_ge_dialog import Strings, assemble_dialog  # noqa: E402
from dgx_ge_explain import explain  # noqa: E402
from dgx_ge_pack import Pack  # noqa: E402

failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


HEAD = "param deg 90\nparam got 0\nscript s\n"


def code_of(lines, game_vars=None):
    """The code of a script of those lines and a hold: after the header (20), one script (2), two params (4)."""
    done = assemble(HEAD + "".join(f"    {line}\n" for line in lines) + "    hold\n", game_vars=game_vars)
    return done.blob[20 + 2 + 4:]


def rotate(arg):
    return bytes([OP["ROTATE"], arg])


hold = bytes([OP["HOLD"]])
check(OP["ROTATE"] == 49 and OP["ROTATE_X"] == 50 and EXPR["rotation"] == 55 and ITS_BODY["rotation"] == 0xF8, "the numbers of the steps")
# a number: quarter turns clockwise, by them or (to, bit 2) into them; to the nearest (a half away from zero), round the circle
for degrees, quarters in ((90, 1), (0, 0), (180, 2), (270, 3), (360, 0), (450, 1), (-90, 3), (45, 1), (44, 0), (-45, 3), (135, 2), (-135, 2),
                          (225, 3), (-270, 1)):
    check(code_of([f"rotate {degrees}"]) == rotate(quarters) + hold, f"rotate {degrees}: {code_of([f'rotate {degrees}']).hex()}")
    check(code_of([f"rotate to {degrees}"]) == rotate(4 | quarters) + hold, f"rotate to {degrees}")
check(code_of(["rotate 45 + 45", "rotate to 3 * 90"]) == rotate(1) + rotate(7) + hold, "constants worked out")
# an expression: worked out at the step, its code as Expr gives it
deg = Expr("deg", {}, {"deg": (0, 90)}, False, "t").code()
check(code_of(["rotate deg"]) == bytes([OP["ROTATE_X"], 0]) + deg + hold, "rotate by a param")
times = Expr("deg * 90", {}, {"deg": (0, 90)}, False, "t").code()
check(code_of(["rotate to (deg * 90)"]) == bytes([OP["ROTATE_X"], 4]) + times + hold, "rotate to an expression")
check(code_of(["rotate game_var.facing"], game_vars=["facing"]) == bytes([OP["ROTATE_X"], 0, 2, EXPR["game_var"], 0]) + hold, "rotate by a variable")
# rotation read: the turn of the actor, degrees; a param of that name is the param; not in a path, not a constant
check(code_of(["set got rotation"]) == bytes([OP["SET_X"], 1, 1, EXPR["rotation"]]) + hold, "set got rotation")
mine = assemble("param rotation 5\nparam got 0\nscript s\n    set got rotation\n    hold\n").blob[26:]
check(mine == bytes([OP["SET_X"], 1, 2, EXPR["param"], 0, OP["HOLD"]]), f"a param named rotation is the param: {mine.hex()}")
for text in ("    rotate\n", "    path x: rotation * t, y: 0\n", "    rotate to\n"):
    try:
        assemble(HEAD + text + "    hold\n")
        failed.append(f"refused: {text.strip()}")
    except AnimError:
        pass
try:
    assemble("define d rotation\nscript s\n    hold\n")
    failed.append("rotation is no constant")
except AnimError:
    pass
# of the one sensed: its.rotation
seen = assemble("param got 0\nscript s\n    sense any within 50 where its.rotation == 90 into got\n    hold\n").blob
check(bytes([EXPR["its"], 0xF8]) in seen, "its.rotation in a sense")
# a dialog's condition of the one choosing: its rotation, read as a script reads it
d = assemble_dialog('dialog d\nroles kid KID\nlabel a\n    choose kid\n        "up" if rotation == 270 -> a\n', Strings(), {"KID": 3},
                    {"KID": {"hp": (0, 3)}})
check(bytes([EXPR["rotation"], EXPR["num"]]) in d.blob, "a dialog's condition reads rotation")
# the explainer: by, back, into, by an expression
for lang, words in (("en", ("turns 90° clockwise", "turns 90° anticlockwise", "turns to 180°", "turns deg° clockwise")),
                    ("ru", ("поворот на 90° по часовой", "поворот на 90° против часовой", "поворот в положение 180°", "поворот на deg° по часовой"))):
    told = explain(HEAD + "    rotate 90\n    rotate -90\n    rotate to 180\n    rotate deg\n    frame 0 100ms\n    hold\n", lang)
    for w in words:
        check(w in told, f"{lang}: {w} not in {told}")
# the packer: a show's pictures and actors turned, a crowd's groups (any: by the dice); a rotation not of quarter turns refused
pack = Pack("T")
s = pack.add_set("S", [(1, 2, 3)])
pack.add_image("P", s, 1, 1, [1], 4)
pack.add_anim("K", "script s\n    hold\n")
pack.add_show("SHOW", pictures=[{"picture": "P", "rotation": 90}, {"picture": "P", "flip": True}],
              actors=[{"name": "a", "kind": "K", "flip": True, "rotation": 270}, {"name": "b", "kind": "K", "rotation": -90}], cues=[(0, "end", None)])
b = pack.blobs[-1][1]
check(struct.unpack_from("<IH", b, 0) == (0x48534744, 2), "a show of version 2")
check(b[12 + 7] == 0 and b[12 + 9] == 1 and b[22 + 7] == 1 and b[22 + 9] == 0, "the pictures: flags, turn")
check(b[32 + 8] == 1 | 3 << 1 and b[42 + 8] == 3 << 1, "the actors: facing left, turned")
pack.add_crowd("CROWD", 4, (0, 0, 10, 10), [{"kind": "K"}], groups=[{"kind": "K", "rotation": 180}, {"kind": "K", "rotation": "any", "flip": "either"}])
b = pack.blobs[-1][1]
check(struct.unpack_from("<IH", b, 0) == (0x52434744, 2) and b[24 + 8 + 12:24 + 8 + 14] == bytes([0, 2]) and b[24 + 8 + 16 + 12:24 + 8 + 16 + 14] == bytes([2, 4]),
      "a crowd of version 2: its groups' flip and turn")
for bad in (45, "90", 1.5, True):
    try:
        pack.add_show("BAD", pictures=[{"picture": "P", "rotation": bad}], cues=[(0, "end", None)])
        failed.append(f"a show's rotation {bad!r} taken")
    except ValueError:
        pass
    try:
        pack.add_crowd("BAD", 4, (0, 0, 10, 10), [{"kind": "K"}], groups=[{"kind": "K", "rotation": bad}])
        failed.append(f"a crowd's rotation {bad!r} taken")
    except ValueError:
        pass
print("test_orient_rules.py: ok" if not failed else "test_orient_rules.py: " + "\n".join(failed))
sys.exit(1 if failed else 0)
