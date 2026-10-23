"""
The assembler of dialogs of DGX-GE: who says what, the answers to choose
from, and what follows in the world, from text into the blob dgx_ge_dialog.c
runs (see include/dgx_ge_dialog.h).

The language, a statement a line, '#' to the end of a line is a comment:

    dialog guard_post               its name: the context of its strings (msgctxt)
    roles guard GUARD "Guard", kid KID "Prince"   who takes part: a role, the kind (an animation of
                                    the pack) of the one who plays it (its params and pockets are what
                                    conditions read), the name it is shown by (a msgid; none: no
                                    name) and its portrait (face <picture>, a picture of the pack;
                                    none: no portrait). The role game is always there: the game itself
    game SETTINGS                   the kind of the game's actor, where the game gives one: its params are
                                    what the game's lines, conditions and controls name without game_var.
                                    (the game's own numbers are rather its variables, Pack.add_variable)
    style talk                      how its lines look from here on (a style of the pack, by name)
    pause                           the game stands while the dialog is open (a page of settings): its
                                    world does not go on; the dialog's own lines, times and controls do

    label start                     a place to go to; the dialog begins at its first statement
        say guard "Halt! Who goes there?"     a line: a msgid, the language chosen at packing
        say! guard "Read this."     an important line: shown whole (scrolled), never cut
        say game "Found!" 3s        a line that goes on by itself so long after it is seen whole
                                    (3s, 500ms); none: it waits for a button
        say game "{left} minute left" "{left} minutes left" 2s   a line of a number: {left}, a param of
                                    the one saying it, is shown as its number ({game_var.left}: a variable
                                    of the game; a line shows the one or the other); two strings: msgid and
                                    msgid_plural, the form the language gives that number (its .po).
                                    Up to four {names} in a line: a translation shows the same names,
                                    in any order (the form goes by the msgid's first)
        choose kid 10s -> silent    no answer in so long: on from that label (none: it waits)
        choose kid                  the answers of a role (the player's), one a line below,
            "A friend." -> friend   the msgid and where it leads;
            "Have a drink." if count(WINE) > 0 -> drink   shown only where the expression is not 0,
                                    of the params and pockets of the one playing the role, the game's
                                    variables (game_var.volume > 2) and its constants (Pack.add_constants:
                                    game_var.board & BOARD_PAD)
        choose game                 a page of settings: answers with controls
            "Sound" control SWITCH game_var.sound   a control (a kind: its first param is its value) of a
                                    variable of the game (or of a param of the one choosing: control
                                    SWITCH sound); activate, more, less go to it (the pack's events of
                                    those names); without -> the choice stays
            "Name" field name -> back      a field of the game (Pack.add_field): its text shown at the
                                    right of the line; activate edits it (typed, a keyboard on the
                                    display or of wires); with -> on from there when it is done
        tell guard calm             an event for the one playing a role (the pack's events)
        give kid WINE 1 to guard    so many of a kind from the pockets of one into the other's
        goto start                  on from a label
        end                         the dialog is over

A line of a choice is told by its indent: two spaces more than choose. A
string is in double quotes; \\" and \\\\ inside. What a msgid becomes is the
packer's (Strings): its translation into the language of the pack, or the
msgid itself.
"""

import re
import struct
import unicodedata
from collections import namedtuple

from dgx_ge_anim import AnimError, Expr

MAGIC = 0x4C444744  # "DGDL"
PAUSE = 1  # the flags of a dialog (DGX_GE_DIALOG_PAUSE): the game stands while it is open
VERSION = 7
OP = {"SAY": 1, "CHOOSE": 2, "TELL": 3, "GIVE": 4, "GOTO": 5, "END": 6, "STYLE": 7, "SAY_N": 8, "SAY_NS": 9}
TALK_NUMBERS = 4  # so many numbers a line shows at most (dgx_ge_dialog.h)
GAME_ROLE = 0xFF  # the game itself, no actor
NO_KIND = 0xFFFF
FIELD = 0xFFFE  # the kind of a control that is a field of the game (DGX_GE_DIALOG_FIELD): its param, the field's number
STAYS = 0xFFFF  # an answer that leads nowhere: its control changed, the choice stays
UI_EVENTS = ("activate", "more", "less")  # the events of the interface a control is told
SAY_VARS = 2  # the flags of a line (1: important): its numbers are the game's variables
ANSWER_VAR = 1  # the flags of an answer: its control edits a variable of the game
SHOWN = r"\{((?:game_var\.)?\w+)\}"  # a number a line shows: {name}, a param of the one saying it; {game_var.name}, a variable of the game

Dialog = namedtuple("Dialog", "blob name roles labels")


class DialogError(Exception):
    pass


def _string(text, where):
    """A string in double quotes at the start of text: (the string, the rest)."""
    m = re.match(r'\s*"((?:[^"\\]|\\.)*)"(.*)$', text)
    if not m:
        raise DialogError(f'{where}: a string in double quotes, please')
    return re.sub(r"\\(.)", r"\1", m.group(1)), m.group(2)


def _ms(text, where):
    """A time: 3s, 500ms; 1..65535 ms."""
    import re as _re
    m = _re.fullmatch(r"(\d+)(ms|s)", text.strip())
    if not m:
        raise DialogError(f"{where}: a time, 3s or 500ms")
    ms = int(m.group(1)) * (1000 if m.group(2) == "s" else 1)
    if not 1 <= ms <= 65535:
        raise DialogError(f"{where}: a time of 1 ms .. 65 s")
    return ms


def _numbers_by_name(order, where):
    """
    What a translation of a line of numbers is stored as: its {names} must be those of the line (order: its msgid's);
    in the line's order it stays as it is (the engine shows the next number at each {name}), else each {name} becomes
    {i}, its place in that order (the engine shows that number), so a language may put them in its own order.
    """
    def shape(text):
        shown = re.findall(SHOWN, text)
        for name in shown:
            if name not in order:
                raise DialogError(f"{where}: '{text}' shows {{{name}}}, a number the line has not")
        if all(name == order[min(k, len(order) - 1)] for k, name in enumerate(shown)):
            return text
        return re.sub(SHOWN, lambda m: "{%d}" % order.index(m.group(1)), text)
    return shape


def assemble_dialog(text, strings, kinds=None, params=None, events=None, styles=None, images=None, timers=None, constants=None, fields=None,
                    game_vars=None):
    """
    The blob of a dialog: Dialog(blob, name, roles, labels).
    strings: a Strings of the pack (msgid in a context to a number).
    kinds: names of kinds (animations of the pack) to their numbers;
    params: names of kinds to {param: (index, initial)} of each, for conditions;
    events: the events of the pack, by name (a list given on: tell names one);
    styles: the styles of the pack, by name (a list given on: a new name is added);
    images: names of the pictures of the pack to their numbers, for portraits;
    timers: the names of the pack's timers, for timer(NAME) in conditions (a list given on: a new name is added);
    constants: the game's names of numbers (Pack.add_constants), for conditions: board & BOARD_PAD;
    fields: the names of the game's text fields to their numbers (Pack.add_field), for answers with a field;
    game_vars: the names of the game's variables (Pack.add_variable), by their numbers: game_var.<name> in conditions, a
    control of one, {game_var.<name>} in a line.
    """
    game_vars = list(game_vars or [])
    images = images or {}
    kinds = kinds or {}
    params = params or {}
    events = events if events is not None else []
    styles = styles if styles is not None else []
    name = None
    game_kind = None  # the name of the kind of the game's actor
    controls = False
    flags = 0  # PAUSE
    roles, role_kinds, role_names, role_faces = [], [], [], []  # names, kinds, names shown (strings), portraits (pictures); 0xFFFF none
    code = bytearray()
    labels = {}  # name -> offset
    fixups = []  # (offset of a target u16, label, where)
    choosing = None  # (offset of its count, indent, role) while lines of a choose follow

    def role_of(r, where, game=True):
        if game and r == "game":
            return GAME_ROLE
        if r not in roles:
            raise DialogError(f"{where}: no role '{r}'")
        return roles.index(r)

    # in NFC, as the fonts have their letters and the .po its msgids (read_po): a text saved decomposed (NFD, macOS) draws so too
    for number, raw in enumerate(unicodedata.normalize("NFC", text).splitlines(), 1):
        where = f"line {number}"
        line = raw
        # a comment: '#' outside a string
        out, quoted, esc = [], False, False
        for c in line:
            if c == "#" and not quoted:
                break
            out.append(c)
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                quoted = not quoted
        line = "".join(out).rstrip()
        if not line.strip():
            continue
        indent = len(line) - len(line.lstrip(" "))
        body = line.strip()
        if choosing is not None and indent > choosing[1] and body.startswith('"'):
            # an answer of the choose above: "msgid" [if expr] [control KIND param | field NAME] [-> label]
            msgid, rest = _string(body, where)
            m = re.fullmatch(r"\s*(?:if\s+(.+?))?\s*(?:control\s+(\w+)\s+((?:game_var\.)?\w+)|field\s+(\w+))?\s*(?:->\s*(\w+))?\s*", rest)
            if not m or not (m.group(2) or m.group(4) or m.group(5)):
                raise DialogError(f'{where}: "<msgid>" [if <expression>] [control <kind> <param> | field <name>] [-> <label>]: '
                                  'a control, a field or a label at least')
            if choosing[2] == GAME_ROLE:
                kind_name = game_kind
            else:
                kind_name = next((n for n, v in kinds.items() if v == role_kinds[choosing[2]]), None)
            control, param, answer_flags = NO_KIND, 0, 0
            if m.group(2):
                if m.group(2) not in kinds:
                    raise DialogError(f"{where}: no kind '{m.group(2)}'")
                own = params.get(kind_name, {})
                if m.group(3).startswith("game_var."):
                    if m.group(3)[9:] not in game_vars:
                        raise DialogError(f"{where}: {m.group(3)}: no such variable of the game (Pack.add_variable)")
                    control, param, answer_flags, controls = kinds[m.group(2)], game_vars.index(m.group(3)[9:]), ANSWER_VAR, True
                elif m.group(3) not in own:
                    raise DialogError(f"{where}: no param '{m.group(3)}' of {kind_name or 'the one choosing'}")
                else:
                    control, param, controls = kinds[m.group(2)], own[m.group(3)][0], True
            if m.group(4):
                if m.group(4) not in (fields or {}):
                    raise DialogError(f"{where}: no field '{m.group(4)}' (Pack.add_field)")
                control, param = FIELD, fields[m.group(4)]
            expr = b"\0"
            if m.group(1):
                try:
                    expr = Expr(m.group(1), dict(constants or {}), params.get(kind_name, {}), False, where, kinds=kinds, timers=timers,
                                game_vars=game_vars).code()
                except AnimError as e:
                    raise DialogError(str(e))
            code[choosing[0]] += 1
            if code[choosing[0]] == 0:
                raise DialogError(f"{where}: 255 answers at most")
            at = len(code)
            code += struct.pack("<HHHBB", strings.id(name, msgid), STAYS, control, param, answer_flags)
            if m.group(5):
                fixups.append((at + 2, m.group(5), where))
            code += expr
            continue
        choosing = None
        word, _, rest = body.partition(" ")
        rest = rest.strip()
        if word == "dialog":
            if name is not None or not re.fullmatch(r"\w+", rest):
                raise DialogError(f"{where}: dialog <name>, once, first")
            name = rest
            continue
        if name is None:
            raise DialogError(f"{where}: dialog <name> first")
        if word == "roles":
            for part in (p.strip() for p in re.findall(r'(?:[^,"]|"(?:[^"\\]|\\.)*")+', rest)):
                m = re.fullmatch(r'(\w+)\s+(\w+)(\s+"(?:[^"\\]|\\.)*")?(?:\s+face\s+(\w+))?', part)
                if not m or m.group(1) in roles or m.group(1) == "game":
                    raise DialogError(f'{where}: roles <role> <kind> ["<name>"] [face <picture>], ...: a new name, not game')
                if m.group(4) and m.group(4) not in images:
                    raise DialogError(f"{where}: no picture '{m.group(4)}' for a face")
                role_faces.append(images[m.group(4)] if m.group(4) else NO_KIND)
                if m.group(2) not in kinds:
                    raise DialogError(f"{where}: no kind '{m.group(2)}'")
                roles.append(m.group(1))
                role_kinds.append(kinds[m.group(2)])
                shown = 0xFFFF
                if m.group(3):
                    msgid, tail = _string(m.group(3), where)
                    if tail.strip():
                        raise DialogError(f"{where}: '{tail.strip()}' after the name")
                    shown = strings.id(name, msgid)
                role_names.append(shown)
            if len(roles) > 8:
                raise DialogError(f"{where}: 8 roles at most")
        elif word == "game":
            if rest not in kinds or game_kind is not None:
                raise DialogError(f"{where}: game <kind>, once: a kind of the pack")
            game_kind = rest
        elif word == "pause":
            if rest or flags & PAUSE:
                raise DialogError(f"{where}: pause, once, alone on its line")
            flags |= PAUSE
        elif word == "label":
            if not re.fullmatch(r"\w+", rest) or rest in labels:
                raise DialogError(f"{where}: bad or repeated label '{rest}'")
            labels[rest] = len(code)
        elif word == "style":
            if not re.fullmatch(r"\w+", rest):
                raise DialogError(f"{where}: style <name>")
            if rest not in styles:
                styles.append(rest)
            code += struct.pack("<BH", OP["STYLE"], styles.index(rest))
        elif word in ("say", "say!"):
            m = re.fullmatch(r"(\w+)\s+(.+)", rest)
            if not m:
                raise DialogError(f'{where}: say <role> "<msgid>"')
            msgid, tail = _string(m.group(2), where)
            plural = None
            if tail.strip().startswith('"'):
                plural, tail = _string(tail, where)
            ms = _ms(tail, where) if tail.strip() else 0
            r = role_of(m.group(1), where)
            shows = set(re.findall(SHOWN, msgid + (plural or "")))
            if plural is None and not shows:
                code += struct.pack("<BBBHH", OP["SAY"], r, 1 if word == "say!" else 0, strings.id(name, msgid), ms)
                continue
            kind_name = game_kind if r == GAME_ROLE else next((n for n, v in kinds.items() if v == role_kinds[r]), None)
            own = params.get(kind_name, {})
            # the numbers a line shows: params of the one saying it, or the game's variables ({game_var.<name>}), not both
            of_vars = {n.startswith("game_var.") for n in shows}
            if len(of_vars) > 1:
                raise DialogError(f"{where}: a line shows params of the one saying it or variables of the game, not both")
            line_flags = (1 if word == "say!" else 0) | (SAY_VARS if of_vars == {True} else 0)
            if of_vars == {True}:
                own = {}
                for n in shows:
                    if n[9:] not in game_vars:
                        raise DialogError(f"{where}: {{{n}}}: no such variable of the game (Pack.add_variable)")
                    own[n] = (game_vars.index(n[9:]), 0)
            order = re.findall(SHOWN, msgid)
            if len(order) > 1:
                # a line of several numbers: each {name} in order, the form by the first; its plural the same names in order
                if len(order) > TALK_NUMBERS:
                    raise DialogError(f"{where}: a line shows {TALK_NUMBERS} numbers at most")
                if plural is not None and re.findall(SHOWN, plural) != order:
                    raise DialogError(f"{where}: the forms of a line show the same numbers in the same order")
                for shown in order:
                    if shown not in own:
                        raise DialogError(f"{where}: no param '{shown}' of {kind_name or 'the one saying it'}")
                shape = _numbers_by_name(order, where)
                first, forms = strings.id_plural(name, msgid, plural, shape) if plural is not None else (strings.id(name, msgid, shape), 1)
                code += struct.pack("<BBBHHBB", OP["SAY_NS"], r, line_flags, first, ms, forms, len(order))
                code += bytes(own[shown][0] for shown in order)
                continue
            # a line of a number: the param of the one saying it, the forms of the language
            if len(shows) != 1:
                raise DialogError(f"{where}: a line of a number shows one param of the one saying it: {{name}}")
            shown = shows.pop()
            if shown not in own:
                raise DialogError(f"{where}: no param '{shown}' of {kind_name or 'the one saying it'}")
            shape = _numbers_by_name([shown], where)
            first, forms = strings.id_plural(name, msgid, plural, shape) if plural is not None else (strings.id(name, msgid, shape), 1)
            code += struct.pack("<BBBHHBB", OP["SAY_N"], r, line_flags, first, ms, forms, own[shown][0])
        elif word == "choose":
            m = re.fullmatch(r"(\w+)(?:\s+(\S+)\s*->\s*(\w+))?", rest)
            if not m:
                raise DialogError(f"{where}: choose <role> [<time> -> <label>]")
            r = role_of(m.group(1), where)
            ms = _ms(m.group(2), where) if m.group(2) else 0
            code += struct.pack("<BBBHH", OP["CHOOSE"], r, 0, ms, 0)
            if m.group(3):
                fixups.append((len(code) - 2, m.group(3), where))
            choosing = (len(code) - 5, indent, r)
        elif word == "tell":
            m = re.fullmatch(r"(\w+)\s+(\w+)", rest)
            if not m:
                raise DialogError(f"{where}: tell <role> <event>")
            if m.group(2) not in events:
                events.append(m.group(2))  # an event of the pack: the kind told may answer it
            if events.index(m.group(2)) > 254:
                raise DialogError(f"{where}: 255 events at most")
            code += struct.pack("<BBB", OP["TELL"], role_of(m.group(1), where, game=False), events.index(m.group(2)))
        elif word == "give":
            m = re.fullmatch(r"(\w+)\s+(\w+)\s+(\d+)\s+to\s+(\w+)", rest)
            if not m:
                raise DialogError(f"{where}: give <role> <kind> <count> to <role>")
            if m.group(2) not in kinds:
                raise DialogError(f"{where}: no kind '{m.group(2)}'")
            n = int(m.group(3))
            if not 1 <= n <= 255:
                raise DialogError(f"{where}: give 1..255")
            code += struct.pack("<BBBHB", OP["GIVE"], role_of(m.group(1), where, game=False), role_of(m.group(4), where, game=False),
                                kinds[m.group(2)], n)
        elif word == "goto":
            if not re.fullmatch(r"\w+", rest):
                raise DialogError(f"{where}: goto <label>")
            code += struct.pack("<BH", OP["GOTO"], 0)
            fixups.append((len(code) - 2, rest, where))
        elif word == "end":
            code.append(OP["END"])
        else:
            raise DialogError(f"{where}: no such statement '{word}'")
    if name is None:
        raise DialogError("no dialog")
    for at, label, where in fixups:
        if label not in labels:
            raise DialogError(f"{where}: no label '{label}'")
        code[at:at + 2] = struct.pack("<H", labels[label])
    if len(code) > 0xFFFF:
        raise DialogError(f"dialog {name}: too long")
    if not code:
        raise DialogError(f"dialog {name}: nothing in it")
    start = 0
    ui = bytearray(b"\xff" * 4)  # the numbers of the events activate, more, less (0xFF none): a control is told them
    for i, e in enumerate(UI_EVENTS if controls else ()):
        if e not in events:
            events.append(e)
        if events.index(e) > 254:
            raise DialogError(f"dialog {name}: 255 events at most")
        ui[i] = events.index(e)
    blob = struct.pack("<IHBBHH", MAGIC, VERSION, len(roles), flags, len(code), start) + bytes(ui)
    blob += b"".join(struct.pack("<H", k) for k in role_kinds) + b"".join(struct.pack("<H", n) for n in role_names)
    blob += b"".join(struct.pack("<H", f) for f in role_faces) + bytes(code)
    return Dialog(blob, name, roles, labels)


class Strings:
    """
    The strings of a pack in one language: msgid in a context (the name of a
    dialog) to a number. A translation (gettext .po: msgctxt, msgid, msgstr)
    gives what it becomes; none, it stays as it is. Equal strings are kept
    once. A string of a number (msgid, msgid_plural) is as many strings in a
    row as the language has forms (its .po header: Plural-Forms; none: two,
    plural=n != 1), and the rule of the language which of them a number takes
    goes with them. The blob ("DGSS", little-endian): "DGSS", version u16,
    count u16, the offset of the rule u32 (0: none, n != 1), then offsets u32
    of each string from the start of the blob, then the strings, UTF-8, each
    ending in 0, then the rule (an expression of the engine, n its param 0).
    """

    MAGIC = 0x53534744  # "DGSS"
    VERSION = 2

    def __init__(self, translations=None):
        self.translations = translations or {}  # (context, msgid) -> msgstr, or the list of its forms; ("", "") the header
        self.texts = []   # the strings, in the language
        self.index = {}   # string -> its number
        self.used = []    # (context, msgid, msgid_plural or None) in the order met: the template of translations

    def id(self, context, msgid, shape=None):
        """A string: its number. shape: what the language's text is stored as (a line of numbers: by their names)."""
        if (context, msgid, None) not in self.used:
            self.used.append((context, msgid, None))
        text = self.translations.get((context, msgid)) or msgid
        if isinstance(text, list):
            raise ValueError(f"{context}: '{msgid}' is translated as a string of a number (msgid_plural)")
        # a line of no numbers shows its {names} as they are written: a translation may not show one the line has not
        unknown = shape is None and next((n for n in re.findall(SHOWN, text) if n not in re.findall(SHOWN, msgid)), None)
        if unknown:
            raise DialogError(f"{context}: '{text}' shows {{{unknown}}}, a number the line '{msgid}' has not")
        text = shape(text) if shape else text
        if text not in self.index:
            self.index[text] = len(self.texts)
            self.texts.append(text)
        return self.index[text]

    def forms(self):
        """How many forms a string of a number has in the language, and the rule which one n takes (C, as in .po)."""
        header = self.translations.get(("", ""), "")
        m = re.search(r"Plural-Forms:\s*nplurals\s*=\s*(\d+)\s*;\s*plural\s*=\s*([^;\n]+)", header)
        return (int(m.group(1)), m.group(2).strip()) if m else (2, "n != 1")

    def id_plural(self, context, msgid, plural, shape=None):
        """A string of a number: the number of its first form and how many there are (in a row); shape as id's."""
        if (context, msgid, plural) not in self.used:
            self.used.append((context, msgid, plural))
        count, _ = self.forms()
        text = self.translations.get((context, msgid))
        if not isinstance(text, list) or not any(text):
            text = [msgid] + [plural] * (count - 1)  # not translated: the first form for one, the other for the rest
        if len(text) != count:
            raise ValueError(f"{context}: '{msgid}' has {len(text)} forms, the language {count}")
        text = [shape(t) for t in text] if shape else text
        first = len(self.texts)
        for t in text:
            self.texts.append(t)
            self.index.setdefault(t, len(self.texts) - 1)
        return first, count

    def characters(self):
        """Every character the strings use: for the font (font2c -f)."""
        return "".join(sorted(set("".join(self.texts))))

    def rule(self):
        """The rule of forms as an expression of the engine (n its param 0), None for n != 1."""
        from dgx_ge_anim import Expr
        _, text = self.forms()
        if re.fullmatch(r"\(?\s*n\s*!=\s*1\s*\)?", text):
            return None
        # C as gettext writes it into the words of the engine's expressions
        text = text.replace("&&", " and ").replace("||", " or ")
        text = re.sub(r"!(?!=)", " not ", text)
        return Expr(text, {}, {"n": (0, 0)}, False, "Plural-Forms").code()

    def blob(self):
        data = [t.encode("utf-8") + b"\0" for t in self.texts]
        at, offsets = 12 + 4 * len(data), []
        for d in data:
            offsets.append(at)
            at += len(d)
        rule = self.rule()
        out = struct.pack("<IHHI", self.MAGIC, self.VERSION, len(data), at if rule else 0)
        return out + b"".join(struct.pack("<I", o) for o in offsets) + b"".join(data) + (rule or b"")

    def pot(self):
        """The template of translations: every msgid in its context, msgstr empty (msgid_plural: msgstr[0], [1])."""
        def q(s):
            return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
        # the header: the text is UTF-8 (msginit and the editors of translations take the charset from it)
        out = 'msgid ""\nmsgstr ""\n"Content-Type: text/plain; charset=UTF-8\\n"\n\n'
        for c, m, plural in self.used:
            if plural is None:
                out += f"msgctxt {q(c)}\nmsgid {q(m)}\nmsgstr \"\"\n\n"
            else:
                out += f"msgctxt {q(c)}\nmsgid {q(m)}\nmsgid_plural {q(plural)}\nmsgstr[0] \"\"\nmsgstr[1] \"\"\n\n"
        return out


def read_po(path):
    """
    A gettext .po file: {(msgctxt, msgid): msgstr} of the entries translated; a string of a number (msgid_plural):
    the list of its forms (msgstr[0], [1], ...); the header (msgid ""): under ("", ""). Its text in NFC (a letter and
    its accent as one character, as fonts have them), a BOM at its start skipped.
    """
    entries, entry, field = {}, {}, None

    def unq(s):
        return re.sub(r"\\(.)", lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), s.strip()[1:-1])

    def done():
        if "msgid" not in entry:
            return
        key = (entry.get("msgctxt", ""), entry["msgid"])
        forms = sorted((int(k[7:-1]), v) for k, v in entry.items() if k.startswith("msgstr["))
        if forms and any(v for _, v in forms):
            entries[key] = [v for _, v in forms]
        elif entry.get("msgstr") and (entry["msgid"] or "msgctxt" not in entry):
            entries[key] = entry["msgstr"]

    with open(path, encoding="utf-8-sig") as f:
        lines = unicodedata.normalize("NFC", f.read()).split("\n")
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("#"):
            if not line and entry:
                done()
                entry, field = {}, None
            continue
        m = re.match(r"(msgctxt|msgid_plural|msgid|msgstr\[\d+\]|msgstr)\s+(\".*\")$", line)
        if m:
            if m.group(1) == "msgctxt" or (m.group(1) == "msgid" and "msgctxt" not in entry and entry):
                if entry and ("msgstr" in entry or any(k.startswith("msgstr[") for k in entry)):
                    done()
                    entry = {}
            field = m.group(1)
            entry[field] = unq(m.group(2))
        elif line.startswith('"') and field:
            entry[field] += unq(line)
    done()
    return entries
