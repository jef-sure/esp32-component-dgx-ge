"""dgx_ge_fonts.py: the rules of which fonts may become pixel fonts at which sizes, and the pick by them."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
from dgx_ge_fonts import Font, RulesError, accessor, candidates, parse_needs, parse_rules, parse_sizes, size_allowed  # noqa: E402

failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


# sizes: numbers, ranges, comparisons; & is and, a comma or a space is or
for text, inside, outside in [("14", [14], [13, 15]), ("16..24", [16, 20, 24], [15, 25]), ("16..", [16, 400], [15]), ("..24", [1, 24], [25]),
                              ("> 12 & < 40", [13, 39], [12, 40]), (">=12&<=40", [12, 40], [11, 41]), ("12, 14 16..18", [12, 14, 17], [13, 15, 19]),
                              ("< 10, > 30", [9, 31], [10, 30]), ("28,32,36", [28, 36], [30])]:
    sizes = parse_sizes(text, "t")
    for n in inside:
        check(size_allowed(sizes, n), f"{text}: {n} should be allowed")
    for n in outside:
        check(not size_allowed(sizes, n), f"{text}: {n} should not be allowed")
check(size_allowed(None, 7), "no sizes: every size")
for bad in ["> ", "12 &", "a", "16..x", "&12"]:
    try:
        parse_sizes(bad, "t")
        failed.append(f"sizes '{bad}' not refused")
    except RulesError:
        pass

# the rules of a text: sources, allow and deny in their order; mistakes refused with their line
sources, rules = parse_rules('source system\nsource dir ~/F\n# a comment\nallow "Terminus*" > 12 & < 40\ndeny "Comic*"\nallow * 40..\n', "r")
check(sources == [("system", None), ("dir", os.path.expanduser("~/F"))], f"sources {sources}")
check([(r.allow, r.pattern) for r in rules] == [(True, "terminus*"), (False, "comic*"), (True, "*")], f"rules {rules}")
check(rules[1].sizes is None and rules[0].where == "r:4", f"deny without sizes, where {rules}")
for bad in ['allow "X"', "allow X 12", "source", "source list", "source web x", "prefer x", 'deny "X" 12 &']:
    try:
        parse_rules(bad, "r")
        failed.append(f"rules '{bad}' not refused")
    except RulesError:
        pass

# the pick: the first rule with a font of the size; plain before bold and slanted, then the order of the sources;
# a denied face never; mono and a family asked for
fonts = [Font("tb.ttf", "Terminus (TTF)", "Bold", True, 0), Font("t.ttf", "Terminus (TTF)", "Medium", True, 1),
         Font("d.ttf", "DejaVu Sans Mono", "Book", True, 2), Font("c.ttf", "Comic Neue", "Regular", False, 3),
         Font("s.ttf", "DejaVu Sans", "Book", False, 4)]
_, rules = parse_rules('allow "Terminus*" > 12 & < 40\nallow "dejavu sans mono" 16..\ndeny "Comic*"\nallow * 40..\n')


def picked(size, **kw):
    return [f.file for _, f in candidates(fonts, rules, size, **kw)]


check(picked(12) == [], f"12: {picked(12)}")
check(picked(16) == ["t.ttf", "tb.ttf", "d.ttf"], f"16: {picked(16)}")
check(picked(48) == ["d.ttf", "t.ttf", "s.ttf", "tb.ttf"], f"48: {picked(48)}")  # Terminus at 48 only by *, plain first; Comic never
check(picked(48, mono=True) == ["d.ttf", "t.ttf", "tb.ttf"], f"48 mono: {picked(48, mono=True)}")
check(picked(16, family="dejavu*") == ["d.ttf"], f"16 dejavu: {picked(16, family='dejavu*')}")
check(picked(48, family="DejaVu Sans Book") == ["s.ttf"], "a family and its style")
_, only = parse_rules('allow * 10..\ndeny "Terminus*" 20..\n')
check([f.file for _, f in candidates(fonts, only, 24)] == ["d.ttf", "c.ttf", "s.ttf"], "deny at sizes: Terminus not at 24")
check([f.file for _, f in candidates(fonts, only, 12)] == ["t.ttf", "d.ttf", "c.ttf", "s.ttf", "tb.ttf"], "deny at sizes: Terminus at 12")

# the needs of a game: a font a line in the order of its table; made by the rules, or a file taken as it is
needs = parse_needs('text 12 mono\nnote 14 family "Arial*"\n# a comment\ntime 8 file "main/fonts/PxCgaThin8.c"\nbig 24\n', "n")
check([(n.name, n.points, n.mono, n.family, n.file) for n in needs] == [("text", 12, True, None, None), ("note", 14, False, "Arial*", None),
      ("time", 8, False, None, "main/fonts/PxCgaThin8.c"), ("big", 24, False, None, None)], f"needs {needs}")
check(needs[2].where == "n:4", "needs: where")
for bad in ["text", "text mono", "12 text", 'time 8 mono file "x.c"', 'a 8 family "x" file "y.c"', "text 12\ntext 14", "text 12 bold"]:
    try:
        parse_needs(bad, "n")
        failed.append(f"needs '{bad}' not refused")
    except RulesError:
        pass
# the function a font's C file gives it by
check(accessor("#include <x>\ndgx_font_t* PxCgaThin8() {\n", "a") == "PxCgaThin8", "accessor: font2c's")
check(accessor("dgx_font_t *font0ss()\n{", "a") == "font0ss", "accessor: spaced")
try:
    accessor("int x;", "a")
    failed.append("accessor of no font not refused")
except RulesError:
    pass

print("test_fonts.py: ok" if not failed else "test_fonts.py: " + "\n".join(failed))
sys.exit(1 if failed else 0)
