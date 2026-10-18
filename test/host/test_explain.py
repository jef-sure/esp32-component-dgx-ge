"""The reverse interpreter tells scripts in words: both languages, repeats told once, the events between kinds."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from dgx_ge_explain import explain, explain_pack  # noqa: E402

POT = """event taken, thrown, landed, kicked
define gravity 432
param bubble 0

script lying
    masks 6
    set bubble 0
    frame 0 83ms
label bubbling
    set bubble 1
    frame 0 83ms
    set bubble 2
    frame 0 83ms
    set bubble 3
    frame 0 83ms
    goto bubbling
script held
    frame 0 1000ms
    hold
script emptied
    tell holder drank
    frame 0 1000ms
    hold
script flying
    contact on landed
    contact 1 below on landed off fell
    accel 0 gravity
    frame 0 1000ms
    hold
on taken: lying -> held now
on thrown: held -> flying now
"""
HAND = """event reach
script reach
    masks 1 5
    hit 5 on 6 event taken
    frame 109 83ms
    frame 109 83ms
    hold
script walk
    frame 7 83ms
    dx 5
    frame 8 83ms
    sound 1
    dx 1
    frame 9 83ms
    goto walk
on reach: walk -> reach now
"""
WORDS = {"ITEM": {"ru": "предмет", "en": "item"}, "taken": {"ru": "взят", "en": "taken"}, "bubble": {"ru": "пузырёк", "en": "bubble"}}
MASKS = {1: "BODY", 5: "HAND", 6: "ITEM"}
failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


for lang, want in (("ru", ["пузырёк по кругу 1…3, кадр 83 мс", "повторяет с «bubbling»", "активны: предмет", "сообщает держащему `drank`",
                           "ищет касания местности; коснулся — `landed`", "вниз gravity = 432",
                           "следит, касается ли BODY чего-то снизу: коснулась — `landed`; перестала — `fell`", "«взят» (`taken`): lying → held, сразу",
                           "**Не отвечает на:** `landed`, `kicked`", "(1000 мс)"]),
                   ("en", ["bubble goes round 1…3, a frame of 83 ms", "repeats from “bubbling”", "active: item", "tells the one holding it `drank`",
                           "“taken” (`taken`): lying → held, at once", "**Does not answer:** `landed`, `kicked`",
                           "watches whether BODY touches anything below: on touching, `landed`; on letting go, `fell`"])):
    text = explain(POT, lang, WORDS, MASKS, "Pot")
    for w in want:
        check(w in text, f"{lang}: '{w}' in\n{text}")
    check("**lying** (" not in text, f"{lang}: a round told with a length")
walk = explain(HAND, "en", WORDS, MASKS)
check("3 frames 7–9 of 83 ms, 6 forward; sound 1" in walk, f"frames with steps and a sound together:\n{walk}")
check("2 frames 109 of 83 ms" in walk, f"one frame held:\n{walk}")
check("HAND meeting item: to it “taken” (`taken`)" in walk, f"a hit in words:\n{walk}")
pack = explain_pack({"KID": HAND, "POT": POT}, "en", WORDS, MASKS)
check("“taken” (`taken`): sent by KID (reach: hit); answered by POT: lying → held." in pack, f"a chain between kinds:\n{pack}")
check("- POT: `thrown`." in pack and "- KID: `reach`." in pack, f"sent by the game:\n{pack}")
check("`landed`" not in pack.split("###")[0], f"an event within one kind is no chain:\n{pack}")
# pockets in words: places, stacks, kept; draw, stow, a goto with a condition, what is thrown out of them
CARRIER = """pockets 3
script throw
label again
    goto idle if count(KNIFE) == 0
    shoot hand KNIFE 40 0
    frame 4 100ms
    goto again
script idle
    draw hand KNIFE
    stow hand
    hold
"""
for lang, want in (("ru", ["Карманы: 3 места.", "если в карманах нет «нож» (`KNIFE`) — к `idle`", "достаёт из карманов «нож» (`KNIFE`) в «hand»",
                           "убирает то, что в «hand», в карманы", "повторяет с «again»"]),
                   ("en", ["Pockets: 3 places.", "if there is no “knife” (`KNIFE`) in the pockets: to `idle`", "throws “knife” (`KNIFE`) from “hand”"])):
    text = explain(CARRIER, lang, {"KNIFE": {"ru": "нож", "en": "knife"}}, MASKS)
    for w in want:
        check(w in text, f"{lang}: '{w}' in\n{text}")
knife = explain("stack 5\nscript lying\n    keep\n    frame 1 10ms\n    hold\n", "ru")
check("В одно место кармана — до 5." in knife and "**lying** (10 мс): мир хранит, новым не уступает; 1 кадр" in knife, f"a kind of pockets:\n{knife}")
stack = explain("script lying\n    keep\n    frame (DAGGERS_1 + amount - 1) 1000ms\n    hold\n", "ru")
check("кадр по выражению DAGGERS_1 + (сколько их лежит) - 1, 1000 мс" in stack, f"a frame by an expression:\n{stack}")
# a dialog in words: lines (translated), answers with their conditions, silence, what follows
from dgx_ge_explain import explain_dialog  # noqa: E402
DIALOG = """dialog post
roles guard GUARD, kid KID
label start
    say guard "Halt!" 2s
    say! game "Read it."
    choose kid 5s -> quiet
        "Wine." if count(WINE) > 0 -> gift
label gift
    give kid WINE 1 to guard
    tell guard calm
    end
label quiet
    goto start
"""
W = {"guard": {"ru": "стражник", "en": "guard"}, "kid": {"ru": "принц", "en": "kid"}, "WINE": {"ru": "вино", "en": "wine"}}
ru = explain_dialog(DIALOG, "ru", W, {("post", "Halt!"): "Стой!"})
for w in ["**Роли:** стражник (`GUARD`), принц (`KID`).", "- стражник: «Стой!» — сам через 2 с", "- игра: «Read it.» (важно: целиком)",
          "- Выбирает принц (молчание 5 с → quiet):", "  - «Wine.» → gift, если в карманах есть «вино» (`WINE`)",
          "- принц → стражник: «вино» (`WINE`) × 1", "- стражник — событие `calm`", "- дальше — start"]:
    check(w in ru, f"dialog ru: '{w}' in\n{ru}")
en = explain_dialog(DIALOG, "en", W)
check("- guard: “Halt!” — on by itself after 2 s" in en and "- kid gives guard: “wine” (`WINE`) × 1" in en, f"dialog en:\n{en}")
# a page of settings: the game's kind, controls (staying, or leading on)
PAGE = """dialog page
game SETTINGS
choose game
    "Sound" control SWITCH sound
    "Quiet" control SWITCH sound -> back
    "Back" -> back
label back
    end
"""
ru = explain_dialog(PAGE, "ru", {"SWITCH": {"ru": "переключатель", "en": "switch"}, "sound": {"ru": "звук", "en": "sound"}})
for w in ["**Настройки игры:** `SETTINGS`.", "  - «Sound» — «переключатель» (`SWITCH`): звук, выбор остаётся",
          "  - «Quiet» — «переключатель» (`SWITCH`): звук, потом → back", "  - «Back» → back"]:
    check(w in ru, f"page ru: '{w}' in\n{ru}")
# a script that branches: told part by part, each from its label, where each goes on; no frames: it stops
mind = explain("param keys 0\ngame near\nscript think\n    set keys 0\n    goto close if game.near\n    set keys 2\n    goto done\n"
               "label close\n    set keys 16\nlabel done\n    hold\n", "ru")
for w in ["- **think**: keys = 0; если game.near — к «close»; keys = 2; дальше — к «done».", "  - «close»: keys = 16; дальше — к «done».",
          "  - «done»: останавливается."]:
    check(w in mind, f"branches: '{w}' in\n{mind}")
# one way through: a label marked where it begins; the end is where the way stops (a hold after a goto is never reached)
round_ = explain("script walk\n    frame 1 10ms\nlabel on\n    frame 2 20ms\n    goto on\n    hold\nscript stay\n    frame 3 10ms\n    goto stay\n    hold\n", "ru")
for w in ["- **walk**: 1 кадр 1, 10 мс; «on»: 1 кадр 2, 20 мс; повторяет с «on».", "- **stay**: 1 кадр 3, 10 мс; по кругу."]:
    check(w in round_, f"one way: '{w}' in\n{round_}")
# the command line: files named as kinds, the events between them first, the masks by name
import contextlib
import io
import tempfile
from dgx_ge_explain import main as explain_main
with tempfile.TemporaryDirectory() as tmp:
    for name, text in (("wolf", "event eaten\nscript hunt\n    masks 2\n    hit 2 on 1 event eaten\n    frame 0 100ms\n    hold\n"),
                       ("sheep", "event eaten\nscript live\n    masks 1\n    frame 0 100ms\n    hold\nscript dead\n    vanish\n    hold\non eaten: live -> dead now\n")):
        with open(os.path.join(tmp, name + ".anim"), "w") as f:
            f.write(text)
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        explain_main(["--lang", "en", "--masks", "1=BODY,2=JAWS", os.path.join(tmp, "wolf.anim"), os.path.join(tmp, "sheep.anim")])
    told = out.getvalue()
    if not ("## WOLF" in told and "## SHEEP" in told and "`eaten`" in told and "JAWS" in told and told.index("WOLF (hunt: hit)") < told.index("## WOLF")):
        failed.append(f"the command line: {told[:300]}")
print("test_explain.py: ok" if not failed else "test_explain.py: " + "\n".join(failed))
sys.exit(1 if failed else 0)
