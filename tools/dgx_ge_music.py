"""
The assembler of music by notes of DGX-GE: a text in MML (the language of
notes of the old sound chips' music, MSX's PLAY "T150O4L8CDEFG") into the
song the engine's mixer plays (src/dgx_ge_music.c; Pack.add_music).

A line is a setting of the song (#...), an instrument or a channel's notes;
';' to the end of a line is a comment.

    #synth nes                      the synthesizer: chip (unless said), nes or fm
    #set chip                       a set of instruments the song takes its own from (tools/music_sets/)
    #tail 300ms                     once every channel's notes are over, so long more, then the song ends
    @1 = square 25% attack 2 decay 40 sustain 8 release 30
    @2 bass = triangle release 80
    @3 = noise volume 10 decay 60 sustain 0
    A t150 o4 l8 @1 v12 c d e f g4 r4 [c e g]2
    A $ c d e f                     more of channel A, after the line before
    B @bass o2 l4 c g c g
    CD r1                           the same notes for channels C and D

The synthesizer (#synth, once, before the notes): chip, the engine's own (a
square, a triangle, noise); nes, the NES's (2A03: a pulse of four duties,
its stepped triangle, noise of two modes, 16 levels of volume); fm, two
operators as the OPL2's (AdLib). Unsaid: the one the project chose
(assemble(synth=), Pack.add_music(synth=), a document's "synth"), else chip.

An instrument, @0..@99, may have a name (a letter, then letters, digits, _ or
-): a channel says @2 or @bass. On the chip and the NES: a wave (square of
12.5%, 25% or 50%, 50% unless said, and on the NES 75%; triangle; noise, on
the NES long or short, long unless said) and its envelope: attack (ms from
silence to the top), decay (ms from the top down to the sustain), sustain
(the level held while the note sounds, 0..15 of the top), release (ms from
where it is to silence, once the note ends); and its volume, 0..15. Unsaid:
volume 15, attack 0, decay 0, sustain 15, release 0. The NES's triangle has
no volume: it sounds whole while its envelope is not silent.

On FM, two operators, the modulator (mod) and the carrier (car), each in
brackets, then the instrument's own:

    @3 = fm mod(mult 1 level 30 attack 2 decay 40 sustain 8 release 30 wave sine feedback 5)
            car(mult 2 level 0 attack 1 decay 200 sustain 10 release 80 percussive) connect fm volume 12

(one line). An operator: mult, its frequency to the note's (0.5, 1..10, 12,
15); level, 0..63, quieter by 0.75 dB a step (63: 47 dB); ksl, quieter the
higher the note, 0, 1.5, 3 or 6 dB an octave; attack, ms from silence to the
top; decay, ms of a fall of 96 dB, at which it falls to its sustain; sustain,
0..15, 3 dB a step down from the top (15; 0 is 93 dB down); release, ms of a
fall of 96 dB, at which it dies once the note ends; never for a time: that
stage does not move (an attack never: the operator is silent); wave sine,
half-sine, abs-sine or quarter-sine; percussive: it does not hold at its
sustain but goes on dying at its release's pace; ksr: its envelope much
faster the higher the note (without, a little faster, as on the chip);
vibrato (7 cents at 6.1 Hz), tremolo (1 dB at 3.7 Hz); the modulator also
feedback 0..7 (its own output into its phase: 7 is 4 pi). Unsaid: mult 1,
level 0, ksl 0, attack 0, decay 0, sustain 15, release 0, sine; no mod():
the modulator is silent, the carrier a sine. Then connect fm (the modulator
into the carrier's phase, unless said) or add (both heard), volume 0..15.

@0, if neither the text nor its set defines it, is a plain square of 50%
(chip, NES) or a sine (FM).

A set (#set name; once): a text of instruments only, its #synth first
(tools/music_sets/<name>.mml of the engine, or the project's: assemble(sets=),
Pack.add_music_set, music/sets/<name>.mml beside a document). The song may
use any of them by number or name; one it defines itself takes the place of
the set's of that number. A set of the chip's serves the NES too. Of a set,
only the instruments the song uses go into it.

A channel: its name, capitals A..H (several: the same notes for each), then
its commands, case not counting:

    c d e f g a b   a note, + or # sharp, - flat; its length after it (c4, c+8., a-16)
    r               a rest, its length after it (r2)
    1 2 4 8 16 32   lengths: a whole note, a half...; also 3 6 12 24 48 (triplets) and 64;
                    each dot adds half of what the one before it added (c4. = c4 + c8)
    &               joins two notes: the same pitch, one longer note (c4&c8); another
                    pitch, the second goes on from the first without a new attack (c4&d4)
    o4              the octave, 0..9 (o4 a: 440 Hz); < one down, > one up
    l8              the length of notes written without one (dots too: l8.)
    t150            the tempo, quarter notes a minute (1..999), of the whole song from here on
    v12             the volume of the channel, 0..15
    @1, @bass       the instrument (a name ends at a space)
    q6              the gate, 1..8: the note sounds 6/8 of its length, then its release
    [ ... ]3        so many times (2 when no number); [ ... | ... ]: the part after | is
                    left out the last time
    $               where the channel goes round from at its end; none: it ends

A song keeps time in ticks, 48 a quarter note (192 a whole), so every length
above is whole. Repeats are written out (the engine never counts them).
Errors name the line and the column.

The song (DGMU), little-endian. Version 1 (the chip's, no tail):

    'DGMU' u32, version u16, channels u8, instruments u8, tempo u16 (to begin with)
Version 2 (another synthesizer, or a tail), the same and then:
    synth u8 (0 chip, 1 nes, 2 fm), 0 u8, tail u16 (ms; 0xFFFF: none, the notes ring out)
Then:
    instruments, of the chip and the NES 10 bytes each: wave u8 (0 square 12.5%, 1 square 25%,
        2 square 50%, 3 triangle, 4 noise, 5 square 75%, 6 noise short), volume u8, sustain u8,
        0, attack u16, decay u16, release u16 (ms)
    of FM 28 bytes each: connect u8 (0 fm, 1 add), volume u8, feedback u8, 0, then the
        modulator and the carrier, 12 bytes each: multiplier * 2 u8, level u8 (0..63),
        ksl u8 (0 none, 1 1.5, 2 3, 3 6 dB an octave), flags u8 (1 percussive, 2 ksr,
        4 vibrato, 8 tremolo), wave u8 (0 sine, 1 half-sine, 2 abs-sine, 3 quarter-sine),
        sustain u8 (0..15, 15 the top), attack u16, decay u16, release u16 (ms; 0xFFFF never)
    channels, 8 bytes each: start u32, loop u32 (0xFFFFFFFF: none), offsets from the song's start
    the channels' events:
        0x00..0x7F len    a note: its MIDI number (69: o4 a), its length in ticks
        0x80 len          a rest
        0x81              the next note goes on into the one after it (&: no release, no attack)
        0x82 n            the instrument (its place in the song's table)
        0x83 v            the volume, 0..15
        0x84 u16          the tempo
        0x85 q            the gate, 1..8
        0xFF              the end: to the loop, or the channel ends
    len: one byte 1..127, or two (0x80 | high, low) up to 32767

Run alone, it assembles a file into a song:

    python3 dgx_ge_music.py song.mml [song.dgmu] [--synth fm] [--sets dir]
"""

import os
import re
import struct
import sys

MAGIC = 0x554D4744  # "DGMU"
VERSION = 1
TICKS = 192  # a whole note
CHANNELS = "ABCDEFGH"
SYNTHS = {"chip": 0, "nes": 1, "fm": 2}
WAVES = {"square": 2, "triangle": 3, "noise": 4}
DUTIES = {"12.5%": 0, "25%": 1, "50%": 2}
NES_DUTIES = {"12.5%": 0, "25%": 1, "50%": 2, "75%": 5}
NES_NOISES = {"long": 4, "short": 6}
NOTES = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}
NOTE, REST, SLUR, INSTRUMENT, VOLUME, TEMPO, GATE, END = 0x00, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0xFF
LONGEST = 32767
DEFAULT = (2, 15, 0, 0, 15, 0)  # wave, volume, attack, decay, sustain, release
NO_TAIL = 0xFFFF
NEVER = 0xFFFF  # an FM stage's time: it does not move
MULTS = {"0.5": 1, **{str(n): 2 * n for n in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 15)}}
KSL = {"0": 0, "1.5": 1, "3": 2, "6": 3}
FM_WAVES = {"sine": 0, "half-sine": 1, "abs-sine": 2, "quarter-sine": 3}
FM_FLAGS = {"percussive": 1, "ksr": 2, "vibrato": 4, "tremolo": 8}
SETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "music_sets")
NAME = r"[a-z][a-z0-9_-]*"


class MusicError(ValueError):
    pass


def _at(line, col):
    return f"line {line}, column {col}"


class FM:
    """An instrument of FM: its connection, volume, feedback and two operators (dicts of their settings)."""

    OP = {"mult": 2, "level": 0, "ksl": 0, "flags": 0, "wave": 0, "sustain": 15, "attack": 0, "decay": 0, "release": 0}

    def __init__(self, connect=0, volume=15, feedback=0, mod=None, car=None):
        self.connect, self.volume, self.feedback = connect, volume, feedback
        self.mod = dict(self.OP, attack=NEVER) if mod is None else dict(self.OP, **mod)  # none: a silent modulator
        self.car = dict(self.OP, **(car or {}))

    def bytes(self):
        out = struct.pack("<BBBB", self.connect, self.volume, self.feedback, 0)
        for o in (self.mod, self.car):
            out += struct.pack("<BBBBBBHHH", o["mult"], o["level"], o["ksl"], o["flags"], o["wave"], o["sustain"], o["attack"], o["decay"], o["release"])
        return out

    def __eq__(self, other):
        return isinstance(other, FM) and self.bytes() == other.bytes()

    def __hash__(self):
        return hash(self.bytes())


def _tone(words, line, col, synth):
    """wave [duty or mode] then key value pairs: (wave, volume, attack, decay, sustain, release)."""
    if not words or words[0][0].lower() not in WAVES:
        fm = " (an instrument of FM: fm mod(...) car(...))" if synth == "fm" else ""
        raise MusicError(f"{_at(line, col)}: an instrument is a wave first: square, triangle or noise{fm}")
    wave = WAVES[words[0][0].lower()]
    i = 1
    if wave == 2 and i < len(words) and words[i][0].endswith("%"):
        duties = NES_DUTIES if synth == "nes" else DUTIES
        if words[i][0] not in duties:
            nes = "" if synth == "nes" else " (75% is the NES's: #synth nes)" if words[i][0] == "75%" else ""
            raise MusicError(f"{_at(line, words[i][1])}: a square of {', '.join(list(duties)[:-1])} or {list(duties)[-1]}, not {words[i][0]}{nes}")
        wave = duties[words[i][0]]
        i += 1
    elif wave == 4 and i < len(words) and words[i][0].lower() in NES_NOISES:
        if synth != "nes":
            raise MusicError(f"{_at(line, words[i][1])}: noise {words[i][0]} is the NES's (#synth nes)")
        wave = NES_NOISES[words[i][0].lower()]
        i += 1
    v = dict(zip(("volume", "attack", "decay", "sustain", "release"), DEFAULT[1:]))
    seen = set()
    while i < len(words):
        key, kc = words[i][0].lower(), words[i][1]
        if key not in v or key in seen:
            raise MusicError(f"{_at(line, kc)}: '{words[i][0]}': volume, attack, decay, sustain or release, once each")
        if i + 1 >= len(words):
            raise MusicError(f"{_at(line, kc)}: {key} <number>")
        text, vc = words[i + 1]
        m = re.fullmatch(r"(\d+)(ms)?", text.lower())
        high = 15 if key in ("volume", "sustain") else 65535
        if not m or (m.group(2) and key in ("volume", "sustain")) or int(m.group(1)) > high:
            raise MusicError(f"{_at(line, vc)}: {key} 0..{high}{' (ms)' if high > 15 else ''}, not {text}")
        v[key] = int(m.group(1))
        seen.add(key)
        i += 2
    return (wave, v["volume"], v["attack"], v["decay"], v["sustain"], v["release"])


def _fm_tokens(text, col):
    """Words and brackets of an instrument of FM, each with its column."""
    return [(t.group(0), col + t.start()) for t in re.finditer(r"[()]|[^\s()]+", text)]


def _fm(text, line, col):
    """fm mod(...) car(...) connect fm|add volume n: an instrument of FM."""
    tokens = _fm_tokens(text, col)
    if not tokens or tokens[0][0].lower() != "fm":
        raise MusicError(f"{_at(line, tokens[0][1] if tokens else col)}: an instrument of FM: fm mod(...) car(...) connect fm volume 15 "
                         "(FM is the synthesizer when a song and its project name none: #synth chip or nes for square, triangle, noise)")
    i, ops, own, seen = 1, {}, {"connect": 0, "volume": 15}, set()

    def number(key, low, high, k):
        if k >= len(tokens):
            raise MusicError(f"{_at(line, tokens[k - 1][1])}: {key} <number>")
        t, c = tokens[k]
        if not re.fullmatch(r"\d+", t) or not low <= int(t) <= high:
            raise MusicError(f"{_at(line, c)}: {key} {low}..{high}, not {t}")
        return int(t)

    while i < len(tokens):
        word, wc = tokens[i][0].lower(), tokens[i][1]
        if word in ("mod", "car"):
            if word in ops:
                raise MusicError(f"{_at(line, wc)}: {word}(...) once")
            if i + 1 >= len(tokens) or tokens[i + 1][0] != "(":
                raise MusicError(f"{_at(line, wc)}: {word}( its settings )")
            j = i + 2
            while j < len(tokens) and tokens[j][0] not in "()":
                j += 1
            if j >= len(tokens) or tokens[j][0] != ")":
                raise MusicError(f"{_at(line, tokens[i + 1][1])}: a ( without its )")
            ops[word] = _fm_op(tokens[i + 2:j], word, line)
            i = j + 1
        elif word in ("connect", "volume"):
            if word in seen:
                raise MusicError(f"{_at(line, wc)}: {word} once")
            seen.add(word)
            if word == "connect":
                if i + 1 >= len(tokens) or tokens[i + 1][0].lower() not in ("fm", "add"):
                    raise MusicError(f"{_at(line, tokens[min(i + 1, len(tokens) - 1)][1])}: connect fm (the modulator into the carrier) or add (both heard)")
                own["connect"] = 0 if tokens[i + 1][0].lower() == "fm" else 1
            else:
                own["volume"] = number("volume", 0, 15, i + 1)
            i += 2
        else:
            raise MusicError(f"{_at(line, wc)}: '{tokens[i][0]}': mod(...), car(...), connect or volume")
    mod = ops.get("mod")
    feedback = mod.pop("feedback", 0) if mod else 0
    return FM(own["connect"], own["volume"], feedback, mod, ops.get("car"))


def _fm_op(tokens, which, line):
    """The settings of an operator: {key: value} as the song's bytes keep them."""
    out, seen, i = {}, set(), 0
    keys = ["mult", "level", "ksl", "attack", "decay", "sustain", "release", "wave"] + (["feedback"] if which == "mod" else [])
    while i < len(tokens):
        word, wc = tokens[i][0].lower(), tokens[i][1]
        if word in FM_FLAGS:
            if word in seen:
                raise MusicError(f"{_at(line, wc)}: {word} once")
            seen.add(word)
            out["flags"] = out.get("flags", 0) | FM_FLAGS[word]
            i += 1
            continue
        if word not in keys or word in seen:
            fb = "" if which == "mod" else " (feedback is the modulator's)" if word == "feedback" else ""
            raise MusicError(f"{_at(line, wc)}: '{tokens[i][0]}': {', '.join(keys)}, or {', '.join(FM_FLAGS)}; once each{fb}")
        seen.add(word)
        if i + 1 >= len(tokens):
            raise MusicError(f"{_at(line, wc)}: {word} <value>")
        text, vc = tokens[i + 1][0].lower(), tokens[i + 1][1]
        if word == "mult":
            if text not in MULTS:
                raise MusicError(f"{_at(line, vc)}: mult 0.5, 1..10, 12 or 15 (the OPL2's), not {text}")
            out["mult"] = MULTS[text]
        elif word == "ksl":
            if text not in KSL:
                raise MusicError(f"{_at(line, vc)}: ksl 0, 1.5, 3 or 6 (dB an octave), not {text}")
            out["ksl"] = KSL[text]
        elif word == "wave":
            if text not in FM_WAVES:
                raise MusicError(f"{_at(line, vc)}: wave sine, half-sine, abs-sine or quarter-sine, not {text}")
            out["wave"] = FM_WAVES[text]
        elif word in ("attack", "decay", "release"):
            m = re.fullmatch(r"(\d+)(ms)?", text)
            if text == "never":
                out[word] = NEVER
            elif not m or int(m.group(1)) >= NEVER:
                raise MusicError(f"{_at(line, vc)}: {word} 0..65534 (ms) or never, not {text}")
            else:
                out[word] = int(m.group(1))
        else:
            high = {"level": 63, "sustain": 15, "feedback": 7}[word]
            if not re.fullmatch(r"\d+", text) or int(text) > high:
                raise MusicError(f"{_at(line, vc)}: {word} 0..{high}, not {text}")
            out[word] = int(text)
        i += 2
    return out


def _instrument(body, line, col, synth):
    """An instrument's text (after '=') on a synthesizer."""
    if synth == "fm":
        return _fm(body, line, col)
    if body.strip()[:2].lower() == "fm" and (len(body.strip()) == 2 or not body.strip()[2].isalnum()):
        raise MusicError(f"{_at(line, col + len(body) - len(body.lstrip()))}: an instrument of FM in a song of the {synth} (#synth fm)")
    words = [(w.group(0), col + w.start()) for w in re.finditer(r"\S+", body)]
    return _tone(words, line, col, synth)


def default_of(synth):
    """@0 when nothing defines it: a square of 50% (chip, NES), a sine (FM)."""
    return FM() if synth == "fm" else DEFAULT


def _settings(text):
    """The lines of a text as (number, column, body), comments gone."""
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split(";", 1)[0]
        if line.strip():
            yield number, len(line) - len(line.lstrip()) + 1, line.strip()


def _instrument_line(body, number, col, synth, instruments, names):
    """@n [name] = ...: into instruments {n: instrument} and names {name: n}; False when the line is none."""
    m = re.match(r"@(\d+)(?:\s+(\S+))?\s*=", body)
    if not m:
        return False
    n = int(m.group(1))
    if n > 99 or n in instruments:
        raise MusicError(f"{_at(number, col)}: instrument @{n}: @0..@99, once")
    if m.group(2) is not None:
        name = m.group(2).lower()
        if not re.fullmatch(NAME, name) or name in names:
            raise MusicError(f"{_at(number, col + m.start(2))}: the name of an instrument: a letter, then letters, digits, _ or -; once ({m.group(2)})")
        names[name] = n
    instruments[n] = _instrument(body[m.end():], number, col + m.end(), synth)
    return True


def read_set(text, name="set"):
    """A set of instruments: (its synthesizer, {n: instrument}, {name: n}); MusicError naming the line and the column."""
    synth, instruments, names = None, {}, {}
    for number, col, body in _settings(text):
        m = re.fullmatch(r"#synth\s+(\S+)", body)
        if m:
            if synth is not None or instruments or m.group(1).lower() not in SYNTHS:
                raise MusicError(f"{_at(number, col)}: #synth chip, nes or fm, once, before the instruments")
            synth = m.group(1).lower()
            continue
        if synth is None:
            raise MusicError(f"{_at(number, col)}: a set says its synthesizer first: #synth chip, nes or fm")
        if not _instrument_line(body, number, col, synth, instruments, names):
            raise MusicError(f"{_at(number, col)}: a set of instruments has only instruments (@1 name = ...)")
    if synth is None:
        raise MusicError(f"{name}: no #synth: a set says its synthesizer first")
    return synth, instruments, names


def engine_set(name):
    """A set of the engine's (tools/music_sets/<name>.mml): its text, or None."""
    path = os.path.join(SETS, f"{name}.mml")
    if not re.fullmatch(NAME, name) or not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8") as f:
        return f.read()


class _Channel:
    """The commands of a channel read into events: ("note", n, ticks, slur), ("rest", ticks), (command, value); the loop mark."""

    def __init__(self, text, instruments, names=None):
        self.text = text  # [(char, line, col)]
        self.i = 0
        self.instruments = instruments
        self.names = names or {}  # the instruments' names: their numbers
        self.octave, self.length = 4, TICKS // 4
        self.loop = None
        self.used = set()  # the instruments it names

    def where(self, i=None):
        i = self.i if i is None else i
        if i < len(self.text):
            return _at(self.text[i][1], self.text[i][2])
        return _at(self.text[-1][1], self.text[-1][2] + 1) if self.text else "the end"

    def peek(self):
        while self.i < len(self.text) and self.text[self.i][0].isspace():
            self.i += 1
        return self.text[self.i][0].lower() if self.i < len(self.text) else ""

    def number(self):
        """Digits right here, or None."""
        start = self.i
        while self.i < len(self.text) and self.text[self.i][0].isdigit():
            self.i += 1
        return int("".join(c for c, _, _ in self.text[start:self.i])) if self.i > start else None

    def value(self, what, low, high, at):
        n = self.number()
        if n is None or not low <= n <= high:
            raise MusicError(f"{self.where(at)}: {what} {low}..{high}")
        return n

    def length_here(self, at, required=False):
        """A length right here (digits, dots), or the default (l) and its dots; in ticks."""
        n = self.number()
        if n is None and required:
            raise MusicError(f"{self.where(at)}: l <length> (l8, l4.)")
        if n is not None and (n == 0 or TICKS % n):
            raise MusicError(f"{self.where(at)}: a length of 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48 or 64, not {n}")
        total = add = TICKS // n if n is not None else self.length
        while self.i < len(self.text) and self.text[self.i][0] == ".":
            if add % 2:
                raise MusicError(f"{self.where(at)}: too many dots: not a whole number of ticks (a 192nd of a whole note)")
            add //= 2
            total += add
            self.i += 1
        return total

    def read(self, closing=None, depth=0):
        """Events up to the end or a closing ]: (events, the index of | in them or None)."""
        events, cut = [], None
        while True:
            c = self.peek()
            at = self.i
            if not c:
                if closing is not None:
                    raise MusicError(f"{self.where(closing)}: a [ without its ]")
                return events, cut
            self.i += 1
            if c in NOTES:
                pitch = NOTES[c] + 12 * (self.octave + 1)
                while self.i < len(self.text) and self.text[self.i][0] in "+#-":
                    pitch += -1 if self.text[self.i][0] == "-" else 1
                    self.i += 1
                if not 0 <= pitch <= 127:
                    raise MusicError(f"{self.where(at)}: the note is out of reach (MIDI 0..127; o4 a is 69)")
                events.append(["note", pitch, self.length_here(at), False, at])
                # & joins the next note: the same pitch one note, another goes on from this one
                while self.peek() == "&":
                    amp = self.i
                    self.i += 1
                    nc = self.peek()
                    if nc not in NOTES:
                        raise MusicError(f"{self.where(amp)}: & joins two notes: a note after it")
                    nat = self.i
                    self.i += 1
                    p2 = NOTES[nc] + 12 * (self.octave + 1)
                    while self.i < len(self.text) and self.text[self.i][0] in "+#-":
                        p2 += -1 if self.text[self.i][0] == "-" else 1
                        self.i += 1
                    if not 0 <= p2 <= 127:
                        raise MusicError(f"{self.where(nat)}: the note is out of reach (MIDI 0..127; o4 a is 69)")
                    t2 = self.length_here(nat)
                    if p2 == events[-1][1]:
                        events[-1][2] += t2
                    else:
                        events[-1][3] = True
                        events.append(["note", p2, t2, False, nat])
            elif c in "rp":
                events.append(["rest", self.length_here(at), at])
            elif c == "o":
                self.octave = self.value("an octave", 0, 9, at)
            elif c == "<":
                self.octave -= 1
            elif c == ">":
                self.octave += 1
            elif c == "l":
                self.length = self.length_here(at, required=True)
            elif c == "t":
                events.append(["tempo", self.value("a tempo (quarter notes a minute)", 1, 999, at), at])
            elif c == "v":
                events.append(["volume", self.value("a volume", 0, 15, at), at])
            elif c == "q":
                events.append(["gate", self.value("a gate (eighths of the note)", 1, 8, at), at])
            elif c == "@":
                if self.i < len(self.text) and self.text[self.i][0].isalpha():
                    start = self.i
                    while self.i < len(self.text) and re.fullmatch(r"[A-Za-z0-9_-]", self.text[self.i][0]):
                        self.i += 1
                    name = "".join(ch for ch, _, _ in self.text[start:self.i]).lower()
                    if name not in self.names:
                        raise MusicError(f"{self.where(at)}: instrument @{name} is not defined (@1 {name} = ..., or in the song's #set)")
                    n = self.names[name]
                else:
                    n = self.value("an instrument", 0, 99, at)
                    if n not in self.instruments and n != 0:
                        raise MusicError(f"{self.where(at)}: instrument @{n} is not defined (@{n} = ...)")
                self.used.add(n)
                events.append(["instrument", n, at])
            elif c == "[":
                body, bar = self.read(at, depth + 1)
                n = self.number()
                n = 2 if n is None else n
                if not 1 <= n <= 255:
                    raise MusicError(f"{self.where(at)}: a repeat of 1..255 times")
                for k in range(n):
                    part = body if k < n - 1 or bar is None else body[:bar]
                    events += [list(e) for e in part]
            elif c == "]":
                if closing is None:
                    raise MusicError(f"{self.where(at)}: a ] without its [")
                return events, cut
            elif c == "|":
                if closing is None or cut is not None:
                    raise MusicError(f"{self.where(at)}: | is inside [ ], once")
                cut = len(events)
            elif c == "$":
                if depth or self.loop is not None:
                    raise MusicError(f"{self.where(at)}: $, where the channel goes round from: once, not inside [ ]")
                self.loop = len(events)
            elif c == "&":
                raise MusicError(f"{self.where(at)}: & joins two notes: a note before it")
            else:
                raise MusicError(f"{self.where(at)}: '{self.text[at][0]}' is no command of the notes")




def _length(n):
    return bytes([n]) if n < 128 else bytes([0x80 | n >> 8, n & 0xFF])


def _record(instrument):
    """An instrument's bytes in the song."""
    if isinstance(instrument, FM):
        return instrument.bytes()
    w, v, a, d, s, r = instrument
    return struct.pack("<BBBBHHH", w, v, s, 0, a, d, r)


def assemble(text, synth=None, sets=None):
    """
    The song (bytes) of an MML text; MusicError, naming the line and the column, when it is wrong. synth: the
    project's synthesizer (chip, nes, fm; None: fm), which the text's #synth overrides. sets: the project's sets of
    instruments, a function of a name giving the set's text or None (then the engine's, tools/music_sets/).
    """
    instruments, names, lines = {}, {}, {}
    order = []  # the channels by first use
    said = {}  # the settings said: #synth, #set, #tail -> (value, line, column)
    for number, col, body in _settings(text):
        m = re.match(r"#(\w+)(?:\s+(.*))?$", body)
        if body.startswith("#"):
            if not m or m.group(1).lower() not in ("synth", "set", "tail") or not m.group(2):
                raise MusicError(f"{_at(number, col)}: a setting of the song: #synth chip|nes|fm, #set <name>, #tail <ms>")
            key, value = m.group(1).lower(), m.group(2).strip().lower()
            if key in said or instruments or lines:
                raise MusicError(f"{_at(number, col)}: #{key} once, before the instruments and the notes")
            if key == "synth" and value not in SYNTHS:
                raise MusicError(f"{_at(number, col + m.start(2))}: #synth chip, nes or fm, not {m.group(2).strip()}")
            if key == "set" and not re.fullmatch(NAME, value):
                raise MusicError(f"{_at(number, col + m.start(2))}: #set <the name of a set>, not {m.group(2).strip()}")
            if key == "tail" and (not re.fullmatch(r"\d+(ms)?", value) or int(value.rstrip("ms")) >= NO_TAIL):
                raise MusicError(f"{_at(number, col + m.start(2))}: #tail 0..65534 (ms), not {m.group(2).strip()}")
            said[key] = (value, number, col + m.start(2))
            continue
        song_synth = said["synth"][0] if "synth" in said else synth or "fm"
        if song_synth not in SYNTHS:
            raise MusicError(f"the synthesizer {song_synth!r}: chip, nes or fm")
        if _instrument_line(body, number, col, song_synth, instruments, names):
            continue
        m = re.match(r"([A-Z]+)(\s|$)", body)
        if not m or any(ch not in CHANNELS for ch in m.group(1)) or len(set(m.group(1))) != len(m.group(1)):
            raise MusicError(f"{_at(number, col)}: a line is an instrument (@1 = square ...) or the notes of channels "
                             "(their names A..H, then the notes)")
        start = col + m.end(1) - 1
        chars = [(ch, number, start + k + 1) for k, ch in enumerate(body[m.end(1):])] + [(" ", number, start + len(body) - m.end(1) + 1)]
        for ch in m.group(1):
            if ch not in lines:
                lines[ch] = []
                order.append(ch)
            lines[ch] += chars
    song_synth = said["synth"][0] if "synth" in said else synth or "fm"
    if song_synth not in SYNTHS:
        raise MusicError(f"the synthesizer {song_synth!r}: chip, nes or fm")
    if not lines:
        raise MusicError("no channel: a line of notes begins with its channel's name, A..H")
    # the set: its instruments under the song's own (the song's of a number takes its place)
    own = dict(instruments)
    if "set" in said:
        name, sl, sc = said["set"]
        set_text = (sets(name) if sets else None)
        if set_text is None:
            set_text = engine_set(name)
        if set_text is None:
            raise MusicError(f"{_at(sl, sc)}: no set of instruments {name} (the project's, or the engine's: {', '.join(sorted(f[:-4] for f in os.listdir(SETS) if f.endswith('.mml')))})")
        try:
            set_synth, set_instruments, set_names = read_set(set_text, name)
        except MusicError as e:
            raise MusicError(f"{_at(sl, sc)}: set {name}: {e}") from None
        if set_synth != song_synth and not (set_synth == "chip" and song_synth == "nes"):
            raise MusicError(f"{_at(sl, sc)}: set {name} is of the {set_synth}, the song of the {song_synth}")
        for n, inst in set_instruments.items():
            instruments.setdefault(n, inst)
        for nm, n in set_names.items():
            if n not in own:
                names.setdefault(nm, n)
    channel_names = sorted(lines)
    streams = []
    for ch in channel_names:
        c = _Channel(lines[ch], instruments, names)
        events, _ = c.read()
        if c.loop is not None and not any(e[0] in ("note", "rest") for e in events[c.loop:]):
            raise MusicError(f"{c.where(len(c.text) - 1)}: channel {ch} goes round from $ through no note or rest: it would take no time")
        streams.append((c, events))
    # the table: the instruments by their numbers (@0..the last defined); with a set, only those used, in order
    if "set" not in said:
        table = [instruments.get(i, default_of(song_synth)) for i in range(max(list(instruments) + [0]) + 1)]
        place = {i: i for i in range(len(table))}
    else:
        wanted = sorted({e[1] for _, events in streams for e in events if e[0] == "instrument"})
        first = any(next((e[0] for e in events if e[0] in ("note", "instrument")), None) == "note" for _, events in streams)
        if first and 0 not in wanted:
            wanted = [0] + wanted  # a channel plays before it names an instrument: @0
        wanted = wanted or [0]
        table = [instruments.get(i, default_of(song_synth)) for i in wanted]
        place = {n: k for k, n in enumerate(wanted)}
    if len(table) > 255:
        raise MusicError(f"{len(table)} instruments: 255 at most")
    for k_stream, (c, events) in enumerate(streams):
        code, loop = bytearray(), None
        for k, e in enumerate(events):
            if k == c.loop:
                loop = len(code)
            if e[0] == "note":
                if e[2] > LONGEST:
                    raise MusicError(f"{c.where(e[4])}: a note longer than {LONGEST} ticks ({LONGEST // TICKS} whole notes)")
                code += (bytes([SLUR]) if e[3] else b"") + bytes([e[1]]) + _length(e[2])
            elif e[0] == "rest":
                if e[1] > LONGEST:
                    raise MusicError(f"{c.where(e[2])}: a rest longer than {LONGEST} ticks")
                code += bytes([REST]) + _length(e[1])
            elif e[0] == "tempo":
                code += struct.pack("<BH", TEMPO, e[1])
            elif e[0] == "instrument":
                code += bytes([INSTRUMENT, place[e[1]]])
            else:
                code += bytes([{"volume": VOLUME, "gate": GATE}[e[0]], e[1]])
        if c.loop is not None and loop is None:
            loop = len(code)
        code.append(END)
        streams[k_stream] = (bytes(code), loop)
    tail = int(said["tail"][0].rstrip("ms")) if "tail" in said else NO_TAIL
    if song_synth == "chip" and tail == NO_TAIL:
        head = struct.pack("<IHBBH", MAGIC, 1, len(channel_names), len(table), 120)
    else:
        head = struct.pack("<IHBBHBBH", MAGIC, 2, len(channel_names), len(table), 120, SYNTHS[song_synth], 0, tail)
    head += b"".join(_record(i) for i in table)
    at = len(head) + 8 * len(streams)
    chans = b""
    for code, loop in streams:
        chans += struct.pack("<II", at, 0xFFFFFFFF if loop is None else at + loop)
        at += len(code)
    return head + chans + b"".join(code for code, _ in streams)


def channels_of(song):
    """How many channels a song has (the engine keeps state for the most of them)."""
    return song[6]


def synth_of(song):
    """A song's synthesizer: chip, nes or fm."""
    return "chip" if struct.unpack_from("<H", song, 4)[0] == 1 else {v: k for k, v in SYNTHS.items()}[song[10]]


def main(argv):
    import argparse

    ap = argparse.ArgumentParser(description="An MML text into a song of the engine (DGMU).")
    ap.add_argument("mml")
    ap.add_argument("out", nargs="?", help="the song's file (none: its size and synthesizer only)")
    ap.add_argument("--synth", choices=sorted(SYNTHS), help="the project's synthesizer, fm when unsaid (the text's #synth overrides it)")
    ap.add_argument("--sets", action="append", default=[], help="a directory of the project's sets (<name>.mml), before the engine's")
    a = ap.parse_args(argv)

    def project_sets(name):
        for d in a.sets:
            p = os.path.join(d, f"{name}.mml")
            if os.path.isfile(p):
                with open(p, encoding="utf-8") as f:
                    return f.read()
        return None

    with open(a.mml, encoding="utf-8") as f:
        text = f.read()
    try:
        song = assemble(text, a.synth, project_sets)
    except MusicError as e:
        print(f"{a.mml}: {e}", file=sys.stderr)
        return 1
    if a.out:
        with open(a.out, "wb") as f:
            f.write(song)
    print(f"{a.mml}: {len(song)} bytes, {channels_of(song)} channels, the {synth_of(song)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
