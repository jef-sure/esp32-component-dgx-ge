"""
The assembler of animations of DGX-GE: scripts of frames and the rules of
passing between them, from text into the blob dgx_ge_anim.c runs (see
include/dgx_ge_anim.h).

The language, a statement a line, '#' to the end of a line is a comment:

    event hit, jump, stop           the events, in this order; more lines add more
    param hp 3                      a number every actor keeps for itself, and what it starts with
    pockets 6                       places for things a character has (dgx_ge_pockets.h)
    stack 10                        how many things of this kind go into one place (none: 1)
    table chance 75 100 75 50       constant numbers, read by index in expressions: chance[skill]
                                    (0 outside the table); declared before they are read
    game distance sees              questions to the game, by number in this order: game.distance in expressions,
                                    the game's answer at the moment of the step (dgx_ge_expr_ask)

    script stand                    an entry point: a row of the matrix
        set action 0                a param of the actor given a value
        add hp -1                   a param changed by so much (it stops at the ends of 16 bits)
        frame KID_415 83ms          a picture for a time: a name of the pack, or a number the game draws by
        frame KID_416 83ms at 2,-1  the picture moved from the actor: forward, down
        frame none 100ms            nothing shown for a time (the actor's picture -1: hidden)
        frame (DAGGERS_1 + amount - 1) 1000ms   the picture worked out now, an expression in
                                    parentheses (pictures of the pack are names in it); amount:
                                    how many the actor is, a stack lying
        dx 3                        move forward (back when negative), pixels
        dy -2                       move down (up when negative)
        flip                        turn round
        velocity 40 0               pixels a second, x forward
        accel 0 300                 pixels a second a second
        signal 3                    tell the game, 0..255
        sound 1                     a sound, 0..255
        masks 0 2                   the masks of collisions active from now, 0..15; a mask, here as in contact
                                    and hit, may be named as the pack names it (masks BODY TIP, as in its meets)
        carry hand SWORD_3          what is held at a point of the pictures from now (none: nothing):
                                    a frame of what is in the hand, as the hand moves
        contact on landed           look for contacts with the terrain (off: not); an event named is sent
                                    to the actor at the moment of one, put where it touched
        contact 1 below on landed off fell   a mask watched: whether it touches anything solid on that side (inside,
                                    below, above, left, right, beside (either side), front, back by the way it
                                    faces, any), within so
                                    many points (within 2; 1 unless said; 0: only inside): the first event at the moment it
                                    touches, the other at the moment it lets go (either alone will do: on landed,
                                    or off fell); contact 1 off: watched no more; ... off fell first within 8:
                                    begun with nothing within 8 points on that side, the off event at once (a
                                    frame that needs a floor begun over nothing: a fall)
        tell holder drank_wine      an event for the one holding this actor (a pot drunk), sent on by the game
        tell cell 451, loosened, 3  the world of the map, the game's to do (rules of a map, mostly): a cell of
                                    it (as dgx_ge_cells counts them) told an event, its own param given the value
        press 720                   a button in a cell of the map pressed as if trodden on (its links fire)
        clear 421                   what lies in a cell of the map gone
        appear KID, mouse_go, 200, 55, left   an actor of a kind (an animation of the pack) comes at x, y, facing left or
                                    not, told an event (its matrix: its script); where it lives is the game's (PoP:
                                    the room of the player; a crowd, dgx_ge_crowd.h: the crowd)
        vanish                      the actor is gone from the game (the game takes it away)
        goto turn if x < 166        x, y: where the actor is now, points of the scene (not forward, unlike vx)
        shoot hand ARROW 300 0      a shot of a kind (an animation of the pack) from a point, pixels a second,
                                    x forward: the game is told (dgx_ge_shots.h fires it); one with pockets
                                    shoots one of the kind out of them, none there: none shot
        draw hand SWORD             one thing of a kind out of the pockets into the hand at a point
        stow hand                   what is held at a point into the pockets
        keep                        the world keeps the actor as it is in this script (a sword lying; not
                                    the shards of a pot): newer things do not take its place
        goto noknife if count(DAGGER) == 0   go there only when the expression is not 0;
                                    count(KIND): how many of a kind are in the pockets
        set drunk now + 10          now: the moment of the step, seconds of the clock of the game
        goto lost if timer(clock) >= 3600000   timer(NAME): milliseconds from the start of a timer of the game
                                    (dgx_ge_timers.h; -1 when it is not going)
        velocity axis(lx) * 120, 0  axis(NAME): an axis of the player's pad (dgx_ge_input.h) at the moment of
                                    the step: lx ly rx ry -1..1, the triggers lt rt 0..1; not in a path
        timer start bomb            a timer of the pack (pack.add_timer) started, its owner the one whose script
                                    this is (an actor, a life of a cell, the rules): its event comes back to it;
                                    timer stop, hold (its time stands), go (on from where it stood)
        release hand 120 -60        let go what is held at the point, at this velocity (pixels a second,
                                    x forward; none given: 0 0): the game is told (a release) and
                                    makes of it what it is, from where the point is
        hit 2 on 1 damage 3 event hurt   what mask 2 does when it meets mask 1 (or several:
                                    on 1 3) of another: the damage the game takes off, an
                                    event for the one met, recoil <event> for this one;
                                    lines of hit one after another go together; they hold
                                    until the next masks or hit, or another script
        branch                      a transition waiting for a branch point is taken here
        hold                        stay on the last frame
        goto runcycle               go on there: a label, or a script (then the actor is in it)
    label runcycle                  a place to go to, inside the script above it

    group air: jump, fall           a name for scripts, for the rules below

    on hit: air -> knocked now      for every script of the group: at once
    on stop: run -> stopping branch at the next branch point (or the end)
    on jump: stand -> leap end      where the script would go on to another or hold

The rules fill a matrix, script by event, and nothing else: no rule is above
another and their order means nothing. A cell two rules give different
answers is an error, so is a script named twice in one rule. There is no
"every script" and no "all but": a script reacts to an event only where it is
named, by itself or in a group, so one forgotten does nothing, which can be
seen, instead of doing the wrong thing, which cannot.

A script that comes to its last line goes on into the next script: end it with
goto or hold.

Params are the state of an actor the game keeps in the actor: lives, what it
is doing. The engine only keeps them and lets scripts set them; what they mean
is the game's. Values are -32768..32767.
"""

import math
import re
import struct
from collections import namedtuple

MAGIC = 0x4E414744  # "DGAN"
VERSION = 10
PARAMS_MAX = 255  # the blob; the runtime keeps DGX_GE_ANIM_PARAMS of them an actor

OP = {
    "FRAME": 1, "FRAME_AT": 2, "DX": 3, "DY": 4, "FLIP": 5, "SET": 6, "VELOCITY": 7, "ACCEL": 8,
    "GOTO": 9, "SCRIPT": 10, "BRANCH": 11, "HOLD": 12, "SIGNAL": 13, "SOUND": 14, "MASKS": 15, "ADD": 16,
    "PATH": 17, "VELOCITY_X": 18, "ACCEL_X": 19, "SET_X": 20, "ADD_X": 21, "CONTACT": 22, "HITS": 23, "CARRY": 24, "RELEASE": 25, "TELL": 26, "SHOOT": 27,
    "STOW": 28, "DRAW": 29, "GOTO_IF": 30, "KEEP": 31, "FRAME_X": 32, "TIMER": 33, "CELL": 34, "PRESS": 35, "CLEAR": 36,
    "APPEAR": 37, "VANISH": 38, "CONTACT_MASK": 39, "SENSE": 40, "STEER": 41, "APPEAR_WITH": 42, "CONTACT_FIRST": 43,
}
# the ways of steer: its number, how many numbers before to/from (speed, force[, radius or spread]), the word, how many after it
STEER = {"seek": (0, 2, "to", 2), "flee": (1, 2, "from", 2), "arrive": (2, 3, "to", 2), "pursue": (3, 2, "to", 4),
         "evade": (4, 2, "from", 4), "wander": (5, 3, "angle", 1)}
TOUCH_SIDES = {"inside": 1, "below": 2, "above": 4, "left": 8, "right": 16, "front": 32, "back": 64, "beside": 24, "any": 31}
TIMER_ACTIONS = ("start", "stop", "hold", "go")

# an expression in the blob: its length, then its steps for a stack, numbers as float32
EXPR = {"num": 1, "param": 2, "t": 3, "+": 4, "-": 5, "*": 6, "/": 7, "neg": 8,
        "min": 9, "max": 10, "abs": 11, "sin": 12, "cos": 13, "sqrt": 14, "exp": 15, "vx": 16, "vy": 17, "field": 18,
        "<": 19, "<=": 20, ">": 21, ">=": 22, "==": 23, "!=": 24, "and": 25, "or": 26, "not": 27, "now": 28, "count": 29, "amount": 30,
        "%": 31, "&": 32, "|": 33, "rnd": 34, "timer": 35, "?": 36, "x": 37, "y": 38, "smoothstep": 39,
        "xor": 40, "bits": 41, "bit": 42, "bitfield": 43, "table": 44, "its": 45, "game": 46,
        "and_then": 47, "or_else": 48, "truth": 49, "if_not": 50, "jump": 51, "axis": 52}
COMPARE = ("<", "<=", ">", ">=", "==", "!=")
VELOCITY_NAMES = ("vx", "vy")  # the velocity of the actor now, points a second, x forward
POSITION_NAMES = ("x", "y")    # where the actor is now, points of the scene (x not forward: as it stands in the scene)
FUNCS = {"min": 2, "max": 2, "abs": 1, "sin": 1, "cos": 1, "sqrt": 1, "exp": 1, "rnd": 1, "smoothstep": 1,
         "xor": 2, "bits": 1, "bit": 1, "field": 3}
FUNC_OPS = {"field": "bitfield"}  # a name of the language whose step is named otherwise ("field" is the step of a word's field)
UNFOLDED = ("rnd",)  # never worked out at assembly: the dice of the game, cast anew every time
AXES = ("lx", "ly", "rx", "ry", "lt", "rt")  # axis(NAME): the axes of a pad in the order of DGX_GE_AXIS_* (dgx_ge_input.h)
WORDS = ("and", "or", "not", "its", "game")  # of the language: no name may be one
# what its. names beyond the params of the one a sense found, by what it is: a body (an actor) or a cell
ITS_BODY = {"vx": 0xF0, "vy": 0xF1, "image": 0xF2, "script": 0xF3, "flip": 0xF4}
ITS_CELL = {"tile": 0xF5, "mod": 0xF6, "cell": 0xF7}
STACK_MAX = 16  # the runtime's stack of an expression

Assembled = namedtuple("Assembled", "blob scripts events params games", defaults=((),))
WHEN = {"now": 0, "branch": 1, "end": 2}


class AnimError(Exception):
    pass


def _int(text, low, high, where):
    try:
        v = int(text, 0)
    except ValueError:
        raise AnimError(f"{where}: not a number: {text}")
    if not low <= v <= high:
        raise AnimError(f"{where}: {v} is not within {low}..{high}")
    return v


def split_top(text, sep=","):
    """Split at the separators outside parentheses."""
    parts, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif c == sep and depth == 0:
            parts.append(text[start:i])
            start = i + 1
    parts.append(text[start:])
    return [p.strip() for p in parts]


class Expr:
    """
    An expression of numbers, defines, params of the actor and, in a path, t
    (seconds from its start): + - * / ( ) and min, max, abs, sin, cos, sqrt,
    exp; comparisons < <= > >= == != and and, or, not, which give 1 or 0;
    % & | of whole numbers; c ? a : b (a where c is not 0, else b; below all
    the rest). and, or and ?: are lazy, as in C: the part not needed is not
    worked out (rnd in it casts no dice). Bits of whole numbers: xor(a, b), bits(x) (how many ones),
    bit(n) (2^n), field(x, first, count). A constant table: name[index].
    One of constants alone is worked out here.
    """

    def __init__(self, text, defines, params, allow_t, where, fields=None, actor=True, kinds=None, timers=None, tables=None, its=None, games=None):
        """
        kinds: names of kinds (animations of the pack) to their numbers, for count(KIND).
        timers: the names of the pack's timers, for timer(NAME): known names keep their numbers, new ones are added.
        fields: names of bits of the words the caller gives (a cell of tiles: 0 its modifier, 1 its variant): (word, first, bits).
        tables: the constant tables of the animation by name: (their offset after the code, their values).
        its: in a sense, what its.<name> may be: {name: number} (the params of the kind looked for, and ITS_BODY or ITS_CELL).
        games: the questions to the game the animation declares (game.<name>), in order: their numbers.
        actor: False where there is no actor (a piece of a tile): its velocity and the moment of its step are no names there.
        """
        self.where = where
        self.actor = actor
        self.fields = fields or {}
        self.kinds = kinds or {}
        self.timers = timers if timers is not None else []
        self.tables = tables or {}
        self.its = its
        self.games = games or []
        self.animation = games is not None  # an expression of an animation's own (games given): not of a tile, a dialog, the pack
        self.tokens = re.findall(r"0[xX][0-9a-fA-F]+|\d+\.?\d*|\.\d+|[A-Za-z_]\w*|<=|>=|==|!=|[-+*/%&|(),<>]|\S", text)
        self.i = 0
        self.defines, self.params, self.allow_t = defines, params, allow_t
        if not self.tokens:
            raise AnimError(f"{where}: an expression is missing")
        self.tree = self.choice()
        if self.i != len(self.tokens):
            raise AnimError(f"{where}: '{self.tokens[self.i]}' does not belong in '{text}'")

    def peek(self):
        return self.tokens[self.i] if self.i < len(self.tokens) else None

    def take(self, want=None):
        tok = self.peek()
        if tok is None or (want and tok != want):
            raise AnimError(f"{self.where}: expected {want or 'more'} in the expression")
        self.i += 1
        return tok

    def choice(self):
        """c ? a : b, below all the rest, right to left: a ? b : c ? d : e is a ? b : (c ? d : e)."""
        node = self.logic()
        if self.peek() == "?":
            self.take()
            yes = self.choice()
            self.take(":")
            node = ("op", "?", [node, yes, self.choice()])
        return node

    def logic(self):
        node = self.both()
        while self.peek() == "or":
            self.take()
            node = ("op", "or", [node, self.both()])
        return node

    def both(self):
        node = self.negation()
        while self.peek() == "and":
            self.take()
            node = ("op", "and", [node, self.negation()])
        return node

    def negation(self):
        if self.peek() == "not":
            self.take()
            return ("op", "not", [self.negation()])
        node = self.bits()
        if self.peek() in COMPARE:
            op = self.take()
            node = ("op", op, [node, self.bits()])
            if self.peek() in COMPARE:
                raise AnimError(f"{self.where}: comparisons do not chain: use and")
        return node

    def bits(self):
        """& and | of whole numbers, below + and -, above the comparisons: mod & 0x7F == 2 is (mod & 0x7F) == 2."""
        node = self.sum()
        while self.peek() in ("&", "|"):
            op = self.take()
            node = ("op", op, [node, self.sum()])
        return node

    def sum(self):
        node = self.product()
        while self.peek() in ("+", "-"):
            op = self.take()
            node = ("op", op, [node, self.product()])
        return node

    def product(self):
        node = self.unary()
        while self.peek() in ("*", "/", "%"):
            op = self.take()
            node = ("op", op, [node, self.unary()])
        return node

    def unary(self):
        if self.peek() == "-":
            self.take()
            return ("op", "neg", [self.unary()])
        if self.peek() == "+":
            self.take()
            return self.unary()
        return self.atom()

    def atom(self):
        tok = self.take()
        if tok == "(":
            node = self.choice()
            self.take(")")
            return node
        if re.fullmatch(r"0[xX][0-9a-fA-F]+", tok):
            return ("num", float(int(tok, 16)))
        if re.fullmatch(r"\d+\.?\d*|\.\d+", tok):
            return ("num", float(tok))
        if tok == "count" and tok not in self.defines and tok not in self.fields and not (self.params and tok in self.params):
            if not self.actor or self.params is None:
                raise AnimError(f"{self.where}: count is of the pockets of an actor, there is none here")
            if self.allow_t:
                raise AnimError(f"{self.where}: count is not for a path")
            self.take("(")
            name = self.take()
            self.take(")")
            if name.isdigit():
                return ("count", _int(name, 0, 65534, self.where))
            if name not in self.kinds:
                raise AnimError(f"{self.where}: no kind '{name}'")
            return ("count", self.kinds[name])
        if tok == "timer" and tok not in self.defines and tok not in self.fields and not (self.params and tok in self.params):
            if not self.actor or self.params is None:
                raise AnimError(f"{self.where}: timer is read by a script, there is none here")
            if self.allow_t:
                raise AnimError(f"{self.where}: timer is not for a path: its formula gives the place by t alone")
            self.take("(")
            name = self.take()
            self.take(")")
            if not re.fullmatch(r"[A-Za-z_]\w*", name):
                raise AnimError(f"{self.where}: timer(<name>), not timer({name})")
            if name not in self.timers:
                self.timers.append(name)
            if self.timers.index(name) > 254:
                raise AnimError(f"{self.where}: 255 timers at most")
            return ("timer", self.timers.index(name))
        if tok == "axis" and self.peek() == "(" and tok not in self.defines and tok not in self.fields and not (self.params and tok in self.params):
            if not self.actor or self.params is None or not self.animation:
                # a tile is laid again only when its cell changes, a dialog's condition is looked at when shown: a stick in
                # them would be seen as it was then, stale
                raise AnimError(f"{self.where}: axis is read by a script of an animation, there is none here")
            if self.allow_t:
                # a path works out the place by t alone: the stick changes as the player moves it
                raise AnimError(f"{self.where}: axis is not for a path: its formula gives the place by t alone")
            self.take("(")
            name = self.take()
            self.take(")")
            if name not in AXES:
                raise AnimError(f"{self.where}: axis({name}): no such axis ({', '.join(AXES)})")
            return ("axis", AXES.index(name))
        if tok == "its" and self.peek() == ".":
            self.take(".")
            name = self.take()
            if self.its is None:
                raise AnimError(f"{self.where}: its. is the one a sense found: only in a sense")
            if name not in self.its:
                raise AnimError(f"{self.where}: its.{name} is no param of the kind looked for (its. needs the kind named)")
            return ("its", self.its[name])
        if tok == "game" and self.peek() == ".":
            self.take(".")
            name = self.take()
            if name not in self.games:
                raise AnimError(f"{self.where}: game.{name} is not declared (game {name} in the animation)")
            if self.allow_t:
                # a path works out the place by t alone: what the game says changes as it plays
                raise AnimError(f"{self.where}: game. is not for a path: its formula gives the place by t alone")
            return ("game", self.games.index(name))
        if tok in self.tables and self.peek() == "[":
            self.take("[")
            index = self.choice()
            self.take("]")
            return ("table", tok, index)
        if tok in FUNCS and self.peek() == "(":
            self.take("(")
            args = [self.choice()]
            while self.peek() == ",":
                self.take()
                args.append(self.choice())
            self.take(")")
            if len(args) != FUNCS[tok]:
                raise AnimError(f"{self.where}: {tok} takes {FUNCS[tok]}")
            return ("op", FUNC_OPS.get(tok, tok), args)
        if tok == "amount" and tok not in self.defines and not (self.params and tok in self.params):
            if not self.actor or self.params is None:
                raise AnimError(f"{self.where}: amount is how many an actor is, there is none here")
            if self.allow_t:
                raise AnimError(f"{self.where}: amount is not for a path")
            return ("amount",)
        if (tok in VELOCITY_NAMES + POSITION_NAMES or tok == "now") and not self.actor and tok not in self.fields:
            raise AnimError(f"{self.where}: {tok} is of an actor, there is none here")
        if tok in POSITION_NAMES and tok not in self.fields and not (self.params and tok in self.params):
            if self.params is None:
                raise AnimError(f"{self.where}: {tok} is where the actor is, not for a constant")
            if self.allow_t:
                raise AnimError(f"{self.where}: {tok} is not for a path: its formula gives the place")
            return (tok,)
        if tok in VELOCITY_NAMES:
            if self.params is None:
                raise AnimError(f"{self.where}: {tok} is the velocity of the actor, not for a constant")
            if self.allow_t:
                # a path makes the velocity itself: it would read its own past, and the place would depend on the frames
                raise AnimError(f"{self.where}: {tok} is not for a path: its formula gives the velocity")
            return (tok,)
        if tok == "now":
            if self.params is None:
                raise AnimError(f"{self.where}: now is the moment of the step, not for a constant")
            return ("now",)
        if tok == "t":
            if not self.allow_t:
                raise AnimError(f"{self.where}: t is the time of a path, only there")
            return ("t",)
        if tok in self.defines:
            return ("num", self.defines[tok])
        if tok in self.fields:
            return ("field",) + tuple(self.fields[tok])
        if self.params and tok in self.params:
            return ("param", self.params[tok][0])
        raise AnimError(f"{self.where}: '{tok}' is no number, define, param or function")

    @staticmethod
    def _calc(op, a):
        if op == "+": return a[0] + a[1]
        if op == "-": return a[0] - a[1]
        if op == "*": return a[0] * a[1]
        if op == "/": return a[0] / a[1] if a[1] else 0.0
        if op == "%": return math.fmod(int(a[0]), int(a[1])) if int(a[1]) else 0.0  # as C: the sign of the dividend
        if op == "&": return float(int(a[0]) & int(a[1]))
        if op == "|": return float(int(a[0]) | int(a[1]))
        if op == "neg": return -a[0]
        if op == "min": return min(a)
        if op == "max": return max(a)
        if op == "abs": return abs(a[0])
        if op in COMPARE:
            return float({"<": a[0] < a[1], "<=": a[0] <= a[1], ">": a[0] > a[1], ">=": a[0] >= a[1], "==": a[0] == a[1],
                          "!=": a[0] != a[1]}[op])
        if op == "and": return float(a[0] != 0 and a[1] != 0)
        if op == "or": return float(a[0] != 0 or a[1] != 0)
        if op == "not": return float(a[0] == 0)
        if op == "?": return a[1] if a[0] != 0 else a[2]
        if op == "sqrt": return math.sqrt(a[0]) if a[0] > 0 else 0.0
        if op == "smoothstep":
            s = min(max(a[0], 0.0), 1.0)
            return s * s * (3 - 2 * s)
        if op == "xor": return float(int(a[0]) ^ int(a[1]))
        if op == "bits": return float(bin(int(a[0]) & 0xFFFFFFFF).count("1"))  # a negative one in two's complement
        if op == "bit": return float(1 << int(a[0])) if 0 <= int(a[0]) <= 30 else 0.0
        if op == "bitfield":
            v, first, count = int(a[0]) & 0xFFFFFFFF, int(a[1]), int(a[2])
            if first < 0 or first > 31 or count <= 0:
                return 0.0
            return float(v >> first if count >= 32 else (v >> first) & ((1 << count) - 1))
        return getattr(math, op)(a[0])

    def fold(self, node=None):
        """The tree with every part of constants worked out."""
        node = self.tree if node is None else node
        if node[0] == "table":
            index = self.fold(node[2])
            if index[0] == "num":
                values, k = self.tables[node[1]][1], int(index[1])
                return ("num", float(values[k]) if 0 <= k < len(values) else 0.0)
            return ("table", node[1], index)
        if node[0] == "truth":
            inner = self.fold(node[1])
            return ("num", float(inner[1] != 0)) if inner[0] == "num" else ("truth", inner)
        if node[0] != "op":
            return node
        if node[1] in ("and", "or", "?"):
            # lazy: a part of constants decides which of the others is worked out, or that none is
            first = self.fold(node[2][0])
            if first[0] == "num":
                if node[1] == "?":
                    return self.fold(node[2][1] if first[1] != 0 else node[2][2])
                if (first[1] != 0) == (node[1] == "or"):
                    return ("num", 1.0 if node[1] == "or" else 0.0)
                return self.fold(("truth", node[2][1]))
            rest = [self.fold(a) for a in node[2][1:]]
            return ("op", node[1], [first] + rest)
        args = [self.fold(a) for a in node[2]]
        if all(a[0] == "num" for a in args) and node[1] not in UNFOLDED:
            return ("num", self._calc(node[1], [a[1] for a in args]))
        return ("op", node[1], args)

    def constant(self):
        """Its value, or None when it changes with the actor or the time."""
        node = self.fold()
        return node[1] if node[0] == "num" else None

    def code(self):
        """The steps for the stack of the runtime, with its length before them."""
        out, depth, high = bytearray(), 0, 0

        def skip(at):
            # the byte before at: how far its jump goes, to here
            if len(out) - at > 255:
                raise AnimError(f"{self.where}: the expression is too large")
            out[at - 1] = len(out) - at

        def emit(node):
            nonlocal depth, high
            if node[0] == "num":
                out.extend(struct.pack("<Bf", EXPR["num"], node[1]))
                depth += 1
            elif node[0] == "param":
                out.extend(struct.pack("<BB", EXPR["param"], node[1]))
                depth += 1
            elif node[0] in ("t", "vx", "vy", "now", "amount", "x", "y"):
                out.append(EXPR[node[0]])
                depth += 1
            elif node[0] == "count":
                out.extend(struct.pack("<BH", EXPR["count"], node[1]))
                depth += 1
            elif node[0] == "timer":
                out.extend(struct.pack("<BB", EXPR["timer"], node[1]))
                depth += 1
            elif node[0] == "axis":
                out.extend(struct.pack("<BB", EXPR["axis"], node[1]))
                depth += 1
            elif node[0] == "field":
                out.extend(struct.pack("<BBBB", EXPR["field"], node[1], node[2], node[3]))
                depth += 1
            elif node[0] in ("its", "game"):
                out.extend(struct.pack("<BB", EXPR[node[0]], node[1]))
                depth += 1
            elif node[0] == "table":
                emit(node[2])
                out.extend(struct.pack("<BH", EXPR["table"], self.tables[node[1]][0]))
            elif node[0] == "truth":
                emit(node[1])
                out.append(EXPR["truth"])
            elif node[0] == "op" and node[1] in ("and", "or"):
                # a, AND_THEN or OR_ELSE over b and TRUTH: b worked out only when a does not say already
                emit(node[2][0])
                out.extend([EXPR["and_then" if node[1] == "and" else "or_else"], 0])
                at = len(out)
                depth -= 1
                emit(node[2][1])
                out.append(EXPR["truth"])
                skip(at)
            elif node[0] == "op" and node[1] == "?":
                # c, IF_NOT over a and JUMP, a, JUMP over b, b: one of them worked out
                emit(node[2][0])
                out.extend([EXPR["if_not"], 0])
                at = len(out)
                depth -= 1
                emit(node[2][1])
                out.extend([EXPR["jump"], 0])
                skip(at)
                at = len(out)
                depth -= 1
                emit(node[2][2])
                skip(at)
            else:
                for a in node[2]:
                    emit(a)
                out.append(EXPR[node[1]])
                depth -= len(node[2]) - 1
            high = max(high, depth)

        emit(self.fold())
        if high > STACK_MAX or len(out) > 255:
            raise AnimError(f"{self.where}: the expression is too large")
        return bytes([len(out)]) + bytes(out)


def half_away(value):
    """Rounded as the engine rounds (lroundf): a half away from zero."""
    return int(math.floor(abs(value) + 0.5)) * (1 if value >= 0 else -1)


def whole(value, low, high, where):
    v = half_away(value)
    if not low <= v <= high:
        raise AnimError(f"{where}: {value:g} is not within {low}..{high}")
    return v


def assemble(text, images=None, points=None, events=None, kinds=None, timers=None, kind_params=None, own=None, constants=None, masks=None):
    """
    The blob of a text, and the names of its scripts, events and params in
    order: Assembled(blob, scripts, events, params, games).
    images: names of pictures to their numbers in the pack; points: names of
    points of pictures to their numbers.
    kinds: names of kinds (animations, blobs of the pack) to their numbers, for shoot, draw and count.
    timers: the names of the pack's timers (timer(NAME)), given on as events are: new names are added.
    events: the events of the pack, shared by all its kinds, by name: the list
    is given on, known names keep their numbers, new ones are added. One kind
    meets another (a hand takes a pot: hit ... event taken) and the event
    must mean the same to both. An event a hit, a recoil or a contact names
    needs no declaring then: it is the pack's, the kind met reacts to it if it
    declares it. The matrix of a kind is as wide as the events known when it
    is made; those added later mean nothing to it. None: the events of this
    text alone.
    kind_params: the names of the params of kinds by their names, in order (for its. of a sense of that kind);
    own: (name, number) of this animation as a kind of the pack: a sense of its own kind.
    constants: the game's names of numbers (TICK_MS) by their values: the text may write them wherever a number goes,
    as if it defined them; it may not define them again.
    masks: the masks of the pack by their names (BODY: 1), as in its meets: the text may name a mask where one goes
    (masks, hit, contact).
    """
    images = images or {}
    points = points or {}
    kinds = dict(kinds or {})
    kind_params = kind_params or {}
    if own:
        kinds.setdefault(own[0], own[1])
    timers = timers if timers is not None else []  # one list for the whole text: a name, one number
    code = bytearray()
    scripts = []          # names, in order
    script_at = {}        # name -> offset
    labels = {}           # name -> (offset, script)
    shared = events is not None
    events = events if shared else []
    declared = set()      # the events this text declares
    constants = dict(constants or {})
    defines = dict(constants)  # name -> value: the game's first
    masks = masks or {}
    tables = {}           # name -> (offset after the code, values)
    games = []            # the questions to the game (game.<name>), by their numbers
    table_bytes = bytearray()
    params = {}           # name -> (index, initial value)
    groups = {}
    rules = []            # (event, sources, target, when, where)
    fixups = []           # (offset of the operand, target name, op offset, where)
    event_fixups = []     # (offset of an event of a hit, its name, where): events may come after the scripts
    hits_at = None        # where the HITS being written begins, while hit lines follow one another
    current = None
    pockets, stack = 0, 0

    def const(text, low, high, where):
        """A number of the code, worked out here from an expression of numbers and defines."""
        return whole(constant(text, where), low, high, where)

    def mask_of(text, where):
        """A mask: its name in the pack, or a number."""
        return masks[text] if text in masks else const(text, 0, 15, where)

    def constant(text, where):
        value = Expr(text, defines, None, False, where, tables=tables, games=games).constant()
        if value is None:
            raise AnimError(f"{where}: '{text}' must be a constant")
        return value

    def operands(text):
        """The operands of a statement: by commas when it has them, else by spaces."""
        return split_top(text) if "," in text else text.split()

    for number, raw in enumerate(text.splitlines(), 1):
        where = f"line {number}"
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        word, _, rest = line.partition(" ")
        rest = rest.strip()
        if word != "hit":
            hits_at = None  # hits go together only one line after another
        if word == "event":
            for e in (x.strip() for x in rest.split(",")):
                if not re.fullmatch(r"\w+", e) or e in declared:
                    raise AnimError(f"{where}: bad or repeated event '{e}'")
                declared.add(e)
                if e not in events:
                    events.append(e)
        elif word == "define":
            m = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)\s+(.+)", rest)
            if m and m.group(1) in constants:
                raise AnimError(f"{where}: {m.group(1)} is a constant of the pack ({constants[m.group(1)]}): not to be defined again")
            if not m or m.group(1) in defines or m.group(1) in params or m.group(1) in tables or m.group(1) in ("t", "now") + VELOCITY_NAMES + POSITION_NAMES + WORDS or m.group(1) in FUNCS:
                raise AnimError(f"{where}: define <name> <value>, a new name")
            defines[m.group(1)] = constant(m.group(2), where)
        elif word == "table":
            m = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)\s+(.+)", rest)
            if not m or m.group(1) in tables or m.group(1) in defines or m.group(1) in params or m.group(1) in ("t", "now") + VELOCITY_NAMES + POSITION_NAMES + WORDS or m.group(1) in FUNCS:
                raise AnimError(f"{where}: table <name> <value> <value> ..., a new name")
            values = [whole(constant(v, where), -32768, 32767, where) for v in operands(m.group(2))]
            if len(values) > 65535 or len(table_bytes) + 2 + 2 * len(values) > 65535:
                raise AnimError(f"{where}: the tables are too large")
            tables[m.group(1)] = (len(table_bytes), values)
            table_bytes += struct.pack(f"<H{len(values)}h", len(values), *values)
        elif word == "game":
            for name in rest.split():
                if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) or name in games:
                    raise AnimError(f"{where}: game <name> <name> ..., new names")
                if len(games) == 255:
                    raise AnimError(f"{where}: 255 questions to the game at most")
                games.append(name)
        elif word == "param":
            m = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)\s+(.+)", rest)
            if not m or m.group(1) in params or m.group(1) in defines or m.group(1) in tables or m.group(1) in ("t", "now") + VELOCITY_NAMES + POSITION_NAMES + WORDS or m.group(1) in FUNCS:
                raise AnimError(f"{where}: param <name> <value>, a new name")
            if len(params) == PARAMS_MAX:
                raise AnimError(f"{where}: {PARAMS_MAX} params at most")
            params[m.group(1)] = (len(params), whole(constant(m.group(2), where), -32768, 32767, where))
        elif word in ("pockets", "stack"):
            if current is not None:
                raise AnimError(f"{where}: {word} is of the animation, before its scripts")
            n = const(rest, 0 if word == "pockets" else 1, 255, where)
            if word == "pockets":
                pockets = n
            else:
                stack = n
        elif word == "group":
            name, _, members = rest.partition(":")
            groups[name.strip()] = [m.strip() for m in members.split(",") if m.strip()]
        elif word == "on":
            m = re.fullmatch(r"(\w+)\s*:\s*(\w+)\s*->\s*(\w+)\s+(now|branch|end)", rest)
            if not m:
                raise AnimError(f"{where}: on <event>: <script or group> -> <script> now|branch|end")
            rules.append((m.group(1), m.group(2), m.group(3), WHEN[m.group(4)], where))
        elif word in ("script", "label"):
            if not re.fullmatch(r"\w+", rest) or rest in script_at or rest in labels:
                raise AnimError(f"{where}: bad or repeated name '{rest}'")
            if word == "script":
                current = rest
                scripts.append(rest)
                script_at[rest] = len(code)
            else:
                if current is None:
                    raise AnimError(f"{where}: a label outside a script")
                labels[rest] = (len(code), current)
        else:
            if current is None:
                raise AnimError(f"{where}: '{word}' outside a script")
            args = rest.replace(",", " ").split()
            if word == "frame" and rest.startswith("("):
                # the picture an expression: in parentheses, the pictures of the pack names in it
                depth, end = 0, None
                for k, c in enumerate(rest):
                    depth += c == "("
                    depth -= c == ")"
                    if depth == 0:
                        end = k
                        break
                if end is None:
                    raise AnimError(f"{where}: frame (<expression>) <ms>: a parenthesis is not closed")
                ms = const(re.sub(r"\s*ms$", "", rest[end + 1:].strip()), 1, 65535, where)
                e = Expr(rest[1:end], {**defines, **images}, params, False, where, kinds=kinds, timers=timers, tables=tables, games=games)
                if e.constant() is not None:
                    code += struct.pack("<BHH", OP["FRAME"], whole(e.constant(), 0, 65535, where), ms)
                else:
                    code += struct.pack("<BH", OP["FRAME_X"], ms) + e.code()
            elif word == "frame":
                m = re.fullmatch(r"(\w+)\s+(.+?)(?:\s+at\s+(.+))?", rest)
                if not m:
                    raise AnimError(f"{where}: frame <picture> <ms>[ms] [at dx, dy]")
                pic = m.group(1)
                if pic == "none":
                    image = 0xFFFF  # nothing shown: the actor's picture -1
                elif pic.isdigit():
                    image = _int(pic, 0, 65535, where)
                elif pic in images:
                    image = images[pic]
                else:
                    raise AnimError(f"{where}: no picture '{pic}'")
                ms = const(re.sub(r"\s*ms$", "", m.group(2)), 1, 65535, where)
                if m.group(3) is None:
                    code += struct.pack("<BHH", OP["FRAME"], image, ms)
                else:
                    at = split_top(m.group(3))
                    if len(at) != 2:
                        raise AnimError(f"{where}: at <dx>, <dy>")
                    code += struct.pack("<BHHbb", OP["FRAME_AT"], image, ms, const(at[0], -128, 127, where), const(at[1], -128, 127, where))
            elif word in ("dx", "dy"):
                code += struct.pack("<Bb", OP[word.upper()], const(rest, -128, 127, where))
            elif word == "timer":
                words = rest.split()
                if len(words) != 2 or words[0] not in TIMER_ACTIONS or not re.fullmatch(r"[A-Za-z_]\w*", words[1]):
                    raise AnimError(f"{where}: timer start|stop|hold|go <name>")
                if words[1] not in timers:
                    timers.append(words[1])
                if timers.index(words[1]) > 254:
                    raise AnimError(f"{where}: 255 timers at most")
                code += struct.pack("<BBB", OP["TIMER"], TIMER_ACTIONS.index(words[0]), timers.index(words[1]))
            elif word == "keep":
                if rest:
                    raise AnimError(f"{where}: keep, alone")
                code.append(OP["KEEP"])
            elif word in ("flip", "branch", "hold"):
                code.append(OP[word.upper()])
            elif word in ("set", "add"):
                m = re.fullmatch(r"(\w+)\s+(.+)", rest)
                if not m or m.group(1) not in params:
                    raise AnimError(f"{where}: {word} <param> <value>, of a param given before")
                index = params[m.group(1)][0]
                e = Expr(m.group(2), defines, params, False, where, kinds=kinds, timers=timers, tables=tables, games=games)
                if e.constant() is not None:
                    code += struct.pack("<BBh", OP[word.upper()], index, whole(e.constant(), -32768, 32767, where))
                else:
                    code += struct.pack("<BB", OP[word.upper() + "_X"], index) + e.code()
            elif word in ("signal", "sound"):
                code += struct.pack("<BB", OP[word.upper()], const(rest, 0, 255, where))
            elif word in ("velocity", "accel"):
                xy = operands(rest)
                if len(xy) != 2:
                    raise AnimError(f"{where}: {word} <x> <y>, or <x>, <y> with expressions")
                ex, ey = (Expr(v, defines, params, False, where, kinds=kinds, timers=timers, tables=tables, games=games) for v in xy)
                # whole numbers as such; one with a fraction (a speed of a game's own pace) kept exact as an expression
                if all(e.constant() is not None and e.constant() == half_away(e.constant()) for e in (ex, ey)):
                    code += struct.pack("<Bhh", OP[word.upper()], whole(ex.constant(), -32768, 32767, where),
                                        whole(ey.constant(), -32768, 32767, where))
                else:
                    code += bytes([OP[word.upper() + "_X"]]) + ex.code() + ey.code()
            elif word == "contact" and rest.split()[:1] and rest.split()[0] not in ("on", "off"):
                # contact <mask> <side> [within <points>] on <event> [off <event>] [first within <points>] | contact <mask> off
                words = rest.split()
                mask = mask_of(words[0], where)
                if words[1:] == ["off"]:
                    code += struct.pack("<BBBBBB", OP["CONTACT_MASK"], mask, 0, 0, 0xFF, 0xFF)
                    continue
                usage = (f"{where}: contact <mask> <side> [within <points>] on <event> [off <event>] [first within <points>] | ... off <event>"
                         " | contact <mask> off")
                if len(words) < 4 or words[1] not in TOUCH_SIDES:
                    raise AnimError(usage)
                i, reach = 2, 1
                if words[i] == "within":
                    if i + 1 >= len(words):
                        raise AnimError(usage)
                    reach = const(words[i + 1], 0, 255, where)  # 0: only inside counts
                    i += 2
                # first within <points>: begun with nothing that near, the off event at once
                first = None
                if len(words) >= i + 3 and words[-3:-1] == ["first", "within"]:
                    first = const(words[-1], 1, 255, where)
                    words = words[:-3]
                # on <event> [off <event>], or off <event> alone
                said = {}
                while i < len(words):
                    if words[i] not in ("on", "off") or i + 1 >= len(words) or words[i] in said:
                        raise AnimError(usage)
                    said[words[i]] = words[i + 1]
                    i += 2
                if not said or (first is not None and "off" not in said):
                    raise AnimError(usage)
                at = len(code)
                code += struct.pack("<BBBBBB", OP["CONTACT_FIRST" if first else "CONTACT_MASK"], mask, TOUCH_SIDES[words[1]], reach, 0xFF, 0xFF)
                if first:
                    code += bytes([first])
                if "on" in said:
                    event_fixups.append((at + 4, said["on"], where))
                if "off" in said:
                    event_fixups.append((at + 5, said["off"], where))
            elif word == "contact":
                words = rest.split()
                if not words or words[0] not in ("on", "off") or len(words) > 2 or (words[0] == "off" and len(words) == 2):
                    raise AnimError(f"{where}: contact on [<event>] | contact off")
                at = len(code)
                code += struct.pack("<BBB", OP["CONTACT"], words[0] == "on", 0xFF)
                if len(words) == 2:
                    event_fixups.append((at + 2, words[1], where))
            elif word == "path":
                m = re.fullmatch(r"x\s*:\s*(.+)", rest)
                xy = split_top(m.group(1)) if m else []
                if len(xy) != 2 or not re.match(r"y\s*:", xy[1]):
                    raise AnimError(f"{where}: path x: <expression of t>, y: <expression of t>")
                ex = Expr(xy[0], defines, params, True, where, tables=tables, games=games)
                ey = Expr(re.sub(r"^y\s*:\s*", "", xy[1]), defines, params, True, where, tables=tables, games=games)
                code += bytes([OP["PATH"]]) + ex.code() + ey.code()
            elif word == "hit":
                # hit <mask> on <mask> [<mask>...] [damage <n>] [event <e>] [recoil <e>]
                words = rest.split()
                if len(words) < 3 or words[1] != "on":
                    raise AnimError(f"{where}: hit <mask> on <masks> [damage n] [event e] [recoil e]")
                mask, on, i = mask_of(words[0], where), 0, 2
                while i < len(words) and words[i] not in ("damage", "event", "recoil"):
                    on |= 1 << mask_of(words[i], where)
                    i += 1
                if not on:
                    raise AnimError(f"{where}: hit {mask} on what?")
                damage, said = 0, {"event": None, "recoil": None}
                while i < len(words):
                    if i + 1 >= len(words):
                        raise AnimError(f"{where}: {words[i]} of what?")
                    if words[i] == "damage":
                        damage = const(words[i + 1], -32768, 32767, where)
                    elif words[i] in said:
                        said[words[i]] = words[i + 1]
                    else:
                        raise AnimError(f"{where}: '{words[i]}' is no part of a hit")
                    i += 2
                if hits_at is None:
                    hits_at = len(code)
                    code += bytes([OP["HITS"], 0])
                if code[hits_at + 1] == 255:
                    raise AnimError(f"{where}: 255 hits at once at most")
                code[hits_at + 1] += 1
                at = len(code)
                code += struct.pack("<BHhBB", mask, on, damage, 0xFF, 0xFF)
                for k, e in (("event", at + 5), ("recoil", at + 6)):
                    if said[k] is not None:
                        event_fixups.append((e, said[k], where))
                continue
            elif word == "carry":
                if len(args) != 2:
                    raise AnimError(f"{where}: carry <point> <picture> (or none)")
                if args[0] not in points:
                    raise AnimError(f"{where}: no point '{args[0]}' on the pictures")
                if args[1] == "none":
                    pic = 0xFFFF
                elif args[1].isdigit():
                    pic = _int(args[1], 0, 65534, where)
                elif args[1] in images:
                    pic = images[args[1]]
                else:
                    raise AnimError(f"{where}: no picture '{args[1]}'")
                code += struct.pack("<BBH", OP["CARRY"], points[args[0]], pic)
            elif word == "release":
                words = rest.split(None, 1)
                if not words or words[0] not in points:
                    raise AnimError(f"{where}: release <point> [<vx> <vy>], a point of the pictures")
                xy = operands(words[1]) if len(words) > 1 else ["0", "0"]
                if len(xy) != 2:
                    raise AnimError(f"{where}: release <point> <vx> <vy>, or <vx>, <vy> with expressions")
                ex, ey = (Expr(v, defines, params, False, where, kinds=kinds, timers=timers, tables=tables, games=games) for v in xy)
                code += struct.pack("<BB", OP["RELEASE"], points[words[0]]) + ex.code() + ey.code()
            elif word == "shoot":
                m = re.fullmatch(r"(\w+)\s+(\w+)\s*(.*)", rest)
                if not m or m.group(1) not in points:
                    raise AnimError(f"{where}: shoot <point> <kind> [<vx> <vy>], a point of the pictures")
                if m.group(2).isdigit():
                    kind = _int(m.group(2), 0, 65535, where)
                elif m.group(2) in kinds:
                    kind = kinds[m.group(2)]
                else:
                    raise AnimError(f"{where}: no kind of shot '{m.group(2)}'")
                xy = operands(m.group(3)) if m.group(3).strip() else ["0", "0"]
                if len(xy) != 2:
                    raise AnimError(f"{where}: shoot <point> <kind> <vx> <vy>")
                ex, ey = (Expr(v, defines, params, False, where, kinds=kinds, timers=timers, tables=tables, games=games) for v in xy)
                code += struct.pack("<BBH", OP["SHOOT"], points[m.group(1)], kind) + ex.code() + ey.code()
            elif word in ("stow", "draw"):
                if not args or args[0] not in points or len(args) != (1 if word == "stow" else 2):
                    raise AnimError(f"{where}: stow <point> | draw <point> <kind>, a point of the pictures")
                if word == "stow":
                    code += struct.pack("<BB", OP["STOW"], points[args[0]])
                else:
                    if args[1].isdigit():
                        kind = _int(args[1], 0, 65534, where)
                    elif args[1] in kinds:
                        kind = kinds[args[1]]
                    else:
                        raise AnimError(f"{where}: no kind '{args[1]}'")
                    code += struct.pack("<BBH", OP["DRAW"], points[args[0]], kind)
            elif word == "tell" and rest.split()[:1] == ["cell"]:
                # a cell of the map told an event, with a value of its own param (0 none): the game's world does it
                xs = split_top(rest[4:].strip())
                if len(xs) not in (2, 3) or not re.fullmatch(r"\w+", xs[1].strip()):
                    raise AnimError(f"{where}: tell cell <cell>, <event>[, <value>]")
                cell = Expr(xs[0], defines, params, False, where, kinds=kinds, timers=timers, tables=tables, games=games)
                value = Expr(xs[2] if len(xs) == 3 else "0", defines, params, False, where, kinds=kinds, timers=timers, tables=tables, games=games)
                at = len(code)
                code += struct.pack("<BB", OP["CELL"], 0xFF) + cell.code() + value.code()
                event_fixups.append((at + 1, xs[1].strip(), where))
            elif word == "appear":
                # an actor of a kind comes where the player is, told an event, at x, y, facing left or not: the game's;
                # with: params given to it, worked out now (dgx_ge_anim_said_params)
                rest, split, given = re.sub(r"\s+with\s+", "\0", rest, count=1).partition("\0")
                given = given if split else None  # the text after with, None without it
                xs = [v.strip() for v in split_top(rest)]
                left = len(xs) == 5 and xs[4] == "left"
                if len(xs) not in (4, 5) or (len(xs) == 5 and not left) or not re.fullmatch(r"\w+", xs[0]) or not re.fullmatch(r"\w+", xs[1]):
                    raise AnimError(f"{where}: appear <kind>, <event>, <x>, <y>[, left]")
                if xs[0].isdigit():
                    kind = _int(xs[0], 0, 65535, where)
                elif kinds and xs[0] in kinds:
                    kind = kinds[xs[0]]
                else:
                    raise AnimError(f"{where}: no kind '{xs[0]}' to appear")
                ex, ey = (Expr(v, defines, params, False, where, kinds=kinds, timers=timers, tables=tables, games=games) for v in xs[2:4])
                at = len(code)
                if given is None:
                    code += struct.pack("<BHBB", OP["APPEAR"], kind, 0xFF, 1 if left else 0) + ex.code() + ey.code()
                else:
                    if own and xs[0] == own[0]:
                        child = sorted(params, key=lambda n: params[n][0])
                    elif xs[0] in kind_params:
                        child = kind_params[xs[0]]
                    else:
                        raise AnimError(f"{where}: appear {xs[0]} with: its params are not known (a kind added before, or this one)")
                    pairs = []
                    for item in split_top(given):
                        name, eq, value = item.partition("=")
                        name = name.strip()
                        if not eq or not value.strip():
                            raise AnimError(f"{where}: appear ... with <param> = <expression>, ...")
                        if name not in child:
                            raise AnimError(f"{where}: appear {xs[0]} with: '{name}' is no param of it")
                        pairs.append(bytes([child.index(name)]) + Expr(value.strip(), defines, params, False, where, kinds=kinds, timers=timers, tables=tables, games=games).code())
                    if not pairs or len(pairs) > 255:
                        raise AnimError(f"{where}: appear ... with <param> = <expression>, ...")
                    code += (struct.pack("<BHBB", OP["APPEAR_WITH"], kind, 0xFF, 1 if left else 0) + ex.code() + ey.code()
                             + bytes([len(pairs)]) + b"".join(pairs))
                event_fixups.append((at + 3, xs[1], where))
            elif word == "sense":
                # the nearest one: an actor of a kind (any) or a cell, within a radius, that makes where not 0; what it is into params
                m = re.fullmatch(r"(\w+)\s+within\s+(.+?)(\s+seen)?(?:\s+where\s+(.+?))?\s+into\s+(.+)", rest)
                if not m:
                    raise AnimError(f"{where}: sense <kind|any|cell> within <radius> [seen] [where <condition>] into <d>[, <dx>[, <dy>]][, <param> = <expression>]...")
                target = m.group(1)
                flags, kind = 0, 0xFFFF
                if target == "cell":
                    flags, its = 1, dict(ITS_CELL)
                elif target == "any":
                    its = dict(ITS_BODY)
                else:
                    if target.isdigit():
                        kind = _int(target, 0, 65534, where)
                    elif target in kinds:
                        kind = kinds[target]
                    else:
                        raise AnimError(f"{where}: no kind '{target}' to sense")
                    names = sorted(params, key=lambda n: params[n][0]) if own and target == own[0] else kind_params.get(target, [])
                    its = dict(ITS_BODY, **{n: i for i, n in enumerate(names)})
                # the radius: a number as it always was, or an expression of its own worked out at the step (flag 4, after where)
                rexpr = Expr(m.group(2), defines, params, False, where, kinds=kinds, timers=timers, tables=tables, games=games)
                if rexpr.constant() is not None:
                    radius, rcode = const(m.group(2), 0, 65535, where), b""
                else:
                    radius, rcode = 0, rexpr.code()
                    flags |= 4
                if m.group(3):
                    flags |= 2  # only one the look reaches, as the terrain veils it
                cond = Expr(m.group(4), defines, params, False, where, kinds=kinds, timers=timers, tables=tables, its=its, games=games).code() if m.group(4) else b"\x00"
                outs, copies = [], []
                for item in split_top(m.group(5)):
                    name, eq, value = item.partition("=")
                    name = name.strip()
                    if name != "_" and name not in params:
                        raise AnimError(f"{where}: sense into '{name}': no such param")
                    if eq:
                        if name == "_":
                            raise AnimError(f"{where}: sense: a copy goes into a param, not _")
                        copies.append(bytes([params[name][0]]) + Expr(value.strip(), defines, params, False, where, kinds=kinds, timers=timers, tables=tables, its=its, games=games).code())
                    elif copies or len(outs) == 3:
                        raise AnimError(f"{where}: sense into: d, dx, dy first, then <param> = <expression>")
                    else:
                        outs.append(0xFF if name == "_" else params[name][0])
                if len(copies) > 255:
                    raise AnimError(f"{where}: sense: too many copies")
                outs += [0xFF] * (3 - len(outs))
                code += struct.pack("<BBHH", OP["SENSE"], flags, kind, radius) + cond + rcode + bytes(outs) + bytes([len(copies)]) + b"".join(copies)
            elif word == "steer":
                # the velocity steered toward or away from a place (and its velocity), or wandering by an angle: Reynolds
                way, _, tail = rest.partition(" ")
                if way not in STEER:
                    raise AnimError(f"{where}: steer seek|flee|arrive|pursue|evade|wander ...")
                number, before, link, after = STEER[way]
                m = re.fullmatch(rf"(.+?)\s+{link}\s+(.+)", tail.strip())
                usage = {"seek": "steer seek <speed>, <force> to <dx>, <dy>", "flee": "steer flee <speed>, <force> from <dx>, <dy>",
                         "arrive": "steer arrive <speed>, <force>, <radius> to <dx>, <dy>",
                         "pursue": "steer pursue <speed>, <force> to <dx>, <dy>, <vx>, <vy>",
                         "evade": "steer evade <speed>, <force> from <dx>, <dy>, <vx>, <vy>",
                         "wander": "steer wander <speed>, <force>, <spread> angle <param>"}[way]
                if not m:
                    raise AnimError(f"{where}: {usage}")
                first, second = split_top(m.group(1)), split_top(m.group(2))
                if len(first) != before or len(second) != after or not all(first) or not all(second):
                    raise AnimError(f"{where}: {usage}")
                angle = 0xFF
                if way == "wander":
                    if second[0] not in params:
                        raise AnimError(f"{where}: steer wander: angle <param>, no param '{second[0]}'")
                    angle, second = params[second[0]][0], []
                exprs = [Expr(v, defines, params, False, where, kinds=kinds, timers=timers, tables=tables, games=games).code() for v in first + second]
                code += struct.pack("<BBBB", OP["STEER"], number, angle, len(exprs)) + b"".join(exprs)
            elif word == "vanish":
                if rest:
                    raise AnimError(f"{where}: vanish, alone")
                code += bytes([OP["VANISH"]])
            elif word in ("press", "clear"):
                # a button of the map pressed as if trodden on; what lies in a cell gone: the game's world does it
                if not rest:
                    raise AnimError(f"{where}: {word} <cell>")
                code += bytes([OP[word.upper()]]) + Expr(rest, defines, params, False, where, kinds=kinds, timers=timers, tables=tables, games=games).code()
            elif word == "tell":
                words = rest.split()
                if len(words) != 2 or words[0] != "holder":
                    raise AnimError(f"{where}: tell holder <event>, or tell cell <cell>, <event>[, <value>]")
                at = len(code)
                code += struct.pack("<BBB", OP["TELL"], 0, 0xFF)
                event_fixups.append((at + 2, words[1], where))
            elif word == "masks":
                bits = 0
                for a in args:
                    bits |= 1 << mask_of(a, where)
                code += struct.pack("<BH", OP["MASKS"], bits)
            elif word == "goto" and re.search(r"\sif\s", rest):
                m = re.fullmatch(r"(\w+)\s+if\s+(.+)", rest)
                if not m:
                    raise AnimError(f"{where}: goto <label or script> if <expression>")
                e = Expr(m.group(2), defines, params, False, where, kinds=kinds, timers=timers, tables=tables, games=games)
                fixups.append((len(code), m.group(1), where, True))
                code += b"\0\0\0\0" + e.code()
            elif word == "goto":
                fixups.append((len(code), rest, where))
                code += b"\0\0\0"
            else:
                raise AnimError(f"{where}: no such statement '{word}'")

    if not scripts:
        raise AnimError("no script")
    if len(scripts) > 255:
        raise AnimError(f"{len(scripts)} scripts, 255 at most")
    for at, name, where in event_fixups:
        if name not in events and shared:
            events.append(name)  # an event of the pack: whoever is met may react to it
        if name not in events:
            raise AnimError(f"{where}: no event '{name}'")
        if events.index(name) > 254:
            raise AnimError(f"{where}: 255 events at most")
        code[at] = events.index(name)
    for at, name, where, *conditional in fixups:
        if conditional:
            # GOTO_IF: a script (1, its number) or a label (0, its offset), the expression after
            if name in script_at:
                code[at:at + 4] = struct.pack("<BBH", OP["GOTO_IF"], 1, scripts.index(name))
            elif name in labels:
                code[at:at + 4] = struct.pack("<BBH", OP["GOTO_IF"], 0, labels[name][0])
            else:
                raise AnimError(f"{where}: no script or label '{name}'")
            continue
        if name in script_at:
            # SCRIPT takes two bytes of the three kept: the third is never reached, the step goes elsewhere
            code[at:at + 3] = struct.pack("<BBx", OP["SCRIPT"], scripts.index(name))
        elif name in labels:
            code[at:at + 3] = struct.pack("<BH", OP["GOTO"], labels[name][0])
        else:
            raise AnimError(f"{where}: no script or label '{name}'")

    index = {s: i for i, s in enumerate(scripts)}
    matrix = [[(0, 0)] * len(events) for _ in scripts]
    filled = {}  # (script, event) -> where it was filled
    for event, source, target, when, where in rules:
        if event not in declared:
            raise AnimError(f"{where}: no event '{event}'")
        if target not in index:
            raise AnimError(f"{where}: no script '{target}'")
        if source in groups:
            rows = groups[source]
        elif source in index:
            rows = [source]
        else:
            raise AnimError(f"{where}: no script or group '{source}'")
        if len(set(rows)) != len(rows):
            raise AnimError(f"{where}: group '{source}' names a script twice")
        for r in rows:
            if r not in index:
                raise AnimError(f"{where}: group '{source}' has '{r}', which is no script")
            cell = (index[target] + 1, when)
            key = (r, event)
            if key in filled and matrix[index[r]][events.index(event)] != cell:
                raise AnimError(f"{where}: '{r}' on '{event}' is given otherwise at {filled[key]}")
            filled.setdefault(key, where)
            matrix[index[r]][events.index(event)] = cell

    names = sorted(params, key=lambda n: params[n][0])
    blob = struct.pack("<IHHHHIBBxx", MAGIC, VERSION, len(scripts), len(events), len(params), len(code), pockets, stack)
    blob += b"".join(struct.pack("<H", script_at[s]) for s in scripts)
    blob += b"".join(struct.pack("<h", params[n][1]) for n in names)
    blob += b"".join(struct.pack("<BB", t, w) for row in matrix for t, w in row)
    blob += bytes(code)
    blob += bytes(table_bytes)  # none: the blob as it was without tables
    return Assembled(bytes(blob), scripts, list(events), names, list(games))
