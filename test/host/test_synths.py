"""
The synthesizers on the packer's side: #synth, #set, #tail; the instruments of each synthesizer (the chip's, the
NES's, FM's operators) into their bytes, their errors naming the line and the column; instruments by name; sets (the
engine's, the project's, a document's music/sets/), their errors, a set's instruments only as used; the project's
synthesizer (Pack.set_music_synth, add_music(synth=), a document's "synth") and a song's own; DGMU version 1 for the
chip as before, version 2 otherwise; dgx_ge_synths of project_include.cmake.
"""
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
from dgx_ge_music import FM, MusicError, assemble, read_set  # noqa: E402
from dgx_ge_pack import Pack  # noqa: E402

failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


def head(b):
    """version, channels, instruments, tempo, synth, tail (None for version 1)."""
    magic, version, channels, count, tempo = struct.unpack_from("<IHBBH", b)
    assert magic == 0x554D4744
    synth, tail = (None, None) if version == 1 else (b[10], struct.unpack_from("<H", b, 12)[0])
    return version, channels, count, tempo, synth, tail


def fm_records(b):
    """The FM instruments of a song: (connect, volume, feedback, mod, car), an operator (mult2, level, ksl, flags, wave, sustain, attack, decay, release)."""
    count = b[7]
    out = []
    for i in range(count):
        r = b[14 + 28 * i:14 + 28 * (i + 1)]
        ops = [struct.unpack_from("<BBBBBBHHH", r, 4 + 12 * k) for k in range(2)]
        out.append((r[0], r[1], r[2], ops[0], ops[1]))
    return out


def tones(b):
    v = head(b)
    at = 10 if v[0] == 1 else 14
    return [struct.unpack_from("<BBBBHHH", b, at + 10 * i) for i in range(v[2])]


def refused(text, *words, **kw):
    try:
        assemble(text, **kw)
    except MusicError as e:
        check(all(w in str(e) for w in words), f"{text!r}: {e} (wanted {words})")
        return
    check(False, f"{text!r}: not refused")


# the chip, as before: version 1, the same bytes from the song's #synth or the project's
plain = assemble("@1 = square 25%\nA @1 c", synth="chip")
check(head(plain)[0] == 1 and assemble("#synth chip\n@1 = square 25%\nA @1 c") == plain, "the chip: version 1, #synth chip the same")
# a song and its project naming none: FM
check(assemble("@1 = fm car(mult 2)\nA @1 c") == assemble("#synth fm\n@1 = fm car(mult 2)\nA @1 c"), "FM when none is named")
refused("@1 = square 25%\nA @1 c", "#synth chip or nes")

# the NES: version 2, synth 1; its duties and noise modes; the chip's instruments the same records
n = assemble("#synth nes\n@1 = square 75%\n@2 = noise short\n@3 = noise long\n@4 = square 12.5% attack 2 decay 40 sustain 8 release 30 volume 9\nA @1 c")
check(head(n)[:3] == (2, 1, 5) and head(n)[4:] == (1, 0xFFFF), f"the NES's head: {head(n)}")
check([t[0] for t in tones(n)] == [2, 5, 6, 4, 0] and tones(n)[4] == (0, 9, 8, 0, 2, 40, 30), f"the NES's waves: {tones(n)}")
check(assemble("@1 = square 75%\nA @1 c", synth="nes") == assemble("#synth nes\n@1 = square 75%\nA @1 c"), "the project's NES")
refused("@1 = square 75%\nA @1 c", "line 1, column 13", "75%", "NES", synth="chip")
refused("@1 = noise short\nA @1 c", "line 1, column 12", "NES", synth="chip")
refused("#synth nes\n@1 = square 30%\nA @1 c", "line 2, column 13", "75%")

# FM: version 2, synth 2, 28 bytes an instrument; the operators' settings into their bytes
f = assemble("#synth fm\n@1 = fm mod(mult 0.5 level 30 ksl 1.5 attack 2 decay 40 sustain 8 release 30 wave half-sine feedback 5 "
             "percussive ksr) car(mult 15 level 0 ksl 6 attack 1ms decay never sustain 15 release 80 wave quarter-sine vibrato tremolo) "
             "connect add volume 12\nA @1 c")
check(head(f)[:3] == (2, 1, 2) and head(f)[4] == 2, f"FM's head: {head(f)}")
recs = fm_records(f)
check(recs[1] == (1, 12, 5, (1, 30, 1, 3, 1, 8, 2, 40, 30), (30, 0, 3, 12, 3, 15, 1, 0xFFFF, 80)), f"FM's instrument: {recs[1]}")
# @0 undefined: a sine (the modulator silent: its attack never); no mod(): the same silent modulator
check(recs[0] == (0, 15, 0, (2, 0, 0, 0, 0, 15, 0xFFFF, 0, 0), (2, 0, 0, 0, 0, 15, 0, 0, 0)), f"FM's @0: {recs[0]}")
g = fm_records(assemble("#synth fm\n@1 = fm car(mult 2)\nA @1 c"))[1]
check(g[3][6] == 0xFFFF and g[4][0] == 4 and g[0] == 0, f"no mod(): {g}")
check(len(assemble("#synth fm\n@1 = fm\nA @1 c")) == len(assemble("#synth fm\n@1 = fm mod(attack never) car()\nA @1 c")), "fm alone: all unsaid")
check(assemble("#synth fm\n@1 = fm car(mult 2)\nA @1 c") == assemble("@1 = fm car ( mult 2 )\nA @1 c", synth="fm"), "the project's FM; spaces in the brackets")
# its errors, with the line and the column
refused("#synth fm\n@1 = square\nA c", "line 2, column 6", "fm mod")
refused("@1 = fm car(mult 2)\nA c", "line 1, column 6", "FM", "#synth fm", synth="chip")
refused("#synth fm\n@1 = fm car(mult 11)\nA c", "line 2, column 18", "mult 0.5, 1..10, 12 or 15")
refused("#synth fm\n@1 = fm car(level 64)\nA c", "line 2, column 19", "level 0..63")
refused("#synth fm\n@1 = fm car(ksl 2)\nA c", "line 2, column 17", "ksl 0, 1.5, 3 or 6")
refused("#synth fm\n@1 = fm car(wave saw)\nA c", "line 2, column 18", "wave sine")
refused("#synth fm\n@1 = fm car(attack 65535)\nA c", "line 2, column 20", "never")
refused("#synth fm\n@1 = fm car(sustain 16)\nA c", "line 2, column 21", "sustain 0..15")
refused("#synth fm\n@1 = fm car(feedback 3)\nA c", "line 2, column 13", "modulator's")
refused("#synth fm\n@1 = fm mod(feedback 8)\nA c", "line 2, column 22", "feedback 0..7")
refused("#synth fm\n@1 = fm car(mult 1 mult 2)\nA c", "line 2, column 20", "once")
refused("#synth fm\n@1 = fm car(mult 1\nA c", "line 2, column 12", "( without its )")
refused("#synth fm\n@1 = fm car mult 1\nA c", "line 2, column 9", "car(")
refused("#synth fm\n@1 = fm car() car()\nA c", "line 2, column 15", "once")
refused("#synth fm\n@1 = fm connect both\nA c", "line 2, column 17", "connect fm")
refused("#synth fm\n@1 = fm volume 16\nA c", "line 2, column 16", "volume 0..15")
refused("#synth fm\n@1 = fm sine\nA c", "line 2, column 9", "'sine'")
refused("#synth fm\n@1 = fm car(percussive percussive)\nA c", "line 2, column 24", "once")

# the settings: once, before the instruments and the notes; their errors
refused("#synth saw\nA c", "line 1, column 8", "#synth chip, nes or fm")
refused("A c\n#synth fm", "line 2, column 1", "before")
refused("#synth fm\n#synth fm\nA c", "line 2, column 1", "once")
refused("#tempo 120\nA c", "line 1, column 1", "#synth")
refused("#tail x\nA c", "line 1, column 7", "#tail 0..65534")
refused("#set Piano!\nA c", "line 1, column 6", "#set")
check(head(assemble("#tail 250ms\nA c", synth="chip"))[4:] == (0, 250) and head(assemble("#tail 0\nA c", synth="chip"))[5] == 0,
      "the tail: version 2 of the chip")

# instruments by name: @name in the notes; unknown, twice, a bad name
named = assemble("@1 lead = square 25%\n@2 bass = triangle\nA @lead c @bass d @1 e", synth="chip")
check(named == assemble("@1 = square 25%\n@2 = triangle\nA @1 c @2 d @1 e", synth="chip"), "@name is its number")
refused("@1 lead = square\nA @leed c", "line 2, column 3", "@leed", synth="chip")
refused("@1 lead = square\n@2 lead = triangle\nA c", "line 2, column 4", "once", synth="chip")
refused("@1 9lead = square\nA c", "line 1, column 4", "name", synth="chip")

# sets: the engine's (chip, msx-music); only the instruments used go in, in their order, @0 when a channel plays before naming one
s = assemble("#set chip\nA @bass c @lead d", synth="chip")
check(head(s)[2] == 2 and [t[0] for t in tones(s)] == [1, 3], f"the chip set, two used: {tones(s)}")
s = assemble("#set chip\nA c @hat d", synth="chip")
check(head(s)[2] == 2 and tones(s)[0][0] == 2 and tones(s)[1][0] == 4, f"@0 first when a note comes before any: {tones(s)}")
lead = tones(assemble("#set chip\nA @lead c", synth="chip"))
check(lead == [(1, 15, 10, 0, 2, 80, 60)], f"a set's instrument as written there: {lead}")
# the song's own of a number takes the set's place; of another number beside it; by number too
own = tones(assemble("#set chip\n@1 = triangle\nA @1 c @bass d @2 e", synth="chip"))
check(own == [(3, 15, 15, 0, 0, 0, 0), (0, 15, 9, 0, 2, 60, 50), (3, 15, 15, 0, 0, 0, 30)], f"own over the set's: {own}")
refused("#set chip\n@1 = triangle\nA @lead c", "line 3, column 3", "@lead", synth="chip")  # its name went with the set's @1
# the NES takes the chip's set; FM does not; msx-music is FM's
check(head(assemble("#synth nes\n#set chip\nA @lead c"))[4] == 1, "a chip set for the NES")
refused("#synth fm\n#set chip\nA c", "line 2, column 6", "set chip is of the chip")
refused("#set msx-music\nA c", "line 1, column 6", "of the fm", synth="chip")
refused("#set chip\nA c", "line 1, column 6", "set chip is of the chip")  # FM when none is named
m = assemble("#synth fm\n#set msx-music\nA @piano c @violin d @15 e")
check(head(m)[2] == 3 and len(m) == 14 + 3 * 28 + 8 + 13, f"msx-music: three of its fifteen: {len(m)}")
synth, inst, names = read_set(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "music_sets", "msx-music.mml")).read())
check(synth == "fm" and sorted(inst) == list(range(1, 16)) and len(names) == 15 and names["piano"] == 3 and names["electric-guitar"] == 15,
      f"msx-music: {sorted(names)}")
synth, inst, names = read_set(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "music_sets", "chip.mml")).read())
check(synth == "chip" and names["square"] == 0 and names["snare"] == 8, f"chip: {names}")
refused("#set nothing-such\nA c", "line 1, column 6", "no set", "msx-music")
# the project's sets: a function of the name, before the engine's
mine = "#synth fm\n@1 bell = fm mod(mult 7 level 20) car(mult 1 decay 900 sustain 0 percussive)\n"
check(fm_records(assemble("#synth fm\n#set chip\nA @bell c", sets=lambda n: mine if n == "chip" else None))[0][3][0] == 14, "the project's set first")
for bad, words in (("@1 = square\n", ("line 1", "#synth")), ("#synth fm\nA c\n", ("line 2", "only instruments")),
                   ("#synth fm\n@1 = square\n", ("line 2", "fm mod")), ("", ("no #synth",))):
    try:
        read_set(bad, "x")
        check(False, f"a set {bad!r} refused")
    except MusicError as e:
        check(all(w in str(e) for w in words), f"a set {bad!r}: {e}")
refused("#set broken\nA c", "line 1, column 6", "set broken", "line 2", sets=lambda n: "#synth chip\n@1 = saw\n")

# the pack: the project's synthesizer, a song's own over it, add_music(synth=), add_music_set, the document's
p = Pack("S")
p.set_music_synth("fm")
p.add_music("a", "@1 = fm car(mult 2)\nA @1 c")
p.add_music("b", "#synth chip\nA c")
p.add_music("c", "A c", synth="nes")
p.add_music_set("bells", mine)
p.add_music("d", "#set bells\nA @bell c")
check([head(x)[4] for x in p.music_data] == [2, None, 1, 2], f"the project's, a song's own, add_music's: {[head(x) for x in p.music_data]}")
for bad in ("saw", "", None):
    try:
        p.set_music_synth(bad)
        check(False, f"synth {bad!r}")
    except ValueError:
        pass
try:
    p.add_music_set("bad", "#synth fm\n@1 = square\n", "music/sets/bad.mml")
    check(False, "a set wrong")
except MusicError as e:
    check(str(e).startswith("music/sets/bad.mml: line 2"), f"a set's error names its file: {e}")
try:
    p.add_music_set("bells", mine)
    check(False, "a set twice")
except ValueError:
    pass
here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "synth_doc")
os.makedirs(os.path.join(here, "music", "sets"), exist_ok=True)
with open(os.path.join(here, "music", "sets", "bells.mml"), "w") as fh:
    fh.write(mine)
with open(os.path.join(here, "music", "theme.mml"), "w") as fh:
    fh.write("#set bells\nA @bell c e g\n")
with open(os.path.join(here, "music", "jingle.mml"), "w") as fh:
    fh.write("#synth chip\nA c e g\n")
with open(os.path.join(here, "game.json"), "w") as fh:
    json.dump({"format": "dgx-ge-sprites", "formatVersion": 1, "name": "g", "sets": [], "synth": "fm", "music": ["theme", "jingle"]}, fh)
d = Pack("D")
d.add_sprites(os.path.join(here, "game.json"))
check(head(d.music_data[0])[4] == 2 and fm_records(d.music_data[0])[0][3][0] == 14 and head(d.music_data[1])[0] == 1,
      f"a document's synth and its music/sets/: {[head(x) for x in d.music_data]}")
with open(os.path.join(here, "game.json"), "w") as fh:
    json.dump({"format": "dgx-ge-sprites", "formatVersion": 1, "name": "g", "sets": [], "synth": "opl3", "music": []}, fh)
try:
    Pack("E").add_sprites(os.path.join(here, "game.json"))
    check(False, "a document's synth unknown")
except ValueError as e:
    check("opl3" in str(e), f"{e}")
check(FM() == FM() and FM().bytes()[:4] == bytes([0, 15, 0, 0]), "FM's default")

# the command line: a file into a song
with tempfile.TemporaryDirectory() as tmp:
    src, dst = os.path.join(tmp, "s.mml"), os.path.join(tmp, "s.dgmu")
    with open(src, "w") as fh:
        fh.write("#set msx-music\nA @organ c\n")
    tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "dgx_ge_music.py")
    r = subprocess.run([sys.executable, "-I", tool, src, dst, "--synth", "fm"], capture_output=True, text=True)
    check(r.returncode == 0 and "the fm" in r.stdout and open(dst, "rb").read() == assemble("#set msx-music\nA @organ c\n", "fm"), f"the command line: {r}")
    r = subprocess.run([sys.executable, "-I", tool, src, dst], capture_output=True, text=True)
    check(r.returncode == 0 and open(dst, "rb").read() == assemble("#set msx-music\nA @organ c\n", "fm"), f"the command line, FM unsaid: {r}")
    r = subprocess.run([sys.executable, "-I", tool, src, "--synth", "chip"], capture_output=True, text=True)
    check(r.returncode == 1 and "of the fm" in r.stderr, f"the command line's error: {r.stderr}")

# dgx_ge_synths of project_include.cmake: DGX_GE_SYNTH_<NAME>=1 for each, said once, chip|nes|fm each once
INCLUDE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "project_include.cmake")


def configure(*calls):
    with tempfile.TemporaryDirectory() as tmp:
        script = os.path.join(tmp, "t.cmake")
        with open(script, "w") as fh:
            fh.write("function(idf_build_get_property var prop)\n    get_property(v GLOBAL PROPERTY IDF_BUILD_${prop})\n"
                     "    set(${var} \"${v}\" PARENT_SCOPE)\nendfunction()\n"
                     "function(idf_build_set_property prop value)\n    set_property(GLOBAL APPEND PROPERTY IDF_BUILD_${prop} \"${value}\")\n"
                     f"endfunction()\ninclude(\"{INCLUDE}\")\n" + "".join(f"{c}\n" for c in calls) +
                     "get_property(v GLOBAL PROPERTY IDF_BUILD_COMPILE_DEFINITIONS)\nmessage(\"DEFS=${v}\")\n")
        r = subprocess.run(["cmake", "-P", script], capture_output=True, text=True)
        defs = [line[5:] for line in r.stderr.splitlines() if line.startswith("DEFS=")]
        return r.returncode, defs[0].split(";") if defs and defs[0] else [], r.stderr


if shutil.which("cmake"):
    rc, defs, said = configure("dgx_ge_synths(chip fm)")
    check(rc == 0 and defs == ["DGX_GE_SYNTH_CHIP=1", "DGX_GE_SYNTH_FM=1"], f"dgx_ge_synths(chip fm): {rc} {defs} {said[-300:]}")
    rc, defs, said = configure("dgx_ge_synths(nes)")
    check(rc == 0 and defs == ["DGX_GE_SYNTH_NES=1"], f"dgx_ge_synths(nes): {defs}")
    for bad in ("", "opl", "chip chip", "FM"):
        rc, defs, said = configure(f"dgx_ge_synths({bad})")
        check(rc != 0 and not defs, f"dgx_ge_synths({bad}): {rc} {defs}")
    rc, defs, said = configure("dgx_ge_synths(chip)", "dgx_ge_synths(fm)")
    check(rc != 0 and "said already" in said, f"said twice: {said[-300:]}")
else:
    print("test_synths.py: dgx_ge_synths skipped (no cmake)")

if failed:
    print("\n".join(failed))
    sys.exit(1)
print("test_synths.py: ok")
