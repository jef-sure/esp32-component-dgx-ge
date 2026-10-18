"""The assembler of dialogs refuses what is wrong; strings by context, translated or as they are; .po in, .pot out."""
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from dgx_ge_dialog import DialogError, Strings, assemble_dialog, read_po  # noqa: E402

failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


KINDS = {"KID": 3, "KNIFE": 5, "SETTINGS": 6, "SWITCH": 7}
PARAMS = {"KID": {"hp": (0, 3)}, "SETTINGS": {"sound": (0, 1), "volume": (1, 2)}, "SWITCH": {"value": (0, 0)}}
PAGE = "dialog p\ngame SETTINGS\nlabel a\n    choose game\n"
BASE = "dialog d\nroles kid KID\nlabel a\n"
cases = {
    "no dialog": "label a\n    end\n",
    "dialog twice": "dialog d\ndialog e\n    end\n",
    "a role of no kind": "dialog d\nroles kid NOPE\n    end\n",
    "a role named game": "dialog d\nroles game KID\n    end\n",
    "a role twice": "dialog d\nroles kid KID, kid KID\n    end\n",
    "say of no role": BASE + '    say nobody "x"\n',
    "say without quotes": BASE + "    say kid hello\n",
    "something after the string": BASE + '    say kid "x" more\n',
    "an answer leading nowhere": BASE + '    choose kid\n        "x" -> nowhere\n',
    "an answer without its arrow": BASE + '    choose kid\n        "x"\n',
    "a condition of no param": BASE + '    choose kid\n        "x" if strength > 1 -> a\n',
    "goto nowhere": BASE + "    goto nowhere\n",
    "give nothing": BASE + "    give kid KNIFE 0 to kid\n",
    "give of no kind": BASE + "    give kid NOPE 1 to kid\n",
    "tell the game": BASE + "    tell game hit\n",
    "a label twice": BASE + "label a\n    end\n",
    "nothing in it": "dialog d\nroles kid KID\n",
    "no such statement": BASE + "    shout kid \"x\"\n",
"a time of nothing": BASE + '    say kid "x" 0s\n',
    "a time too long": BASE + '    say kid "x" 70s\n',
    "not a time": BASE + '    say kid "x" soon\n',
    "a choice's time leading nowhere": BASE + '    choose kid 3s -> nowhere\n        "x" -> a\n',
    "a choice's time without its label": BASE + '    choose kid 3s\n        "x" -> a\n',
    "a role name not quoted": "dialog d\nroles kid KID Prince\n    end\n",
    "nine roles": "dialog d\nroles " + ", ".join(f"r{i} KID" for i in range(9)) + "\n    end\n",
    "a control of no kind": PAGE + '        "x" control NOPE sound\n',
    "a control of no param": PAGE + '        "x" control SWITCH loudness\n',
    "a control of the game with no game kind": 'dialog p\nlabel a\n    choose game\n        "x" control SWITCH sound\n',
    "the game's kind twice": "dialog p\ngame SETTINGS\ngame SETTINGS\n    end\n",
    "the game of no kind": "dialog p\ngame NOPE\n    end\n",
    "a control before its condition": PAGE + '        "x" control SWITCH sound if volume > 1\n',
    "a face of no picture": 'dialog d\nroles kid KID "Kid" face NOPE\n    end\n',
    "a face before the name": 'dialog d\nroles kid KID face FACE "Kid"\n    end\n',
}
for name, text in cases.items():
    try:
        assemble_dialog(text, Strings(), KINDS, PARAMS, images={"FACE": 4})
        failed.append(f"{name} was not refused")
    except DialogError:
        pass

# strings: one msgid in two contexts, one translated; equal strings once; a '#' in a string is no comment
s = Strings({("first", "Thanks."): "Спасибо."})
a = assemble_dialog('dialog first\nroles kid KID\n    say kid "Thanks."\n    say kid "a # b"\n    say game "Thanks."\n    end\n', s, KINDS, PARAMS)
b = assemble_dialog('dialog second\n    say game "Thanks."\n    say game "Спасибо."\n    end\n', s, KINDS, PARAMS)
check(s.texts == ["Спасибо.", "a # b", "Thanks."], f"strings {s.texts}")
check(s.used == [("first", "Thanks.", None), ("first", "a # b", None), ("second", "Thanks.", None), ("second", "Спасибо.", None)], f"used {s.used}")
check(set("Спасибо") <= set(s.characters()), "the characters of the language for the font")
code = a.blob[16 + 6:]  # past the header, the kind, the name and the face of the one role
check(code[:7] == bytes([1, 0, 0]) + struct.pack("<HH", 0, 0), f"say kid string 0, no time: {code[:7].hex()}")
check(b.roles == [] and a.roles == ["kid"], "roles")
# the blob of strings: "DGSS", version, count, no rule (English: n != 1), offsets, UTF-8 ending in 0
blob = s.blob()
magic, version, count, rule = struct.unpack_from("<IHHI", blob, 0)
first = struct.unpack_from("<I", blob, 12)[0]
check(magic == 0x53534744 and version == 2 and rule == 0, "the header of the strings blob")
check(count == 3 and blob[first:first + len("Спасибо.".encode())].decode() == "Спасибо.", "the strings blob")
# strings of a number: the forms of the language in a row (Russian: three) and its rule after the strings
ru = Strings({("", ""): "Plural-Forms: nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && "
                        "(n%100<10 || n%100>=20) ? 1 : 2);\n",
              ("t", "{hp} life"): ["{hp} жизнь", "{hp} жизни", "{hp} жизней"]})
d = assemble_dialog('dialog t\nroles kid KID\n    say kid "{hp} life" "{hp} lives" 1s\n    say kid "{hp} apple" "{hp} apples"\n    end\n',
                    ru, KINDS, PARAMS)
check(ru.texts == ["{hp} жизнь", "{hp} жизни", "{hp} жизней", "{hp} apple", "{hp} apples", "{hp} apples"], f"forms {ru.texts}")
code = d.blob[16 + 6:]
check(code[:9] == bytes([8, 0, 0]) + struct.pack("<HHBB", 0, 1000, 3, 0), f"say_n kid string 0, 1s, 3 forms, param hp: {code[:9].hex()}")
_, _, _, rule = struct.unpack_from("<IHHI", ru.blob(), 0)
check(rule and ru.blob()[rule] == len(ru.blob()) - rule - 1, "the rule at its offset, to the end")
check(ru.forms() == (3, "(n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2)"), "the forms of the header")
pot = ru.pot()
check('msgid "{hp} life"\nmsgid_plural "{hp} lives"\nmsgstr[0] ""\nmsgstr[1] ""' in pot, "a string of a number in the template")
for name, text in (("two params shown", 'dialog t\nroles kid KID\n    say kid "{hp} of {gold}"\n    end\n'),
                   ("a param of nobody", 'dialog t\nroles kid KID\n    say kid "{gold} gold" "{gold} gold"\n    end\n'),
                   ("a plural of no param", 'dialog t\nroles kid KID\n    say kid "one" "many"\n    end\n')):
    try:
        assemble_dialog(text, Strings(), KINDS, PARAMS)
        failed.append(f"{name} was not taken as wrong")
    except DialogError:
        pass
# a choose: answers with conditions of the kind's params and of pockets; important lines
c = assemble_dialog('dialog c\nroles kid KID\nlabel top\n    say! kid "!"\n    choose kid\n        "x" if hp > 1 -> top\n'
                    '        "y" if count(KNIFE) > 0 -> top\n        "z" -> top\n    end\n', Strings(), KINDS, PARAMS)
code = c.blob[22:]
check(code[0] == 1 and code[2] == 1, "say! is important")
check(code[7] == 2 and code[9] == 3 and code[10:14] == bytes(4), f"choose kid of three answers, no time: {code[7:14].hex()}")
# times: a line, a choice and where its silence leads
d = assemble_dialog('dialog t\nlabel a\n    say game "x" 3s\n    choose game 500ms -> b\n        "y" -> a\nlabel b\n    end\n', Strings(), KINDS, PARAMS)
code = d.blob[16:]
check(struct.unpack_from("<H", code, 5)[0] == 3000 and struct.unpack_from("<HH", code, 10) == (500, d.labels["b"]), f"times {code.hex()}")
# names roles are shown by: msgids in the dialog's context, a comma inside one is no separator; none: 0xFFFF
sn = Strings({("n", "Guard"): "Стражник"})
n = assemble_dialog('dialog n\nroles guard KID "Guard", kid KID "Prince, the", x KID\n    end\n', sn, KINDS, PARAMS)
names = struct.unpack_from("<3H", n.blob, 16 + 6)
check(n.roles == ["guard", "kid", "x"] and sn.texts[names[0]] == "Стражник" and sn.texts[names[1]] == "Prince, the" and names[2] == 0xFFFF,
      f"role names {names} {sn.texts}")
# faces: a picture a role, after the names; none: 0xFFFF
f = assemble_dialog('dialog f\nroles guard KID "Guard" face FACE, kid KID face FACE2, x KID\n    end\n', Strings(), KINDS, PARAMS,
                    images={"FACE": 4, "FACE2": 9})
check(struct.unpack_from("<3H", f.blob, 16 + 12) == (4, 9, 0xFFFF), f"role faces {f.blob[28:34].hex()}")
# controls: the game's params by its kind, an answer with no label stays (0xFFFF), the events of the interface added to
# the pack's and named in the header; no controls: none named
events = ["hit"]
p = assemble_dialog(PAGE + '        "Sound" control SWITCH sound\n        "Loud" if volume > 1 control SWITCH volume -> a\n'
                    '        "Back" -> b\nlabel b\n    end\n', Strings(), KINDS, PARAMS, events)
check(events == ["hit", "activate", "more", "less"] and p.blob[12:16] == bytes([1, 2, 3, 0xFF]), f"ui events {events} {p.blob[12:16].hex()}")
code = p.blob[16:]
first = struct.unpack_from("<HHHB", code, 7)
check(first[1:] == (0xFFFF, 7, 0) and code[14] == 0, f"a control of sound, staying: {first} {code[14]}")
second = struct.unpack_from("<HHHB", code, 15)
check(second[1:] == (0, 7, 1) and code[22] > 0, f"a control of volume leading to a, a condition: {second}")
check(d.blob[12:16] == b"\xff" * 4, "no controls: no events named")
# .po in (with a multi-line msgstr and an untranslated entry), .pot out
po = tempfile.NamedTemporaryFile("w", suffix=".po", delete=False, encoding="utf-8")
po.write('msgid ""\nmsgstr ""\n\nmsgctxt "d"\nmsgid "Hello"\nmsgstr ""\n"При"\n"вет"\n\nmsgctxt "d"\nmsgid "Bye"\nmsgstr ""\n\n'
         'msgctxt "e"\nmsgid "Say \\"hi\\""\nmsgstr "Скажи \\"привет\\""\n')
po.close()
t = read_po(po.name)
os.unlink(po.name)
check(t == {("d", "Hello"): "Привет", ("e", 'Say "hi"'): 'Скажи "привет"'}, f"read_po {t}")
# .po with a header of forms and a string of a number
po = tempfile.NamedTemporaryFile("w", suffix=".po", delete=False, encoding="utf-8")
po.write('msgid ""\nmsgstr ""\n"Plural-Forms: nplurals=3; plural=n%10==1 ? 0 : n<5 ? 1 : 2;\\n"\n\n'
         'msgctxt "c"\nmsgid "{n} min"\nmsgid_plural "{n} mins"\nmsgstr[0] "{n} минута"\nmsgstr[1] "{n} минуты"\nmsgstr[2] ""\n"{n} минут"\n\n'
         'msgctxt "c"\nmsgid "{n} s"\nmsgid_plural "{n} ss"\nmsgstr[0] ""\nmsgstr[1] ""\nmsgstr[2] ""\n')
po.close()
t = read_po(po.name)
os.unlink(po.name)
check(t.get(("c", "{n} min")) == ["{n} минута", "{n} минуты", "{n} минут"] and ("c", "{n} s") not in t, f"read_po of forms {t}")
check(Strings(t).forms() == (3, "n%10==1 ? 0 : n<5 ? 1 : 2"), f"forms {Strings(t).forms()}")
check('msgctxt "first"\nmsgid "Thanks."\nmsgstr ""' in s.pot(), "the template of translations")
print("test_dialog_rules.py: ok" if not failed else "test_dialog_rules.py: " + "\n".join(failed))
sys.exit(1 if failed else 0)
