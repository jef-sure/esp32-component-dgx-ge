"""
The packer's music by notes: MML into songs (notes, octaves, lengths, dots, ties, repeats, the loop point, instruments),
errors naming the line and the column; Pack.add_music, its names in the header, the blob SOUNDS of version 2 with
music and of version 1, as before, without; a document's musics beside it; the script's music and the expression
music; the explainer's words for them.
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
from dgx_ge_anim import AnimError, assemble as assemble_anim  # noqa: E402
from dgx_ge_explain import explain  # noqa: E402
from dgx_ge_music import MusicError, assemble as assemble_music  # noqa: E402


def assemble(text, synth="chip", **more):
    """The songs of this test are of the chip (the synthesizer when none is named is FM)."""
    return assemble_music(text, synth=synth, **more)


from dgx_ge_pack import Pack  # noqa: E402

failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


def song(text):
    """The head, the instruments and the channels' events of a song, read back: (tempo, instruments, [(events, loop)])."""
    b = assemble(text)
    magic, version, channels, count, tempo = struct.unpack_from("<IHBBH", b)
    assert magic == 0x554D4744 and version == 1
    instruments = [struct.unpack_from("<BBBBHHH", b, 10 + 10 * i) for i in range(count)]
    out = []
    for c in range(channels):
        start, loop = struct.unpack_from("<II", b, 10 + 10 * count + 8 * c)
        events, at, loop_at = [], start, None
        while True:
            if at == loop:
                loop_at = len(events)
            op = b[at]
            at += 1
            if op == 0xFF:
                break
            if op < 0x80 or op == 0x80:
                n = b[at]
                at += 1
                if n & 0x80:
                    n = (n & 0x7F) << 8 | b[at]
                    at += 1
                events.append(("note", op, n) if op < 0x80 else ("rest", n))
            elif op == 0x81:
                events.append(("slur",))
            elif op == 0x84:
                events.append(("tempo", struct.unpack_from("<H", b, at)[0]))
                at += 2
            else:
                events.append(({0x82: "instrument", 0x83: "volume", 0x85: "gate"}[op], b[at]))
                at += 1
        out.append((events, loop_at if loop != 0xFFFFFFFF else None))
    return tempo, instruments, out


def refused(text, *words):
    try:
        assemble(text)
    except MusicError as e:
        check(all(w in str(e) for w in words), f"{text!r}: {e} (wanted {words})")
        return
    check(False, f"{text!r}: not refused")


# notes: o4 a is 69 (440 Hz), c d e of octave 4, sharps and flats, < and >, the octave given
_, _, [(ev, _)] = song("A o4 a c d e c+ c# d- < c > > c o2 b")
check([e[1] for e in ev] == [69, 60, 62, 64, 61, 61, 61, 48, 72, 47], f"notes: {ev}")
# lengths: 48 ticks a quarter; 1 2 8 16 32, triplets 3 12, dots each half of the one before, l with its dots
_, _, [(ev, _)] = song("A c c1 c2 c8 c16 c32 c3 c12 c4. c4.. c2... l8 c l8. c c4 l16 r r8.")
check([e[-1] for e in ev] == [48, 192, 96, 24, 12, 6, 64, 16, 72, 84, 180, 24, 36, 48, 12, 36], f"lengths: {ev}")
# a tie: the same pitch one note, another a slur (the next goes on from it); across lengths and dots
_, _, [(ev, _)] = song("A c4&c8. d4 & e8 & e8 f")
check(ev == [("note", 60, 84), ("note", 62, 48), ("slur",), ("note", 64, 48), ("note", 65, 48)] or
      ev == [("note", 60, 84), ("slur",), ("note", 62, 48), ("note", 64, 48), ("note", 65, 48)], f"ties: {ev}")
check(ev == [("note", 60, 84), ("slur",), ("note", 62, 48), ("note", 64, 48), ("note", 65, 48)], f"the slur before its note: {ev}")
# repeats written out, nested, the part after | left out the last time; the commands inside them too
_, _, [(ev, _)] = song("A [c [d e]2]2 [f | g]3 [a]")
check([e[1] for e in ev] == [60, 62, 64, 62, 64, 60, 62, 64, 62, 64, 65, 67, 65, 67, 65, 69, 69], f"repeats: {ev}")
_, _, [(ev, _)] = song("A [v3 c]2")
check(ev == [("volume", 3), ("note", 60, 48)] * 2, f"commands in a repeat: {ev}")
# tempo, volume, gate, instrument, the loop point; a long note in two bytes
tempo, inst, [(ev, loop)] = song("@1 = square 12.5% attack 2 decay 40ms sustain 8 release 30 volume 9\nA t150 v12 q6 @1 c $ d1&d1")
check(tempo == 120 and ev[:4] == [("tempo", 150), ("volume", 12), ("gate", 6), ("instrument", 1)] and loop == 5 and ev[5] == ("note", 62, 384),
      f"commands, loop: {ev} {loop}")
check(inst == [(2, 15, 15, 0, 0, 0, 0), (0, 9, 8, 0, 2, 40, 30)], f"instruments: {inst}")  # @0 a plain square of 50%
_, inst, _ = song("@0 = triangle release 80\n@2 = noise\nA c")
check(inst == [(3, 15, 15, 0, 0, 0, 80), (2, 15, 15, 0, 0, 0, 0), (4, 15, 15, 0, 0, 0, 0)], f"instruments by number: {inst}")
_, inst, _ = song("@1 = square 25%\nA c")
check(inst[1][0] == 1, f"a square of 25%: {inst}")
# channels: sorted by name, lines adding to one, several names one line, case not counting in the notes
_, _, chans = song("B o2 c\nA C D\nA E\nCD g")
check([[e[1] for e in ev] for ev, _ in chans] == [[60, 62, 64], [36], [67], [67]], f"channels: {chans}")
check(song("A T150 O5 L8 V3 C")[2] == song("A t150 o5 l8 v3 c")[2], "the case of the notes does not count")
# comments; a channel's loop with no note in it refused
check(song("A c ; d e\n; a line of a comment\n")[2][0][0] == [("note", 60, 48)], "comments")
# errors: the line and the column
refused("A c\nA c64.", "line 2, column 3", "dots")
refused("A c5", "line 1, column 3", "length")
refused("A o4 c x", "line 1, column 8", "'x'")
refused("A [c d", "line 1, column 3", "]")
refused("A c ]", "line 1, column 5", "[")
refused("A c | d", "line 1, column 5", "|")
refused("A $ c $ d", "line 1, column 7", "$")
refused("A [$ c]", "line 1, column 4", "$")
refused("A c $ v3", "$", "no time")
refused("A @3 c", "line 1, column 3", "@3")
refused("A o9 g >c", "line 1, column 9", "out of reach")
refused("A c & r", "line 1, column 5", "&")
refused("A & c", "line 1, column 3", "&")
refused("A t0 c", "line 1, column 3", "tempo")
refused("A v16", "line 1, column 3", "volume")
refused("A q9", "line 1, column 3", "gate")
refused("A l c", "line 1, column 3", "l <length>")
refused("x c d", "line 1, column 1", "instrument")
refused("AA c", "line 1, column 1")
refused("I c", "line 1, column 1")
refused("@1 = saw\nA c", "line 1", "wave")
refused("@1 = square 30%\nA c", "line 1, column 13", "12.5%")
refused("@1 = square sustain 16\nA c", "line 1, column 21", "sustain")
refused("@1 = noise attack\nA c", "line 1, column 12", "attack")
refused("@1 = noise\n@1 = noise\nA c", "line 2", "once")
refused("; only a comment\n", "no channel")
refused("A " + "c1&" * 171 + "c1", "longer")

# the pack: musics by name in the header, their songs after the sounds (SOUNDS 2); none: SOUNDS 1 as before
p = Pack("T")
p.set_music_synth("chip")  # its songs are of the chip, as this test reads them
p.add_sound("beep", samples=[0, 1000, -1000, 0], rate=8000, codec="pcm8")
plain = p._sounds_blob()
check(plain[:8] == struct.pack("<IHH", 0x4E534744, 1, 1) and struct.unpack_from("<I", plain, 8)[0] == 8 + 12, "SOUNDS 1 without music")
p.add_anim("PLAYER", "script go\n    music theme\n    goto go if music >= 0\n    music stop\n    hold\n")
check(p.musics == ["theme"], f"a script names a music: {p.musics}")
check(p._sounds_blob() == plain, "musics named only: SOUNDS 1, as before")
check(p.add_music("jingle", "A c d e") == 1 and p.add_music("theme", "A t200 c") == 0, "add_music: the numbers of the names")
try:
    p.add_music("jingle", "A c")
    check(False, "a music given its notes twice")
except ValueError as e:
    check("twice" in str(e), f"twice: {e}")
try:
    p.add_music("bad", "A c\nA x", path="music/bad.mml")
    check(False, "an MML error")
except MusicError as e:
    check(str(e).startswith("music/bad.mml: line 2, column 3:"), f"an error of a file: {e}")
check(p.add_music("later") == 2, "a name only")
blob = p._sounds_blob()
magic, version, sounds, musics, zero = struct.unpack_from("<IHHHH", blob)
check(version == 2 and sounds == 1 and musics == 3 and zero == 0, f"SOUNDS 2: {version} {sounds} {musics}")
off, size = struct.unpack_from("<II", blob, 12 + 12)
check(blob[off:off + size] == assemble("A t200 c"), "the song of a music where its entry says")
check(struct.unpack_from("<I", blob, 12)[0] == 12 + 12 + 8 * 3, "the sound's samples after the musics' entries")
check(struct.unpack_from("<II", blob, 12 + 12 + 16) == (0, 0), "a name only: no song")
header = p.header("T")
check("#define T_MUSIC_THEME 0" in header and "#define T_MUSIC_JINGLE 1" in header and "#define T_MUSIC_COUNT 3" in header, header)
# the anim: music <name> the number, music stop 255; the expression music, not for a path
code = assemble_anim("script go\n    music theme\n    music stop\n    music theme 500ms\n    music stop 2s\n    music volume 30% 500ms\n"
                     "    music volume 100%\n    hold\n", musics=["x", "theme"]).blob
check(all(bytes(b) in code for b in ([45, 1, 0, 0], [45, 255, 0, 0], [45, 1, 0xF4, 1], [45, 255, 0xD0, 7], [46, 30, 0xF4, 1], [46, 100, 0, 0])),
      "music, its fade, its level in the blob")
for bad in ("script go\n    music\n    hold\n", "script go\n    music 3\n    hold\n", "script go\n    path x: music, y: 0\n    hold\n",
            "script go\n    music theme 66s\n    hold\n", "script go\n    music theme 5\n    hold\n", "script go\n    music volume 101%\n    hold\n",
            "script go\n    music volume 30\n    hold\n", "script go\n    music theme 1s 2s\n    hold\n"):
    try:
        assemble_anim(bad)
        check(False, f"refused: {bad!r}")
    except AnimError:
        pass
check(bytes([53]) in assemble_anim("param p 0\nscript go\n    set p music + 1\n    hold\n").blob, "the expression music")
check(bytes([53]) not in assemble_anim("param music 0\nscript go\n    set music music + 1\n    hold\n").blob, "a param named music is the param")

# a document's musics: music/<name>.mml beside it, or a name only (a warning at the write)
here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "music_doc")
os.makedirs(os.path.join(here, "music"), exist_ok=True)
with open(os.path.join(here, "music", "theme.mml"), "w") as f:
    f.write("@1 = triangle\nA @1 c e g\n")
with open(os.path.join(here, "game.json"), "w") as f:
    json.dump({"format": "dgx-ge-sprites", "formatVersion": 1, "name": "g", "sets": [], "synth": "chip", "music": ["theme", "later"]}, f)
d = Pack("D")
d.add_sprites(os.path.join(here, "game.json"))
check(d.musics == ["theme", "later"] and d.music_data[0] == assemble("@1 = triangle\nA @1 c e g\n") and d.music_data[1] is None, f"a document's: {d.musics}")
d.write(os.path.join(here, "d.pack"), os.path.join(here, "d.h"), os.path.join(here, "d.sounds"))
check(any("later" in w and "never" in w for w in d.warnings), f"a name without notes warned: {d.warnings}")
with open(os.path.join(here, "d.sounds"), "rb") as f:
    check(f.read()[4] == 2, "the songs in the file of the sounds")

for name in ("stop", "volume", "a b"):
    try:
        Pack("N").add_music(name, "A c")
        check(False, f"a music named {name!r}")
    except ValueError:
        pass

# the explainer
for lang, words in (("ru", ["играет музыку `theme`", "если музыка ещё играет", "музыка смолкает", "(смена затуханием за 500ms)",
                            "громкость музыки — 30% от настройки за 1s"]),
                    ("en", ["plays the music `theme`", "if a music still plays", "the music stops", "(fading over 500ms)",
                            "the music at 30% of its volume over 1s"])):
    text = explain("script go\n    music theme\nlabel w\n    frame 0 10ms\n    goto w if music >= 0\n    music stop\n    music theme 500ms\n"
                   "    music volume 30% 1s\n    hold\n", lang)
    for w in words:
        check(w in text, f"{lang}: {w!r} in {text}")

if failed:
    print("\n".join(failed))
    sys.exit(1)
print("test_music.py: ok")
