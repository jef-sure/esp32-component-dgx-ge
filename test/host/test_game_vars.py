"""
The game's variables in the tools: declared in a pack (and in a document), named in the header, their blob; read in
scripts and dialogs (game_var.<name>), written by set and add, edited by controls, shown in lines; refused where they
would be wrong (an undeclared one, one named as a constant, a number of the game or a question, a tile, a path); the
explainer words them.
"""
import json
import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from dgx_ge_anim import EXPR, OP, AnimError, Expr, assemble  # noqa: E402
from dgx_ge_dialog import DialogError, Strings, assemble_dialog  # noqa: E402
from dgx_ge_explain import explain, explain_dialog  # noqa: E402
from dgx_ge_pack import Pack  # noqa: E402

here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, "build", "game_vars")
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


def code_of(blob):
    """The code of a blob of one script and no params, events or tables: past the header, the script's start, the matrix."""
    scripts, events, params, size = struct.unpack_from("<HHHI", blob, 6)
    at = 20 + 2 * scripts + 2 * params + 2 * scripts * events
    return blob[at:at + size]


def num(v):
    """An expression of one number, as the assembler writes it."""
    return bytes([5]) + struct.pack("<Bf", EXPR["num"], v)


VARS = ["sound", "score"]

# set and add: the variable's number, the expression after it (a constant one too); read: VAR and the number
a = assemble("script s\n    set game_var.score 3\n    add game_var.sound -1\n    hold\n", game_vars=VARS)
check(code_of(a.blob) == bytes([OP["SET_VAR"], 1]) + num(3) + bytes([OP["ADD_VAR"], 0]) + num(-1) + bytes([OP["HOLD"]]),
      f"set and add of variables: {code_of(a.blob).hex()}")
a = assemble("param hp 1\nscript s\n    set hp game_var.score + 1\n    hold\n", game_vars=VARS)
check(bytes([EXPR["game_var"], 1]) in code_of(a.blob), f"a variable read: {code_of(a.blob).hex()}")
# a variable is the store's: no param of the actor named after it, no question to the game
check(a.params == ["hp"], f"params {a.params}")
refused("set of an undeclared variable", lambda: assemble("script s\n    set game_var.lives 3\n    hold\n", game_vars=VARS),
        ("game_var.lives", "add_variable"))
refused("add of an undeclared variable", lambda: assemble("script s\n    add game_var.lives 3\n    hold\n", game_vars=VARS), ("game_var.lives",))
refused("an undeclared variable read", lambda: assemble("script s\n    goto s if game_var.lives > 0\n    hold\n", game_vars=VARS),
        ("game_var.lives", "no such variable"))
refused("a variable in a path", lambda: assemble("script s\n    path x: game_var.score * t, y: 0\n    hold\n", game_vars=VARS), ("path",))
refused("a question named as a variable", lambda: assemble("game score\nscript s\n    hold\n", game_vars=VARS), ("score", "game_var.score"))
refused("a param named game_var", lambda: assemble("param game_var 1\nscript s\n    hold\n", game_vars=VARS))
refused("set game_var. of no name", lambda: assemble("script s\n    set game_var. 3\n    hold\n", game_vars=VARS))
# a question to the game stays a question: game.<name> as it was, beside a variable of another name
q = assemble("game far\nparam hp 1\nscript s\n    set hp game.far + game_var.score\n    hold\n", game_vars=VARS)
check(bytes([EXPR["game"], 0]) in code_of(q.blob) and bytes([EXPR["game_var"], 1]) in code_of(q.blob), f"a question and a variable: {code_of(q.blob).hex()}")

# the pack: declared in order, the header names them and how many, the blob VARIABLES the values they begin with
p = Pack("V")
p.add_variable("volume", 2)
p.add_variable("score")
p.add_variable("best", -5.0)  # a whole number as a JSON writer may give it
p.add_anim("HERO", "event go\nparam hp 3\nscript stand\n    add game_var.score game_var.volume * 10\n    hold\n")
p.write(os.path.join(out, "v.pack"), os.path.join(out, "v_pack.h"))
header = open(os.path.join(out, "v_pack.h")).read()
for line in ("#define V_GAME_VAR_VOLUME 0", "#define V_GAME_VAR_SCORE 1", "#define V_GAME_VAR_BEST 2", "#define V_GAME_VAR_COUNT 3",
             "#define V_BLOB_VARIABLES 1"):
    check(line in header, f"the header has no {line!r}")
blob = dict(p.blobs)["VARIABLES"]
check(blob == struct.pack("<IHH3i", 0x52564744, 1, 3, 2, 0, -5), f"the blob of the variables {blob.hex()}")
check("V_HERO_GAME_" not in header and "#define V_ANIM_PARAMS 2" in header, "the variables are no params, no questions")
# none declared: no blob, no lines
p = Pack("N")
p.add_anim("HERO", "event go\nscript stand\n    hold\n")
check("VARIABLES" not in dict(p.blobs) and "GAME_VAR" not in p.header("N"), "no variables: nothing of them")

# refusals of the pack: values, names twice, a name of another thing (either order), a name the header would give twice
for bad in (2.5, True, "3", 2 ** 31, None):
    refused(f"a variable of the value {bad!r}", lambda: Pack("R").add_variable("x", bad), ("variable x",))
for bad in ("1x", "a-b", "", "game_var.x"):
    refused(f"a variable named {bad!r}", lambda: Pack("R").add_variable(bad, 0), ("variable",))
p = Pack("R")
p.add_variable("lives", 3)
refused("a variable declared twice", lambda: p.add_variable("lives", 4), ("declared already",))
refused("a variable named as another in upper case", lambda: p.add_variable("LIVES", 4), ("declared already",))
refused("a constant named as a variable", lambda: p.add_constants(lives=3), ("constant lives", "variable"))
refused("a number of the game named as a variable", lambda: p.add_game(Lives=3), ("Lives", "variable"))
refused("a question named as a variable", lambda: p.add_anim("HERO", "game lives\nscript s\n    hold\n"), ("lives", "variable"))
refused("a question named as a variable in upper case", lambda: p.add_anim("HERO", "game LIVES\nscript s\n    hold\n"), ("LIVES", "variable"))
refused("a question named as a variable, of animations made together", lambda: p.add_anims([("HERO", "game Lives\nscript s\n    hold\n")]),
        ("Lives", "variable"))
refused("a dialog's question of an undeclared variable", lambda: p.add_dialog('dialog d\n    say game "{game_var.hp} left" 1s\n    end\n'),
        ("game_var.hp",))
p = Pack("R")
p.add_constants(BOARD_PAD=1)
p.add_game(kinds=8)
p.add_anim("HERO", "game far\nscript s\n    hold\n")
refused("a variable named as a constant", lambda: p.add_variable("board_pad", 0), ("board_pad", "constant"))
refused("a variable named as a number of the game", lambda: p.add_variable("kinds", 0), ("kinds", "number of the game"))
refused("a variable named as a question", lambda: p.add_variable("far", 0), ("far", "question", "HERO"))
# a number of the game VAR_SCORE is V_GAME_VAR_SCORE too: two things, refused even with the same number
p = Pack("R")
p.add_game(VAR_SCORE=1)
p.add_variable("score", 0)
refused("a number of the game named as a variable's macro", lambda: p.header("R"), ("R_GAME_VAR_SCORE",))
p = Pack("R")
p.add_variable("score", 0)
p.add_game(kinds=0)
p.add_game(VAR_SCORE=0)  # its place 1, the variable's number 0: other lines
refused("a number of the game named as a variable's macro, another number", lambda: p.header("R"), ("R_GAME_VAR_SCORE",))
# a tile's piece reads no variable (laid again only when its cell changes)
refused("a variable in a tile", lambda: Expr("game_var.light", {}, {}, False, "a piece", {"mod": (0, 0, 8)}, actor=False), ("game_var.light",))
# the most a pack declares: a variable's number is a byte
p = Pack("R")
for i in range(256):
    p.add_variable(f"v{i}", i)
refused("a variable past the most", lambda: p.add_variable("more", 0), ("256",))

# a document: its variables, [{"name", "value"}] by their numbers, before its dialogs read them
doc = {"format": "dgx-ge-sprites", "formatVersion": 1, "sets": [], "variables": [{"name": "volume", "value": 3}, {"name": "score"}]}
os.makedirs(os.path.join(out, "doc", "dialogs"))
with open(os.path.join(out, "doc", "game.json"), "w") as f:
    json.dump(doc, f)
with open(os.path.join(out, "doc", "dialogs", "page.dlg"), "w") as f:
    f.write('dialog page\n    say game "Score {game_var.score}" 1s\n    end\n')
p = Pack("D")
p.add_sprites(os.path.join(out, "doc", "game.json"))
check(p.variables == [("volume", 3), ("score", 0)], f"the document's variables {p.variables}")
# the letters the pack can print: a hole of a variable is the digits and the minus, not its braces nor its name
check(p.letters() == "".join(sorted(set("0123456789- Scoer"))), f"the letters of a line of a variable {p.letters()!r}")

# dialogs: a control of a variable (its number, the flag 1), of a param as before (flag 0); a condition reads one; a line
# shows them ({game_var.<name>}: the flag 2 of the line); a line of both, or of an undeclared one, refused
KINDS = {"KID": 3, "SWITCH": 7, "SETTINGS": 6}
PARAMS = {"KID": {"hp": (0, 3)}, "SETTINGS": {"sound": (0, 1)}, "SWITCH": {"value": (0, 0)}}
d = assemble_dialog('dialog p\ngame SETTINGS\n    choose game\n        "Score" control SWITCH game_var.score\n'
                    '        "Sound" if game_var.sound > 0 control SWITCH sound -> b\nlabel b\n    end\n', Strings(), KINDS, PARAMS,
                    game_vars=VARS)
code = d.blob[16 + 6:]  # past the header and the game's role (no roles)
code = d.blob[16:]
first = struct.unpack_from("<HHHBB", code, 7)
second = struct.unpack_from("<HHHBB", code, 16)
check(first[2:] == (7, 1, 1) and code[15] == 0, f"a control of a variable: {first}")
check(second[2:] == (7, 0, 0) and bytes([EXPR["game_var"], 0]) in code[24:24 + 1 + code[24]], f"a control of a param, a condition of a variable: {second}")
d = assemble_dialog('dialog l\n    say game "Score {game_var.score}" 1s\n    say! game "{game_var.score} of {game_var.sound}" 1s\n    end\n',
                    Strings(), KINDS, PARAMS, game_vars=VARS)
code = d.blob[16:]
check(code[0] == 8 and code[2] == 2 and code[8] == 1, f"a line of a variable: {code[:9].hex()}")  # SAY_N, flags 2, the variable 1
check(d.blob[7] == 0, f"the flags of a line are not the dialog's: it stops no game ({d.blob[7]})")
check(code[9] == 9 and code[11] == 3 and code[17] == 2 and code[18:20] == bytes([1, 0]), f"a line of two variables: {code[9:20].hex()}")  # SAY_NS
refused("a line of a param and a variable", lambda: assemble_dialog('dialog m\nroles kid KID\n    say kid "{hp} of {game_var.score}" 1s\n    end\n',
                                                                        Strings(), KINDS, PARAMS, game_vars=VARS), ("not both",))
refused("a line of an undeclared variable", lambda: assemble_dialog('dialog m\n    say game "{game_var.lives}" 1s\n    end\n', Strings(), KINDS, PARAMS,
                                                                       game_vars=VARS), ("game_var.lives",))
refused("a control of an undeclared variable", lambda: assemble_dialog('dialog m\n    choose game\n        "x" control SWITCH game_var.lives\n',
                                                                          Strings(), KINDS, PARAMS, game_vars=VARS), ("game_var.lives",))
refused("a condition of an undeclared variable", lambda: assemble_dialog('dialog m\n    choose game\n        "x" if game_var.lives -> m_end\nlabel m_end\n    end\n',
                                                                            Strings(), KINDS, PARAMS, game_vars=VARS), ("game_var.lives",))
# a translation shows the same variables, in its own order
s = Strings({("o", "{game_var.score} of {game_var.sound}"): "из {game_var.sound} — {game_var.score}"})
d = assemble_dialog('dialog o\n    say game "{game_var.score} of {game_var.sound}" 1s\n    end\n', s, KINDS, PARAMS, game_vars=VARS)
check(s.texts == ["из {1} — {0}"], f"a translation of a line of variables in another order: {s.texts}")

# the explainer: set and add of a variable, a control of one
ru = explain("script s\n    set game_var.score 0\n    add game_var.score 10\n    frame 0 100ms\n    hold\n", "ru", {}, {})
en = explain("script s\n    set game_var.score 0\n    add game_var.score 10\n    frame 0 100ms\n    hold\n", "en", {}, {})
check("переменная игры score = 0" in ru and "переменная игры score += 10" in ru, f"the explainer's variables, ru: {ru}")
check("the game's variable score = 0" in en and "the game's variable score += 10" in en, f"the explainer's variables, en: {en}")
page = 'dialog p\n    choose game\n        "Volume" control SLIDER game_var.volume\n'
ru = explain_dialog(page, "ru", {"volume": {"ru": "громкость", "en": "volume"}})
en = explain_dialog(page, "en")
check("«Volume» — `SLIDER`: переменная игры громкость, выбор остаётся" in ru, f"the explainer's control of a variable, ru: {ru}")
check("“Volume” — `SLIDER`: the game's variable volume, the choice stays" in en, f"the explainer's control of a variable, en: {en}")
# a frame's time that reads a variable is worked out as the frame begins
ru = explain("script s\n    frame 0 (game_var.speed)ms\n    hold\n", "ru", {}, {})
check("вычисляется в начале кадра" in ru, f"a frame's time of a variable: {ru}")

print("test_game_vars.py: ok" if not failed else "test_game_vars.py:\n  " + "\n  ".join(failed))
sys.exit(1 if failed else 0)
