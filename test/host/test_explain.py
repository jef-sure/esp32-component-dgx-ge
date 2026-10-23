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
# a frame's time worked out as the frame begins (from a param): told so, and the script has no length; a moved frame told alone; a
# time of a define worked out, its frames together, the length known
STEPS = "define tick 83\nparam speed 100\nscript playing\n    signal 2\n    frame none (speed)ms\n    goto playing\n" \
        "script falling\n    frame PIECE (speed * 2)ms at 1, 2\nlabel lying_down\n    frame PIECE 50ms\n    hold\nscript lying\n    frame STONE tick\n    frame STONE tick\n    frame 1 10ms at 2, 1\n    hold\n"
for lang, want in (("ru", ["ничего не показывает speed мс (длительность вычисляется в начале кадра); по кругу",
                           "**falling**: кадр PIECE (картинка сдвинута: 1, 2), speed * 2 мс (длительность вычисляется в начале кадра)",
                           "**lying** (176 мс): 2 кадра STONE по tick = 83 мс; кадр 1 (картинка сдвинута: 2, 1), 10 мс"]),
                   ("en", ["shows nothing for speed ms (the time worked out as the frame begins); round and round",
                           "**falling**: frame PIECE (the picture moved by 1, 2), speed * 2 ms (the time worked out as the frame begins)",
                           "**lying** (176 ms): 2 frames STONE of tick = 83 ms; frame 1 (the picture moved by 2, 1), 10 ms"])):
    text = explain(STEPS, lang)
    for w in want:
        check(w in text, f"{lang}: '{w}' in\n{text}")
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
# a frame's time a name (a constant of the pack): told as written, the length of its script not told
named = explain("param k 0\nscript s\n    frame 1 TICK_MS\n    frame 2 TICK_MS\n    goto t\nscript t\n    set k 1\n    frame 3 T\n    set k 2\n"
                "    frame 3 T\n    set k 3\n    frame 3 T\n    hold\nscript u\n    frame 1 10ms\n    frame 2 TICK_MS\n    hold\n", "ru")
check("- **s**: 2 кадра 1–2 по TICK_MS мс; дальше — `t`." in named and "- **t**: k по кругу 1…3, кадр T мс" in named and "- **u**: 1 кадр 1" in named,
      f"frames of named times:\n{named}")
# the game's answers lost: a script that signals and goes on into another, answering no event itself (the game answers
# SIG_LOCK with full at once, from its say: the actor is still in locking, which no rule names); one named by a rule,
# alone or in a group, is not told; nor one that stays in itself after its signal (hold, its own label, itself), nor a
# kind of no rules; a goto to a label and a run past the end into the next script keep the script; vanish ends it
from dgx_ge_explain import lost_answers  # noqa: E402
PLAY = """event lock, full
script idle
    frame none 1000ms
    hold
script locking
    sound 3
    signal SIG_LOCK
    goto idle
script flashing
    frame none 500ms
    goto idle
on lock: idle -> locking now
on full: idle -> flashing now
"""
check(lost_answers(PLAY) == [("locking", ["SIG_LOCK"], ["idle"])], f"a script signalling and going on, answering nothing: {lost_answers(PLAY)}")
for lang, want in (("ru", "- Состояние «locking» подаёт сигнал SIG_LOCK и переходит в «idle», но само не отвечает ни на одно событие: событие, "
                          "которым игра сразу ответит на сигнал, застанет актёра ещё в «locking» и пропадёт. Если игра так отвечает, назовите "
                          "«locking» в правилах этого события (само состояние или группу, где оно есть)."),
                   ("en", "- The state “locking” gives signal SIG_LOCK and goes on to “idle”, but answers no event itself: an event the game "
                          "answers the signal with at once finds the actor still in “locking” and is lost.")):
    text = explain(PLAY, lang)
    check("**" + ("Ответ игры пропадёт" if lang == "ru" else "The game's answer would be lost") + ":**" in text and want in text,
          f"{lang}: lost answers told:\n{text}")
fixed = PLAY.replace("on full: idle -> flashing now", "group any: idle, locking\non full: any -> flashing now")
check(lost_answers(fixed) == [] and "пропадёт" not in explain(fixed, "ru"), f"a script named in a group: {lost_answers(fixed)}")
check(lost_answers(PLAY.replace("on lock: idle -> locking now\non full: idle -> flashing now\n", "")) == [], "a kind of no rules")
STAYS = """event go
script rest
    hold
script over
    signal 2
    frame none 100ms
    hold
script back
    goto rest
script round
    signal 1
label again
    frame none 100ms
    goto again
script self
    signal 4
    frame none 100ms
    goto self
script gone
    signal 5
    vanish
    goto rest
on go: rest -> over now
"""
check(lost_answers(STAYS) == [], f"scripts that stay in themselves after their signals: {lost_answers(STAYS)}")
WAYS = """event go
script rest
    hold
script into
    signal 1
script next
    goto rest if x > 3
    frame none 100ms
    goto there
script far
    signal 2
    goto mark
script other
    frame none 10ms
label mark
    frame none 10ms
    goto there
script quiet
    goto rest
script there
    sound 1
    goto rest
on go: rest -> into now
on go: next -> into now
on go: other -> into now
"""
check(lost_answers(WAYS) == [("into", ["1"], ["rest", "there"]), ("far", ["2"], ["there"])],
      f"a run past the end and a goto of a label (another script's too) keep the script: {lost_answers(WAYS)}")
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
# --help: what the command line takes, not an unknown option
out = io.StringIO()
with contextlib.redirect_stdout(out):
    explain_main(["--help"])
if "--lang" not in out.getvalue():
    failed.append(f"--help: {out.getvalue()[:200]}")
print("test_explain.py: ok" if not failed else "test_explain.py: " + "\n".join(failed))
sys.exit(1 if failed else 0)
