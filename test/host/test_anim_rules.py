"""The assembler refuses a matrix two rules fill otherwise, "every script", a script twice in a group, params not given."""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from dgx_ge_anim import AnimError, assemble  # noqa: E402

BASE = """
event hit
script a
    frame 1 10ms
    hold
script b
    frame 1 10ms
    hold
"""
cases = {
    "conflict": BASE + "group g: a, b\non hit: g -> a now\non hit: b -> b now\n",
    "every": BASE + "on hit: * -> a now\n",
    "smoothstep takes one": BASE + "script c\n    dx smoothstep(1, 2)\n    hold\n",
    "twice": BASE + "group g: a, a\non hit: g -> b now\n",
    "no param": BASE + "script c\n    set hp 1\n    hold\n",
    "param twice": "param hp 1\nparam hp 2\n" + BASE,
    "param too big": "param hp 40000\n" + BASE,
    "t outside a path": BASE + "script c\n    velocity t, 0\n    hold\n",
    "unknown name": BASE + "script c\n    dx speed\n    hold\n",
    "define twice": "define g 1\ndefine g 2\n" + BASE,
    "param in a constant place": "param p 1\n" + BASE + "script c\n    dx p\n    hold\n",
    "path without y": BASE + "script c\n    path x: t\n    hold\n",
    "velocity in a path": BASE + "script c\n    path x: vx * t, y: 0\n    hold\n",
    "chained comparison": BASE + "script c\n    dx 1 < 2 < 3\n    hold\n",
    "hit of no event": BASE + "script c\n    hit 1 on 2 event nothing\n    hold\n",
    "hit on nothing": BASE + "script c\n    hit 1 damage 2\n    hold\n",
    "hit of a mask 16": BASE + "script c\n    hit 16 on 1\n    hold\n",
    "hit of what": BASE + "script c\n    hit 1 on 2 damage\n    hold\n",
    "carry at no point": BASE + "script c\n    carry knee 1\n    hold\n",
    "carry of what": BASE + "script c\n    carry hand\n    hold\n",
    "define named and": "define and 1\n" + BASE,
    "param named not": "param not 1\n" + BASE,
    "define of a digit first": "define 2x 1\n" + BASE,
    "tell cell of no event": BASE + "script c\n    tell cell 3, nothing\n    hold\n",
    "tell cell of no cell": BASE + "script c\n    tell cell hit\n    hold\n",
    "press of nothing": BASE + "script c\n    press\n    hold\n",
    "clear of a word": BASE + "script c\n    clear room\n    hold\n",
    "appear of no kind": BASE + "script c\n    appear NOBODY, hit, 1, 2\n    hold\n",
    "appear without y": BASE + "script c\n    appear 3, hit, 1\n    hold\n",
    "appear facing up": BASE + "script c\n    appear 3, hit, 1, 2, up\n    hold\n",
    "vanish where": BASE + "script c\n    vanish 3\n    hold\n",
    "first without off": BASE + "script c\n    contact 1 below on hit first within 8\n    hold\n",
    "x of a path": BASE + "script c\n    path x: x + t, y: 0\n    hold\n",
    "x as a param": "param x 1\n" + BASE,
    "timer in a path": BASE + "script c\n    path x: timer(clock) * t, y: 0\n    hold\n",
    "timer of a number": "param p 0\n" + BASE + "script c\n    set p timer(3)\n    hold\n",
    "timer begun": BASE + "script c\n    timer begin bomb\n    hold\n",
    "timer of no name": BASE + "script c\n    timer start\n    hold\n",
    "param not ascii": "param сила 1\n" + BASE,
    "pockets after the scripts": BASE + "pockets 3\n",
    "stack of 0": "stack 0\n" + BASE,
    "keep of something": BASE + "script c\n    keep 1\n    hold\n",
    "keep outside a script": "keep\n" + BASE,
    "draw of no kind": BASE + "script c\n    draw hand SWORD\n    hold\n",
    "draw at no point": BASE + "script c\n    draw knee 3\n    hold\n",
    "stow at no point": BASE + "script c\n    stow knee\n    hold\n",
    "count of no kind": BASE + "script c\n    goto a if count(SWORD) > 0\n    hold\n",
    "count in a path": BASE + "script c\n    path x: count(3) * t, y: 0\n    hold\n",
    "goto if nowhere": BASE + "script c\n    goto nowhere if 1\n    hold\n",
    "goto if of nothing": BASE + "script c\n    goto a if\n    hold\n",
    "frame expression not closed": BASE + "script c\n    frame (1 + amount 10ms\n    hold\n",
    "amount in a path": BASE + "script c\n    path x: amount * t, y: 0\n    hold\n",
    "frame expression of no picture": BASE + "script c\n    frame (NOPE + amount) 10ms\n    hold\n",
    "table of a param": "param p 1\ntable tab 1 p\n" + BASE,
    "table twice": "table tab 1\ntable tab 2\n" + BASE,
    "table value too big": "table tab 40000\n" + BASE,
    "table named as a param": "param tab 1\ntable tab 1\n" + BASE,
    "table of nothing": "table tab\n" + BASE,
    "table read before it is": BASE + "script c\n    dx tab[0]\n    hold\ntable tab 1\n",
    "table index not closed": "table tab 1 2\n" + BASE + "script c\n    dx tab[0\n    hold\n",
    "field takes three": BASE + "script c\n    dx field(1, 2)\n    hold\n",
    "xor takes two": BASE + "script c\n    dx xor(1)\n    hold\n",
    "its outside a sense": "param p 0\n" + BASE + "script c\n    set p its.p\n    hold\n",
    "sense of no kind": "param p 0\n" + BASE + "script c\n    sense NOPE within 5 into p\n    hold\n",
    "sense into no param": "param p 0\n" + BASE + "script c\n    sense any within 5 into q\n    hold\n",
    "its param of anyone": "param p 0\n" + BASE + "script c\n    sense any within 5 where its.p > 0 into p\n    hold\n",
    "its of a cell's param": "param p 0\n" + BASE + "script c\n    sense cell within 5 where its.p > 0 into p\n    hold\n",
    "its velocity of a cell": "param p 0\n" + BASE + "script c\n    sense cell within 5 where its.vx > 0 into p\n    hold\n",
    "sense without into": "param p 0\n" + BASE + "script c\n    sense any within 5\n    hold\n",
    "sense radius of no param": "param p 0\n" + BASE + "script c\n    sense any within q into p\n    hold\n",
    "sense copy into _": "param p 0\n" + BASE + "script c\n    sense any within 5 into p, _ = its.vx\n    hold\n",
    "sense plain after a copy": "param p 0\n" + BASE + "script c\n    sense any within 5 into p = its.vx, p\n    hold\n",
    "sense four plain": "param p 0\n" + BASE + "script c\n    sense any within 5 into p, p, p, p\n    hold\n",
    "param named its": "param its 1\n" + BASE,
    "steer of no way": BASE + "script c\n    steer chase 1, 2 to 3, 4\n    hold\n",
    "steer flee to": BASE + "script c\n    steer flee 1, 2 to 3, 4\n    hold\n",
    "steer seek without force": BASE + "script c\n    steer seek 1 to 3, 4\n    hold\n",
    "steer arrive without radius": BASE + "script c\n    steer arrive 1, 2 to 3, 4\n    hold\n",
    "steer pursue without velocity": BASE + "script c\n    steer pursue 1, 2 to 3, 4\n    hold\n",
    "steer wander of no param": BASE + "script c\n    steer wander 1, 2, 3 angle nothing\n    hold\n",
    "steer of an empty number": BASE + "script c\n    steer seek 1, to 3, 4\n    hold\n",
    "appear with of a kind unknown": BASE + "script c\n    appear 3, hit, 1, 2 with hp = 1\n    hold\n",
    "appear with nothing": "param hp 1\n" + BASE + "script c\n    appear 3, hit, 1, 2 with\n    hold\n",
    "appear with no value": "param hp 1\n" + BASE + "script c\n    appear 3, hit, 1, 2 with hp\n    hold\n",
    "game not declared": BASE + "script c\n    dx game.far\n    hold\n",
    "game declared after": BASE + "script c\n    dx game.far\n    hold\ngame far\n",
    "game twice": "game far far\n" + BASE,
    "game of no name": "game 3\n" + BASE,
    "game in a path": "game far\n" + BASE + "script c\n    path x: game.far * t, y: 0\n    hold\n",
    "param named game": "param game 1\n" + BASE,
    "axis in a path": BASE + "script c\n    path x: axis(lx) * t, y: 0\n    hold\n",
    "axis of no such name": BASE + "script c\n    dx axis(z)\n    hold\n",
    "axis of a number": BASE + "script c\n    dx axis(0)\n    hold\n",
    "axis in a param's start": "param p axis(lx)\n" + BASE,
}
failed = 0
for name, text in cases.items():
    try:
        assemble(text, {}, {"hand": 0})
        print(f"test_anim_rules.py: {name} was not refused")
        failed += 1
    except AnimError:
        pass
# expressions of defines and params where they may be
assemble("define g 432\ndefine half g / 2\nparam p 2 * 3\n" + BASE + "script c\n    dx half / 54\n    velocity 1, -p\n    path x: min(t, 1) * g, y: sin(t * 2)\n    hold\n")
# comparisons and logic, worked out here when they are of constants
from dgx_ge_anim import EXPR, Expr  # noqa: E402
for text, want in [("1 < 2", 1), ("2 <= 1", 0), ("3 == 3 and not 2 != 2", 1), ("0 or 1 > 2", 0), ("not 0 + 1", 0), ("(1 < 2) * 5 + 1", 6),
                   ("min(2 > 1, 7)", 1), ("-7 % 3", -1), ("7 % -3", 1), ("0x3B & 0x0F | 0x80", 0x8B), ("1 + 2 & 3", 3), ("2 | 1 == 3", 1)]:
    got = Expr(text, {}, {}, False, "test").constant()
    if got != want:
        print(f"test_anim_rules.py: {text} is {got}, not {want}")
        failed += 1
# bits of whole numbers, worked out here as the runtime does them
for text, want in [("xor(5, 3)", 6), ("xor(-1, 32767)", -32768), ("bits(255)", 8), ("bits(-1)", 32), ("bit(4)", 16), ("bit(31)", 0),
                   ("bit(-1)", 0), ("field(0x1F0, 4, 3)", 7), ("field(5, -1, 2)", 0), ("field(5, 0, 0)", 0), ("field(0x50, 4, 40)", 5)]:
    got = Expr(text, {}, {}, False, "test").constant()
    if got != want:
        print(f"test_anim_rules.py: {text} is {got}, not {want}")
        failed += 1
# a constant table: its numbers after the code (length, values), read by index; a constant index worked out here, 0 outside
done = assemble("param s 0\ntable tab 1 2 -3\n" + BASE + "script c\n    set s tab[s] + tab[2]\n    hold\n")
if not done.blob.endswith(struct.pack("<H3h", 3, 1, 2, -3)):
    print(f"test_anim_rules.py: table not after the code {done.blob.hex()}")
    failed += 1
tables = {"tab": (6, [1, 2, -3])}
for text, want in [("tab[1]", 2), ("tab[3]", 0), ("tab[-1]", 0), ("tab[1.7]", 2)]:
    got = Expr(text, {}, {}, False, "test", tables=tables).constant()
    if got != want:
        print(f"test_anim_rules.py: {text} is {got}, not {want}")
        failed += 1
if Expr("tab[p]", {}, {"p": (0, 0)}, False, "test", tables=tables).code() != bytes([5, EXPR["param"], 0, EXPR["table"], 6, 0]) or EXPR["table"] != 44:
    print("test_anim_rules.py: a table not as the runtime reads it")
    failed += 1
# sense: of its own kind its. reads its own params; of another kind (added before) that kind's; as the runtime reads it
from dgx_ge_anim import OP  # noqa: E402
done = assemble("param hp 3\nparam d 0\nparam m 0\n" + BASE + "script c\n    sense HERB within 80 where its.hp > 2 into d, _, _, m = its.vx\n"
                "    sense WOLF within 9 where its.bite into d\n    sense cell within 40 into _, d, m = its.cell\n    hold\n",
                kinds={"WOLF": 3}, kind_params={"WOLF": ["bite"]}, own=("HERB", 7))
want = (struct.pack("<BBHH", OP["SENSE"], 0, 7, 80) + Expr("its.hp > 2", {}, {}, False, "t", its={"hp": 0}).code() + bytes([1, 0xFF, 0xFF, 1, 2])
        + bytes([2, EXPR["its"], 0xF0])
        + struct.pack("<BBHH", OP["SENSE"], 0, 3, 9) + bytes([2, EXPR["its"], 0]) + bytes([1, 0xFF, 0xFF, 0])
        + struct.pack("<BBHH", OP["SENSE"], 1, 0xFFFF, 40) + b"\x00" + bytes([0xFF, 1, 0xFF, 1, 2]) + bytes([2, EXPR["its"], 0xF7])
        + bytes([OP["HOLD"]]))
if not done.blob.endswith(want) or OP["SENSE"] != 40 or EXPR["its"] != 45:
    print(f"test_anim_rules.py: sense {done.blob.hex()} does not end with {want.hex()}")
    failed += 1
# game.: the number of the question, as declared, in order; the assembler gives the names back
done = assemble("param k 0\ngame far\ngame sees near\n" + BASE + "script c\n    set k game.near * 2 + game.far\n    hold\n")
want = bytes([OP["SET_X"], 0]) + bytes([2 + 5 + 1 + 2 + 1, EXPR["game"], 2]) + struct.pack("<Bf", EXPR["num"], 2) + bytes([EXPR["*"], EXPR["game"], 0, EXPR["+"]])
if done.games != ["far", "sees", "near"] or want not in done.blob or EXPR["game"] != 46:
    print(f"test_anim_rules.py: game {done.games} {done.blob.hex()} has no {want.hex()}")
    failed += 1
# axis() only in a script of an animation: not in a piece of a tile, not in a dialog's condition (stale there)
for name, make in [("a tile", lambda: Expr("axis(lx)", {}, {}, False, "t", actor=False)),
                   ("a dialog", lambda: Expr("axis(lx) > 0", {}, {"hp": (0, 0)}, False, "t", kinds={}, timers=[]))]:
    try:
        make().code()
        print(f"test_anim_rules.py: axis in {name} was not refused")
        failed += 1
    except AnimError:
        pass
# axis(NAME): the step and the number of the axis, in the order of DGX_GE_AXIS_*; the step comes after all the others
done = assemble("param k 0\n" + BASE + "script c\n    set k axis(rt) + axis(lx)\n    hold\n")
want = bytes([OP["SET_X"], 0]) + bytes([2 + 2 + 1, EXPR["axis"], 5, EXPR["axis"], 0, EXPR["+"]])
if want not in done.blob or EXPR["axis"] != 52:
    print(f"test_anim_rules.py: axis {done.blob.hex()} has no {want.hex()}")
    failed += 1
# steer: the way, the param of the angle, how many, the expressions in order
done = assemble("param a 0\nparam d 0\n" + BASE + "script c\n    steer evade 50, 20 from d, 7, 0, 25\n    steer wander 10, 1000, 15 angle a\n    hold\n")
num = lambda v: Expr(str(v), {}, {}, False, "t").code()
want = (bytes([OP["STEER"], 4, 0xFF, 6]) + num(50) + num(20) + Expr("d", {}, {"d": (1, 0)}, False, "t").code() + num(7) + num(0) + num(25)
        + bytes([OP["STEER"], 5, 0, 3]) + num(10) + num(1000) + num(15) + bytes([OP["HOLD"]]))
if not done.blob.endswith(want) or OP["STEER"] != 41:
    print(f"test_anim_rules.py: steer {done.blob.hex()} does not end with {want.hex()}")
    failed += 1
# appear ... with: of its own kind its own params; without with the step as it was; a param it does not have refused
done = assemble("param hp 3\nparam g 0\n" + BASE + "script c\n    appear HERB, hit, 1, 2 with g = hp + 1\n    appear HERB, hit, 1, 2\n    hold\n", own=("HERB", 5))
want = (struct.pack("<BHBB", OP["APPEAR_WITH"], 5, 0, 0) + num(1) + num(2) + bytes([1, 1]) + Expr("hp + 1", {}, {"hp": (0, 3)}, False, "t").code()
        + struct.pack("<BHBB", OP["APPEAR"], 5, 0, 0) + num(1) + num(2) + bytes([OP["HOLD"]]))
if not done.blob.endswith(want) or OP["APPEAR_WITH"] != 42:
    print(f"test_anim_rules.py: appear with {done.blob.hex()} does not end with {want.hex()}")
    failed += 1
try:
    assemble("param hp 1\n" + BASE + "script c\n    appear HERB, hit, 1, 2 with lives = 1\n    hold\n", own=("HERB", 5))
    print("test_anim_rules.py: appear with a param it has not taken")
    failed += 1
except AnimError:
    pass
# rnd: never worked out here, cast every time
if Expr("rnd(3)", {}, {}, False, "test").constant() is not None:
    print("test_anim_rules.py: rnd(3) was folded")
    failed += 1
# hits: their events may be named before they are given; one after another they go together, not across scripts
from dgx_ge_anim import OP  # noqa: E402
done = assemble("script a\n    masks 1\n    hit 1 on 2 event late\n    hit 1 on 3 recoil late\n    frame 1 10ms\n    hit 1 on 2\n"
                "script b\n    hit 1 on 3\n    hold\nevent late\n")
code = done.blob
counts = [code[i + 1] for i in range(len(code) - 1) if code[i] == OP["HITS"] and code[i + 1] in (1, 2)]
if 2 not in counts or counts.count(1) < 2:
    print(f"test_anim_rules.py: hits grouped wrong {counts}")
    failed += 1
# events of a pack: one name, one number for every kind; a hit names an event of another kind without declaring it
shared = []
hand = assemble("event reach, stop\nscript crouch\n    masks 5\n    hit 5 on 6 event taken\n    frame 1 10ms\n    hold\n", events=shared)
pot = assemble("event taken\nscript lying\n    frame 2 10ms\n    hold\nscript held\n    frame 2 10ms\n    hold\n"
               "on taken: lying -> held now\n", events=shared)
if shared != ["reach", "stop", "taken"] or hand.events != shared or pot.events != shared:
    print(f"test_anim_rules.py: shared events {shared}")
    failed += 1
taken_at = next(i for i in range(len(hand.blob)) if hand.blob[i] == OP["HITS"]) + 2 + 5  # its event byte
if hand.blob[taken_at] != 2:
    print(f"test_anim_rules.py: the hit's event is {hand.blob[taken_at]}, not 2 (taken)")
    failed += 1
_, _, _, n_events = struct.unpack_from("<IHHH", pot.blob, 0)[:4]
if struct.unpack_from("<H", pot.blob, 8)[0] != 3:
    print("test_anim_rules.py: the pot's matrix is not as wide as the pack's events")
    failed += 1
try:  # a rule needs the event declared by its own text
    assemble("script a\n    hold\non taken: a -> a now\n", events=["taken"])
    print("test_anim_rules.py: a rule on an event not declared taken")
    failed += 1
except AnimError:
    pass
# a piece of a tile has no actor: its velocity and now are no names there (they would be 0 without a word)
for name in ("now", "vx", "vy"):
    try:
        Expr(name + " + 1", {}, {}, False, "tile", {"mark": (1, 0, 2)}, actor=False).code()
        print(f"test_anim_rules.py: {name} taken in a tile")
        failed += 1
    except AnimError:
        pass
if Expr("mark + 1", {}, {}, False, "tile", {"mark": (1, 0, 2)}, actor=False).code()[1] != EXPR["field"]:
    print("test_anim_rules.py: a field of a tile not taken")
    failed += 1
# amount is of an actor: none in a tile; a frame of an expression: FRAME_X, ms, then the expression; of constants a FRAME
try:
    Expr("amount", {}, {}, False, "tile", {"mark": (1, 0, 2)}, actor=False).code()
    print("test_anim_rules.py: amount taken in a tile")
    failed += 1
except AnimError:
    pass
done = assemble("script a\n    frame (P + amount - 1) 100ms\n    frame (P + 2) 50ms\n    hold\n", {"P": 10})
code = done.blob[20 + 2:]
want = bytes([OP["FRAME_X"], 100, 0]) + Expr("10 + amount - 1", {}, {}, False, "t").code() + bytes([OP["FRAME"], 12, 0, 50, 0, OP["HOLD"]])
if code != want:
    print(f"test_anim_rules.py: frame of an expression {code.hex()} not {want.hex()}")
    failed += 1
# count is of an actor's pockets: none in a tile
try:
    Expr("count(3)", {}, {}, False, "tile", {"mark": (1, 0, 2)}, actor=False).code()
    print("test_anim_rules.py: count taken in a tile")
    failed += 1
except AnimError:
    pass
# pockets, stack in the header, keep a step; goto if to a label and to a script, the expression after; draw, stow, count by the kinds
done = assemble("pockets 6\nstack 10\nscript a\n    keep\nlabel top\n    goto top if count(SWORD) == 0\n    goto b if 1 < 2\n"
                "    draw hand SWORD\n    stow hand\n    hold\nscript b\n    hold\n", {}, {"hand": 4}, kinds={"SWORD": 9})
header = struct.unpack_from("<IHHHHIBBBB", done.blob, 0)
if header[1] != 10 or header[6:10] != (6, 10, 0, 0):
    print(f"test_anim_rules.py: header {header}")
    failed += 1
code = done.blob[20 + 2 * 2:]
want = (bytes([OP["KEEP"], OP["GOTO_IF"], 0, 1, 0]) + Expr("count(SWORD) == 0", {}, {}, False, "t", kinds={"SWORD": 9}).code()
        + bytes([OP["GOTO_IF"], 1, 1, 0]) + Expr("1 < 2", {}, {}, False, "t").code()
        + bytes([OP["DRAW"], 4, 9, 0, OP["STOW"], 4, OP["HOLD"]]))
if not code.startswith(want):
    print(f"test_anim_rules.py: code {code.hex()} not {want.hex()}")
    failed += 1
if Expr("count(SWORD)", {}, {}, False, "t", kinds={"SWORD": 9}).code() != bytes([3, EXPR["count"], 9, 0]):
    print("test_anim_rules.py: count not as the runtime reads it")
    failed += 1
# timer(NAME): the pack's names, given on, kept; as the runtime reads it
names = ["bomb"]
done = assemble("param p 0\n" + BASE + "script c\n    set p timer(clock) + timer(bomb)\n    hold\n", timers=names)
if names != ["bomb", "clock"] or Expr("timer(bomb)", {}, {}, False, "t", timers=names).code() != bytes([2, EXPR["timer"], 0]):
    print(f"test_anim_rules.py: timers {names}")
    failed += 1
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from dgx_ge_pack import Pack  # noqa: E402
for what, call in (("never described", lambda p: p.add_anim("W", "param q 0\nscript s\n    set q timer(lost)\n    hold\n")),
                   ("until shorter", lambda p: p.add_timer("a", every="2s", until="1s")),
                   ("two kinds", lambda p: p.add_timer("a", once="1s", every="1s")),
                   ("a reset of nothing", lambda p: p.add_timer("a", once="1s", reset="room")),
                   ("no unit", lambda p: p.add_timer("a", once="100"))):
    pack = Pack("T")
    try:
        call(pack)
        pack.write(os.devnull)
        print(f"test_anim_rules.py: a timer {what} was taken")
        failed += 1
    except (ValueError, AnimError):
        pass
# timer start|stop|hold|go NAME: the pack's names, as timer(NAME) reads them
names = ["clock"]
done = assemble(BASE + "script c\n    timer start bomb\n    timer go clock\n    hold\n", timers=names)
code = done.blob[-7:]
if names != ["clock", "bomb"] or code != bytes([OP["TIMER"], 0, 1, OP["TIMER"], 3, 0, OP["HOLD"]]) or OP["TIMER"] != 33:
    print(f"test_anim_rules.py: timer steps {names} {code.hex()}")
    failed += 1
# c ? a : b: below all the rest, right to left; worked out here when constant; as the runtime reads it
for text, want in (("1 ? 2 : 3", 2), ("0 ? 2 : 0 ? 4 : 5", 5), ("(1 ? 0 : 1) ? 7 : 8", 8), ("2 + 3 > 4 ? 10 : 20", 10)):
    if Expr(text, {}, {}, False, "t").constant() != want:
        print(f"test_anim_rules.py: {text} is not {want}")
        failed += 1
# lazy: c, IF_NOT over a and JUMP, a, JUMP over b, b; a and b: a, AND_THEN over b and TRUTH (OR_ELSE of or)
P, num = {"p": (0, 0), "q": (1, 0)}, lambda v: struct.pack("<Bf", EXPR["num"], v)
lazy = {"p ? 1 : 2": bytes([EXPR["param"], 0, EXPR["if_not"], 7]) + num(1) + bytes([EXPR["jump"], 5]) + num(2),
        "p and q": bytes([EXPR["param"], 0, EXPR["and_then"], 3, EXPR["param"], 1, EXPR["truth"]]),
        "p or q": bytes([EXPR["param"], 0, EXPR["or_else"], 3, EXPR["param"], 1, EXPR["truth"]]),
        "0 and rnd(3)": num(0), "1 or rnd(3)": num(1), "1 and q": bytes([EXPR["param"], 1, EXPR["truth"]]),
        "0 ? rnd(3) : q": bytes([EXPR["param"], 1])}
for text, want in lazy.items():
    got = Expr(text, {}, P, False, "t").code()
    if got != bytes([len(want)]) + want:
        print(f"test_anim_rules.py: {text} is {got.hex()}, not {want.hex()}")
        failed += 1
if (EXPR["and_then"], EXPR["or_else"], EXPR["truth"], EXPR["if_not"], EXPR["jump"]) != (47, 48, 49, 50, 51):
    print("test_anim_rules.py: ? and or not as the runtime reads them")
    failed += 1
# a show: refused with no end, a role twice, a cue of no role or no event, an animation not added
for what, call in (("no end", lambda p: p.add_show("S", actors=[{"name": "a", "kind": "W"}], cues=[(100, "a", "go")])),
                   ("a role twice", lambda p: p.add_show("S", actors=[{"name": "a", "kind": "W"}, {"name": "a", "kind": "W"}], cues=[(1, "end", None)])),
                   ("a cue of nobody", lambda p: p.add_show("S", actors=[{"name": "a", "kind": "W"}], cues=[(1, "b", "go"), (2, "end", None)])),
                   ("no such event", lambda p: p.add_show("S", actors=[{"name": "a", "kind": "W"}], cues=[(1, "a", "run"), (2, "end", None)])),
                   ("no animation", lambda p: p.add_show("S", actors=[{"name": "a", "kind": "X"}], cues=[(1, "end", None)]))):
    pack = Pack("T")
    pack.add_set("A", [(1, 2, 3)])
    pack.add_image("P", 0, 1, 1, [1], 1, (0, 0))
    pack.add_anim("W", "event go\nscript s\n    frame P 100ms\n    hold\n")
    try:
        call(pack)
        print(f"test_anim_rules.py: a show with {what} was taken")
        failed += 1
    except ValueError:
        pass
# nothing shown: frame none is the picture 0xFFFF (the runtime reads it as -1, hidden)
done = assemble("script a\n    frame none 10ms\n    hold\n")
if struct.pack("<HH", 0xFFFF, 10) not in done.blob:
    print("test_anim_rules.py: frame none is not 0xFFFF")
    failed += 1
# a radius of a sense worked out at the step (flag 4, its expression after where); a number as before (no flag, no expression)
d1 = assemble("param p 0\n" + BASE + "script c\n    sense any within 30 - p / 4 into p\n    hold\n")
d2 = assemble("param p 0\n" + BASE + "script c\n    sense any within 30 into p\n    hold\n")
i1, i2 = d1.blob.index(bytes([40])), d2.blob.index(bytes([40]))
if d1.blob[i1 + 1] != 4 or d1.blob[i1 + 4:i1 + 6] != b"\0\0" or d2.blob[i2 + 1] != 0 or d2.blob[i2 + 4:i2 + 6] != struct.pack("<H", 30) or len(d1.blob) <= len(d2.blob):
    print("test_anim_rules.py: a radius of a sense as an expression")
    failed += 1
# the same answer twice is no conflict
assemble(BASE + "group g: a, b\non hit: g -> a now\non hit: b -> a now\n")
# the pack's masks by name where a mask goes, the game's constants wherever a number goes: the same bytes as the numbers
MASKS, GAME = {"BODY": 1, "TIP": 3, "FEET": 10}, {"CUT_MS": 100, "SAY": 2}
named = assemble("event hit\nevent landed\nscript a\n    masks BODY TIP\n    hit TIP on BODY FEET damage 1\n    contact FEET below on landed\n"
                 "    frame 1 CUT_MS ms\n    signal SAY\n    hold\n", constants=GAME, masks=MASKS)
plain = assemble("event hit\nevent landed\nscript a\n    masks 1 3\n    hit 3 on 1 10 damage 1\n    contact 10 below on landed\n"
                 "    frame 1 100ms\n    signal 2\n    hold\n")
if named.blob != plain.blob:
    print("constants: not the bytes of the numbers")
    failed += 1
for what, text in (("a constant defined again", "define CUT_MS 50\n" + BASE), ("a mask the pack has not", BASE + "script c\n    masks HEAD\n    hold\n"),
                   ("a mask's name as a number", BASE + "script c\n    frame 1 BODY ms\n    hold\n")):
    try:
        assemble(text, constants=GAME, masks=MASKS)
        print(f"{what} was not refused")
        failed += 1
    except AnimError:
        pass
print("test_anim_rules.py: ok" if not failed else f"test_anim_rules.py: {failed} failed")
sys.exit(1 if failed else 0)
